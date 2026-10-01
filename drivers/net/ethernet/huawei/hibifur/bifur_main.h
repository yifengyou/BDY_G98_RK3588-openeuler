/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#ifndef BIFUR_MAIN_H__
#define BIFUR_MAIN_H__

#include "hinic3_lld.h"
#include "asm-generic/int-ll64.h"

#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/timer.h>

enum bifur_func_type {
	BIFUR_EXCLUSIVE_PF = 1,
	BIFUR_SHARED_PF,
	BIFUR_RESOURCE_PF,
	BIFUR_RESOURCE_VF,
	BIFUR_FUNC_TYPE_MAX
};

struct bifur_flow_dev {
	atomic_t bifur_dev_ref;
	bool has_created; /* bifur dev created or not */
};

struct bifur_adapter {
	struct list_head lld_dev_head; /* pcie device list head */
	struct mutex bifur_dev_mutex;   /* lock for bifur dev list */
	struct workqueue_struct *event_workq; /* global work queue */
	u8 bifur_enabled;	/* used for mark whether to enable traffic bifurcation */
	u8 bond_bifur_enabled; /* used for mark whether to enable bond status of bifurcation vfs */
	u16 bond_id;
};

struct bifur_vf_mgr;
struct bifur_lld_dev {
	struct list_head list;
	struct hinic3_lld_dev *lld_dev;
	struct bifur_vf_mgr *vf_mgr;
	struct bifur_flow_dev bifur_dev; /* bifur device */
	enum bifur_func_type pf_type;
	struct work_struct netdev_link;
	u8 link_status;
	u32 dbdf;
};

void bifur_dev_hold(struct bifur_lld_dev *bifur_dev);
void bifur_dev_put(struct bifur_lld_dev *bifur_dev);
void bifur_dev_list_lock(void);
void bifur_dev_list_unlock(void);
struct bifur_lld_dev *bifur_get_resource_dev(void);
struct bifur_adapter *bifur_get_adp(void);
struct bifur_lld_dev *bifur_get_shared_dev_by_dbdf(u32 dbdf);
#endif
