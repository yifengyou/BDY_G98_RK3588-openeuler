// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */
#include "bifur_common.h"
#include "bifur_pfile.h"
#include "bifur_main.h"
#include "bifur_vf_mgr.h"

#define PDE_DATA(inode) pde_data(inode)

int bifur_alloc_vf_mgr(struct bifur_lld_dev *bifur_dev)
{
	struct bifur_vf_mgr *vf_mgr = NULL;
	struct bifur_vf_info *vf_info = NULL;

	vf_mgr = kzalloc(sizeof(*vf_mgr), GFP_KERNEL);
	if (!vf_mgr) {
		BIFUR_DEV_ERR(bifur_dev->lld_dev, "Alloc vf mgr failed\n");
		return -ENOMEM;
	}

	bifur_dev->vf_mgr = vf_mgr;
	vf_info = kzalloc(sizeof(*vf_info) * BIFUR_VF_NUM, GFP_KERNEL);
	if (!vf_info) {
		kfree(vf_mgr);
		BIFUR_DEV_ERR(bifur_dev->lld_dev, "Alloc vf info failed\n");
		return -ENOMEM;
	}

	vf_mgr->vf_sum = 0;
	vf_mgr->vf_in_use = 0;
	vf_mgr->vf_info = vf_info;
	mutex_init(&vf_mgr->vf_mgr_mutex);
	if (!vf_mgr->bifur_proc_root) {
		vf_mgr->bifur_proc_root = proc_mkdir_mode(BIFUR_MOD_NAME, BIFUR_PROC_DIR_MOD,
							  init_net.proc_net);
		if (!vf_mgr->bifur_proc_root) {
			kfree(vf_mgr);
			kfree(vf_info);
			bifur_dev->vf_mgr = NULL;
			BIFUR_DEV_ERR(bifur_dev->lld_dev, "Bifur create dir failed.");
			return -ENOMEM;
		}
	}

	BIFUR_DEV_INFO(bifur_dev->lld_dev, "Alloc vf mgr success\n");
	return 0;
}

void bifur_free_vf_mgr(struct bifur_lld_dev *bifur_dev)
{
	if (bifur_dev->vf_mgr && bifur_dev->vf_mgr->vf_info) {
		kfree(bifur_dev->vf_mgr->vf_info);
		bifur_dev->vf_mgr->vf_info = NULL;
	}

	if (bifur_dev->vf_mgr != NULL) {
		kfree(bifur_dev->vf_mgr);
		bifur_dev->vf_mgr = NULL;
	}

	remove_proc_entry(BIFUR_MOD_NAME, init_net.proc_net);
	BIFUR_DEV_INFO(bifur_dev->lld_dev, "Free vf mgr success\n");
}

struct bifur_vf_info *bifur_find_vf(struct bifur_vf_mgr *vf_mgr, u32 pf_dbdf)
{
	u32 i;
	struct bifur_vf_info *vf_info = NULL;

	mutex_lock(&vf_mgr->vf_mgr_mutex);
	for (i = 0; i < vf_mgr->vf_sum; ++i) {
		vf_info = &vf_mgr->vf_info[i];
		if (!vf_info->in_use) {
			vf_info->in_use = 1;
			vf_info->pf_dbdf = pf_dbdf;
			vf_mgr->vf_in_use++;
			mutex_unlock(&vf_mgr->vf_mgr_mutex);
			return vf_info;
		}
	}
	mutex_unlock(&vf_mgr->vf_mgr_mutex);

	return NULL;
}

void bifur_vf_info_hold(struct bifur_vf_info *dev)
{
	atomic_inc(&dev->refcount);
}

void bifur_vf_info_put(struct bifur_vf_info *dev)
{
	if (atomic_dec_and_test(&dev->refcount))
		pr_info("Dev(%s) pci_bdf(0x%x) comp complete.", dev->name, dev->vf_dbdf);
}

void bifur_dev_file_add(struct bifur_dev_file_t *dev_file)
{
	mutex_lock(&dev_file->dev->dev_file_mgt.lock);
	list_add_tail(&dev_file->node, &dev_file->dev->dev_file_mgt.list);
	mutex_unlock(&dev_file->dev->dev_file_mgt.lock);
}

void bifur_dev_file_del(struct bifur_dev_file_t *dev_file)
{
	mutex_lock(&dev_file->dev->dev_file_mgt.lock);
	list_del(&dev_file->node);
	mutex_unlock(&dev_file->dev->dev_file_mgt.lock);
}

int bifur_proc_open(struct inode *inode, struct file *filp)
{
	struct bifur_vf_info *dev = (struct bifur_vf_info *)PDE_DATA(file_inode(filp));
	struct bifur_dev_file_t *dev_file = kzalloc(sizeof(*dev_file), GFP_KERNEL);

	if (!dev_file)
		return -ENOMEM;

	atomic_set(&dev_file->refcount, 1);
	dev_file->dev = dev;
	bifur_vf_info_hold(dev_file->dev);

	bifur_dev_file_add(dev_file);
	filp->private_data = dev_file;

	pr_info("Open proc dev(%s) success, filp(%p).", dev->name, filp);

	return nonseekable_open(inode, filp);
}

int bifur_proc_close(struct inode *inode, struct file *filp)
{
	struct bifur_dev_file_t *dev_file = filp->private_data;
	struct bifur_vf_info *dev = dev_file->dev;
	struct bifur_vf_mgr *vf_mgr = dev->vf_mgr;

	pr_info("Close proc dev(%s), pci_bdf(0x%x), filp(%p).", dev_file->dev->name,
		dev_file->dev->vf_dbdf, filp);

	bifur_dev_file_del(dev_file);
	mutex_lock(&vf_mgr->vf_mgr_mutex);
	dev->in_use = 0;
	dev->pf_dbdf = 0;
	vf_mgr->vf_in_use--;
	bifur_vf_info_put(dev_file->dev);
	mutex_unlock(&vf_mgr->vf_mgr_mutex);

	memset(dev_file, 0, sizeof(*dev_file));
	kfree(dev_file);
	filp->private_data = NULL;

	return 0;
}

#ifdef HAVE_PROC_OPS
const struct proc_ops g_bifur_proc_fops = {
	.proc_open = bifur_proc_open,
	.proc_release = bifur_proc_close,
};
#else
const struct file_operations g_bifur_proc_fops = {
	.owner = THIS_MODULE,
	.open = bifur_proc_open,
	.llseek = NULL,
	.release = bifur_proc_close,
};
#endif

int bifur_dev_proc_build(struct bifur_vf_info *dev)
{
	struct proc_dir_entry *dir = NULL;
	char pci_dev_name[BIFURNAMSIZ] = { 0 };
	struct bifur_vf_mgr *vf_mgr = dev->vf_mgr;

	int ret = sprintf(pci_dev_name, "0x%x", dev->vf_dbdf);

	if (ret < 0) {
		pr_err("Bifur dev(%s) proc dir create fail, bdf(0x%x).", dev->name, dev->vf_dbdf);
		return -ENOEXEC;
	}

	dev->cdev.proc_dir = proc_mkdir_mode(pci_dev_name, BIFUR_PROC_DIR_MOD,
					     vf_mgr->bifur_proc_root);
	if (!dev->cdev.proc_dir) {
		pr_err("Bifur dev(%s) proc dir create fail.", dev->name);
		return -EINVAL;
	}

	dir = proc_create_data(BIFUR_CDEV_PROC_NAME, BIFUR_PROC_FILE_MOD,
						   dev->cdev.proc_dir, &g_bifur_proc_fops, dev);
	if (!dir) {
		remove_proc_entry(pci_dev_name, vf_mgr->bifur_proc_root);
		dev->cdev.proc_dir = NULL;
		pr_err("Bifur dev(%s) create card file failed.", dev->name);
		return -EPERM;
	}

	pr_info("Bifur dev(%p) name(%s,%s) proc build success.", dev, dev->name, pci_dev_name);
	return 0;
}

int bifur_dev_proc_destroy(struct bifur_vf_info *dev)
{
	char pci_dev_name[BIFURNAMSIZ] = { 0 };
	struct bifur_vf_mgr *vf_mgr = dev->vf_mgr;

	int ret = sprintf(pci_dev_name, "0x%x", dev->vf_dbdf);

	if (ret < 0) {
		pr_err("Bifur dev(%s) proc dir create fail, bdf(0x%x).", dev->name, dev->vf_dbdf);
		return -ENOEXEC;
	}

	remove_proc_entry(BIFUR_CDEV_PROC_NAME, dev->cdev.proc_dir);
	remove_proc_entry(pci_dev_name, vf_mgr->bifur_proc_root);
	dev->cdev.proc_dir = NULL;

	pr_info("Bifur dev(%s) proc destroy success, pci_dev_name(%s).", dev->name, pci_dev_name);

	return 0;
}

int bifur_find_pf_by_vf(struct bifur_vf_mgr *vf_mgr, u32 vf_dbdf, u32 *pf_dbdf)
{
	u32 i;
	struct bifur_vf_info *vf_info = NULL;

	mutex_lock(&vf_mgr->vf_mgr_mutex);
	for (i = 0; i < vf_mgr->vf_sum; ++i) {
		vf_info = vf_mgr->vf_info + i;
		if (vf_info->vf_dbdf == vf_dbdf && vf_info->in_use) {
			*pf_dbdf = vf_info->pf_dbdf;
			mutex_unlock(&vf_mgr->vf_mgr_mutex);
			return 0;
		}
	}
	mutex_unlock(&vf_mgr->vf_mgr_mutex);

	return -EINVAL;
}

int bifur_vf_cdev_init(struct bifur_vf_info *dev)
{
	int ret;

	mutex_init(&dev->dev_file_mgt.lock);
	INIT_LIST_HEAD(&dev->dev_file_mgt.list);

	ret = bifur_dev_proc_build(dev);
	if (ret != 0) {
		pr_err("Init dev build failed, ret(%d).", ret);
		return ret;
	}

	return 0;
}

void bifur_vf_cdev_uninit(struct bifur_vf_info *dev)
{
	(void)bifur_dev_proc_destroy(dev);
}
