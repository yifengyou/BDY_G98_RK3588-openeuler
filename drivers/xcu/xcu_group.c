// SPDX-License-Identifier: GPL-2.0+
/*
 * Code for NPU driver support
 *
 * Copyright (C) 2025-2026 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
#include <linux/kthread.h>
#include <linux/rwsem.h>
#include <linux/slab.h>
#include <linux/xcu_group.h>
#include <linux/xsched.h>

int num_active_xcu;
static DEFINE_SPINLOCK(xcu_mgr_lock);
struct xsched_cu *xsched_cu_mgr[XSCHED_NR_CUS];
static DECLARE_RWSEM(xcu_group_rwsem);
struct xcu_group *xcu_group_init(int id)
{
	struct xcu_group *node = kzalloc(sizeof(*node), GFP_KERNEL);

	if (!node)
		return NULL;

	node->id = id;
	node->type = XCU_TYPE_XPU;
	idr_init(&node->next_layer);
	return node;
}
EXPORT_SYMBOL(xcu_group_init);

int __xcu_group_attach(struct xcu_group *new_group,
						struct xcu_group *previous_group)
{
	int id = new_group->id;

	if (id == -1)
		id = idr_alloc(&previous_group->next_layer, new_group, 0,
					INT_MAX, GFP_KERNEL);
	else
		id = idr_alloc(&previous_group->next_layer, new_group, id,
					id + 1, GFP_KERNEL);

	if (id < 0) {
		XSCHED_ERR("Fail to attach xcu_group: id conflict @ %s\n",
				__func__);
		return -EEXIST;
	}
	new_group->id = id;
	new_group->previous_layer = previous_group;

	return 0;
}

int xcu_group_attach(struct xcu_group *new_group,
						struct xcu_group *previous_group)
{
	int ret;

	down_write(&xcu_group_rwsem);
	ret = __xcu_group_attach(new_group, previous_group);
	up_write(&xcu_group_rwsem);

	return ret;
}
EXPORT_SYMBOL(xcu_group_attach);

static inline void __xcu_group_detach(struct xcu_group *group)
{
	if (!group || !group->previous_layer)
		return;

	idr_remove(&group->previous_layer->next_layer, group->id);
	group->previous_layer = NULL;
}

void xcu_group_detach(struct xcu_group *group)
{
	down_write(&xcu_group_rwsem);
	__xcu_group_detach(group);
	up_write(&xcu_group_rwsem);
}
EXPORT_SYMBOL(xcu_group_detach);

void xcu_group_free(struct xcu_group *group)
{
	idr_destroy(&group->next_layer);
	if (group != xcu_group_root)
		kfree(group);
}
EXPORT_SYMBOL(xcu_group_free);

static struct xcu_group *__xcu_group_find_nolock(struct xcu_group *group, int id)
{
	return idr_find(&group->next_layer, id);
}

struct xcu_group *xcu_group_find(struct xcu_group *group, int id)
{
	struct xcu_group *result;

	down_read(&xcu_group_rwsem);
	result = __xcu_group_find_nolock(group, id);
	up_read(&xcu_group_rwsem);

	return result;
}
EXPORT_SYMBOL(xcu_group_find);

/* This function runs "run" callback for a given xcu_group
 * and a given vstream that are passed within
 * xcu_op_handler_params object
 */
int xcu_run(struct xcu_op_handler_params *params)
{
	if (!params->group->opt || !params->group->opt->run) {
		XSCHED_ERR("No function [run] called.\n");
		return -EINVAL;
	}

	return params->group->opt->run(params);
}

/* This function runs "wait" callback for a given xcu_group
 * and a given vstream that are passed within
 * xcu_op_handler_params object
 */
int xcu_wait(struct xcu_op_handler_params *params)
{
	if (!params->group->opt || !params->group->opt->wait) {
		XSCHED_ERR("No function [wait] called.\n");
		return -EINVAL;
	}

	return params->group->opt->wait(params);
}

/* This function runs "complete" callback for a given xcu_group
 * and a given vstream that are passed within
 * xcu_op_handler_params object.
 */
int xcu_complete(struct xcu_op_handler_params *params)
{
	return 0;
}

/* This function runs "finish" callback for a given xcu_group
 * and a given vstream that are passed within
 * xcu_op_handler_params object.
 *
 * This handler provides an interface to implement deallocation
 * and freeing memory for SQ and CQ buffers.
 */
int xcu_finish(struct xcu_op_handler_params *params)
{
	if (!params->group->opt || !params->group->opt->finish) {
		XSCHED_ERR("No function [finish] called.\n");
		return -EINVAL;
	}

	return params->group->opt->finish(params);
}

/* This function runs a "alloc" callback for a given xcu_group
 * and a given vstream that are passed within
 * xcu_op_handler_params object.
 *
 * This handler provides an interface to implement allocation
 * and registering memory for SQ and CQ buffers.
 */
int xcu_alloc(struct xcu_op_handler_params *params)
{
	if (!params->group->opt || !params->group->opt->alloc) {
		XSCHED_ERR("No function [alloc] called.\n");
		return -EINVAL;
	}

	return params->group->opt->alloc(params);
}

/* This function runs a "logic_alloc" callback for a given xcu_group
 * and a given vstream that are passed within
 * xcu_op_handler_params object.
 *
 * This handler provides an interface to implement allocation
 * and registering memory of logic CQ buffer.
 */
int xcu_logic_alloc(struct xcu_op_handler_params *params)
{
	if (!params->group->opt || !params->group->opt->logic_alloc) {
		XSCHED_ERR("No function [logic_alloc] called.\n");
		return -EINVAL;
	}

	return params->group->opt->logic_alloc(params);
}

/* This function runs a "logic_free" callback for a given xcu_group
 * and a given vstream that are passed within
 * xcu_op_handler_params object.
 *
 * This handler provides an interface to implement deallocation
 * and unregistering memory of a logic CQ buffer.
 */
int xcu_logic_free(struct xcu_op_handler_params *params)
{
	if (!params->group->opt || !params->group->opt->logic_free) {
		XSCHED_ERR("No function [logic_free] called.\n");
		return -EINVAL;
	}

	return params->group->opt->logic_free(params);
}

/* This function runs a "sqe_op" callback for a given xcu_group
 * and a given vstream that are passed within
 * xcu_op_handler_params object.
 *
 * This handler provides an interface to set or get sqe info.
 */
int xcu_sqe_op(struct xcu_op_handler_params *params)
{
	if (!params->group->opt || !params->group->opt->sqe_op) {
		XSCHED_ERR("No function [sqe_op] called.\n");
		return -EINVAL;
	}

	return params->group->opt->sqe_op(params);
}

static struct xcu_group __xcu_group_root = {
	.id = 0,
	.type = XCU_TYPE_ROOT,
	.next_layer = IDR_INIT(next_layer),
};

struct xcu_group *xcu_group_root = &__xcu_group_root;
EXPORT_SYMBOL(xcu_group_root);

static int nr_active_cu_inc(void)
{
	int cur_num = -1;

	spin_lock(&xcu_mgr_lock);
	if (num_active_xcu >= XSCHED_NR_CUS)
		goto out_unlock;

	cur_num = num_active_xcu;
	num_active_xcu++;

out_unlock:
	spin_unlock(&xcu_mgr_lock);
	return cur_num;
}

static int nr_active_cu_dec(void)
{
	int cur_num = -1;

	spin_lock(&xcu_mgr_lock);
	if (num_active_xcu <= 0)
		goto out_unlock;

	cur_num = num_active_xcu;
	num_active_xcu--;

out_unlock:
	spin_unlock(&xcu_mgr_lock);
	return cur_num;
}

/*
 * Initialize and register xcu in xcu_manager array.
 */
int xsched_xcu_register(struct xcu_group *group, uint32_t phys_id)
{
	int xcu_cur_num, ret = 0;
	struct xsched_cu *xcu;

	if (phys_id >= XSCHED_NR_CUS) {
		XSCHED_ERR("phys_id [%u] is out of valid range [0, %d).\n",
			phys_id, XSCHED_NR_CUS);
		return -EINVAL;
	}

	if (!group) {
		XSCHED_ERR("group cannot be NULL.\n");
		return -EINVAL;
	}

	xcu_cur_num = nr_active_cu_inc();
	if (xcu_cur_num < 0) {
		XSCHED_ERR("Number of present XCU's exceeds %d: %d.\n",
			XSCHED_NR_CUS, num_active_xcu);
		return -ENOSPC;
	}

	xcu = kzalloc(sizeof(struct xsched_cu), GFP_KERNEL);
	if (!xcu) {
		if (nr_active_cu_dec() < 0) {
			XSCHED_ERR("num_active_xcu [%d] must be > 0.\n",
				num_active_xcu);
		}
		XSCHED_ERR("Fail to alloc xcu for phys_id [%u].\n", phys_id);
		return -ENOMEM;
	}

	group->xcu = xcu;
	xsched_cu_mgr[phys_id] = xcu;

	/* Init xcu's internals. */
	ret = xsched_xcu_init(xcu, group, phys_id);
	if (ret != 0) {
		group->xcu = NULL;
		xsched_cu_mgr[phys_id] = NULL;
		kfree(xcu);
		XSCHED_ERR("Fail to init xcu[%u].", xcu->id);
		return ret;
	}

#ifdef CONFIG_CGROUP_XCU
	init_fair_xsched_group(root_xcg, xcu, &xcu->xrq.cfs);
#endif /* CONFIG_CGROUP_XCU */

	return 0;
}
EXPORT_SYMBOL(xsched_xcu_register);

int xsched_xcu_unregister(struct xcu_group *group, uint32_t phys_id)
{
	struct xsched_cu *xcu;

	if (phys_id >= XSCHED_NR_CUS) {
		XSCHED_ERR("phys_id [%u] is out of valid range [0, %d).\n",
			phys_id, XSCHED_NR_CUS);
		return -EINVAL;
	}

	if (!group || !group->xcu || group->xcu != xsched_cu_mgr[phys_id]) {
		XSCHED_ERR("group is invalid or cannot mapping to phys_id [%u].\n", phys_id);
		return -EINVAL;
	}

	xcu = group->xcu;
	if (!xcu) {
		XSCHED_ERR("xcu for phys_id [%u] not found.\n", phys_id);
		return -EINVAL;
	}

	if (nr_active_cu_dec() < 0) {
		XSCHED_ERR("No active XCU\n");
		return -EPERM;
	};

	if (xcu->worker) {
		mutex_lock(&xcu->xcu_lock);
		wake_up_interruptible(&xcu->wq_xcu_idle);
		mutex_unlock(&xcu->xcu_lock);

		kthread_stop(xcu->worker);
		xcu->worker = NULL;
	} else {
		XSCHED_ERR("The worker for xcu [%u] must not be NULL.\n", xcu->id);
	}

	group->xcu = NULL;
	xsched_cu_mgr[phys_id] = NULL;
	kfree(xcu);

	return 0;
}
EXPORT_SYMBOL(xsched_xcu_unregister);

