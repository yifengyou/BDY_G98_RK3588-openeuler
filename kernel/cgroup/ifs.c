// SPDX-License-Identifier: GPL-2.0-only
/*
 * Interference statistics for cgroup
 *
 * Copyright (C) 2025-2025 Huawei Technologies Co., Ltd
 */

#include <linux/sched/clock.h>
#include "cgroup-internal.h"

#define TDESC_MAX_SLOT	64
#define TDESC_BUF_SIZE	32

enum {
	IFS_TIMER_CLK,
	IFS_TIMER_TSC,
	IFS_TIMER_NUM,
};

/* time range description, for printing */
struct ifs_tdesc {
	char tdesc_str[TDESC_MAX_SLOT][TDESC_BUF_SIZE];
	int tdesc_num;
};

/* smt interference */
struct smt_itf {
	u64 total_time; /* total time of all smt interferences */
	u64 enter_time; /* enter time of the most recent smt interference */
	atomic_t lock;
};

struct smt_info {
	struct smt_itf *itf;
	u64 prev_read_time;
	bool is_noidle;
};

static DEFINE_PER_CPU(u64, ifs_tsc_freq);
static DEFINE_PER_CPU(struct smt_itf, smt_itf);
static DEFINE_PER_CPU(struct smt_info, smt_info);
static DEFINE_PER_CPU_READ_MOSTLY(int, smt_sibling) = -1;

static struct ifs_tdesc ifs_tdesc[IFS_TIMER_NUM];

static DEFINE_PER_CPU(struct cgroup_ifs_cpu, cgrp_root_ifs_cpu);
struct cgroup_ifs cgroup_root_ifs = {
	.pcpu = &cgrp_root_ifs_cpu,
};

DEFINE_STATIC_KEY_FALSE(cgrp_ifs_enabled);
DEFINE_STATIC_KEY_TRUE(cgrp_ifs_tsc_available);

#ifdef CONFIG_CGROUP_IFS_DEFAULT_ENABLED
static bool ifs_enable = true;
#else
static bool ifs_enable;
#endif

#ifdef CONFIG_SCHEDSTATS
static bool ifs_sleep_enable;
#endif

#ifdef CONFIG_IRQ_TIME_ACCOUNTING
static bool ifs_irq_enable;
#endif

static int __init setup_ifs(char *str)
{
	return kstrtobool(str, &ifs_enable) == 0;
}
__setup("cgroup_ifs=", setup_ifs);

void cgroup_ifs_set_smt(cpumask_t *sibling)
{
	int cpu;
	int cpuid1 = -1;
	int cpuid2 = -1;
	bool off = false;
	struct smt_itf *itf;

	for_each_cpu(cpu, sibling) {
		if (cpuid1 == -1) {
			cpuid1 = cpu;
		} else if (cpuid2 == -1) {
			cpuid2 = cpu;
		} else {
			*per_cpu_ptr(&smt_sibling, cpu) = -1;
			off = true;
		}
	}

	if (cpuid1 != -1)
		*per_cpu_ptr(&smt_sibling, cpuid1) = off ? -1 : cpuid2;

	if (cpuid2 != -1)
		*per_cpu_ptr(&smt_sibling, cpuid2) = off ? -1 : cpuid1;

	if (cpuid1 != -1 && cpuid2 != -1 && !off) {
		itf = per_cpu_ptr(&smt_itf, cpuid1);
		per_cpu_ptr(&smt_info, cpuid1)->itf = itf;
		per_cpu_ptr(&smt_info, cpuid2)->itf = itf;
	}
}

static void account_smttime(struct cgroup_ifs *ifs, struct smt_info *info,
			    u64 total_time)
{
	u64 delta;

	if (!ifs)
		return;

	delta = total_time - info->prev_read_time;
	if (!delta)
		return;

	info->prev_read_time = total_time;
	cgroup_ifs_account_delta(this_cpu_ptr(ifs->pcpu), IFS_SMT, delta);
}

static inline void ifs_smt_lock(atomic_t *lock)
{
	while (!atomic_cmpxchg_acquire(lock, 0, 1))
		barrier();
}

static inline void ifs_smt_unlock(atomic_t *lock)
{
	atomic_set_release(lock, 0);
}

void cgroup_ifs_account_smttime(struct task_struct *prev,
				struct task_struct *next,
				struct task_struct *idle)
{
	struct smt_info *ci, *si;
	struct smt_itf *itf;
	u64 now, total_time;
	int sibling;
	s64 delta;

	sibling = this_cpu_read(smt_sibling);
	if (sibling == -1 || prev == next)
		return;

	ci = this_cpu_ptr(&smt_info);
	si = per_cpu_ptr(&smt_info, sibling);
	itf = ci->itf;

	total_time = 0;
	now = sched_clock_cpu(smp_processor_id());

	/* idle => non-idle */
	if (prev == idle) {
		ifs_smt_lock(&itf->lock);
		/* if sibling is also noidle, mark smt interference start */
		if (si->is_noidle)
			itf->enter_time = now;
		ci->is_noidle = true;
		ifs_smt_unlock(&itf->lock);
	/* non-idle => idle */
	} else if (next == idle) {
		ifs_smt_lock(&itf->lock);
		if (itf->enter_time) { /* in smt interference */
			delta = now - itf->enter_time;
			if (delta > 0)
				itf->total_time += delta;
			/* leave smt interference */
			itf->enter_time = 0;
		}
		total_time = itf->total_time;
		ci->is_noidle = false;
		ifs_smt_unlock(&itf->lock);

		account_smttime(task_ifs(prev), ci, total_time);
	/* non-idle => non-idle */
	} else {
		ifs_smt_lock(&itf->lock);
		if (itf->enter_time) { /* in smt interference */
			delta = now - itf->enter_time;
			if (delta > 0) {
				itf->total_time += delta;
				itf->enter_time = now;
			}
		}
		total_time = itf->total_time;
		ifs_smt_unlock(&itf->lock);

		account_smttime(task_ifs(prev), ci, total_time);
	}
}

int cgroup_ifs_alloc(struct cgroup *cgrp)
{
	cgrp->ifs = kzalloc(sizeof(struct cgroup_ifs), GFP_KERNEL);
	if (!cgrp->ifs)
		return -ENOMEM;

	cgrp->ifs->pcpu = alloc_percpu(struct cgroup_ifs_cpu);
	if (!cgrp->ifs->pcpu) {
		kfree(cgrp->ifs);
		return -ENOMEM;
	}

	return 0;
}

void cgroup_ifs_free(struct cgroup *cgrp)
{
	free_percpu(cgrp->ifs->pcpu);
	kfree(cgrp->ifs);
}

static const char *ifs_type_name(int type)
{
	char *name = NULL;

	switch (type) {
	case IFS_SPINLOCK:
		name = "spinlock";
		break;
	case IFS_MUTEX:
		name = "mutex";
		break;
	case IFS_SMT:
		name = "smt";
		break;
	case IFS_RUNDELAY:
		name = "rundelay";
		break;
	case IFS_WAKELAT:
		name = "wakelat";
		break;
	case IFS_THROTTLE:
		name = "throttle";
		break;
#ifdef CONFIG_IRQ_TIME_ACCOUNTING
	case IFS_SOFTIRQ:
		name = "softirq";
		break;
	case IFS_HARDIRQ:
		name = "hardirq";
		break;
#endif
#ifdef CONFIG_SCHEDSTATS
	case IFS_SLEEP:
		name = "sleep";
		break;
#endif
	default:
		break;
	}

	return name;
}

static int tdesc_print(char *buf, int size, u64 nsecs)
{
	int ret;

	if (nsecs < NSEC_PER_USEC)
		ret = snprintf(buf, size, "%llu ns", nsecs);
	else if (nsecs < NSEC_PER_MSEC)
		ret = snprintf(buf, size, "%llu.%02llu us",
			       nsecs / NSEC_PER_USEC,
			       (nsecs % NSEC_PER_USEC) / 10);
	else if (nsecs < NSEC_PER_SEC)
		ret = snprintf(buf, size, "%llu.%02llu ms",
			       nsecs / NSEC_PER_MSEC,
			       (nsecs % NSEC_PER_MSEC) / 10000);
	else
		ret = snprintf(buf, size, "%llu.%02llu s",
			       nsecs / NSEC_PER_SEC,
			       (nsecs % NSEC_PER_SEC) / 10000000);
	return ret;
}

static void tdesc_init(struct ifs_tdesc *desc, u64 freq)
{
	u64 start = 1, end;
	u64 left, right;
	int len, size;
	int i = 0;
	char *buf;

	do {
		end = start << 1;
		if (freq == NSEC_PER_SEC)
			left = start;
		else
			left = (((u64)NSEC_PER_SEC << 5) / freq * start) >> 5;
		right = left << 1;

		len = 0;
		size = TDESC_BUF_SIZE;

		buf = &desc->tdesc_str[TDESC_MAX_SLOT - 1 - i][0];
		len += snprintf(buf, size, "[");
		len += tdesc_print(buf + len, size - len, left);

		/* less than 1 hour*/
		if (left <  (u64)NSEC_PER_SEC * 3600ULL) {
			len += snprintf(buf + len, size - len, ", ");
			len += tdesc_print(buf + len, size - len, right);
			len += snprintf(buf + len, size - len, ")");
		} else {
			snprintf(buf + len, size - len, " .... )");
			desc->tdesc_num = TDESC_MAX_SLOT - 1 - i;
			break;
		}
		start = end;
	} while (++i < TDESC_MAX_SLOT);
}

static void cgroup_ifs_tdesc_init(void)
{
	tdesc_init(&ifs_tdesc[IFS_TIMER_CLK], NSEC_PER_SEC);
	if (cgroup_ifs_tsc_available())
		tdesc_init(&ifs_tdesc[IFS_TIMER_TSC], this_cpu_read(ifs_tsc_freq));
}

static u64 tsc_cycles_to_nsec(u64 tsc_cycles)
{
#if defined(__aarch64__) || defined(__x86_64__)
	return (((u64)NSEC_PER_SEC << 5) / this_cpu_read(ifs_tsc_freq) * tsc_cycles) >> 5;
#else
	return tsc_cycles;
#endif
}

static void cgroup_ifs_tsc_init(void)
{
	u64 freq = 0;
	int cpu;

#if defined(__aarch64__)
	asm volatile ("MRS %0, CNTFRQ_EL0" : "=r" (freq));
#elif defined(__x86_64__)
	if (likely(boot_cpu_has(X86_FEATURE_CONSTANT_TSC)))
		freq = tsc_khz * 1000;
#endif
	if (!freq) {
		pr_warn("IFS: no constant tsc, use default clocksource as time source\n");
		static_branch_disable(&cgrp_ifs_tsc_available);
		return;
	}

	for_each_possible_cpu(cpu)
		per_cpu(ifs_tsc_freq, cpu) = freq;
}

static bool should_print(int type)
{
#ifdef CONFIG_SCHEDSTATS
	if (type == IFS_SLEEP)
		return ifs_sleep_enable;
#endif
#ifdef CONFIG_IRQ_TIME_ACCOUNTING
	if (type == IFS_SOFTIRQ || type == IFS_HARDIRQ)
		return ifs_irq_enable;
#endif
	return true;
}

static bool use_tsc(enum ifs_types t)
{
	return cgroup_ifs_tsc_available() && (t == IFS_SPINLOCK || t == IFS_MUTEX);
}

static int print_sum_time(struct cgroup_ifs *ifs, struct seq_file *seq)
{
	u64 time[NR_IFS_TYPES] = { 0 };
	int cpu;
	int i;

	for_each_possible_cpu(cpu) {
		for (i = 0; i < NR_IFS_TYPES; i++) {
			if (!should_print(i))
				continue;
			time[i] += per_cpu_ptr(ifs->pcpu, cpu)->time[i];
		}
	}

	seq_printf(seq, "%-18s%s\n", "Interference", "Total Time (ns)");

	for (i = 0; i < NR_IFS_TYPES; i++) {
		if (!should_print(i))
			continue;
		if (use_tsc(i))
			time[i] = tsc_cycles_to_nsec(time[i]);
		seq_printf(seq, "%-18s%llu\n", ifs_type_name(i), time[i]);
	}

	seq_puts(seq, "\n");

	return 0;
}

static int print_hist_count(struct cgroup_ifs *ifs, struct seq_file *seq)
{
	struct cgroup_ifs_hist *h;
	struct ifs_tdesc *desc;
	bool is_print_title;
	const char *name;
	u64 start;
	int count;
	int i, j;
	int cpu;

	h = kzalloc(sizeof(struct cgroup_ifs_hist), GFP_KERNEL);
	if (!h)
		return -ENOMEM;

	for_each_possible_cpu(cpu) {
		struct cgroup_ifs_cpu *ifsc = per_cpu_ptr(ifs->pcpu, cpu);

		for (i = 0; i < NR_IFS_TYPES; i++) {
			if (!should_print(i))
				continue;
			for (j = 0; j < CGROUP_IFS_HIST_SLOTS; j++)
				h->counts[i][j] += ifsc->hist.counts[i][j];
		}
	}

	for (i = 0; i < NR_IFS_TYPES; i++) {
		name = ifs_type_name(i);
		is_print_title = false;
		start = 1ULL << 63;

		if (!should_print(i))
			continue;

		if (use_tsc(i))
			desc = &ifs_tdesc[IFS_TIMER_TSC];
		else
			desc = &ifs_tdesc[IFS_TIMER_CLK];

		count = 0;
		for (j = 0; j < CGROUP_IFS_HIST_SLOTS; j++) {
			if (j < desc->tdesc_num) {
				count += h->counts[i][j];
				continue;
			} else if (j == desc->tdesc_num) {
				count += h->counts[i][j];
			} else {
				count = h->counts[i][j];
			}

			if (count) {
				if (unlikely(!is_print_title)) {
					is_print_title = true;
					seq_printf(seq, "%s distribution\n", name);
				}
				seq_printf(seq, "%-24s: %d\n",
					   desc->tdesc_str[j], count);
			}
			start /= 2;
		}

		if (is_print_title)
			seq_puts(seq, "\n");
	}

	kfree(h);
	return 0;
}

static int cgroup_ifs_show(struct seq_file *seq, void *v)
{
	struct cgroup __maybe_unused *cgrp = seq_css(seq)->cgroup;
	struct cgroup_ifs __maybe_unused *ifs = cgroup_ifs(cgrp);
	int ret;

	if (!ifs) {
		pr_info("cgroup_ino(cgrp) = %ld\n", cgroup_ino(cgrp));
		return -EINVAL;
	}

#ifdef CONFIG_CGROUP_CPUACCT
	if ((!cgroup_subsys_on_dfl(cpuacct_cgrp_subsys) && cgroup_on_dfl(cgrp)) ||
	    (cgroup_subsys_on_dfl(cpuacct_cgrp_subsys) && !cgroup_on_dfl(cgrp))) {
		pr_info("cgroup version mismatch: subsystem %s, cgroup %s\n",
			cgroup_subsys_on_dfl(cpuacct_cgrp_subsys) ? "v2" : "v1",
			cgroup_on_dfl(cgrp) ? "v2" : "v1");
		return -EOPNOTSUPP;
	}
#endif

	ret = print_sum_time(ifs, seq);
	if (ret)
		return ret;

	ret = print_hist_count(ifs, seq);
	if (ret)
		return ret;

	return 0;
}

static ssize_t cgroup_ifs_write(struct kernfs_open_file *of, char *buf,
				size_t nbytes, loff_t off)
{
	struct cgroup *cgrp = seq_css(of->seq_file)->cgroup;
	struct cgroup_ifs *ifs = cgroup_ifs(cgrp);
	struct cgroup_ifs_cpu *ifsc;
	bool clear;
	int cpu;

	if (!ifs)
		return -EOPNOTSUPP;

	if (kstrtobool(strstrip(buf), &clear) < 0)
		return -EINVAL;

	if (!clear) {
		for_each_possible_cpu(cpu) {
			ifsc = per_cpu_ptr(ifs->pcpu, cpu);
			memset(ifsc->time, 0, sizeof(ifsc->time));
			memset(ifsc->hist.counts, 0, sizeof(ifsc->hist.counts));
		}
	}

	return nbytes;
}

static struct cftype cgroup_ifs_files[] = {
	{
		.name = "interference.stat",
		.seq_show = cgroup_ifs_show,
		.write = cgroup_ifs_write,
	},
	{ }     /* terminate */
};

struct cftype cgroup_v1_ifs_files[] = {
	{
		.name = "interference.stat",
		.flags = CFTYPE_NO_PREFIX,
		.seq_show = cgroup_ifs_show,
		.write = cgroup_ifs_write,
	},
	{ }     /* terminate */
};

void cgroup_ifs_init(void)
{
	if (!ifs_enable)
		return;

	cgroup_ifs_tsc_init();
	BUG_ON(cgroup_init_cftypes(NULL, cgroup_ifs_files));
	cgroup_ifs_tdesc_init();
}

int cgroup_ifs_add_files(struct cgroup_subsys_state *css, struct cgroup *cgrp)
{
	int ret = 0;

	if (!ifs_enable)
		return 0;

	ret = cgroup_addrm_files(css, cgrp, cgroup_ifs_files, true);
	if (ret < 0) {
		cgroup_addrm_files(css, cgrp, cgroup_ifs_files, false);
		return ret;
	}

	return 0;
}

void cgroup_ifs_rm_files(struct cgroup_subsys_state *css, struct cgroup *cgrp)
{
	if (ifs_enable)
		cgroup_addrm_files(css, cgrp, cgroup_ifs_files, false);
}

#ifdef CONFIG_SCHEDSTATS
void cgroup_ifs_enable_sleep_account(void)
{
	ifs_sleep_enable = true;
}
#endif

#ifdef CONFIG_IRQ_TIME_ACCOUNTING
void cgroup_ifs_enable_irq_account(bool enable)
{
	ifs_irq_enable = enable;
}
#endif

static __init int cgroup_ifs_enable(void)
{
	if (!ifs_enable)
		return 0;

	static_branch_enable(&cgrp_ifs_enabled);
	return 0;
}

/*
 * Execution Timing Constraints:
 * 1. Must be late enough (e.g., after SUBSYS_INITCALL) to avoid the
 * intermediate state of the core cgroup subsystem initialization, ensuring
 * all internal structures are stable.
 * 2. Must execute strictly before cgroup_v1_ifs_init(), which runs at
 * LATE_INITCALL_SYNC, as cgroup_v1_ifs_init() relies on 'cgrp_ifs_enabled'
 * being set before its execution begins.
 */
device_initcall(cgroup_ifs_enable);
