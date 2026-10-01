/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UNIC_CTX_DEBUGFS_H__
#define __UNIC_CTX_DEBUGFS_H__

int unic_dbg_dump_jfs_ctx_sw(struct seq_file *s, void *data);
int unic_dbg_dump_jfr_ctx_sw(struct seq_file *s, void *data);
int unic_dbg_dump_sq_jfc_ctx_sw(struct seq_file *s, void *data);
int unic_dbg_dump_rq_jfc_ctx_sw(struct seq_file *s, void *data);
int unic_dbg_dump_jfs_context_hw(struct seq_file *s, void *data);
int unic_dbg_dump_jfr_context_hw(struct seq_file *s, void *data);
int unic_dbg_dump_sq_jfc_context_hw(struct seq_file *s, void *data);
int unic_dbg_dump_rq_jfc_context_hw(struct seq_file *s, void *data);
int unic_dbg_dump_sq_rq_cq_info(struct seq_file *s, void *data);

#endif /* __UNIC_CTX_DEBUGFS_H__ */
