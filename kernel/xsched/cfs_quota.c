// SPDX-License-Identifier: GPL-2.0+
/*
 * Bandwidth provisioning for XPU device
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
#include <linux/timer.h>
#include <linux/xsched.h>

static struct workqueue_struct *quota_workqueue;

static void xsched_group_throttle(struct xsched_group *xg, struct xsched_cu *xcu)
{
	int xcu_id = xcu->id;
	struct xsched_rq_cfs *cfs_rq;

	lockdep_assert_held(&xcu->xcu_lock);

	cfs_rq = xsched_group_cfs_rq(xg, xcu_id);
	if (cfs_rq->throttled)
		return;

	cfs_rq->throttled = true;
	cfs_rq->nr_throttled++;
	cfs_rq->start_throttled_time = ktime_get();

	/**
	 * When an xse triggers XCU throttling, only the corresponding gse is
	 * dequeued from this XCU's group scheduling entity (gse) hierarchy,
	 * no further propagation or global dequeue occurs, ensuring throttling
	 * is scoped to the affected XCU.
	 */
	dequeue_ctx(&xg->perxcu_priv[xcu_id].xse);
}

static void xsched_group_unthrottle(struct xsched_group *xg)
{
	struct xsched_rq_cfs *cfs_rq;
	struct xsched_cu *xcu;
	ktime_t now = ktime_get();
	int id;

	for_each_active_xcu(xcu, id) {
		mutex_lock(&xcu->xcu_lock);

		cfs_rq = xsched_group_cfs_rq(xg, id);
		if (!cfs_rq || !cfs_rq->throttled) {
			mutex_unlock(&xcu->xcu_lock);
			continue;
		}

		/*
		 * Avoid inserting empty groups into the rbtree;
		 * only mark them as throttled.
		 */
		cfs_rq->throttled = false;
		cfs_rq->throttled_time += ktime_to_ns(
			ktime_sub(now, cfs_rq->start_throttled_time));
		cfs_rq->start_throttled_time = 0;

		if (cfs_rq->nr_running > 0) {
			enqueue_ctx(&xg->perxcu_priv[id].xse, xcu);
			wake_up_interruptible(&xcu->wq_xcu_idle);
		}

		mutex_unlock(&xcu->xcu_lock);
	}
}

void xsched_quota_refill(struct work_struct *work)
{
	struct xsched_group *xg = container_of(work, struct xsched_group, refill_work);

	spin_lock(&xg->lock);
	xg->runtime = max((xg->runtime - xg->quota), (s64)0);
	hrtimer_start(&xg->quota_timeout, ns_to_ktime(xg->period), HRTIMER_MODE_REL_SOFT);
	spin_unlock(&xg->lock);

	if (xg->runtime >= xg->quota) {
		XSCHED_DEBUG("xcu_cgroup [css=0x%lx] is still be throttled @ %s\n",
			(uintptr_t)&xg->css, __func__);
		return;
	}

	xsched_group_unthrottle(xg);
}

static enum hrtimer_restart quota_timer_cb(struct hrtimer *hrtimer)
{
	struct xsched_group *xg;

	xg = container_of(hrtimer, struct xsched_group, quota_timeout);
	queue_work(quota_workqueue, &xg->refill_work);

	return HRTIMER_NORESTART;
}

void xsched_quota_account(struct xsched_group *xg, s64 exec_time)
{
	spin_lock(&xg->lock);
	xg->runtime += exec_time;
	spin_unlock(&xg->lock);
}

void xsched_quota_check(struct xsched_group *xg, struct xsched_cu *xcu)
{
	bool throttled;

	spin_lock(&xg->lock);
	throttled = (xg->quota > 0) ? (xg->runtime >= xg->quota) : false;
	spin_unlock(&xg->lock);

	if (throttled)
		xsched_group_throttle(xg, xcu);
}

void xsched_quota_init(void)
{
	quota_workqueue = create_singlethread_workqueue("xsched_quota_workqueue");
}

void xsched_quota_timeout_init(struct xsched_group *xg)
{
	hrtimer_init(&xg->quota_timeout, CLOCK_MONOTONIC, HRTIMER_MODE_REL_SOFT);
	xg->quota_timeout.function = quota_timer_cb;
}

void xsched_quota_timeout_update(struct xsched_group *xg)
{
	struct hrtimer *t;

	if (!xg)
		return;

	t = &xg->quota_timeout;
	hrtimer_cancel(t);

	if (xg->quota > 0 && xg->period > 0)
		hrtimer_start(t, ns_to_ktime(xg->period), HRTIMER_MODE_REL_SOFT);
	else
		xsched_group_unthrottle(xg);
}
