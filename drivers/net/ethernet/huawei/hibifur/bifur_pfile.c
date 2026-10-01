// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#include <linux/kernel.h>
#include "nic_mpu_cmd.h"
#include "hinic3_hw.h"
#include "hinic3_mgmt_interface.h"
#include "hinic3_common.h"

#include "bifur_common.h"
#include "bifur_vf_mgr.h"
#include "bifur_main.h"
#include "bifur_pfile.h"

#define BIFUR_GLOBAL_CDEV_NAME "bifur_gdev"
struct bifur_cdev g_bifur_global_dev;
struct bifur_global_file_list_t g_bifur_global_file_list;

struct class *g_bifur_class;

void bifur_global_dev_uninit(void)
{
	struct bifur_proc_file_t *tmp = NULL;
	struct bifur_proc_file_t *proc_file = NULL;
	struct bifur_cdev *bdev = &g_bifur_global_dev;

	mutex_lock(&g_bifur_global_file_list.lock);
	list_for_each_entry_safe(proc_file, tmp, &g_bifur_global_file_list.list, node) {
		list_del(&proc_file->node);
	}
	mutex_unlock(&g_bifur_global_file_list.lock);

	device_destroy(g_bifur_class, bdev->cdev_id);
	class_destroy(g_bifur_class);
	g_bifur_class = NULL;
	cdev_del(&bdev->cdev);
	unregister_chrdev_region(bdev->cdev_id, 1);
	pr_info("Bifur destroy global cdev(%s) succeed.", BIFUR_GLOBAL_CDEV_NAME);
}

void bifur_global_file_del(struct bifur_proc_file_t *proc_file)
{
	mutex_lock(&g_bifur_global_file_list.lock);
	list_del(&proc_file->node);
	mutex_unlock(&g_bifur_global_file_list.lock);
}

int bifur_global_dev_close(struct inode *inode, struct file *filp)
{
	struct bifur_proc_file_t *proc_file = filp->private_data;

	pr_info("Close global proc_file(%p), filp(%p).", proc_file, filp);
	bifur_global_file_del(proc_file); // Direct chain removal without traversing
	kfree(proc_file);

	return 0;
}

/* One dpdk process has only one process_file. A unique file is added to the global list. */
int bifur_global_file_add(struct bifur_proc_file_t *add_proc_file)
{
	struct bifur_proc_file_t *tmp = NULL;
	struct bifur_proc_file_t *proc_file = NULL;

	mutex_lock(&g_bifur_global_file_list.lock);

	list_for_each_entry_safe(proc_file, tmp, &g_bifur_global_file_list.list, node) {
		if (proc_file->pid == add_proc_file->pid) {
			mutex_unlock(&g_bifur_global_file_list.lock);
			pr_err("Process(%u) file is exist.", proc_file->pid);
			return -EPERM;
		}
	}

	list_add_tail(&add_proc_file->node, &g_bifur_global_file_list.list);
	mutex_unlock(&g_bifur_global_file_list.lock);

	return 0;
}

struct bifur_proc_file_t *bifur_alloc_proc_file(void)
{
	struct bifur_proc_file_t *proc_file = kzalloc(sizeof(struct bifur_proc_file_t), GFP_KERNEL);

	if (!proc_file)
		return NULL;

	proc_file->pid = current->pid;

	return proc_file;
}

int bifur_global_dev_open(struct inode *inode, struct file *filp)
{
	int ret;
	struct bifur_proc_file_t *proc_file = bifur_alloc_proc_file();

	if (!proc_file)
		return -ENOMEM;

	ret = bifur_global_file_add(proc_file);
	if (ret != 0) {
		pr_err("Duplicate processes(%u) open global char dev.", current->pid);
		kfree(proc_file);
		return -EEXIST;
	}
	filp->private_data = proc_file;

	pr_info("Open proc global proc file success, proc_file(%p), filp(%p) pid(%u).",
			proc_file, filp, proc_file->pid);

	return nonseekable_open(inode, filp);
}

static int bifur_drv_cmd_func_attr_get(struct file *filp, struct bifur_msg *cmd)
{
	struct bifur_lld_dev *bifur_dev = NULL;
	struct bifur_lld_dev *tmp_dev = NULL;
	struct bifur_adapter *adp = bifur_get_adp();
	struct list_head *head = &adp->lld_dev_head;
	int i;

	struct bifur_func_attr_get_cmd_msg *query_cmd;
	struct bifur_func_attr_get_cmd_rsp *query_resp;

	query_cmd = (struct bifur_func_attr_get_cmd_msg *)(cmd->in_buf);
	query_resp = (struct bifur_func_attr_get_cmd_rsp *)(cmd->out_buf);
	if ((!query_cmd) || (!query_resp) ||
		cmd->in_buf_len < sizeof(struct bifur_func_attr_get_cmd_msg) ||
		cmd->out_buf_len < sizeof(struct bifur_func_attr_get_cmd_rsp)) {
		pr_err("Input param fail, in_buf_len(%u), out_buf_len(%u).",
		       cmd->in_buf_len, cmd->out_buf_len);
		return -EPERM;
	}

	query_resp->func_type = 0;
	cmd->out_data_len = sizeof(struct bifur_func_attr_get_cmd_rsp);

	bifur_dev_list_lock();
	if (adp->bifur_enabled == BIFUR_DISABLED) {
		bifur_dev_list_unlock();
		query_resp->func_type = BIFUR_EXCLUSIVE_PF;
		pr_info("Didn't enable traffic bifurcation, functions are exclusive.\n");
		return 0;
	}
	bifur_dev_list_unlock();

	list_for_each_entry_safe(bifur_dev, tmp_dev, head, list) {
		if (bifur_dev->dbdf == query_cmd->dbdf) {
			query_resp->func_type = bifur_dev->pf_type;
			break;
		}

		if (bifur_dev->pf_type == BIFUR_RESOURCE_PF) {
			for (i = 0; i < bifur_dev->vf_mgr->vf_sum; ++i) {
				if (bifur_dev->vf_mgr->vf_info[i].vf_dbdf == query_cmd->dbdf) {
					query_resp->func_type = BIFUR_RESOURCE_VF;
					break;
				}
			}
		}
	}

	pr_info("Do get func attr cmd success\n");
	return 0;
}

static inline void bifur_release_vf(struct bifur_vf_mgr *vf_mgr, struct bifur_vf_info *vf_info)
{
	mutex_lock(&vf_mgr->vf_mgr_mutex);
	vf_info->in_use = 0;
	vf_mgr->vf_in_use--;
	mutex_unlock(&vf_mgr->vf_mgr_mutex);
}

static int get_global_func_id_by_dbdf(u32 dbdf, u16 *glb_func_id)
{
	struct bifur_lld_dev *bifur_dev = NULL;
	struct bifur_lld_dev *tmp_dev = NULL;
	struct bifur_adapter *adp = bifur_get_adp();
	struct list_head *head = &adp->lld_dev_head;
	int i;

	list_for_each_entry_safe(bifur_dev, tmp_dev, head, list) {
		if (bifur_dev->dbdf == dbdf) {
			*glb_func_id = hinic3_global_func_id(bifur_dev->lld_dev->hwdev);
			return 0;
		}

		if (bifur_dev->pf_type == BIFUR_RESOURCE_PF) {
			for (i = 0; i < bifur_dev->vf_mgr->vf_sum; ++i) {
				if (bifur_dev->vf_mgr->vf_info[i].vf_dbdf == dbdf) {
					*glb_func_id = bifur_dev->vf_mgr->vf_info[i].glb_func_id;
					return 0;
				}
			}
		}
	}
	return -ENODEV;
}

static int bifur_set_vf_tx_port(struct bifur_lld_dev *bifur_dev, u32 src_dbdf, u32 dst_dbdf)
{
	int err;
	struct hinic3_func_er_value_cmd vf_fwd_id_cfg = {0};
	u16 out_size = sizeof(struct hinic3_func_er_value_cmd);

	err = get_global_func_id_by_dbdf(src_dbdf, &vf_fwd_id_cfg.vf_id);
	if (err != 0) {
		BIFUR_DEV_ERR(bifur_dev->lld_dev, "Do not exit this vf, vf(%u)\n", src_dbdf);
		return err;
	}
	BIFUR_DEV_INFO(bifur_dev->lld_dev, "src_vf(0x%x), vf_id(%u)\n",
		       src_dbdf, vf_fwd_id_cfg.vf_id);

	err = get_global_func_id_by_dbdf(dst_dbdf, &vf_fwd_id_cfg.er_fwd_id);
	if (err != 0) {
		BIFUR_DEV_ERR(bifur_dev->lld_dev, "Do not exit this port, port dbdf(%u)\n",
			      dst_dbdf);
		return err;
	}
	BIFUR_DEV_INFO(bifur_dev->lld_dev, "dst_dbdf(0x%x), er_fwd_id(%u)\n",
		       dst_dbdf, vf_fwd_id_cfg.er_fwd_id);

	err = hinic3_msg_to_mgmt_sync(bifur_dev->lld_dev->hwdev, HINIC3_MOD_L2NIC,
				      HINIC3_NIC_CMD_SET_FUNC_ER_FWD_ID, &vf_fwd_id_cfg,
				      sizeof(vf_fwd_id_cfg), &vf_fwd_id_cfg, &out_size, 0,
				      HINIC3_CHANNEL_DEFAULT);
	if (vf_fwd_id_cfg.msg_head.status != 0 || err != 0 || out_size == 0) {
		BIFUR_DEV_ERR(bifur_dev->lld_dev,
					  "Failed to set VF forward id config. err(%d), sts(%u), out_size(%u)\n",
					  err, vf_fwd_id_cfg.msg_head.status, out_size);
		return -EIO;
	}
	return 0;
}

static int bifur_drv_cmd_vf_alloc(struct file *filp, struct bifur_msg *cmd)
{
	int err;
	struct bifur_lld_dev *bifur_dev = NULL;
	struct bifur_vf_info *vf_info = NULL;
	struct bifur_vf_alloc_cmd_msg *query_cmd = (struct bifur_vf_alloc_cmd_msg *)(cmd->in_buf);
	struct bifur_vf_alloc_cmd_rsp *query_resp = (struct bifur_vf_alloc_cmd_rsp *)(cmd->out_buf);
	struct bifur_adapter *adp = bifur_get_adp();

	if (!query_cmd || !query_resp ||
		cmd->in_buf_len < sizeof(struct bifur_vf_alloc_cmd_msg) ||
		cmd->out_buf_len < sizeof(struct bifur_vf_alloc_cmd_rsp)) {
		pr_err("Input param fail, in_buf_len(%u), out_buf_len(%u).",
		       cmd->in_buf_len, cmd->out_buf_len);
		return -EINVAL;
	}

	bifur_dev_list_lock();
	if (adp->bifur_enabled == BIFUR_DISABLED) {
		bifur_dev_list_unlock();
		pr_err("Didn't enable traffic bifurcation.\n");
		return -EPERM;
	}
	bifur_dev_list_unlock();

	/* found the bifur device */
	bifur_dev = bifur_get_resource_dev();
	if (!bifur_dev)
		return -EINVAL;

	vf_info = bifur_find_vf(bifur_dev->vf_mgr, query_cmd->dbdf);
	if (!vf_info) {
		bifur_dev_put(bifur_dev);
		BIFUR_DEV_ERR(bifur_dev->lld_dev, "Alloc vf failed, %u vf in use\n",
			      bifur_dev->vf_mgr->vf_in_use);
		return -EFAULT;
	}

	err = bifur_set_vf_tx_port(bifur_dev, vf_info->vf_dbdf, query_cmd->dbdf);
	if (err) {
		bifur_release_vf(bifur_dev->vf_mgr, vf_info);
		bifur_dev_put(bifur_dev);
		BIFUR_DEV_ERR(bifur_dev->lld_dev, "Set vf forward id failed, vf(%u), dst_pf(%u)\n",
			      vf_info->vf_dbdf, query_cmd->dbdf);
		return err;
	}
	query_resp->vf_dbdf = vf_info->vf_dbdf;

	BIFUR_DEV_INFO(bifur_dev->lld_dev, "pf_dbdf: 0x%x\n", query_cmd->dbdf);
	BIFUR_DEV_INFO(bifur_dev->lld_dev, "alloc_vf_dbdf: 0x%x\n", query_resp->vf_dbdf);

	cmd->out_data_len = sizeof(struct bifur_vf_alloc_cmd_rsp);
	bifur_dev_put(bifur_dev);
	pr_info("Do vf alloc cmd success\n");
	return 0;
}

static int bifur_drv_cmd_mac_get(struct file *filp, struct bifur_msg *cmd)
{
	int ret;
	u32 pf_dbdf;
	struct bifur_lld_dev *bifur_dev = NULL;
	struct bifur_lld_dev *shared_bifur_dev = NULL;
	struct net_device *net_dev = NULL;
	struct bifur_adapter *adp = bifur_get_adp();

	struct bifur_mac_get_cmd_msg *query_cmd = (struct bifur_mac_get_cmd_msg *)(cmd->in_buf);
	struct bifur_mac_get_cmd_rsp *query_resp = (struct bifur_mac_get_cmd_rsp *)(cmd->out_buf);

	if (!query_cmd || !query_resp ||
		cmd->in_buf_len < sizeof(struct bifur_mac_get_cmd_msg) ||
		cmd->out_buf_len < sizeof(struct bifur_mac_get_cmd_rsp)) {
		pr_err("Input param fail, in_buf_len(%u), out_buf_len(%u).",
		       cmd->in_buf_len, cmd->out_buf_len);
		return -EINVAL;
	}

	bifur_dev_list_lock();
	if (adp->bifur_enabled == BIFUR_DISABLED) {
		bifur_dev_list_unlock();
		pr_err("Didn't enable traffic bifurcation.\n");
		return -EPERM;
	}
	bifur_dev_list_unlock();

	bifur_dev = bifur_get_resource_dev();
	if (!bifur_dev)
		return -EINVAL;

	ret = bifur_find_pf_by_vf(bifur_dev->vf_mgr, query_cmd->dbdf, &pf_dbdf);
	if (ret != 0) {
		bifur_dev_put(bifur_dev);
		pr_err("Find pf dbdf failed, vf dbdf(0x%x)\n", query_cmd->dbdf);
		return -EFAULT;
	}

	/* found shared dev by pf dbdf */
	shared_bifur_dev = bifur_get_shared_dev_by_dbdf(pf_dbdf);
	if (!shared_bifur_dev) {
		bifur_dev_put(bifur_dev);
		return -EINVAL;
	}

	/* found net device by shared dev lld dev */
	net_dev = hinic3_get_netdev_by_lld(shared_bifur_dev->lld_dev);
	if (!net_dev) {
		bifur_dev_put(bifur_dev);
		bifur_dev_put(shared_bifur_dev);
		pr_err("Get net device by lld dev failed, pf_dbdf(0x%x)\n", pf_dbdf);
		return -EINVAL;
	}

	ether_addr_copy(query_resp->mac, net_dev->dev_addr);
	bifur_dev_put(bifur_dev);
	bifur_dev_put(shared_bifur_dev);

	cmd->out_data_len = sizeof(struct bifur_mac_get_cmd_rsp);
	pr_info("DO Get mac cmd of vf success\n");
	return 0;
}

struct {
	enum bifur_cmd_m drv_cmd;
	int (*cmd_handle)(struct file *filp, struct bifur_msg *msg);
} g_bifur_cmd_table[] = {
	{BIFUR_DRV_CMD_FUNC_ATTR_GET, bifur_drv_cmd_func_attr_get},
	{BIFUR_DRV_CMD_VF_ALLOC, bifur_drv_cmd_vf_alloc},
	{BIFUR_DRV_CMD_MAC_GET, bifur_drv_cmd_mac_get},
};

int bifur_cmd_exec(struct file *file, struct bifur_msg *msg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(g_bifur_cmd_table); i++) {
		if (g_bifur_cmd_table[i].drv_cmd == msg->drv_cmd)
			return g_bifur_cmd_table[i].cmd_handle(file, msg);
	}

	pr_err("Cmd(%u) is not supported.", msg->drv_cmd);
	return -EOPNOTSUPP;
}

int bifur_msg_copy_from_usr(const char __user *ubuf, size_t size, struct bifur_msg *usr,
			    struct bifur_msg *knl)
{
	u64 ret = 0;

	ret = copy_from_user(usr, ubuf, size);
	if (ret != 0) {
		pr_err("Copy msg from user failed, ret(0x%llx).", ret);
		return -EFAULT;
	}

	if (usr->in_buf && (usr->in_buf_len == 0 || usr->in_buf_len > BIFUR_MAX_CMD_LEN)) {
		pr_err("Invalid in buf param, cmd(%u) in_buf_len(%u).",
		       usr->drv_cmd, usr->in_buf_len);
		return -EINVAL;
	}
	if (usr->out_buf && (usr->out_buf_len == 0 || usr->out_buf_len > BIFUR_MAX_CMD_LEN)) {
		pr_err("Invalid out buf param, cmd(%u) out_buf_len(%u).",
		       usr->drv_cmd, usr->out_buf_len);
		return -EINVAL;
	}
	knl->drv_cmd = usr->drv_cmd;
	knl->in_buf_len = usr->in_buf_len;
	knl->out_buf_len = usr->out_buf_len;

	if (usr->in_buf) {
		knl->in_buf = kzalloc((size_t)usr->in_buf_len, GFP_KERNEL);
		if (!knl->in_buf)
			return -ENOMEM;
		ret = copy_from_user(knl->in_buf, usr->in_buf, (size_t)usr->in_buf_len);
		if (ret != 0) {
			pr_err("Cmd(%u) copy in_buf from user failed, ret(0x%llx).",
			       usr->drv_cmd, ret);
			BUFUR_CHECK_KFREE(knl->in_buf);
			return -EFAULT;
		}
	}

	if (usr->out_buf) {
		knl->out_buf = kzalloc((size_t)usr->out_buf_len, GFP_KERNEL);
		if (!knl->out_buf) {
			BUFUR_CHECK_KFREE(knl->in_buf);
			return -ENOMEM;
		}
	}
	return 0;
}

void bifur_free_knl_msg_buf(struct bifur_msg *msg)
{
	BUFUR_CHECK_KFREE(msg->in_buf);
	BUFUR_CHECK_KFREE(msg->out_buf);
}

int bifur_msg_copy_to_usr(struct bifur_msg *usr, struct bifur_msg *knl)
{
	u64 ret;
	u32 copy_len;

	if (!usr->out_buf || knl->out_data_len == 0) {
		usr->out_data_len = 0;
		return 0;
	}

	copy_len = (usr->out_buf_len > knl->out_data_len) ? knl->out_data_len : usr->out_buf_len;
	ret = copy_to_user(usr->out_buf, knl->out_buf, (ulong)copy_len);
	if (ret != 0) {
		pr_err("Cmd(%u) copy out_buf to user failed, ret(0x%llx).", usr->drv_cmd, ret);
		return -EFAULT;
	}

	return 0;
}

ssize_t bifur_file_write(struct file *file, const char __user *ubuf, size_t size, loff_t *pos)
{
	int ret = 0;
	struct bifur_msg usr_msg = { 0 };
	struct bifur_msg knl_msg = { 0 };

	if (!ubuf || size < sizeof(struct bifur_msg) || size > BIFUR_MAX_CMD_LEN) {
		pr_err("Invalid param, size(%lu).", size);
		return -EINVAL;
	}

	ret = bifur_msg_copy_from_usr(ubuf, size, &usr_msg, &knl_msg);
	if (ret != 0)
		return ret;

	ret = bifur_cmd_exec(file, &knl_msg);
	if (ret != 0) {
		bifur_free_knl_msg_buf(&knl_msg);
		return (ret < 0) ? ret : -EFAULT;
	}

	ret = bifur_msg_copy_to_usr(&usr_msg, &knl_msg);
	if (ret != 0) {
		bifur_free_knl_msg_buf(&knl_msg);
		return ret;
	}
	bifur_free_knl_msg_buf(&knl_msg);

	return 0;
}

ssize_t bifur_proc_write(struct file *file, const char __user *ubuf, size_t size, loff_t *pos)
{
	return bifur_file_write(file, ubuf, size, pos);
}

static const struct file_operations g_bifur_global_cdev_fops = {
	.owner = THIS_MODULE,
	.open = bifur_global_dev_open,
	.release = bifur_global_dev_close,
	.write = bifur_proc_write,
};

/* When the module is initialized, prepare the global character device. */
int bifur_global_dev_init(void)
{
	char *name = BIFUR_GLOBAL_CDEV_NAME;
	struct device *device;
	struct bifur_cdev *bdev = &g_bifur_global_dev;

	int ret = alloc_chrdev_region(&bdev->cdev_id, 0, 1, name);

	if (ret < 0) {
		pr_err("Bifur cdev(%s) alloc card chrdev region fail, ret(%d).", name, ret);
		return -EFAULT;
	}

	cdev_init(&bdev->cdev, &g_bifur_global_cdev_fops);

	ret = cdev_add(&bdev->cdev, bdev->cdev_id, 1);
	if (ret < 0) {
		unregister_chrdev_region(bdev->cdev_id, 1);
		pr_err("Bifur cdev(%s) add cdev fail, ret(%d).", name, ret);
		return -EFAULT;
	}

	g_bifur_class = class_create(BIFUR_MOD_NAME);
	if (IS_ERR(g_bifur_class)) {
		unregister_chrdev_region(bdev->cdev_id, 1);
		cdev_del(&bdev->cdev);
		pr_err("Bifur create class fail.");
		return -EEXIST;
	}

	device = device_create(g_bifur_class, NULL, bdev->cdev_id, NULL, "%s", name);
	if (IS_ERR(device)) {
		class_destroy(g_bifur_class);
		unregister_chrdev_region(bdev->cdev_id, 1);
		cdev_del(&bdev->cdev);
		pr_err("Bifur cdev(%s) create device fail.", name);
		return -EFAULT;
	}

	mutex_init(&g_bifur_global_file_list.lock);
	INIT_LIST_HEAD(&g_bifur_global_file_list.list);
	pr_info("Bifur create global cdev(%s) succeed.", name);

	return 0;
}
