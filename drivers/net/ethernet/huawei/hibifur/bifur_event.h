/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#ifndef BIFUR_EVENT_H__
#define BIFUR_EVENT_H__
#include <linux/workqueue.h>
#include <linux/notifier.h>
#include <linux/if.h>

#define BIFUR_BOND_2_FUNC_NUM 2

struct bifur_bond_work {
	char name[IFNAMSIZ];
	struct work_struct work;
};

void bifur_set_bond_enable(u8 bond_bifur_en);
int bifur_bond_init(void);
void bifur_bond_exit(void);
int bifur_register_net_event(void);
void bifur_unregister_net_event(void);
void bifur_netdev_event(struct work_struct *work);

#endif
