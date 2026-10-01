/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_DEBUGFS_H__
#define __CDMA_DEBUGFS_H__

#include <ub/ubase/ubase_comm_debugfs.h>

struct cdma_dev;

enum cdma_dbg_dentry_type {
	CDMA_DBG_DENTRY_CONTEXT,
	CDMA_DBG_DENTRY_RES_INFO,
	CDMA_DBG_DENTRY_ENTRY_INFO,
	/* must be the last entry. */
	CDMA_DBG_DENTRY_ROOT,
};

/* ctx debugfs start */
struct cdma_ctx_info {
	u32 start_idx;
	u32 ctx_size;
	u8 op;
	const char *ctx_name;
};

enum cdma_dbg_ctx_type {
	CDMA_DBG_JFS_CTX = 0,
	CDMA_DBG_SQ_JFC_CTX = 1,
};
/* ctx debugfs end */

struct cdma_dbgfs_cfg_info {
	const char *name;
	bool dentry_valid[CDMA_DBG_DENTRY_ROOT];
	const struct file_operations file_ops;
};

struct cdma_dbgfs_cfg {
	u32 queue_id;
	u32 entry_pi;
	u32 entry_ci;
};

enum cdma_dbgfs_cfg_type {
	CDMA_QUEUE_ID = 0,
	CDMA_ENTRY_PI,
	CDMA_ENTRY_CI
};

struct cdma_dbgfs {
	struct ubase_dbgfs dbgfs;
	struct cdma_dbgfs_cfg cfg;
};

int cdma_dbg_init(struct cdma_dev *cdev);
void cdma_dbg_uninit(struct cdma_dev *cdev);

#endif /* __CDMA_DEBUGFS_H__ */
