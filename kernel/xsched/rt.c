// SPDX-License-Identifier: GPL-2.0+
/*
 * Real-Time Scheduling Class for XPU device
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

#include <uapi/linux/sched/types.h>
#include <linux/hash.h>
#include <linux/hashtable.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/xsched.h>
#include <linux/vstream.h>

#define XSCHED_RT_TIMESLICE	(10 * NSEC_PER_MSEC)

static inline void
xse_rt_add(struct xsched_entity *xse, struct xsched_cu *xcu)
{
	list_add_tail(&xse->rt.list_node, &xcu->xrq.rt.rq[xse->rt.prio]);
}

static inline void xse_rt_del(struct xsched_entity *xse)
{
	list_del_init(&xse->rt.list_node);
}

static inline void xse_rt_move_tail(struct xsched_entity *xse)
{
	struct xsched_cu *xcu = xse->xcu;

	list_move_tail(&xse->rt.list_node, &xcu->xrq.rt.rq[xse->rt.prio]);
}

static void dequeue_ctx_rt(struct xsched_entity *xse)
{
	struct xsched_cu *xcu = xse->xcu;

	xse_rt_del(xse);
	xse->on_rq = false;
	xcu->xrq.rt.nr_running--;
}

static void enqueue_ctx_rt(struct xsched_entity *xse, struct xsched_cu *xcu)
{
	xse_rt_add(xse, xcu);
	xse->on_rq = true;
	xcu->xrq.rt.nr_running++;
}

static inline bool has_running_rt(struct xsched_cu *xcu)
{
	return !!xcu->xrq.rt.nr_running;
}

static inline struct xsched_entity *xrq_next_xse(struct xsched_cu *xcu,
						int prio)
{
	return list_first_entry(&xcu->xrq.rt.rq[prio], struct xsched_entity,
				rt.list_node);
}

/* Return the next priority for pick_next_ctx taking into
 * account if there are pending kicks on certain priority.
 */
static inline uint32_t get_next_prio_rt(struct xsched_rq *xrq)
{
	unsigned int curr_prio;

	for_each_xse_prio(curr_prio) {
		if (!list_empty(&xrq->rt.rq[curr_prio]))
			return curr_prio;
	}
	return NR_XSE_PRIO;
}

static struct xsched_entity *pick_next_ctx_rt(struct xsched_cu *xcu)
{
	int next_prio;

	next_prio = get_next_prio_rt(&xcu->xrq);
	if (next_prio >= NR_XSE_PRIO)
		return NULL;

	return xrq_next_xse(xcu, next_prio);
}

static void put_prev_ctx_rt(struct xsched_entity *xse)
{
	xse->rt.timeslice -= xse->last_exec_runtime;

	if (xse->rt.timeslice <= 0) {
		xse->rt.timeslice = XSCHED_RT_TIMESLICE;
		xse_rt_move_tail(xse);
	}
}

static bool check_preempt_ctx_rt(struct xsched_entity *xse)
{
	return true;
}

void rq_init_rt(struct xsched_rq *xrq)
{
	int prio = 0;

	if (!xrq)
		return;

	xrq->rt.nr_running = 0;

	for_each_xse_prio(prio) {
		INIT_LIST_HEAD(&xrq->rt.rq[prio]);
	}
}

void xse_init_rt(struct xsched_entity *xse)
{
	struct task_struct *p;

	p = find_task_by_vpid(xse->tgid);
	if (p)
		xse->rt.prio = p->_resvd->xse_attr.xsched_priority;

	xse->class = &rt_xsched_class;
	xse->rt.timeslice = XSCHED_RT_TIMESLICE;
	INIT_LIST_HEAD(&xse->rt.list_node);
}

void xse_deinit_rt(struct xsched_entity *xse) { }

struct xsched_class rt_xsched_class = {
	.class_id = XSCHED_TYPE_RT,
	.kick_slice = XSCHED_RT_KICK_SLICE,
	.rq_init = rq_init_rt,
	.xse_init = xse_init_rt,
	.xse_deinit = xse_deinit_rt,
	.dequeue_ctx = dequeue_ctx_rt,
	.enqueue_ctx = enqueue_ctx_rt,
	.pick_next_ctx = pick_next_ctx_rt,
	.put_prev_ctx = put_prev_ctx_rt,
	.check_preempt = check_preempt_ctx_rt,
	.has_running = has_running_rt,
};

void xsched_rt_prio_set(pid_t tgid, unsigned int prio)
{
	unsigned int id;
	struct xsched_cu *xcu;
	struct xsched_context *ctx;
	struct xsched_entity *xse;

	for_each_active_xcu(xcu, id) {
		mutex_lock(&xcu->ctx_list_lock);
		mutex_lock(&xcu->xcu_lock);

		ctx = ctx_find_by_tgid_and_xcu(tgid, xcu);
		if (!ctx || ctx->xse.class != &rt_xsched_class) {
			mutex_unlock(&xcu->xcu_lock);
			mutex_unlock(&xcu->ctx_list_lock);
			continue;
		}

		xse = &ctx->xse;
		xse->rt.prio = clamp_t(unsigned int, prio, XSE_PRIO_HIGH, XSE_PRIO_LOW);
		if (xse->on_rq) {
			xse_rt_del(xse);
			xse_rt_add(xse, xcu);
		}

		mutex_unlock(&xcu->xcu_lock);
		mutex_unlock(&xcu->ctx_list_lock);
	}
}
