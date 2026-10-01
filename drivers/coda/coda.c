// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024. Huawei Technologies Co., Ltd. All rights reserved.
 */
#include <linux/kvm_host.h>
#include <linux/iommu.h>
#include <linux/io-pgtable.h>
#include <asm/virtcca_coda.h>

/*
 * This linked list stores a list of devices managed by CoDA,
 * and if the device is identified as secure in the list, it's called CC device.
 */
struct coda_dev_hash_node {
	u32               sid;			/* BDF number of the device */
	u32               vmid;			/* VM id */
	u32               root_bd;		/* Root bus and device number. */
	u32               vm_type;		/* 0:none; 1:host; 2:nvm; 3:cvm */
	u32               host_s2vmid;	/* Host SMMU s2vmid */
	u64               s_smmu_id;	/* The security SMMU id for the device */
	u64               msi_addr;		/* MSI addr for CC device */
	struct hlist_node node;			/* Device hash table node */
	bool              secure;		/* Device secure attribute */
};

static DEFINE_HASHTABLE(g_coda_dev_htable, MAX_CC_DEV_NUM_ORDER);

/* The lock during the operation of the CoDA mananged devices linked list */
static DEFINE_SPINLOCK(coda_dev_lock);

/* Protect root port status from racing */
static DEFINE_SPINLOCK(pcipc_enable_lock);

/**
 * get_root_bd - Traverse pcie topology to find the root <bus,device> number
 * @dev: The device for which to get root bd
 *
 * Returns:
 * %-1 if error or not pci device
 */
static int get_root_bd(struct device *dev)
{
	struct pci_dev *pdev;

	if (!dev_is_pci(dev))
		return -1;

	pdev = to_pci_dev(dev);
	if (pdev->bus == NULL)
		return -1;

	/*
	 * If pdev is virtual function, it is necessary
	 * to find its parent physical function
	 * before calling the pci_is_root_bus interface.
	 */
	if (pdev->is_virtfn)
		pdev = pci_physfn(pdev);

	while (!pci_is_root_bus(pdev->bus))
		pdev = pci_upstream_bridge(pdev);

	return pci_dev_id(pdev) & MASK_DEV_FUNCTION;
}

/**
 * get_child_devices_rec - Traverse pcie topology to find child devices
 * If dev is a bridge, get it's children
 * If dev is a regular device, get itself
 * @dev: Device for which to get child devices
 * @devs: All child devices under input dev
 * @max_devs: Max num of devs
 * @ndev: Num of child devices
 * @pdevs: All child pci_dev under input dev
 */
static void get_child_devices_rec(struct pci_dev *dev, uint16_t *devs,
	int max_devs, int *ndev, struct pci_dev **pdevs)
{
	struct pci_bus *bus = dev->subordinate;

	if (bus) { /* dev is a bridge */
		struct pci_dev *child;

		list_for_each_entry(child, &bus->devices, bus_list) {
			get_child_devices_rec(child, devs, max_devs, ndev, pdevs);
		}
	} else { /* dev is a regular device */
		uint16_t bdf = pci_dev_id(dev);
		int i;
		/* check if bdf is already in devs */
		for (i = 0; i < *ndev; i++) {
			if (devs[i] == bdf)
				return;
		}
		/* check overflow */
		if (*ndev >= max_devs) {
			pr_warn("S_SMMU: devices num over max devs\n");
			return;
		}
		devs[*ndev] = bdf;
		pdevs[*ndev] = dev;
		*ndev = *ndev + 1;
	}
}

/**
 * get_sibling_devices - Get all devices which share the same root_bd as dev
 * @dev: Device for which to get child devices
 * @devs: All child devices under input dev
 * @pdevs: All child pci_dev under input dev
 * @max_devs: Max num of devs
 *
 * Returns:
 * %0 if get child devices failure
 */
static int get_sibling_devices(struct device *dev, uint16_t *devs,
							   struct pci_dev **pdevs, int max_devs)
{
	struct pci_dev *pdev;
	int ndev = 0;

	if (!dev_is_pci(dev))
		return ndev;

	pdev = to_pci_dev(dev);
	if (pdev->bus == NULL)
		return ndev;

	/*
	 * If pdev is virtual function, it is necessary
	 * to find its parent physical function
	 * before calling the pci_is_root_bus interface.
	 */
	if (pdev->is_virtfn) {
		devs[ndev] = pci_dev_id(pdev);
		pdevs[ndev] = pdev;
		ndev = ndev + 1;
		pdev = pci_physfn(pdev);
	}

	while (!pci_is_root_bus(pdev->bus))
		pdev = pci_upstream_bridge(pdev);

	get_child_devices_rec(pdev, devs, max_devs, &ndev, pdevs);
	return ndev;
}

/**
 * add_coda_dev_obj - Add device obj to CoDA managed devices hash table
 * @node: Struct of CoDA device config
 *
 * Returns:
 * %0 if add obj success
 * %-ENOMEM if alloc obj failed
 */
static int add_coda_dev_obj(struct coda_dev_hash_node *node)
{
	struct coda_dev_hash_node *obj;

	spin_lock(&coda_dev_lock);
	hash_for_each_possible(g_coda_dev_htable, obj, node, node->sid) {
		if (obj->sid == node->sid) {
			obj->vmid = node->vmid;
			obj->root_bd = node->root_bd;
			obj->secure = node->secure;
			obj->vm_type = node->vm_type;

			if (node->host_s2vmid != 0)
				obj->host_s2vmid = node->host_s2vmid;

			obj->msi_addr = 0;
			obj->s_smmu_id = node->s_smmu_id;
			spin_unlock(&coda_dev_lock);
			return 0;
		}
	}

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj) {
		spin_unlock(&coda_dev_lock);
		return -ENOMEM;
	}

	obj->sid = node->sid;
	obj->vmid = node->vmid;
	obj->root_bd = node->root_bd;
	obj->vm_type = node->vm_type;
	obj->host_s2vmid = node->host_s2vmid;
	obj->s_smmu_id = node->s_smmu_id;
	obj->secure = node->secure;
	hash_add(g_coda_dev_htable, &obj->node, node->sid);
	spin_unlock(&coda_dev_lock);

	return 0;
}

/**
 * add_coda_pci_dev - Add pci device to CoDA managed devices hash table
 * @pdev: Struct of pci device
 *
 * Returns:
 * %0 if add obj success
 */
int add_coda_pci_dev(struct pci_dev *pdev)
{
	struct coda_dev_hash_node node = {0};

	node.sid = pci_dev_id(pdev);
	node.secure = true;
	node.vm_type = CC_DEV_HOST_TYPE;
	return add_coda_dev_obj(&node);
}

/**
 * delete_coda_dev_obj - Delete device obj to CoDA hash table
 * @sid: Stream id of dev
 *
 */
static void delete_coda_dev_obj(u32 sid)
{
	struct coda_dev_hash_node *obj;

	spin_lock(&coda_dev_lock);
	hash_for_each_possible(g_coda_dev_htable, obj, node, sid) {
		if (obj != NULL && obj->sid == sid) {
			hash_del(&obj->node);
			kfree(obj);
			spin_unlock(&coda_dev_lock);
			return;
		}
	}
	spin_unlock(&coda_dev_lock);
}

/**
 * is_cc_root_bd - Whether the root port is secure or not
 * @root_bd: Root port bus device num
 *
 * Returns:
 * %true if the root bd is secure
 * %false if the root bd is non-secure
 */
bool is_cc_root_bd(u32 root_bd)
{
	int bkt;
	struct coda_dev_hash_node *obj;

	spin_lock(&coda_dev_lock);
	hash_for_each(g_coda_dev_htable, bkt, obj, node) {
		if (obj->root_bd == root_bd && obj->secure) {
			spin_unlock(&coda_dev_lock);
			return true;
		}
	}

	spin_unlock(&coda_dev_lock);
	return false;
}

/**
 * is_cc_vmid - Whether the VM is confidential VM
 * @vmid: VM id
 *
 * Returns:
 * %true if the VM is confidential
 * %false if the VM is not confidential
 */
bool is_cc_vmid(u32 vmid, u64 s_smmu_id)
{
	int bkt;
	struct coda_dev_hash_node *obj;
	bool secure = false;

	spin_lock(&coda_dev_lock);
	hash_for_each(g_coda_dev_htable, bkt, obj, node) {
		if (vmid > 0 && obj->vmid == vmid && obj->s_smmu_id == s_smmu_id) {
			secure = obj->secure;
			spin_unlock(&coda_dev_lock);
			return secure;
		}
	}

	spin_unlock(&coda_dev_lock);
	return secure;
}
EXPORT_SYMBOL_GPL(is_cc_vmid);

/**
 * is_cc_dev - If the device is switch to secure world by PCIe protection controller,
 * it's called cc dev.
 * @sid: Stream id of dev
 *
 * Returns:
 * %true if the dev is confidential
 * %false if the dev is not confidential
 */
bool is_cc_dev(u32 sid)
{
	struct coda_dev_hash_node *obj;
	unsigned long flags;
	bool secure = false;

	spin_lock_irqsave(&coda_dev_lock, flags);
	hash_for_each_possible(g_coda_dev_htable, obj, node, sid) {
		if (obj != NULL && obj->sid == sid) {
			secure = obj->secure;
			spin_unlock_irqrestore(&coda_dev_lock, flags);
			return secure;
		}
	}

	spin_unlock_irqrestore(&coda_dev_lock, flags);
	return secure;
}
EXPORT_SYMBOL(is_cc_dev);

/**
 * get_g_cc_dev_msi_addr - Obtain the msi address of confidential device
 * @sid: Stream id of dev
 *
 * Returns:
 * %0 if does not find the confidential device that matches the stream id
 * %msi_addr return the msi address of confidential device that matches the stream id
 */
u64 get_g_cc_dev_msi_addr(u32 sid)
{
	struct coda_dev_hash_node *obj;
	u64 msi_addr = 0;

	spin_lock(&coda_dev_lock);
	hash_for_each_possible(g_coda_dev_htable, obj, node, sid) {
		if (obj != NULL && obj->sid == sid && obj->secure) {
			msi_addr = obj->msi_addr;
			spin_unlock(&coda_dev_lock);
			return msi_addr;
		}
	}

	spin_unlock(&coda_dev_lock);
	return msi_addr;
}
EXPORT_SYMBOL_GPL(get_g_cc_dev_msi_addr);

/**
 * set_g_cc_dev_msi_addr - Set the msi address of confidential device
 * @sid: Stream id of dev
 * @msi_addr: Msi address
 */
void set_g_cc_dev_msi_addr(u32 sid, u64 msi_addr)
{
	struct coda_dev_hash_node *obj;

	spin_lock(&coda_dev_lock);
	hash_for_each_possible(g_coda_dev_htable, obj, node, sid) {
		if (obj != NULL && obj->sid == sid && !obj->msi_addr && obj->secure) {
			obj->msi_addr = msi_addr;
			spin_unlock(&coda_dev_lock);
			return;
		}
	}
	spin_unlock(&coda_dev_lock);
}

/**
 * get_g_coda_dev_vm_type - Obtain the VM type of CoDA device
 * @sid: Stream id of dev
 *
 * Returns:
 * %vm_type return the VM type of confidential device that matches the stream id
 * %CC_DEV_NONE_TYPE if does not find the confidential device that matches the stream id
 */
u32 get_g_coda_dev_vm_type(u32 sid)
{
	struct coda_dev_hash_node *obj;
	u32 vm_type = CC_DEV_NONE_TYPE;

	spin_lock(&coda_dev_lock);
	hash_for_each_possible(g_coda_dev_htable, obj, node, sid) {
		if (obj != NULL && obj->sid == sid) {
			vm_type = obj->vm_type;
			spin_unlock(&coda_dev_lock);
			return vm_type;
		}
	}

	spin_unlock(&coda_dev_lock);
	return vm_type;
}

/**
 * get_g_cc_dev_host_s2vmid - Obtain the host s2vmid of confidential device
 * @sid: Stream id of dev
 *
 * Returns:
 * %host_s2vmid return the host s2vmid of confidential device that matches the stream id
 * %0 if does not find the confidential device that matches the stream id
 */
static u32 get_g_cc_dev_host_s2vmid(u32 sid)
{
	struct coda_dev_hash_node *obj;
	u32 host_s2vmid = 0;

	spin_lock(&coda_dev_lock);
	hash_for_each_possible(g_coda_dev_htable, obj, node, sid) {
		if (obj != NULL && obj->sid == sid && obj->secure) {
			host_s2vmid = obj->host_s2vmid;
			spin_unlock(&coda_dev_lock);
			return host_s2vmid;
		}
	}

	spin_unlock(&coda_dev_lock);
	return host_s2vmid;
}

/* CoDA managed devices hash table init */
void g_coda_dev_table_init(void)
{
	hash_init(g_coda_dev_htable);
}
EXPORT_SYMBOL(g_coda_dev_table_init);

/**
 * virtcca_tmi_dev_attach - Complete the stage2 page table establishment
 * for the CC device
 * @arm_smmu_domain: The handle of SMMU domain
 * @kvm: The handle of VM
 *
 * Returns:
 * %0 if attach dev success
 * %-ENXIO if the root port of device does not have pcipc capability
 */
u32 virtcca_tmi_dev_attach(struct arm_smmu_domain *arm_smmu_domain, struct kvm *kvm)
{
	unsigned long flags;
	int i, j;
	struct arm_smmu_master *master;
	struct arm_smmu_master_domain *master_domain;
	int ret = 0;
	u64 cmd[CMDQ_ENT_DWORDS] = {0};
	struct virtcca_cvm *virtcca_cvm = kvm->arch.virtcca_cvm;

	spin_lock_irqsave(&arm_smmu_domain->devices_lock, flags);
	/*
	 * Traverse all devices under the secure smmu domain and
	 * set the correspnding address translation table for each device
	 */
	list_for_each_entry(master_domain, &arm_smmu_domain->devices, devices_elm) {
		master = master_domain->master;
		if (master && master->num_streams >= 0) {
			for (i = 0; i < master->num_streams; i++) {
				u32 sid = master->streams[i].id;

				cmd[0] = 0;
				cmd[1] = 0;
				for (j = 0; j < i; j++)
					if (master->streams[j].id == sid)
						break;
				if (j < i)
					continue;
				ret = tmi_dev_attach(sid, virtcca_cvm->rd,
					arm_smmu_domain->smmu->s_smmu_id,
					arm_smmu_domain->s2_cfg.vmid);
				if (ret) {
					dev_err(arm_smmu_domain->smmu->dev, "CoDA: dev protected failed!\n");
					ret = -ENXIO;
					goto out;
				}
				/* Need to config STE */
				cmd[0] |= FIELD_PREP(CMDQ_0_OP, CMDQ_OP_CFGI_STE);
				cmd[0] |= FIELD_PREP(CMDQ_CFGI_0_SID, sid);
				cmd[1] |= FIELD_PREP(CMDQ_CFGI_1_LEAF, true);
				tmi_smmu_queue_write(cmd[0], cmd[1],
					arm_smmu_domain->smmu->s_smmu_id);
			}
		}
	}

out:
	spin_unlock_irqrestore(&arm_smmu_domain->devices_lock, flags);
	return ret;
}

/* When a PF hosts numerous VFs, the tmi_dev_delegate operation may exceed
 * acceptable latency thresholds. To mitigate SOFTLOCKUP risks, the process
 * must be split into batches with sufficiently small device counts per batch.
 */
static inline int tmi_dev_delegate_batch(struct tmi_dev_delegate_params *params)
{
		u8 i, j;
		struct tmi_dev_delegate_params *p;
		int ret;

		p = kzalloc(sizeof(*p), GFP_KERNEL);
		if (!p)
			return -ENOMEM;

		p->root_bd = params->root_bd;

		for (i = 0; i < params->num_dev; i += 4) {
			for (j = 0; i + j < params->num_dev && j < 4; j++)
				p->devs[j] = params->devs[i + j];
			p->num_dev = j;
			if (i + j == params->num_dev)
				p->last_batch = 1;

			ret = tmi_dev_delegate(__pa(p));
			if (ret)
				break;
		}

		kfree(p);
		return ret;
}

/**
 * virtcca_delegate_cc_dev - Delegate device to secure world, calling tmi_dev_delegate
 * to enable the PCIPC function of the root port, and managing all secure devices under
 * the root port by adding them to the CoDA management linked list.
 * @smmu: An SMMUv3 instance
 * @root_bd: The port where the CC device is located
 * @dev: CC device
 * @params: Delegate device parameters
 * @s2vmid: SMMU STE s2vmid
 *
 * Returns:
 * %0 if delegate success
 * %-ENOMEM if alloc params failed
 * %-EINVAL if the dev is invalid
 */
static inline int virtcca_delegate_cc_dev(uint16_t root_bd, struct arm_smmu_device *smmu,
	struct device *dev, struct tmi_dev_delegate_params *params, uint16_t *s2vmid)
{
	int i;
	u64 ret = 0;
	struct coda_dev_hash_node node = {0};

	dev_info(smmu->dev, "CoDA: Delegate %d devices as %02x:%02x to secure\n",
			params->num_dev, root_bd >> DEV_BUS_NUM,
			(root_bd & MASK_DEV_BUS) >> DEV_FUNCTION_NUM);
	ret = tmi_dev_delegate_batch(params);
	if (ret) {
		dev_err(smmu->dev, "CoDA: failed to delegate device to secure\n");
		goto out;
	}

	for (i = 0; i < params->num_dev; i++) {
		/* Add the CC device information to the CoDA mananged devices linked list. */
		node.sid = params->devs[i];
		node.vmid = s2vmid[i];
		node.root_bd = root_bd;
		node.vm_type = CC_DEV_HOST_TYPE;
		node.host_s2vmid = s2vmid[i];
		node.s_smmu_id = smmu->s_smmu_id;
		node.secure = true;
		ret = add_coda_dev_obj(&node);
		if (ret) {
			dev_err(smmu->dev, "CoDA: failed to add cc dev to CoDA management linked list\n");
			break;
		}
	}

out:
	return ret;
}

/**
 * add_cc_dev_to_coda_dev_table - Add CC device to CoDA managed devices hash table
 * @smmu: An SMMUv3 instance
 * @smmu_domain: The handle of smmu_domain
 * @root_bd: The port where the CC device is located
 * @master: SMMU private data for each master
 *
 * Returns:
 * %0 if add to hash table success
 * %-ENOMEM if alloc obj failed
 * %-EINVAL if stream id is invalid
 */
static inline int add_cc_dev_to_coda_dev_table(struct arm_smmu_device *smmu,
	struct arm_smmu_domain *smmu_domain, uint16_t root_bd, struct arm_smmu_master *master)
{
	int i, j;
	struct coda_dev_hash_node node = {0};
	u64 ret = 0;

	for (i = 0; i < master->num_streams; i++) {
		u32 sid = master->streams[i].id;

		for (j = 0; j < i; j++)
			if (master->streams[j].id == sid)
				break;
		if (j < i)
			continue;

		if (!is_cc_dev(sid)) {
			dev_err(smmu->dev, "CoDA: sid 0x%x is not CC dev\n", sid);
			return -EINVAL;
		}

		/* Add the confidential device information to the CoDA mananged devices list */
		node.sid = sid;
		node.vmid = smmu_domain->s2_cfg.vmid;
		node.root_bd = root_bd;
		node.vm_type = CC_DEV_CVM_TYPE;
		node.host_s2vmid = 0;
		node.s_smmu_id = smmu->s_smmu_id;
		node.secure = true;
		ret = add_coda_dev_obj(&node);
		if (ret) {
			dev_err(smmu->dev, "Failed to add cc dev 0x%x to CoDA linked list\n", sid);
			break;
		}
	}
	return ret;
}

/**
 * virtcca_enable_cc_dev - Enable the PCIe protection controller function
 * of the CC device
 * @smmu_domain: The handle of smmu_domain
 * @master: SMMU private data for each master
 * @dev: CC device
 * @params: Delegate device parameters
 * @s2vmid: SMMU STE s2vmid
 *
 * Returns:
 * %0 if the root port of CC dev successfully set up PCIPC capability
 * %-ENOMEM alloc STE params failed
 * %-EINVAL set STE config content failed
 */
static int virtcca_enable_cc_dev(struct arm_smmu_domain *smmu_domain,
	struct arm_smmu_master *master, struct device *dev,
	struct tmi_dev_delegate_params *params, uint16_t *s2vmid)
{
	u64 ret = 0;
	uint16_t root_bd = get_root_bd(dev);
	struct arm_smmu_device *smmu = smmu_domain->smmu;

	/* To prevent duplicate enabling of PCIPC. */
	if (!is_cc_root_bd(root_bd)) {
		ret = virtcca_delegate_cc_dev(root_bd, smmu, dev, params, s2vmid);
		if (ret)
			return ret;
	}

	ret = add_cc_dev_to_coda_dev_table(smmu, smmu_domain, root_bd, master);
	return ret;
}

/**
 * virtcca_create_ste_entry -  Call the tmi interface to set single STE entry
 * in the SMMU for the corresponding device
 * @smmu: An SMMUv3 instance
 * @master: SMMU private data for each master
 * @sid: Stream id
 * @host: Host driver or nvm stage2
 * @new_vf: After enabling the PCIPC feature, the VF that needs to be created
 *
 * Returns:
 * %0 if set STE config content success
 * %-ENOMEM alloc STE params failed
 * %-EINVAL set STE config content failed
 */
static int virtcca_create_ste_entry(struct pci_dev *pci_dev, bool host, bool new_vf)
{
	int ret = 0;
	struct tmi_device_create_params *params_ptr = NULL;
	struct arm_smmu_domain *smmu_domain = NULL;
	struct io_pgtable *data;
	struct io_pgtable_cfg *cfg;
	struct arm_smmu_s2_cfg *s2_cfg;

	params_ptr = kzalloc(sizeof(*params_ptr), GFP_KERNEL);
	if (!params_ptr)
		return -ENOMEM;

	/*
	 * If the stage 2 to be set in the secure SMMU for the device is for the host driver side,
	 * it is obtained via the default_domain. If the device is assigned to a CVM,
	 * the stage 2 set in the secure SMMU needs to be obtained via the domain.
	 */
	if (host)
		smmu_domain = to_smmu_domain(iommu_get_dma_domain(&(pci_dev->dev)));
	else
		smmu_domain = to_smmu_domain(iommu_get_domain_for_dev(&(pci_dev->dev)));

	if (!smmu_domain->smmu) {
		dev_err(&(pci_dev->dev), "CoDA: Security smmu has not been correctly set\n");
		kfree(params_ptr);
		return -EINVAL;
	}

	data = io_pgtable_ops_to_pgtable(smmu_domain->pgtbl_ops);
	cfg = &data->cfg;
	typeof(&cfg->arm_lpae_s2_cfg.vtcr) vtcr = &cfg->arm_lpae_s2_cfg.vtcr;

	s2_cfg = &smmu_domain->s2_cfg;
	/* Set ste config */
	params_ptr->sid = pci_dev_id(pci_dev);
	params_ptr->smmu_id = smmu_domain->smmu->s_smmu_id;
	params_ptr->root_bd = get_root_bd(&pci_dev->dev);
	params_ptr->s2vmid = s2_cfg->vmid;
	params_ptr->s2ttb = cfg->arm_lpae_s2_cfg.vttbr;
	params_ptr->s2t0sz = vtcr->tsz;
	params_ptr->s2sl0 = vtcr->sl;
	params_ptr->host = host;
	params_ptr->new_vf = new_vf;
	ret = tmi_dev_create(__pa(params_ptr));

	kfree(params_ptr);
	return ret;
}

static int arm_s_smmu_streams_cmp_key(const void *lhs, const struct rb_node *rhs)
{
	struct arm_smmu_stream *stream_rhs =
		rb_entry(rhs, struct arm_smmu_stream, node);
	const u32 *sid_lhs = lhs;

	if (*sid_lhs < stream_rhs->id)
		return -1;
	if (*sid_lhs > stream_rhs->id)
		return 1;
	return 0;
}

static inline struct arm_smmu_master *
arm_s_smmu_find_master(struct arm_smmu_device *smmu, u32 sid)
{
	struct rb_node *node;

	lockdep_assert_held(&smmu->streams_mutex);

	node = rb_find(&sid, &smmu->streams, arm_s_smmu_streams_cmp_key);
	if (!node)
		return NULL;
	return rb_entry(node, struct arm_smmu_stream, node)->master;
}

/**
 * virtcca_create_ste_entries - Setting up STE entries for all the devices under the same root port
 * @smmu: An SMMUv3 instance
 * @dev: CC device
 * @params: Delegate device parameters
 * @s2vmid: SMMU STE s2vmid
 *
 * Return
 * %0 if the STE tables on all devices under the root port are set successfully
 * %-ENOMEM alloc STE params failed
 * %-EINVAL set STE config content failed or does not find corresponding master info
 */
static int virtcca_create_ste_entries(struct arm_smmu_device *smmu,
	struct device *dev, struct tmi_dev_delegate_params *params, uint16_t *s2vmid)
{
	int ret = 0;
	struct arm_smmu_master *master = NULL;
	struct iommu_domain *domain = NULL;
	struct arm_smmu_domain *smmu_domain = NULL;

	/*
	 * Because the PCIPC function will switch all devices under the root port to
	 * the secure state, all devices stage2 under the root port need to be translated
	 * through the secure SMMU. Therefore, before switching to secure state,
	 * it is necessary to configure the security SMMU STE entries
	 * for all PF and VF under the corresponding root port
	 */
	for (int i = 0; i < params->num_dev; i++) {
		mutex_lock(&smmu->streams_mutex);
		master = arm_s_smmu_find_master(smmu, params->devs[i]);
		if (!master) {
			ret = -EINVAL;
			mutex_unlock(&smmu->streams_mutex);
			return ret;
		}
		mutex_unlock(&smmu->streams_mutex);
		domain = iommu_get_dma_domain(master->dev);
		smmu_domain = to_smmu_domain(domain);
		s2vmid[i] = smmu_domain->s2_cfg.vmid;
		ret = virtcca_create_ste_entry(to_pci_dev(master->dev), true, false);
		if (ret) {
			pr_err("Failed to create dev 0x%x STE\n", params->devs[i]);
			return ret;
		}
	}

	return ret;
}

/**
 * virtcca_check_dev_is_assigned_to_nvm - Check whether any device has already been assigned
 * to normal VM if the PCIPC is enabled for the root port
 * @params: Delegate device parameters
 *
 * Returns:
 * %true if device has been passthrough nvm or sid is invalid
 * %false if the root port can enable PCIPC
 */
static bool virtcca_check_dev_is_assigned_to_nvm(struct tmi_dev_delegate_params *params)
{
	for (int i = 0; i < params->num_dev; i++) {
		if (get_g_coda_dev_vm_type(params->devs[i]) == CC_DEV_NVM_TYPE) {
			pr_err("CoDA: device sid 0x%x has already been assigned to nvm\n",
				params->devs[i]);
			return true;
		}
	}
	return false;
}

/**
 * virtcca_get_all_cc_dev_info - Retrieve all devices under the root port
 * @dev: CC device
 * @params: Delegate device parameters
 * @pdevs: CC struct pci_dev pointers
 *
 * Returns:
 * %0 if get all devices under the root port successful
 * %-EINVAL if the total number of devices under the root port exceeds the maximum
 */
static int virtcca_get_all_cc_dev_info(struct device *dev,
	struct tmi_dev_delegate_params *params, struct pci_dev **pdevs)
{
	int ret = 0;
	uint16_t root_bd = get_root_bd(dev);

	params->root_bd = root_bd;
	params->num_dev = get_sibling_devices(dev, params->devs, pdevs, MAX_DEV_PER_PORT);
	if (params->num_dev >= MAX_DEV_PER_PORT) {
		ret = -EINVAL;
		return ret;
	}
	return ret;
}

/**
 * virtcca_destroy_devices - Destroy device security SMMU STE table under the root port
 * @params: Delegate device parameters
 *
 */
static void virtcca_destroy_devices(struct tmi_dev_delegate_params *params)
{
	for (int i = 0; i < params->num_dev; i++)
		tmi_dev_destroy(params->devs[i], true);
}

/**
 * virtcca_create_cc_dev_ste - Traverse the devices under the root port and set the
 * secure SMMU STE table for them
 * @smmu: An SMMUv3 instance
 * @pdevs: CC struct pci_dev pointers
 * @dev: CC device
 * @params: Delegate device parameters
 * @s2vmid: SMMU STE s2vmid
 *
 * Return
 * %0 if set STE success
 * %-EINVAL set STE config content failed or does not find corresponding master info
 */
static int virtcca_create_cc_dev_ste(struct arm_smmu_device *smmu, struct pci_dev **pdevs,
	struct device *dev, struct tmi_dev_delegate_params *params, uint16_t *s2vmid)
{
	int ret = 0;
	uint16_t root_bd = get_root_bd(dev);

	if (!is_cc_root_bd(root_bd)) {
		/* Get all devices information under the same root port */
		ret = virtcca_get_all_cc_dev_info(dev, params, pdevs);
		if (ret)
			return ret;

		/*
		 * To determine if the PCIPC functionality of the root port can be enabled,
		 * If the device under the root port that need to enable PCIPC function is
		 * assigned to a normal VM, enabling PCIPC feature will switch
		 * the device to a secure world, resulting in the normal VM
		 * not functioning properly.
		 */
		ret = virtcca_check_dev_is_assigned_to_nvm(params);
		if (ret)
			return ret;

		/* Setting up STE entries for all the devices under the same root port */
		ret = virtcca_create_ste_entries(smmu, dev, params, s2vmid);
		if (ret)
			return ret;
	}
	return ret;
}

/* Unbind the drivers before switching to secure state.
 * In SR-IOV scenaios： Unbind all VFs' native drivers.
 * Otherwise: Unbind the PF's native drivers.
 */
static int virtcca_unbind_drivers(struct pci_dev **pdevs, uint16_t nr)
{
	bool is_sriov = false;

	for (uint16_t i = 0; i < nr; i++) {
		if (!pdevs[i])
			return -EINVAL;
		if (pdevs[i] == pci_physfn(pdevs[i]))
			continue;
		is_sriov = true;
		/* Only unbind the VF with its own driver */
		if (!pdevs[i]->driver ||
		    !strcmp(pdevs[i]->driver->name, "vfio-pci"))
			continue;
		device_release_driver(&pdevs[i]->dev);
	}

	if (is_sriov)
		return 0;
	for (uint16_t i = 0; i < nr; i++) {
		if (pdevs[i] == pci_physfn(pdevs[i]) && pdevs[i]->driver
		    && strcmp(pdevs[i]->driver->name, "vfio-pci"))
			device_release_driver(&pdevs[i]->dev);
	}
	return 0;
}

/**
 * virtcca_attach_each_dev_to_cvm - Attach each device under the same group to cvm,
 * attach device includes setting STE and enabling PCIPC.
 * @domain: The handle of iommu_domain
 * @dev: CC device
 *
 * Returns:
 * %0 if the domain does not need to enable secure or the domain
 * successfully set up security features
 * %-EINVAL if the SMMU does not initialize secure state
 * %-ENOMEM if the device create secure STE failed
 * %-ENOENT if the device does not have fwspec
 */
static int virtcca_attach_each_dev_to_cvm(struct device *dev, void *domain)
{
	int ret;
	struct iommu_domain *iommu_domain = (struct iommu_domain *)domain;
	uint16_t s2vmid[MAX_DEV_PER_PORT] = {0}; /* BDF under the root port */
	struct iommu_fwspec *fwspec = NULL;
	struct arm_smmu_device *smmu = NULL;
	struct arm_smmu_domain *smmu_domain = NULL;
	struct arm_smmu_master *master = NULL;
	struct tmi_dev_delegate_params *params = NULL;
	struct pci_dev **pdevs;

	if (!is_virtcca_cvm_enable())
		return 0;

	fwspec = dev_iommu_fwspec_get(dev);
	if (!fwspec)
		return -ENOENT;

	smmu_domain = to_smmu_domain(iommu_domain);
	master = dev_iommu_priv_get(dev);
	smmu = master->smmu;

	if (!smmu || !virtcca_smmu_enable(smmu)) {
		dev_err(dev, "CoDA: security smmu has not been initialized for the device\n");
		return -EINVAL;
	}

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params)
		return -ENOMEM;
	pdevs = kcalloc(MAX_DEV_PER_PORT, sizeof(struct pci_dev *), GFP_KERNEL);
	if (!pdevs)
		return -ENOMEM;
	/*
	 * When enabling the PCIPC function, setting the secure SMMU page table for all devices
	 * under the root port and adding to the linked list require atomic operations. This is
	 * to prevent the devices under the same root port from being assigned to different CVM,
	 * which could result in overwriting the STE table of a device under the same root port
	 * due to the delay in seeing the root port status.
	 */
	spin_lock(&pcipc_enable_lock);
	/*
	 * Obtain device information under the root port
	 * and set security SMMU STE table for it
	 */
	ret = virtcca_create_cc_dev_ste(smmu, pdevs, dev, params, s2vmid);
	if (ret)
		goto out;

	if (!is_cc_root_bd(get_root_bd(dev)))
		ret = virtcca_unbind_drivers(pdevs, params->num_dev);
	if (ret)
		goto out;

	/*
	 * Enable the PCIe protection controller function under the root port
	 */
	ret = virtcca_enable_cc_dev(smmu_domain, master, dev, params, s2vmid);
	if (ret)
		goto out;

	dev_info(smmu->dev, "CoDA: attach confidential dev: %s", dev_name(dev));

out:
	if (ret)
		virtcca_destroy_devices(params);

	spin_unlock(&pcipc_enable_lock);
	kfree(pdevs);
	kfree(params);
	return ret;
}

/**
 * virtcca_attach_each_dev_to_nvm - Attach each device under the same group to nvm
 * @dev: the struct of device
 * @domain: The handle of iommu_domain
 *
 * Returns:
 * %0 if the device is not a CC device
 * %-EINVAL if the device is a CC device, CC device is not allowed to assign to the normal VM
 */
static int virtcca_attach_each_dev_to_nvm(struct device *dev, void *domain)
{
	int i, j;
	u32 sid = 0;
	int ret = 0;
	struct coda_dev_hash_node node = {0};
	uint16_t root_bd;
	struct arm_smmu_device *smmu = NULL;
	struct arm_smmu_master *master = NULL;

	if (!is_virtcca_cvm_enable())
		return 0;

	master = dev_iommu_priv_get(dev);
	smmu = master->smmu;

	if (!virtcca_smmu_enable(smmu))
		return 0;

	root_bd = get_root_bd(dev);
	spin_lock(&pcipc_enable_lock);
	if (is_cc_root_bd(root_bd)) {
		dev_err(smmu->dev,
		"CoDA: the security device under the root port 0x%x is not allowed to assign to \
		the normal VM\n", root_bd);
		spin_unlock(&pcipc_enable_lock);
		return -EINVAL;
	}
	spin_unlock(&pcipc_enable_lock);

	for (i = 0; i < master->num_streams; i++) {
		sid = master->streams[i].id;
		/* Bridged PCI devices may end up with duplicated IDs */
		for (j = 0; j < i; j++)
			if (master->streams[j].id == sid)
				break;
		if (j < i)
			continue;

		node.sid = sid;
		node.vm_type = CC_DEV_NVM_TYPE;
		node.s_smmu_id = smmu->s_smmu_id;
		node.secure = false;
		ret = add_coda_dev_obj(&node);
		if (ret)
			pr_err("CoDA: attach device to nvm, add device 0x%x to CoDA linked list \
					failed\n", sid);
	}

	return 0;
}

/**
 * virtcca_detach_each_dev_from_vm -  Detach each device under the same group from nvm or cvm
 * 1、NVM：Delete the device information corresponding to the NVM from the CoDA management list
 * 2、CVM：Set the device that has already been assigned to the CVM in the CoDA management list
 * back to the host driver state and restore the device's STE stage 2 to the host driver
 * @dev: The struct of device
 * @domain: The handle of iommu_domain
 *
 */
static int virtcca_detach_each_dev_from_vm(struct device *dev, void *domain)
{
	int i, j;
	struct coda_dev_hash_node node = {0};
	int ret = 0;
	struct arm_smmu_device *smmu = NULL;
	struct arm_smmu_master *master = NULL;

	if (!is_virtcca_cvm_enable())
		return 0;

	master = dev_iommu_priv_get(dev);
	smmu = master->smmu;

	if (!virtcca_smmu_enable(smmu))
		return 0;

	for (i = 0; i < master->num_streams; i++) {
		u32 sid = master->streams[i].id;
		/* Bridged PCI devices may end up with duplicated IDs */
		for (j = 0; j < i; j++)
			if (master->streams[j].id == sid)
				break;
		if (j < i)
			continue;

		/*
		 * If the device is CC dev and is assigned to the cvm,
		 * we need call the tmi_dev_destroy to restore STE stage 2 to host driver
		 */
		if (is_cc_dev(sid)) {
			if (get_g_coda_dev_vm_type(sid) == CC_DEV_CVM_TYPE) {
				node.sid = sid;
				node.vm_type = CC_DEV_HOST_TYPE;
				node.host_s2vmid = 0;
				node.root_bd = get_root_bd(dev);
				node.s_smmu_id = smmu->s_smmu_id;
				node.vmid = get_g_cc_dev_host_s2vmid(sid);
				node.secure = true;
				ret = add_coda_dev_obj(&node);
				tmi_dev_destroy(sid, 0);
				if (ret)
					pr_err("CoDA: detach device from vm, add cc device 0x%x \
						failed\n", sid);
			}
		} else {
			delete_coda_dev_obj(sid);
		}
	}
	return 0;
}

/**
 * virtcca_attach_dev - The VFIO driver calls this interface to
 * attach the device to the VM
 * @domain: The handle of iommu domain
 * @group: Iommu group
 * @iommu_secure : Whether the iommu is secure or not
 *
 * Returns:
 * %0 if attach the all devices success
 * %-ENOMEM if the device create secure STE failed
 * %-ENOENT if the device does not have fwspec
 */
int virtcca_attach_dev(struct iommu_domain *domain, struct iommu_group *group,
	bool iommu_secure)
{
	int ret = 0;

	if (!is_virtcca_cvm_enable())
		return ret;

	if (iommu_secure)
		ret = iommu_group_for_each_dev(group, (void *)domain,
				virtcca_attach_each_dev_to_cvm);
	else
		ret = iommu_group_for_each_dev(group, (void *)domain,
				virtcca_attach_each_dev_to_nvm);

	return ret;
}
EXPORT_SYMBOL_GPL(virtcca_attach_dev);

/**
 * virtcca_detach_dev - The VFIO driver calls this interface to
 * detach the device from the VM
 * @domain: The handle of iommu domain
 * @group: Iommu group
 *
 */
void virtcca_detach_dev(struct iommu_domain *domain, struct iommu_group *group)
{
	if (!is_virtcca_cvm_enable())
		return;

	iommu_group_for_each_dev(group, (void *)domain, virtcca_detach_each_dev_from_vm);
	return;
}
EXPORT_SYMBOL_GPL(virtcca_detach_dev);

/**
 * virtcca_vdev_create - Create a VF device, call the tmi interface to set the security
 * ste configuration and add VF info to the CoDA management linked list.
 * @pci_dev: VF pci device
 *
 * Returns:
 * 0 for success
 */
int virtcca_vdev_create(struct pci_dev *pci_dev)
{
	int ret = 0, i, j;
	struct coda_dev_hash_node node = {0};
	struct arm_smmu_device *smmu = NULL;
	struct arm_smmu_master *master = NULL;
	struct arm_smmu_domain *smmu_domain = NULL;
	struct iommu_domain *domain = NULL;
	uint16_t root_bd = get_root_bd(&pci_dev->dev);

	domain = iommu_get_domain_for_dev(&pci_dev->dev);
	smmu_domain = to_smmu_domain(domain);
	master = dev_iommu_priv_get(&pci_dev->dev);
	smmu = master->smmu;
	if (!smmu || !virtcca_smmu_enable(smmu)) {
		dev_err(&pci_dev->dev, "CoDA: security SMMU has not been initialized for the device\n");
		return -EINVAL;
	}
	for (i = 0; i < master->num_streams; i++) {
		u32 sid = master->streams[i].id;
		/* Bridged PCI devices may end up with duplicated IDs */
		for (j = 0; j < i; j++)
			if (master->streams[j].id == sid)
				break;
		if (j < i)
			continue;

		/* Set single STE entry in the SMMU for the corresponding device */
		ret = virtcca_create_ste_entry(to_pci_dev(master->dev), true, true);
		if (ret) {
			/* In the SRIOV scenario, if a new VF (Virtual Function) is created on a
			 * root port where the PCIPC function has been enabled, the new VF will
			 * call the virtcca_vdev_create interface when binding any driver. However,
			 * the STE will not change after the first binding driver, so when
			 * TMI_ERROR_STE_CREATED is returned, this call is ignored.
			 */
			if (ret == TMI_ERROR_STE_CREATED) {
				ret = 0;
				continue;
			}
			pr_err("Failed to create vdev 0x%x STE\n", sid);
			return ret;
		}

		node.sid = sid;
		node.vmid = smmu_domain->s2_cfg.vmid;
		node.root_bd = root_bd;
		node.vm_type = CC_DEV_HOST_TYPE;
		node.host_s2vmid = smmu_domain->s2_cfg.vmid;
		node.s_smmu_id = smmu->s_smmu_id;
		node.secure = true;
		ret = add_coda_dev_obj(&node);
		if (ret) {
			pr_err("Failed to add vdev to CoDA linked list\n");
			break;
		}
	}
	return ret;
}
EXPORT_SYMBOL_GPL(virtcca_vdev_create);
