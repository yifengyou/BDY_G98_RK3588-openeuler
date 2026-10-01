// SPDX-License-Identifier: GPL-2.0
/*
 * IO inflight relative controller
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/time64.h>
#include <linux/parser.h>
#include <linux/blk-cgroup.h>

#include "blk-cgroup.h"
#include "blk-rq-qos.h"
#include "blk-mq.h"
#include "blk-io-hierarchy/stats.h"

#define IOINFG_WEIGHT_UNINIT	(CGROUP_WEIGHT_MAX + 1)
#define IOINF_MIN_INFLIGHT	3
#define IOINFG_MIN_INFLIGHT	1
/* default wake-up time in jiffies for backgroup job, see ioinf_timer_fn() */
#define IOINF_TIMER_PERID	(HZ / 2)
/* Minimum wait queue count for offline cgroups. */
#define IOINFG_MIN_WQ_NR	8
/* minimal number of samples for congestion control */
#define IOINF_MIN_SAMPLES	100

/* scale inflight from 1/1000 to 100 */
enum {
	MIN_SCALE	= 1,		/* one thousandth. */
	DFL_SCALE	= 100,		/* one tenth. */
	SCALE_GRAN	= 1000,		/* The control granularity is 1/1000. */
	MAX_SCALE	= 100000,	/* A hundredfold. */
};

/* io.inf.qos controls */
enum {
	INF_ENABLE,
	INF_INFLIGHT,
	INF_FLAGS,

	QOS_ENABLE,
	QOS_RLAT,
	QOS_WLAT,
	QOS_RPCT,
	QOS_WPCT,

	NR_QOS_CTRL_PARAMS
};

/* qos control params */
struct ioinf_params {
	bool enabled;
	bool qos_enabled;
	u32 inflight;
	unsigned long flags;
	u64 rlat;
	u64 wlat;
	u32 rpct;
	u32 wpct;
};

struct ioinf_io_stat {
	u64 nr;
	u64 lat;
	u64 met;
};

struct ioinf_lat_stat {
	struct ioinf_io_stat read;
	struct ioinf_io_stat write;
};

struct ioinf_rq_wait {
	wait_queue_head_t	*wait;
	u32			wq_nr;
	atomic_t		next_wq;
	atomic_t		sleepers;

	atomic_t		inflight;
	u32			hinflight;
	u32			max_inflight;
	u32			last_max;
	u32			exhausted;
	u32			issued;
};

/* the global conrtol structure */
struct ioinf {
	struct rq_qos		rqos;

	struct ioinf_params	params;
	u32			inflight;
	u32			scale;
	u32			old_scale;
	u32			max_scale;
	u32			scale_step;

	/* default time for ioinf_timer_fn */
	unsigned long		inf_timer_perid;
	struct timer_list	inf_timer;

	/* global lock */
	spinlock_t		lock;

	/* for offline cgroups */
	struct ioinf_rq_wait	offline;
	/* for online cgroups */
	struct ioinf_rq_wait	online;

	/* timer for ioinf_wakeup_timer_fn */
	struct hrtimer		wakeup_timer;
	bool			waking;

	struct ioinf_lat_stat	last_stat;
	struct ioinf_lat_stat	cur_stat;
	struct ioinf_lat_stat	delta_stat;
	struct ioinf_lat_stat __percpu *stat;
};

/* per disk-cgroup pair structure */
struct ioinf_gq {
	struct blkg_policy_data	pd;
	struct ioinf		*inf;

	/* weight < 0: offline; weight > 0: online; weight == 0: unset */
	int			user_weight;
	int			dfl_user_weight;
};

/* per cgroup structure, used to record default weight for all disks */
struct ioinf_cgrp {
	struct blkcg_policy_data	cpd;

	/* weight < 0: offline; weight > 0: online; weight == 0: unset */
	int				dfl_user_weight;
};

/* io-inflight flags bit */
enum {
	/*
	 * Cgroups with unset weight are not throttled and latency is not
	 * recorded. Without this flag, such cgroups are treated as offline.
	 */
	DEFAULT_NOLIMIT,

	/* If QoS not met, also throttle online, trading BW for latency. */
	THROTTLE_ONLINE,

	NR_INF_FLAGS
};

static inline int inf_test_flag(struct ioinf *inf, int bit)
{
	return test_bit(bit, &inf->params.flags);
}

static int infg_user_weight(struct ioinf_gq *infg)
{
	if (infg->user_weight)
		return infg->user_weight;

	/* if user doesn't set per disk weight, use the cgroup default weight */
	if (infg->dfl_user_weight)
		return infg->dfl_user_weight;

	/* No limit for Cgroups with unset weight */
	if (inf_test_flag(infg->inf, DEFAULT_NOLIMIT))
		return 0;

	/* Cgroups with unset weight are treated as offline. */
	return -1;
}

static bool infg_offline(struct ioinf_gq *infg)
{
	return infg_user_weight(infg) < 0;
}

static bool infg_nolimit(struct ioinf_gq *infg)
{
	return infg_user_weight(infg) == 0;
}

static struct ioinf *rqos_to_inf(struct rq_qos *rqos)
{
	return container_of(rqos, struct ioinf, rqos);
}

static struct ioinf *q_to_inf(struct request_queue *q)
{
	return rqos_to_inf(rq_qos_id(q, RQ_QOS_INFLIGHT));
}

static struct ioinf_gq *pd_to_infg(struct blkg_policy_data *pd)
{
	if (!pd)
		return NULL;

	return container_of(pd, struct ioinf_gq, pd);
}

static struct blkcg_policy blkcg_policy_ioinf;

static struct ioinf_gq *blkg_to_infg(struct blkcg_gq *blkg)
{
	return pd_to_infg(blkg_to_pd(blkg, &blkcg_policy_ioinf));
}

static struct ioinf_cgrp *blkcg_to_infcg(struct blkcg *blkcg)
{
	struct blkcg_policy_data *cpd =
		blkcg_to_cpd(blkcg, &blkcg_policy_ioinf);

	return container_of(cpd, struct ioinf_cgrp, cpd);
}

static struct blkcg_gq *ioinf_bio_blkg(struct bio *bio)
{
	struct blkcg_gq *blkg = bio->bi_blkg;

	if (!blkg || !blkg->online)
		return NULL;

	if (blkg->blkcg->css.cgroup->level == 0)
		return NULL;

	return blkg;
}

static struct ioinf_gq *ioinf_bio_infg(struct bio *bio)
{
	struct ioinf_gq *infg;
	struct blkcg_gq *blkg = ioinf_bio_blkg(bio);

	if (!blkg)
		return NULL;

	infg = blkg_to_infg(blkg);
	if (!infg)
		return NULL;

	return infg;
}

static void ioinf_set_hinflight(struct ioinf_rq_wait *rqw, u32 new)
{
	rqw->hinflight = new;
	rqw->last_max = max(rqw->last_max >> 1, rqw->max_inflight);
	rqw->max_inflight = IOINFG_MIN_INFLIGHT;
}

static inline void ioinf_rqw_wake_up_all(struct ioinf_rq_wait *rqw)
{
	if (!atomic_read(&rqw->sleepers))
		return;

	for (int i = 0; i < rqw->wq_nr; i++)
		wake_up_all(&rqw->wait[i]);
}

static void ioinf_wake_up_all(struct ioinf *inf)
{
	ioinf_rqw_wake_up_all(&inf->online);
	ioinf_rqw_wake_up_all(&inf->offline);
}

static enum hrtimer_restart ioinf_wakeup_timer_fn(struct hrtimer *timer)
{
	struct ioinf *inf = container_of(timer, struct ioinf, wakeup_timer);

	WRITE_ONCE(inf->waking, false);
	ioinf_wake_up_all(inf);

	return HRTIMER_NORESTART;
}

void ioinf_done(struct ioinf *inf, struct ioinf_rq_wait *rqw)
{
	int inflight;

	if (!inf->params.enabled)
		return;

	inflight = atomic_dec_return(&rqw->inflight);
	if (inflight >= (int)rqw->hinflight)
		return;

	if (!READ_ONCE(inf->waking) && atomic_read(&rqw->sleepers)) {
		WRITE_ONCE(inf->waking, true);
		hrtimer_start(&inf->wakeup_timer, 0, HRTIMER_MODE_REL);
	}
}

struct ioinf_rq_qos_wait_data {
	struct wait_queue_entry wq;
	struct task_struct *task;
	struct ioinf_rq_wait *rqw;
	struct ioinf *inf;
	bool is_prio;
	bool do_wakeup;
	bool got_token;
};

static bool ioinf_inflight_cb(struct ioinf_rq_qos_wait_data *data)
{
	struct ioinf *inf = data->inf;
	struct ioinf_rq_wait *rqw = data->rqw;
	u32 inflight;
	u32 sleepers = 0;

	if (!inf->params.enabled)
		return true;

	if (!data->do_wakeup)
		sleepers = atomic_read(&rqw->sleepers);
retry:
	/*
	 * IOs which may cause priority inversions are
	 * dispatched directly, even if they're over limit.
	 */
	inflight = atomic_read(&rqw->inflight);
	if (inflight + sleepers < rqw->hinflight || data->is_prio) {
		inflight = atomic_inc_return(&rqw->inflight);

		if (inflight > rqw->max_inflight)
			rqw->max_inflight = inflight;
		rqw->issued++;
		return true;
	}

	rqw->max_inflight = max(rqw->max_inflight, inflight + 1);
	if (rqw == &inf->offline) {
		rqw->exhausted++;
		return false;
	}

	if (inf->offline.hinflight > IOINFG_MIN_INFLIGHT) {
		/* Reclaim half of the inflight budget from offline groups. */
		inf->offline.hinflight = inf->offline.hinflight >> 1;
		inf->online.hinflight = inf->inflight - inf->offline.hinflight;
		goto retry;
	}

	rqw->exhausted++;
	/* wake up ioinf_timer_fn() immediately to adjust scale */
	if (inf->scale < inf->max_scale || !inf_test_flag(inf, THROTTLE_ONLINE))
		timer_reduce(&inf->inf_timer, jiffies + 1);
	return false;
}

static int ioinf_wake_fn(struct wait_queue_entry *curr,
			 unsigned int mode, int wake_flags, void *key)
{
	struct ioinf_rq_qos_wait_data *data = container_of(curr,
				struct ioinf_rq_qos_wait_data, wq);

	/*
	 * If we fail to get a budget, return -1 to interrupt
	 * the wake up loop in __wake_up_common.
	 */
	if (!ioinf_inflight_cb(data))
		return -1;

	data->got_token = true;
	wake_up_process(data->task);
	list_del_init_careful(&curr->entry);
	return 1;
}

static void ioinf_throttle(struct ioinf *inf, struct ioinf_rq_wait *rqw,
			   struct bio *bio, bool is_prio)
{
	bool has_sleeper;
	u32 wq_idx;
	struct ioinf_rq_qos_wait_data data = {
		.wq = {
			.func	= ioinf_wake_fn,
			.entry	= LIST_HEAD_INIT(data.wq.entry),
		},
		.task = current,
		.rqw = rqw,
		.inf = inf,
		.is_prio = is_prio,
		.do_wakeup = false,
	};

	if (!timer_pending(&inf->inf_timer))
		timer_reduce(&inf->inf_timer, jiffies + inf->inf_timer_perid);

	if (ioinf_inflight_cb(&data))
		return;

	bio_hierarchy_start_io_acct(bio, STAGE_IOINF);
	data.do_wakeup = true;
	wq_idx = atomic_fetch_inc(&rqw->next_wq) % rqw->wq_nr;
	has_sleeper = !prepare_to_wait_exclusive(&rqw->wait[wq_idx], &data.wq,
						 TASK_UNINTERRUPTIBLE);
	atomic_inc(&rqw->sleepers);
	do {
		/* The memory barrier in set_task_state saves us here. */
		if (data.got_token)
			break;
		if (!has_sleeper && ioinf_inflight_cb(&data)) {
			finish_wait(&rqw->wait[wq_idx], &data.wq);

			/*
			 * We raced with rq_qos_wake_function() getting a token,
			 * which means we now have two. Put our local token
			 * and wake anyone else potentially waiting for one.
			 */
			if (data.got_token)
				ioinf_done(inf, rqw);
			break;
		}
		io_schedule();
		has_sleeper = true;
		set_current_state(TASK_UNINTERRUPTIBLE);
	} while (1);

	finish_wait(&rqw->wait[wq_idx], &data.wq);
	atomic_dec(&rqw->sleepers);
	bio_hierarchy_end_io_acct(bio, STAGE_IOINF);
}

static void ioinf_rqos_throttle(struct rq_qos *rqos, struct bio *bio)
{
	struct ioinf *inf = rqos_to_inf(rqos);
	struct ioinf_gq *infg = ioinf_bio_infg(bio);
	bool is_prio;

	if (!inf->params.enabled || !infg || infg_nolimit(infg))
		return;

	is_prio = bio_issue_as_root_blkg(bio) || fatal_signal_pending(current);

	if (infg_offline(infg)) {
		ioinf_throttle(inf, &inf->offline, bio, is_prio);
		return;
	}

	if (!inf->online.issued && !inf->params.qos_enabled)
		inf->max_scale = inf->scale = inf->old_scale = SCALE_GRAN;
	ioinf_throttle(inf, &inf->online, bio, is_prio);
}

static void ioinf_rqos_track(struct rq_qos *rqos, struct request *rq,
			     struct bio *bio)
{
	struct blkcg_gq *blkg = ioinf_bio_blkg(bio);

	if (!blkg)
		return;

	rq->blkg = blkg;
}

static void ioinf_record_lat(struct ioinf *inf, struct request *rq)
{
	u64 lat;

	lat = rq->io_end_time_ns ? rq->io_end_time_ns : blk_time_get_ns();
	lat -= rq->alloc_time_ns;

	switch (req_op(rq)) {
	case REQ_OP_READ:
		this_cpu_inc(inf->stat->read.nr);
		this_cpu_add(inf->stat->read.lat, lat);
		if (inf->params.qos_enabled && lat <= inf->params.rlat)
			this_cpu_inc(inf->stat->read.met);
		break;
	case REQ_OP_WRITE:
		this_cpu_inc(inf->stat->write.nr);
		this_cpu_add(inf->stat->write.lat, lat);
		if (inf->params.qos_enabled && lat <= inf->params.wlat)
			this_cpu_inc(inf->stat->write.met);
		break;
	default:
		break;
	}
}

static void ioinf_rqos_done_bio(struct rq_qos *rqos, struct bio *bio)
{
	struct blkcg_gq *blkg = ioinf_bio_blkg(bio);
	struct ioinf_gq *infg;
	struct ioinf *inf;

	if (!blkg || !bio_flagged(bio, BIO_QOS_THROTTLED))
		return;

	infg = blkg_to_infg(blkg);
	if (!infg)
		return;

	inf = infg->inf;
	if (!inf->params.enabled || infg_nolimit(infg))
		return;

	if (infg_offline(infg))
		ioinf_done(inf, &inf->offline);
	else
		ioinf_done(inf, &inf->online);
}

static void ioinf_rqos_done(struct rq_qos *rqos, struct request *rq)
{
	struct blkcg_gq *blkg = rq->blkg;
	struct ioinf_gq *infg;

	if (!blkg)
		return;

	rq->blkg = NULL;

	infg = blkg_to_infg(blkg);
	if (!infg || !infg->inf->params.enabled ||
	    infg_offline(infg) || infg_nolimit(infg))
		return;

	ioinf_record_lat(infg->inf, rq);
}

static void ioinf_rqos_exit(struct rq_qos *rqos)
{
	struct ioinf *inf = rqos_to_inf(rqos);

	blk_mq_unregister_hierarchy(rqos->disk->queue, STAGE_IOINF);
	blkcg_deactivate_policy(rqos->disk, &blkcg_policy_ioinf);

	hrtimer_cancel(&inf->wakeup_timer);
	timer_shutdown_sync(&inf->inf_timer);
	ioinf_wake_up_all(inf);
	kfree(inf->online.wait);
	kfree(inf->offline.wait);
	free_percpu(inf->stat);
	kfree(inf);
}

static inline u64 ioinf_qos_met_percent(struct ioinf_io_stat *io_stat)
{
	if (!io_stat->nr)
		return 0;
	return div_u64(io_stat->met * 100, io_stat->nr);
}

static int ioinf_stat_show(void *data, struct seq_file *m)
{
	struct rq_qos *rqos = data;
	struct ioinf *inf = rqos_to_inf(rqos);
	struct ioinf_lat_stat *stat;

	if (!inf->params.enabled) {
		seq_puts(m, "\tinf.qos disabled.\n");
		return 0;
	}

	spin_lock_irq(&inf->lock);

	seq_printf(m, "scale %u/%u inflight %u->%u\n",
		   inf->scale, SCALE_GRAN,
		   inf->params.inflight, inf->inflight);

	seq_printf(m, "online inflight %d/%u, sleepers: %d\n",
		   atomic_read(&inf->online.inflight),
		   inf->online.hinflight, atomic_read(&inf->online.sleepers));
	seq_printf(m, "offline inflight %d/%u, sleepers: %d\n",
		   atomic_read(&inf->offline.inflight),
		   inf->offline.hinflight, atomic_read(&inf->offline.sleepers));

	stat = &inf->delta_stat;
	seq_puts(m, "online average latency:\n");
	seq_printf(m, "(%llu/%llu-%llu-%llu%%) (%llu/%llu-%llu-%llu%%)\n",
		   stat->read.met, stat->read.nr, stat->read.lat,
		   ioinf_qos_met_percent(&stat->read),
		   stat->write.met, stat->write.nr, stat->write.lat,
		   ioinf_qos_met_percent(&stat->write));
	spin_unlock_irq(&inf->lock);

	return 0;
}

static const struct blk_mq_debugfs_attr ioinf_debugfs_attrs[] = {
	{"stat", 0400, ioinf_stat_show},
	{},
};

static struct rq_qos_ops ioinf_rqos_ops = {
	.throttle	= ioinf_rqos_throttle,
	.done_bio	= ioinf_rqos_done_bio,
	.done		= ioinf_rqos_done,
	.track		= ioinf_rqos_track,
	.exit		= ioinf_rqos_exit,

#ifdef CONFIG_BLK_DEBUG_FS
	.debugfs_attrs = ioinf_debugfs_attrs,
#endif
};

static void __inflight_scale_up(struct ioinf *inf, u32 aim, bool force)
{
	u32 new_scale;

	inf->old_scale = inf->scale;
	if (aim < inf->inflight || inf->scale >= MAX_SCALE)
		return;

	new_scale = DIV_ROUND_UP(aim * SCALE_GRAN, inf->params.inflight);
	if (new_scale <= inf->old_scale) {
		if (!force)
			return;
		new_scale = inf->scale + inf->scale_step;
	}

	inf->scale = umin(new_scale, inf->max_scale);
}

static void inflight_scale_up(struct ioinf *inf, u32 aim)
{
	__inflight_scale_up(inf, aim, false);
}

static void inflight_force_scale_up(struct ioinf *inf, u32 aim)
{
	__inflight_scale_up(inf, aim, true);
}

static void __inflight_scale_down(struct ioinf *inf, u32 aim, bool force)
{
	u32 new_scale;

	inf->old_scale = inf->scale;
	if (inf->inflight <= IOINF_MIN_INFLIGHT || inf->scale <= MIN_SCALE)
		return;

	new_scale = DIV_ROUND_UP(aim * SCALE_GRAN, inf->params.inflight);
	if (new_scale >= inf->old_scale) {
		if (!force)
			return;
		new_scale = inf->scale - inf->scale_step;
	}

	inf->scale = new_scale;
}

static void inflight_scale_down(struct ioinf *inf, u32 aim)
{
	__inflight_scale_down(inf, aim, false);
}

static void inflight_force_scale_down(struct ioinf *inf, u32 aim)
{
	__inflight_scale_down(inf, aim, true);
}

u32 ioinf_calc_budget(struct ioinf_rq_wait *rqw)
{
	u32 new_budget;
	u64 exhausted = rqw->exhausted;
	u64 issued = rqw->issued;

	new_budget = max(rqw->last_max, rqw->max_inflight);
	/* How much budget is needed to avoid 'exhausted'? */
	if (exhausted && issued)
		new_budget += div_u64(exhausted * new_budget, issued);

	return new_budget;
}

static void ioinf_sample_cpu_lat(struct ioinf_lat_stat *cur, int cpu,
				 struct ioinf_lat_stat __percpu *stat)
{
	struct ioinf_lat_stat *pstat = per_cpu_ptr(stat, cpu);

	cur->read.nr += pstat->read.nr;
	cur->read.lat += pstat->read.lat;
	cur->read.met += pstat->read.met;
	cur->write.nr += pstat->write.nr;
	cur->write.lat += pstat->write.lat;
	cur->write.met += pstat->write.met;
}

static inline void ioinf_update_delta_io_stat(struct ioinf_io_stat cur,
	struct ioinf_io_stat last, struct ioinf_io_stat *delta)
{
	u64 lat = delta->nr * delta->lat;

	delta->nr += cur.nr - last.nr;
	delta->met += cur.met - last.met;
	if (delta->nr > 0) {
		lat += cur.lat - last.lat;
		delta->lat = div_u64(lat, delta->nr);
	}
}

static void ioinf_update_delta_stat(struct ioinf_lat_stat *cur,
	struct ioinf_lat_stat *last, struct ioinf_lat_stat *delta)
{
	ioinf_update_delta_io_stat(cur->read, last->read, &delta->read);
	ioinf_update_delta_io_stat(cur->write, last->write, &delta->write);
}

static void ioinf_sample_lat(struct ioinf *inf)
{
	int cpu;

	inf->last_stat = inf->cur_stat;
	memset(&inf->cur_stat, 0, sizeof(struct ioinf_lat_stat));
	for_each_possible_cpu(cpu)
		ioinf_sample_cpu_lat(&inf->cur_stat, cpu, inf->stat);

	if (!inf->params.qos_enabled)
		memset(&inf->delta_stat, 0, sizeof(struct ioinf_lat_stat));
	if (inf->delta_stat.read.nr >= IOINF_MIN_SAMPLES)
		memset(&inf->delta_stat.read, 0, sizeof(struct ioinf_io_stat));
	if (inf->delta_stat.write.nr >= IOINF_MIN_SAMPLES)
		memset(&inf->delta_stat.write, 0, sizeof(struct ioinf_io_stat));
	ioinf_update_delta_stat(&inf->cur_stat, &inf->last_stat,
				&inf->delta_stat);
}

static int ioinf_online_busy(struct ioinf *inf)
{
	struct ioinf_lat_stat *stat = &inf->delta_stat;
	int met_percent, unmet_percent = 0;

	if (stat->read.nr >= IOINF_MIN_SAMPLES) {
		met_percent = ioinf_qos_met_percent(&stat->read);
		unmet_percent = inf->params.rpct - met_percent;
	}
	if (stat->write.nr >= IOINF_MIN_SAMPLES) {
		met_percent = ioinf_qos_met_percent(&stat->write);
		if (unmet_percent < inf->params.wpct - met_percent)
			unmet_percent = inf->params.wpct - met_percent;
	}

	return unmet_percent;
}

static
void ioinf_update_inflight(struct ioinf *inf, u32 new_online, u32 new_offline)
{
	inf->scale = clamp(inf->scale, MIN_SCALE, MAX_SCALE);
	inf->inflight = inf->params.inflight * inf->scale / SCALE_GRAN;
	if (inf->inflight < IOINF_MIN_INFLIGHT) {
		inf->inflight = IOINF_MIN_INFLIGHT;
		inf->scale = inf->inflight * SCALE_GRAN / inf->params.inflight;
	}

	if (new_online < inf->inflight)
		new_offline = inf->inflight - new_online;
	else
		new_offline = min(new_offline, IOINFG_MIN_INFLIGHT);

	if (inf_test_flag(inf, THROTTLE_ONLINE)) {
		new_online = inf->inflight - new_offline;
	} else {
		inf->inflight = new_online + new_offline;
		inf->scale = inf->inflight * SCALE_GRAN / inf->params.inflight;
	}

	ioinf_set_hinflight(&inf->offline, new_offline);
	inf->offline.exhausted = 0;
	inf->offline.issued = 0;

	ioinf_set_hinflight(&inf->online, new_online);
	inf->online.exhausted = 0;
	inf->online.issued = 0;

	ioinf_wake_up_all(inf);
}

static void ioinf_timer_fn(struct timer_list *timer)
{
	struct ioinf *inf = container_of(timer, struct ioinf, inf_timer);
	struct ioinf_rq_wait *online = &inf->online;
	struct ioinf_rq_wait *offline = &inf->offline;
	unsigned long flags;
	u32 online_budget, offline_budget, total_budget;
	int unmet_percent = 0;

	spin_lock_irqsave(&inf->lock, flags);
	ioinf_sample_lat(inf);
	if (inf->params.qos_enabled)
		unmet_percent = ioinf_online_busy(inf);

	online_budget = ioinf_calc_budget(online);
	offline_budget = ioinf_calc_budget(offline);
	total_budget = online_budget + offline_budget;

	if (unmet_percent < 0 && inf->max_scale < MAX_SCALE)
		inf->max_scale++;

	if (unmet_percent > 0) {
		inf->max_scale = inf->scale;
		if (inf->max_scale > inf->scale_step)
			inf->max_scale -= inf->scale_step;
		total_budget = umin(online->hinflight + offline->hinflight,
				    total_budget);
		total_budget -= total_budget * unmet_percent / 100;
		inflight_force_scale_down(inf, total_budget);
	} else if (inf->scale < inf->max_scale && online->exhausted) {
		inflight_force_scale_up(inf, total_budget);
		if (inf->scale > inf->max_scale)
			inf->scale = (inf->old_scale + inf->max_scale + 1) / 2;
	} else if (!online->issued && online_budget <= IOINFG_MIN_INFLIGHT) {
		inf->max_scale = inf->scale = inf->old_scale = MAX_SCALE;
	} else if (inf->scale < inf->max_scale && inf->params.qos_enabled) {
		inflight_scale_up(inf, total_budget);
	} else if (inf->old_scale < inf->scale) {
		inflight_scale_down(inf, total_budget);
	}

	ioinf_update_inflight(inf, online_budget, offline_budget);

	spin_unlock_irqrestore(&inf->lock, flags);
	mod_timer(&inf->inf_timer, jiffies + inf->inf_timer_perid);
}

static u32 ioinf_default_inflight(struct ioinf *inf)
{
	u32 inflight = inf->params.inflight * DFL_SCALE / SCALE_GRAN;

	if (inflight < IOINF_MIN_INFLIGHT)
		inflight = IOINF_MIN_INFLIGHT;
	inf->scale = DIV_ROUND_UP(inflight * SCALE_GRAN, inf->params.inflight);
	inf->old_scale = inf->scale;

	return inf->params.inflight * inf->scale / SCALE_GRAN;
}

static inline int ioinf_rqw_init(struct ioinf_rq_wait *rqw)
{
	int i;

	rqw->wait = kcalloc(rqw->wq_nr, sizeof(wait_queue_head_t), GFP_KERNEL);
	if (!rqw->wait)
		return -ENOMEM;

	for (i = 0; i < rqw->wq_nr; i++)
		init_waitqueue_head(&rqw->wait[i]);

	return 0;
}

static int blk_ioinf_init(struct gendisk *disk)
{
	struct ioinf *inf;
	int ret = -ENOMEM;

	inf = kzalloc(sizeof(*inf), GFP_KERNEL);
	if (!inf)
		return ret;

	inf->stat = alloc_percpu(struct ioinf_lat_stat);
	if (!inf->stat)
		goto free_inf;

	inf->offline.wq_nr = umax(num_possible_cpus() / 2, IOINFG_MIN_WQ_NR);
	ret = ioinf_rqw_init(&inf->offline);
	if (ret)
		goto free_stat;

	inf->online.wq_nr = 1;
	ret = ioinf_rqw_init(&inf->online);
	if (ret)
		goto free_wq;

	spin_lock_init(&inf->lock);
	inf->params.inflight = disk->queue->nr_requests;
	inf->inflight = ioinf_default_inflight(inf);
	inf->max_scale = MAX_SCALE;
	inf->inf_timer_perid = IOINF_TIMER_PERID;

	inf->offline.hinflight = inf->inflight - IOINFG_MIN_INFLIGHT;
	inf->online.hinflight = IOINFG_MIN_INFLIGHT;

	timer_setup(&inf->inf_timer, ioinf_timer_fn, 0);
	hrtimer_init(&inf->wakeup_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	inf->wakeup_timer.function = ioinf_wakeup_timer_fn;
	inf->waking = false;

	ret = rq_qos_add(&inf->rqos, disk, RQ_QOS_INFLIGHT, &ioinf_rqos_ops);
	if (ret)
		goto err_cancel_timer;

	ret = blkcg_activate_policy(disk, &blkcg_policy_ioinf);
	if (ret)
		goto err_del_qos;

	blk_mq_register_hierarchy(disk->queue, STAGE_IOINF);
	return 0;

err_del_qos:
	rq_qos_del(&inf->rqos);
err_cancel_timer:
	hrtimer_cancel(&inf->wakeup_timer);
	timer_shutdown_sync(&inf->inf_timer);
	kfree(inf->online.wait);
free_wq:
	kfree(inf->offline.wait);
free_stat:
	free_percpu(inf->stat);
free_inf:
	kfree(inf);
	return ret;
}

static u64 ioinf_weight_prfill(struct seq_file *sf, struct blkg_policy_data *pd,
			       int off)
{
	const char *dname = blkg_dev_name(pd->blkg);
	struct ioinf_gq *infg = pd_to_infg(pd);
	struct blkcg *blkcg = css_to_blkcg(seq_css(sf));
	struct ioinf_cgrp *infcg = blkcg_to_infcg(blkcg);

	if (!infg->inf->params.enabled)
		return 0;

	if (dname && infg_user_weight(infg) != infcg->dfl_user_weight)
		seq_printf(sf, "%s %d\n", dname, infg_user_weight(infg));

	return 0;
}

static int ioinf_weight_show(struct seq_file *sf, void *v)
{
	struct blkcg *blkcg = css_to_blkcg(seq_css(sf));
	struct ioinf_cgrp *infcg = blkcg_to_infcg(blkcg);

	if (infcg->dfl_user_weight == IOINFG_WEIGHT_UNINIT)
		return 0;

	seq_printf(sf, "default %d\n", infcg->dfl_user_weight);
	blkcg_print_blkgs(sf, blkcg, ioinf_weight_prfill, &blkcg_policy_ioinf,
			  seq_cft(sf)->private, false);

	return 0;
}

static void propagate_parent_dfl_user_weight(struct ioinf_gq *root)
{
	struct cgroup_subsys_state *pos_css;
	struct blkcg_gq *blkg;
	struct ioinf_gq *infg;
	struct ioinf_cgrp *infcg;

	rcu_read_lock();
	blkg_for_each_descendant_pre(blkg, pos_css, pd_to_blkg(&root->pd)) {
		infcg = blkcg_to_infcg(blkg->blkcg);
		if (infcg && infcg->dfl_user_weight != root->dfl_user_weight)
			infcg->dfl_user_weight = root->dfl_user_weight;
		infg = blkg_to_infg(blkg);
		if (infg && infg->dfl_user_weight != root->dfl_user_weight)
			infg->dfl_user_weight = root->dfl_user_weight;
	}
	rcu_read_unlock();
}

static void ioinf_default_weight_update(struct blkcg *blkcg, int v)
{
	struct ioinf_cgrp *infcg = blkcg_to_infcg(blkcg);
	struct blkcg_gq *blkg;
	struct hlist_node *tmp;
	struct ioinf_gq *infg;

	if (v == infcg->dfl_user_weight)
		return;

	infcg->dfl_user_weight = v;
	spin_lock_irq(&blkcg->lock);
	hlist_for_each_entry_safe(blkg, tmp, &blkcg->blkg_list, blkcg_node) {
		infg = blkg_to_infg(blkg);
		if (infg && infg->dfl_user_weight != v) {
			spin_unlock_irq(&blkcg->lock);
			blk_mq_freeze_queue(infg->inf->rqos.disk->queue);
			blk_mq_quiesce_queue(infg->inf->rqos.disk->queue);
			infg->dfl_user_weight = v;
			propagate_parent_dfl_user_weight(infg);
			blk_mq_unquiesce_queue(infg->inf->rqos.disk->queue);
			blk_mq_unfreeze_queue(infg->inf->rqos.disk->queue);
			spin_lock_irq(&blkcg->lock);
		}
	}
	spin_unlock_irq(&blkcg->lock);
}

static void propagate_parent_user_weights(struct ioinf_gq *root)
{
	struct cgroup_subsys_state *pos_css;
	struct blkcg_gq *blkg;
	struct ioinf_gq *infg;

	rcu_read_lock();
	blkg_for_each_descendant_pre(blkg, pos_css, pd_to_blkg(&root->pd)) {
		infg = blkg_to_infg(blkg);
		if (infg && infg->user_weight != root->user_weight)
			infg->user_weight = root->user_weight;
	}
	rcu_read_unlock();
}

static int infg_weight_write(struct blkcg *blkcg, char *buf)
{
	struct blkg_conf_ctx ctx;
	struct ioinf_gq *infg;
	int ret;
	int v;

	blkg_conf_init(&ctx, buf);
	ret = blkg_conf_prep(blkcg, &blkcg_policy_ioinf, &ctx);
	if (ret) {
		blkg_conf_exit(&ctx);
		return ret;
	}

	infg = blkg_to_infg(ctx.blkg);
	if (!strncmp(ctx.body, "default", 7)) {
		v = infg->dfl_user_weight;
	} else if (kstrtoint(ctx.body, 0, &v) || abs(v) > CGROUP_WEIGHT_MAX) {
		blkg_conf_exit(&ctx);
		return -EINVAL;
	}

	spin_unlock_irq(&bdev_get_queue(ctx.bdev)->queue_lock);
	blk_mq_freeze_queue(infg->inf->rqos.disk->queue);
	blk_mq_quiesce_queue(infg->inf->rqos.disk->queue);
	infg->user_weight = v;
	propagate_parent_user_weights(infg);
	blk_mq_unquiesce_queue(infg->inf->rqos.disk->queue);
	blk_mq_unfreeze_queue(infg->inf->rqos.disk->queue);
	spin_lock_irq(&bdev_get_queue(ctx.bdev)->queue_lock);

	blkg_conf_exit(&ctx);
	return 0;
}

static ssize_t ioinf_weight_write(struct kernfs_open_file *of, char *buf,
				  size_t nbytes, loff_t off)
{
	struct blkcg *blkcg = css_to_blkcg(of_css(of));
	int ret;

	if (!strchr(buf, ':')) {
		int v;

		if (sscanf(buf, "default %d", &v) != 1 && kstrtoint(buf, 0, &v))
			return -EINVAL;

		if (abs(v) > CGROUP_WEIGHT_MAX)
			return -EINVAL;

		ioinf_default_weight_update(blkcg, v);
		return nbytes;
	}

	ret = infg_weight_write(blkcg, buf);
	return ret ? ret : nbytes;
}

static u64 ioinf_qos_prfill(struct seq_file *sf, struct blkg_policy_data *pd,
			    int off)
{
	const char *dname = blkg_dev_name(pd->blkg);
	struct ioinf *inf = q_to_inf(pd->blkg->q);
	struct ioinf_params params;

	if (!dname)
		return 0;

	params = inf->params;
	seq_printf(sf, "%s enable=%d inflight=%u flags=%lu qos_enable=%d",
		   dname, params.enabled, params.inflight, params.flags,
		   params.qos_enabled);

	if (inf->params.qos_enabled)
		seq_printf(sf, " rlat=%llu rpct=%u wlat=%llu wpct=%u",
			   params.rlat, params.rpct, params.wlat, params.wpct);

	seq_putc(sf, '\n');
	return 0;
}

static int ioinf_qos_show(struct seq_file *sf, void *v)
{
	struct blkcg *blkcg = css_to_blkcg(seq_css(sf));

	blkcg_print_blkgs(sf, blkcg, ioinf_qos_prfill,
			  &blkcg_policy_ioinf, seq_cft(sf)->private, false);
	return 0;
}

static const match_table_t qos_ctrl_tokens = {
	{ INF_ENABLE,		"enable=%u"	},
	{ INF_INFLIGHT,		"inflight=%u"	},
	{ INF_FLAGS,		"flags=%u"	},
	{ QOS_ENABLE,		"qos_enable=%u"	},
	{ QOS_RLAT,		"rlat=%u"	},
	{ QOS_WLAT,		"wlat=%u"	},
	{ QOS_RPCT,		"rpct=%u"	},
	{ QOS_WPCT,		"wpct=%u"	},
	{ NR_QOS_CTRL_PARAMS,	NULL		},
};

static ssize_t ioinf_qos_write(struct kernfs_open_file *of, char *input,
			       size_t nbytes, loff_t off)
{
	struct blkg_conf_ctx ctx;
	struct gendisk *disk;
	struct ioinf *inf;
	struct ioinf_params params = {0};
	char *body, *p;
	int ret;

	blkg_conf_init(&ctx, input);

	ret = blkg_conf_open_bdev(&ctx);
	if (ret)
		goto err;

	body = ctx.body;
	disk = ctx.bdev->bd_disk;
	if (!queue_is_mq(disk->queue)) {
		ret = -EOPNOTSUPP;
		goto err;
	}

	inf = q_to_inf(disk->queue);
	if (!inf) {
		ret = blk_ioinf_init(disk);
		if (ret)
			goto err;
		inf = q_to_inf(disk->queue);
	}
	params = inf->params;

	while ((p = strsep(&body, " \t\n"))) {
		substring_t args[MAX_OPT_ARGS];
		u64 v;

		if (!*p)
			continue;

		switch (match_token(p, qos_ctrl_tokens, args)) {
		case INF_ENABLE:
			if (match_u64(&args[0], &v))
				goto einval;
			params.enabled = !!v;
			continue;
		case INF_INFLIGHT:
			if (match_u64(&args[0], &v) || v == 0 || v > UINT_MAX)
				goto einval;
			params.inflight = v;
			continue;
		case INF_FLAGS:
			if (match_u64(&args[0], &v) || v >= 1 << NR_INF_FLAGS)
				goto einval;
			params.flags = v;
			continue;
		case QOS_ENABLE:
			if (match_u64(&args[0], &v))
				goto einval;
			params.qos_enabled = !!v;
			continue;
		case QOS_RLAT:
			if (match_u64(&args[0], &v) || v == 0)
				goto einval;
			params.rlat = v;
			continue;
		case QOS_WLAT:
			if (match_u64(&args[0], &v) || v == 0)
				goto einval;
			params.wlat = v;
			continue;
		case QOS_RPCT:
			if (match_u64(&args[0], &v) || v > 100)
				goto einval;
			params.rpct = v;
			continue;
		case QOS_WPCT:
			if (match_u64(&args[0], &v) || v > 100)
				goto einval;
			params.wpct = v;
			continue;
		default:
			goto einval;
		}
	}

	if (!params.enabled && !inf->params.enabled)
		goto out;

	blk_mq_freeze_queue(disk->queue);
	blk_mq_quiesce_queue(disk->queue);

	if (params.enabled && !inf->params.enabled) {
		blk_stat_enable_accounting(disk->queue);
		blk_queue_flag_set(QUEUE_FLAG_RQ_ALLOC_TIME, disk->queue);
	} else if (inf->params.enabled && !params.enabled) {
		blk_stat_disable_accounting(disk->queue);
		blk_queue_flag_clear(QUEUE_FLAG_RQ_ALLOC_TIME, disk->queue);
	}

	spin_lock_irq(&inf->lock);
	inf->params = params;
	inf->old_scale = inf->max_scale = MAX_SCALE;
	if (inf->inflight != params.inflight) {
		inf->scale = SCALE_GRAN;
		inf->scale_step = DIV_ROUND_UP(SCALE_GRAN,
					       inf->params.inflight);
		ioinf_update_inflight(inf, inf->online.hinflight,
				      inf->offline.hinflight);
	}
	spin_unlock_irq(&inf->lock);

	blk_mq_unquiesce_queue(disk->queue);
	blk_mq_unfreeze_queue(disk->queue);
out:
	blkg_conf_exit(&ctx);
	return nbytes;

einval:
	ret = -EINVAL;
err:
	blkg_conf_exit(&ctx);
	return ret;
}

static struct cftype ioinf_files[] = {
	{
		.name = "inf.weight",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = ioinf_weight_show,
		.write = ioinf_weight_write,
	},
	{
		.name = "inf.qos",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.seq_show = ioinf_qos_show,
		.write = ioinf_qos_write,
	},
	{}
};

static struct cftype ioinf_legacy_files[] = {
	{
		.name = "inf.weight",
		.flags = CFTYPE_NOT_ON_ROOT,
		.seq_show = ioinf_weight_show,
		.write = ioinf_weight_write,
	},
	{
		.name = "inf.qos",
		.flags = CFTYPE_ONLY_ON_ROOT,
		.seq_show = ioinf_qos_show,
		.write = ioinf_qos_write,
	},
	{}
};

static struct blkcg_policy_data *ioinf_cpd_alloc(gfp_t gfp)
{
	struct ioinf_cgrp *infcg = kzalloc(sizeof(*infcg), gfp);

	if (!infcg)
		return NULL;

	infcg->dfl_user_weight = IOINFG_WEIGHT_UNINIT;
	return &infcg->cpd;
}

static void ioinf_cpd_free(struct blkcg_policy_data *cpd)
{
	kfree(container_of(cpd, struct ioinf_cgrp, cpd));
}

static struct blkg_policy_data *ioinf_pd_alloc(struct gendisk *disk,
					       struct blkcg *blkcg, gfp_t gfp)
{
	struct ioinf_gq *infg = kzalloc_node(sizeof(*infg), gfp, disk->node_id);

	if (!infg)
		return NULL;

	return &infg->pd;
}

static void ioinf_pd_init(struct blkg_policy_data *pd)
{
	struct ioinf_gq *infg = pd_to_infg(pd);
	struct blkcg_gq *blkg = pd_to_blkg(pd);
	struct ioinf_cgrp *infcg = blkcg_to_infcg(blkg->blkcg);
	struct blkcg_gq *parent = blkg->parent;

	infg->inf = q_to_inf(blkg->q);
	if (parent)
		infg->user_weight = blkg_to_infg(parent)->user_weight;

	/* Inherit the parent cgroup's dfl_user_weight if it was not set. */
	if (infcg->dfl_user_weight == IOINFG_WEIGHT_UNINIT) {
		if (!parent || parent->blkcg->css.cgroup->level == 0)
			infcg->dfl_user_weight = 0;
		else
			infcg->dfl_user_weight =
				blkcg_to_infcg(parent->blkcg)->dfl_user_weight;
	}

	infg->dfl_user_weight = infcg->dfl_user_weight;
}

static void ioinf_pd_free(struct blkg_policy_data *pd)
{
	struct ioinf_gq *infg = pd_to_infg(pd);

	kfree(infg);
}

static struct blkcg_policy blkcg_policy_ioinf = {
	.dfl_cftypes	= ioinf_files,
	.legacy_cftypes = ioinf_legacy_files,

	.cpd_alloc_fn	= ioinf_cpd_alloc,
	.cpd_free_fn	= ioinf_cpd_free,

	.pd_alloc_fn	= ioinf_pd_alloc,
	.pd_init_fn	= ioinf_pd_init,
	.pd_free_fn	= ioinf_pd_free,
};

static int __init ioinf_init(void)
{
	return blkcg_policy_register(&blkcg_policy_ioinf);
}

static void __exit ioinf_exit(void)
{
	blkcg_policy_unregister(&blkcg_policy_ioinf);
}

MODULE_AUTHOR("Baokun Li, Yu Kuai and others");
MODULE_DESCRIPTION("Block IO infligt I/O controller");
MODULE_LICENSE("GPL");
module_init(ioinf_init);
module_exit(ioinf_exit);
