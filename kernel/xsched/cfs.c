// SPDX-License-Identifier: GPL-2.0+
/*
 * Completely Fair Scheduling (CFS) Class for XPU device
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
#include <linux/xsched.h>

static void xs_rq_add(struct xsched_entity_cfs *xse)
{
	struct xsched_rq_cfs *cfs_rq = xse->cfs_rq;
	struct rb_node **link = &cfs_rq->ctx_timeline.rb_root.rb_node;
	struct rb_node *parent = NULL;
	struct xsched_entity_cfs *entry;
	bool leftmost = true;

	while (*link) {
		parent = *link;
		entry = rb_entry(parent, struct xsched_entity_cfs, run_node);
		if (xse->xruntime <= entry->xruntime) {
			link = &parent->rb_left;
		} else {
			link = &parent->rb_right;
			leftmost = false;
		}
	}

	rb_link_node(&xse->run_node, parent, link);
	rb_insert_color_cached(&xse->run_node, &cfs_rq->ctx_timeline, leftmost);
}

static void xs_rq_remove(struct xsched_entity_cfs *xse)
{
	struct xsched_rq_cfs *cfs_rq = xse->cfs_rq;

	rb_erase_cached(&xse->run_node, &cfs_rq->ctx_timeline);
}

/**
 * xs_cfs_rq_update() - Update entity's runqueue position with new xruntime
 */
static void xs_cfs_rq_update(struct xsched_entity_cfs *xse_cfs, u64 new_xrt)
{
	xs_rq_remove(xse_cfs);
	xse_cfs->xruntime = new_xrt;
	xs_rq_add(xse_cfs);
}

static inline struct xsched_entity_cfs *
xs_pick_first(struct xsched_rq_cfs *cfs_rq)
{
	struct rb_node *left = rb_first_cached(&cfs_rq->ctx_timeline);

	if (!left)
		return NULL;

	return rb_entry(left, struct xsched_entity_cfs, run_node);
}

/**
 * xs_update() - Account xruntime and runtime metrics.
 * @xse_cfs: Point to CFS scheduling entity.
 * @delta: Execution time in last period
 */
static void xs_update(struct xsched_entity_cfs *xse_cfs, u64 delta)
{
	u64 new_xrt;

	new_xrt = xse_cfs->xruntime +
		xs_calc_delta_fair(delta, xse_cfs->weight);

	xs_cfs_rq_update(xse_cfs, new_xrt);
	xse_cfs->sum_exec_runtime += delta;
}

static void update_min_xruntime(struct xsched_rq_cfs *cfs_rq)
{
	struct xsched_entity_cfs *leftmost = xs_pick_first(cfs_rq);

	cfs_rq->min_xruntime = leftmost ? leftmost->xruntime : XSCHED_TIME_INF;
}

/*
 * Xsched Fair class methods
 * For rq manipulation we rely on root runqueue lock already acquired in core.
 * Access xsched_group_xcu_priv requires no locks because one thread per XCU.
 */
static void dequeue_ctx_fair(struct xsched_entity *xse)
{
	struct xsched_entity *child = xse;
	struct xsched_rq_cfs *rq;

	for_each_xse(child) {
		if (!child->on_rq)
			break;

		rq = xsched_cfs_rq_of(child);

		xs_rq_remove(&child->cfs);
		child->on_rq = false;
		rq->nr_running--;

		/**
		 * Dequeue the group's scheduling entity (GSE) from
		 * its parent runqueue when the group becomes empty,
		 * so it no longer participates in scheduling until
		 * new tasks arrive.
		 */
		if (rq->nr_running > 0)
			break;
	}
}

static void place_xsched_entity(struct xsched_rq_cfs *rq, struct xsched_entity *xse)
{
	struct xsched_entity_cfs *xse_cfs = &xse->cfs;

	if (!rq)
		return;

	xse_cfs->cfs_rq = rq;
	if (rq->min_xruntime != XSCHED_TIME_INF)
		xse_cfs->xruntime = max(xse_cfs->xruntime, rq->min_xruntime);

	xs_rq_add(xse_cfs);
}

/**
 * enqueue_ctx_fair() - Add context to the runqueue
 * @xse: xsched entity of context
 * @xcu: executor
 *
 * In contrary to enqueue_task it is called once on context init.
 * Although groups reside in tree, their nodes not counted in nr_running.
 * The xruntime of a group xsched entitry represented by min xruntime inside.
 */
static void enqueue_ctx_fair(struct xsched_entity *xse, struct xsched_cu *xcu)
{
	struct xsched_rq_cfs *rq;
	struct xsched_entity *child = xse;

	for_each_xse(child) {
		/*
		 * Terminate upward traversal of parent groups if
		 * Xse is already enqueued or Group is throttled.
		 */
		if (child->on_rq)
			break;

#ifdef CONFIG_CGROUP_XCU
		if (child->is_group && xsched_entity_throttled(child))
			break;
#endif

		rq = xsched_cfs_rq_of(child);
		place_xsched_entity(rq, child);
		child->on_rq = true;
		rq->nr_running++;
		update_min_xruntime(rq);
	}
}

static inline bool has_running_fair(struct xsched_cu *xcu)
{
	return !!xcu->xrq.cfs.nr_running;
}

static inline struct xsched_rq_cfs *
next_cfs_rq_of(struct xsched_entity_cfs *xse)
{
#ifdef CONFIG_CGROUP_XCU
	struct xsched_entity *se = container_of(xse, struct xsched_entity, cfs);

	if (se->is_group)
		return xse_this_cfs_rq(se);
#endif
	return NULL;
}

static struct xsched_entity *pick_next_ctx_fair(struct xsched_cu *xcu)
{
	struct xsched_entity_cfs *xse;
	struct xsched_rq_cfs *rq = &xcu->xrq.cfs;
	u64 now = ktime_get_ns();

	for (; rq; rq = next_cfs_rq_of(xse)) {
		xse = xs_pick_first(rq);
		if (!xse)
			return NULL;

		xse->exec_start = now;
	}

	return container_of(xse, struct xsched_entity, cfs);
}

static inline bool
xs_should_preempt_fair(struct xsched_entity *xse)
{
	return (atomic_read(&xse->submitted_one_kick) >= XSCHED_CFS_KICK_SLICE);
}

static void put_prev_ctx_fair(struct xsched_entity *xse)
{
	struct xsched_entity *prev = xse;

	for_each_xse(prev)
		xs_update(&prev->cfs, xse->last_exec_runtime);

#ifdef CONFIG_CGROUP_XCU
	struct xsched_group *group = xse->parent_grp;
	struct xsched_cu *xcu = xse->xcu;

	for_each_xsched_group(group) {
		xsched_quota_account(group, (s64)xse->last_exec_runtime);
		xsched_quota_check(group, xcu);
	}
#endif
}

void init_xsched_cfs_rq(struct xsched_rq_cfs *cfs_rq)
{
	cfs_rq->nr_running = 0;
	cfs_rq->ctx_timeline = RB_ROOT_CACHED;
	cfs_rq->min_xruntime = XSCHED_TIME_INF;
}

void rq_init_fair(struct xsched_rq *xrq)
{
	if (!xrq)
		return;

	init_xsched_cfs_rq(&xrq->cfs);
}

void xse_init_fair(struct xsched_entity *xse)
{
	xse->class = &fair_xsched_class;
	xse->cfs.weight = XSCHED_CFS_WEIGHT_DFLT;
}

void xse_deinit_fair(struct xsched_entity *xse)
{
	/* TODO Cgroup exit */
}

struct xsched_class fair_xsched_class = {
	.class_id = XSCHED_TYPE_CFS,
	.kick_slice = XSCHED_CFS_KICK_SLICE,
	.rq_init = rq_init_fair,
	.xse_init = xse_init_fair,
	.xse_deinit = xse_deinit_fair,
	.dequeue_ctx = dequeue_ctx_fair,
	.enqueue_ctx = enqueue_ctx_fair,
	.pick_next_ctx = pick_next_ctx_fair,
	.put_prev_ctx = put_prev_ctx_fair,
	.check_preempt = xs_should_preempt_fair,
	.has_running = has_running_fair,
};
