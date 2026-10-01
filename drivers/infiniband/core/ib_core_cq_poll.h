/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025. Huawei Technologies Co., Ltd. All rights reserved.
 */
#ifndef __POLL_CQ_KTHREAD_H
#define __POLL_CQ_KTHREAD_H

struct cq_poll_node {
	struct list_head list;
	void *cq;
	unsigned long time_used_ns;
	unsigned long poll_cq_cnt;
	unsigned long max_time_ns;
};

extern void add_cq_to_poll_list(void *cq);
extern void del_cq_from_poll_list(void *del_cq);
extern void clear_cq_poll_list(void);

#endif /* __POLL_CQ_KTHREAD_H */
