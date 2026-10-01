/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025. Huawei Technologies Co., Ltd. All rights reserved.
 */
#ifndef __IB_CORE_POLL_KTHREAD_H
#define __IB_CORE_POLL_KTHREAD_H

struct polling_kthread {
	/* configs */
	int use_polling_kthread;
	unsigned int debug_cq_poll_stat;

	/* vars */
	unsigned long cqe_polling_cnt;

	/* ops */
	void (*add_to_poll_list)(void *new_entry);
	void (*del_from_poll_list)(void *deleted_entry);
};

extern struct polling_kthread *get_kthread_polling_ctx(void);
extern int ib_poll_cq_thread(void *data);

#endif /* __IB_CORE_POLL_KTHREAD_H */
