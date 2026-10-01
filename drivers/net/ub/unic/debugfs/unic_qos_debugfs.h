/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UNIC_QOS_DEBUGFS_H__
#define __UNIC_QOS_DEBUGFS_H__

int unic_dbg_dump_vl_queue(struct seq_file *s, void *data);
int unic_dbg_dump_dscp_vl_map(struct seq_file *s, void *data);
int unic_dbg_dump_prio_vl_map(struct seq_file *s, void *data);
int unic_dbg_dump_dscp_prio(struct seq_file *s, void *data);
int unic_dbg_dump_pfc_param(struct seq_file *s, void *data);

#endif /* __UNIC_QOS_DEBUGFS_H__ */
