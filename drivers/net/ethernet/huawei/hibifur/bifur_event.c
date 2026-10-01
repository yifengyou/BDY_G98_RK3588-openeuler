// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */
#include <net/bonding.h>
#include "hinic3_srv_nic.h"
#include "hinic3_lld.h"
#include "hinic3_bond.h"
#include "hinic3_mt.h"
#include "nic_mpu_cmd.h"
#include "hinic3_hw.h"
#include "hinic3_mgmt_interface.h"

#include "bifur_common.h"
#include "bifur_vf_mgr.h"
#include "bifur_main.h"
#include "bifur_event.h"

static int bifur_set_vf_bond_enable(struct bifur_lld_dev *bifur_dev, u16 vf_id, u8 bond_bifur_en)
{
	int err;
	struct hinic3_bond_mask_cmd bond_info = { 0 };
	u16 out_size = sizeof(struct hinic3_bond_mask_cmd);

	bond_info.msg_head.status = 1;
	bond_info.func_id = vf_id;
	bond_info.bond_en = bond_bifur_en;

	err = hinic3_msg_to_mgmt_sync(bifur_dev->lld_dev->hwdev, HINIC3_MOD_L2NIC,
					HINIC3_NIC_CMD_SET_BOND_MASK, &bond_info,
					sizeof(bond_info), &bond_info,
					&out_size, 0, HINIC3_CHANNEL_DEFAULT);
	if (bond_info.msg_head.status != 0 || err != 0 || out_size == 0) {
		BIFUR_DEV_ERR(bifur_dev->lld_dev,
			      "Failed to set VF forward id config. err(%d), sts(%u), out_size(%u)\n",
			      err, bond_info.msg_head.status, out_size);
		return -EIO;
	}
	return 0;
}

void bifur_set_bond_enable(u8 bond_bifur_en)
{
	int i, err;
	struct bifur_vf_info *vf_info = NULL;
	struct bifur_vf_mgr *vf_mgr = NULL;
	struct bifur_lld_dev *bifur_src_dev = bifur_get_resource_dev();

	if (!bifur_src_dev) {
		pr_err("Bifur source pf didn't inited.\n");
		return;
	}
	vf_mgr = bifur_src_dev->vf_mgr;

	mutex_lock(&vf_mgr->vf_mgr_mutex);
	for (i = 0; i < vf_mgr->vf_sum; ++i) {
		vf_info = &vf_mgr->vf_info[i];
		err = bifur_set_vf_bond_enable(bifur_src_dev, vf_info->glb_func_id, bond_bifur_en);
		if (err != 0) {
			BIFUR_DEV_WARN(bifur_src_dev->lld_dev,
					"Failed to set VF(0x%x) bond enable(%u).\n",
					vf_info->glb_func_id, bond_bifur_en);
		}
	}

	mutex_unlock(&vf_mgr->vf_mgr_mutex);
	bifur_dev_put(bifur_src_dev);
}

static void bifur_attach_bond_work(struct work_struct *_work)
{
	int ret;
	u16 bond_id;
	struct bifur_bond_work *work = container_of(_work, struct bifur_bond_work, work);
	struct bifur_adapter *adp = bifur_get_adp();

	if (!adp) {
		pr_err("Bifur driver init failed.\n");
		kfree(work);
		return;
	}

	ret = hinic3_bond_attach(work->name, HINIC3_BOND_USER_OVS, &bond_id);
	if (ret) {
		pr_info("%s: hinic3 bond attach failed, ret(%d).\n", __func__, ret);
		kfree(work);
		return;
	}

	bifur_dev_list_lock();
	adp->bond_id = bond_id;
	adp->bond_bifur_enabled = BIFUR_ENABLED;
	bifur_dev_list_unlock();

	pr_info("bifur_attach: %s: bond_name(%s), bond_id(%u)\n", __func__, work->name, bond_id);
	bifur_set_bond_enable(BIFUR_ENABLED);

	kfree(work);
}

static void bifur_queue_bond_work(struct bifur_adapter *adp, struct net_device *upper_netdev)
{
	struct bifur_bond_work *work;
	struct bonding *bond = netdev_priv(upper_netdev);

	if (!bond) {
		pr_info("%s: (name:%s) has no bond dev.\n", __func__, upper_netdev->name);
		return;
	}

	work = kzalloc(sizeof(*work), GFP_KERNEL);
	if (!work)
		return;

	(void)strscpy(work->name, upper_netdev->name, strlen(upper_netdev->name));
	INIT_WORK(&work->work, bifur_attach_bond_work);
	(void)queue_work(adp->event_workq, &work->work);
}

static void bifur_detach_nic_bond_work(struct work_struct *work)
{
	struct bifur_bond_work *detach_work = container_of(work, struct bifur_bond_work, work);
	struct bifur_adapter *adp = bifur_get_adp();
	u16 bond_id;

	if (!adp) {
		pr_err("Bifur driver init failed.\n");
		kfree(detach_work);
		return;
	}

	bifur_dev_list_lock();
	bond_id = adp->bond_id;
	adp->bond_bifur_enabled = BIFUR_DISABLED;
	bifur_dev_list_unlock();

	hinic3_bond_detach(bond_id, HINIC3_BOND_USER_OVS);
	bifur_set_bond_enable(BIFUR_DISABLED);
	kfree(detach_work);
}

static void bifur_queue_detach_bond_work(struct bifur_adapter *adp)
{
	struct bifur_bond_work *work;

	work = kzalloc(sizeof(*work), GFP_KERNEL);
	if (!work)
		return;

	INIT_WORK(&work->work, bifur_detach_nic_bond_work);

	(void)queue_work(adp->event_workq, &work->work);
}

static bool bifur_can_do_bond(struct bonding *bond)
{
	bool ret = false;
	int slave_cnt = 0;
	struct slave *slave = NULL;
	struct list_head *iter = NULL;
	struct hinic3_lld_dev *lld_dev = NULL;
	struct hinic3_lld_dev *ppf_dev = NULL;

	if (!bond)
		return ret;

	rcu_read_lock();
	bond_for_each_slave_rcu(bond, slave, iter) {
		lld_dev = hinic3_get_lld_dev_by_netdev(slave->dev);
		if (!lld_dev)
			goto out;

		if (!hinic3_support_bifur(lld_dev->hwdev, NULL))
			goto out;

		if (!ppf_dev) {
			ppf_dev = hinic3_get_ppf_lld_dev(lld_dev);
			if (!ppf_dev)
				goto out;
		}

		slave_cnt++;
		pr_info("%s:can do bond? slave_cnt(%d), slave_name(%s)", __func__,
				slave_cnt, slave->dev->name);
	}

	ret = (slave_cnt == BIFUR_BOND_2_FUNC_NUM);
out:
	rcu_read_unlock();
	return ret;
}

static int bifur_bond_netdev_event(struct bifur_adapter *adp,
				   struct netdev_notifier_changeupper_info *info,
				   struct net_device *net_dev)
{
	struct bonding *bond = NULL;
	struct net_device *upper_netdev = info->upper_dev;

	if (net_eq(dev_net(net_dev), &init_net) == 0)
		return NOTIFY_DONE;

	if (!upper_netdev)
		return NOTIFY_DONE;

	if (!netif_is_lag_master(upper_netdev))
		return NOTIFY_DONE;

	bond = netdev_priv(upper_netdev);
	if (!bifur_can_do_bond(bond)) {
		bifur_queue_detach_bond_work(adp);
		pr_info("%s: (name:%s) has no bond dev.\n", __func__, upper_netdev->name);
		return NOTIFY_DONE;
	}

	bifur_queue_bond_work(adp, upper_netdev);

	return NOTIFY_DONE;
}

int bifur_bond_init(void)
{
	int ret = 0;
	struct net_device *upper_netdev;
	struct bifur_adapter *adp = bifur_get_adp();

	if (!adp) {
		pr_err("Bifur driver init failed.\n");
		return -EINVAL;
	}

	rtnl_lock();
	for_each_netdev(&init_net, upper_netdev) {
		if (netif_is_bond_master(upper_netdev) &&
			bifur_can_do_bond(netdev_priv(upper_netdev))) {
			bifur_queue_bond_work(adp, upper_netdev);
			break;
		}
	}
	rtnl_unlock();

	pr_info("%s: bond init exit.\n", __func__);
	return ret;
}

void bifur_bond_exit(void)
{
	struct bifur_adapter *adp = bifur_get_adp();

	if (!adp) {
		pr_err("Bifur driver init failed.\n");
		return;
	}
	bifur_queue_detach_bond_work(adp);
}

void bifur_notify_vf_link_status(struct hinic3_lld_dev *lld_dev, u8 port_id, u16 vf_id,
				 u8 link_status)
{
	struct mag_cmd_get_link_status link;
	u16 out_size = sizeof(link);
	int err;

	(void)memset(&link, 0, sizeof(link));
	link.status = link_status;
	link.port_id = port_id;

	err = hinic3_mbox_to_vf_no_ack(lld_dev->hwdev, vf_id, HINIC3_MOD_HILINK,
				       MAG_CMD_GET_LINK_STATUS, &link, sizeof(link),
				       &link, &out_size, HINIC3_CHANNEL_NIC);
	if (err == MBOX_ERRCODE_UNKNOWN_DES_FUNC) {
		pr_err("Vf%d not initialized, disconnect it\n", HW_VF_ID_TO_OS(vf_id));
		return;
	}

	if (err || !out_size || link.head.status) {
		pr_err("Send link change event to VF %d failed, err: %d, status: 0x%x, out_size: 0x%x\n",
			   HW_VF_ID_TO_OS(vf_id), err, link.head.status, out_size);
	}
}

void bifur_notify_all_vfs_link_changed(struct hinic3_lld_dev *lld_dev, u32 dbdf, u8 link_status)
{
	struct bifur_lld_dev *bifur_src_dev = NULL;
	struct bifur_vf_mgr *vf_mgr = NULL;
	struct bifur_vf_info *vf_info = NULL;
	u16 i;
	u8 port_id;

	bifur_src_dev = bifur_get_resource_dev();
	if (!bifur_src_dev)
		return;

	vf_mgr = bifur_src_dev->vf_mgr;
	port_id = hinic3_physical_port_id(lld_dev->hwdev);

	mutex_lock(&vf_mgr->vf_mgr_mutex);
	for (i = 0; i < vf_mgr->vf_sum; ++i) {
		vf_info = &vf_mgr->vf_info[i];
		if (vf_info->pf_dbdf == dbdf && vf_info->in_use)
			bifur_notify_vf_link_status(bifur_src_dev->lld_dev, port_id,
						    OS_VF_ID_TO_HW(i), link_status);
	}
	mutex_unlock(&vf_mgr->vf_mgr_mutex);

	bifur_dev_put(bifur_src_dev);
}

void bifur_netdev_event(struct work_struct *work)
{
	struct bifur_lld_dev *bifur_dev = container_of(work, struct bifur_lld_dev, netdev_link);

	bifur_notify_all_vfs_link_changed(bifur_dev->lld_dev, bifur_dev->dbdf,
									  bifur_dev->link_status);

	bifur_dev_put(bifur_dev);
}

static int bifur_net_link_event(struct bifur_adapter *adp, unsigned long event,
				struct net_device *dev)
{
	u32 dbdf;
	struct pci_dev *pcidev = NULL;
	struct bifur_lld_dev *bifur_dev = NULL;
	struct hinic3_nic_dev *nic_dev = NULL;

	nic_dev = netdev_priv(dev);
	pcidev = nic_dev->pdev;
	dbdf = PCI_DBDF(pci_domain_nr(pcidev->bus), pcidev->bus->number,
			PCI_DBDF_DEVID(pcidev->devfn), PCI_DBDF_FUNCTION(pcidev->devfn));

	bifur_dev = bifur_get_shared_dev_by_dbdf(dbdf);
	if (!bifur_dev)
		return NOTIFY_DONE;

	bifur_dev->link_status = (event == NETDEV_UP ? 1 : 0);
	(void)queue_work(adp->event_workq, &bifur_dev->netdev_link);
	return NOTIFY_OK;
}

int bifur_net_event_callback(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct bifur_adapter *adp = bifur_get_adp();

	if (unlikely(!dev)) {
		pr_err("bifur notify dev null\n");
		return NOTIFY_DONE;
	}

	/* only self-developed NICs can be processed */
	if (!hinic3_get_lld_dev_by_netdev(dev))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
	case NETDEV_DOWN:
		return bifur_net_link_event(adp, event, dev);
	case NETDEV_CHANGEUPPER:
		return bifur_bond_netdev_event(adp, (struct netdev_notifier_changeupper_info *)ptr,
					       dev);
	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block g_bifur_event_nb = {
	.notifier_call = bifur_net_event_callback
};

int bifur_register_net_event(void)
{
	return register_netdevice_notifier(&g_bifur_event_nb);
}

void bifur_unregister_net_event(void)
{
	(void)unregister_netdevice_notifier(&g_bifur_event_nb);
}
