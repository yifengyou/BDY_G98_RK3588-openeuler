/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#ifndef BIFUR_VF_MGR_H__
#define BIFUR_VF_MGR_H__

#define BIFURNAMSIZ 20
#define BIFUR_PROC_FILE_MOD 0640
#define BIFUR_CDEV_PROC_NAME "bifur_vdev"

struct bifur_dev_file_mgt_t {
	struct mutex lock;
	struct list_head list;
};
struct bifur_lld_dev;
struct bifur_vf_mgr;

struct bifur_vf_info {
	char name[BIFURNAMSIZ];
	u32 vf_dbdf;
	u32 pf_dbdf;
	u16 glb_func_id;
	bool in_use;
	struct bifur_cdev cdev;
	struct bifur_dev_file_mgt_t dev_file_mgt;
	struct bifur_vf_mgr *vf_mgr;
	atomic_t refcount;
};

struct bifur_vf_mgr {
	u32 vf_sum;
	u32 vf_in_use;
	struct bifur_vf_info *vf_info;
	struct mutex vf_mgr_mutex;
	struct proc_dir_entry *bifur_proc_root;
};

struct bifur_dev_file_t {
	struct list_head node;
	struct bifur_vf_info *dev;

	atomic_t refcount;
};

int bifur_alloc_vf_mgr(struct bifur_lld_dev *bifur_dev);

void bifur_free_vf_mgr(struct bifur_lld_dev *bifur_dev);

struct bifur_vf_info *bifur_find_vf(struct bifur_vf_mgr *vf_mgr, u32 dbdf);

int bifur_find_pf_by_vf(struct bifur_vf_mgr *vf_mgr, u32 vf_dbdf, u32 *pf_dbdf);

int bifur_vf_cdev_init(struct bifur_vf_info *dev);
void bifur_vf_cdev_uninit(struct bifur_vf_info *dev);

#endif
