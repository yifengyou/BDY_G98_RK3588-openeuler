// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Huawei Technologies Co., Ltd */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

struct task_struct *bpf_current_level1_reaper(void) __ksym;
void bpf_task_release(struct task_struct *p) __ksym;
struct mem_cgroup *bpf_mem_cgroup_from_task(struct task_struct *p) __ksym;
void bpf_si_memswinfo(struct bpf_sysinfo *si) __ksym;
unsigned long bpf_atomic_long_read(const atomic_long_t *v) __ksym;
unsigned long bpf_page_counter_read(struct page_counter *pc) __ksym;
void bpf_rcu_read_lock(void) __ksym;
void bpf_rcu_read_unlock(void) __ksym;
void cgroup_rstat_flush_atomic(struct cgroup *cgrp) __ksym;

/* Axiom */
#define PAGE_SHIFT		12
#define PAGE_SIZE		(1UL << PAGE_SHIFT)

#define LONG_MAX		((long)(~0UL >> 1))
/* include/linux/page_counter.h */
#define PAGE_COUNTER_MAX	(LONG_MAX / PAGE_SIZE)

char _license[] SEC("license") = "GPL";

/* Reference: https://docs.ebpf.io/ebpf-library/libbpf/ebpf/__ksym/ */
extern void cgrp_dfl_root __ksym;
/* Reference: cgroup_on_dfl() */
static inline bool cgroup_on_dfl(const struct cgroup *cgrp)
{
	return cgrp->root == &cgrp_dfl_root;
}

static void extract_swap_info(struct mem_cgroup *memcg, const struct bpf_sysinfo *si,
			      u64 *out_swapusage, u64 *out_swaptotal)
{
	u64 swapusage = 0, swaptotal = 0;

	if (cgroup_on_dfl(memcg->css.cgroup)) { // if memcg is on V2 hierarchy
		swaptotal = memcg->swap.max;
		swapusage = bpf_page_counter_read(&memcg->swap);
	} else {
		/* si.totalram and memcg->memory.{max,...} are all in pages */
		u64 limit = memcg->memory.max;
		/*
		 * Reference: page_counter_read().
		 * memcg->memory.usage is atomic, should be read by (bpf_)atomic_long_read.
		 * Consider using mem_cgroup_usage(memcg, true/false)?
		 *
		 * memcg.memory.usage is read-only for userspace, and its validity is maintained by
		 * kernel. So don't need to compare it with `limit` or threshold it.
		 */
		u64 usage = bpf_page_counter_read(&memcg->memory);
		u64 memsw_limit = memcg->memsw.max; // memsw = mem + swap
		u64 memsw_usage = bpf_page_counter_read(&memcg->memsw);

		if (memsw_usage > usage)
			swapusage = memsw_usage - usage;

		/*
		 * How swaptotal is deduced:
		 *   1. If memsw.max is not set, then there's no limit to the amount of swap
		 *      the container can use, so it can use up to all swap of the host.
		 *   2. If memsw.max is set but mem.max is not, we can't deduce swap limit from
		 *      memsw.max solely or memsw.max - mem.max. The best guess would be the
		 *      minimum between memsw.max and the global swap amount.
		 *   3. If both mem{,sw}.max are set, then we deduce swaptotal from their
		 *      difference. Note that memsw.max < mem.max is possible, since both are
		 *      configurable by users. Nevertheless reasonably it shouldn't, as in V1
		 *      memsw = mem + swap in semantics.
		 */
		if (memsw_limit == PAGE_COUNTER_MAX) // i.e. not limited
			swaptotal = si->totalswap;
		else if (limit == PAGE_COUNTER_MAX)
			swaptotal = memsw_limit < si->totalswap ? memsw_limit : si->totalswap;
		else
			swaptotal = memsw_limit > limit ? memsw_limit - limit : 0;

		/*
		 * swaptotal is estimated based on user-customizable values (for cgroup V1), while
		 * swapusage is deduced from OS-maintained values. They might not align with each
		 * other. Make a final effort to keep these values plausible.
		 */
		if (swaptotal < swapusage)
			swaptotal = swapusage;
	}
	/* Set a "hard limit" for swaptotal. *usage are always valid, as stated above */
	if (swaptotal > si->totalswap)
		swaptotal = si->totalswap;

	*out_swapusage = swapusage;
	*out_swaptotal = swaptotal;
}

#define RET_OK    0
#define RET_FAIL  1

SEC("iter/generic_single")
s64 dump_swaps(struct bpf_iter__generic_single *ctx)
{
	struct seq_file *m = ctx->meta->seq;
	struct task_struct *reaper;
	struct mem_cgroup *memcg;
	struct bpf_sysinfo si = {};
	u64 swapusage = 0, swaptotal = 0;
	u64 kb_per_page;

	reaper = bpf_current_level1_reaper();
	if (!reaper)
		return RET_FAIL;
	bpf_rcu_read_lock();
	memcg = bpf_mem_cgroup_from_task(reaper);
	if (!memcg) {
		bpf_rcu_read_unlock();
		bpf_task_release(reaper);
		return RET_FAIL;
	}

	bpf_si_memswinfo(&si);
	cgroup_rstat_flush_atomic(memcg->css.cgroup);

	extract_swap_info(memcg, &si, &swapusage, &swaptotal);

	/* si.mem_unit: (actual) PAGE_SIZE */
	kb_per_page = si.mem_unit >> 10;
	/* Reference: swap_show(). Aligned with LXCFS. */
	BPF_SEQ_PRINTF(m, "Filename\t\t\t\tType\t\tSize\t\tUsed\t\tPriority\n");
	if (swaptotal > 0)
		BPF_SEQ_PRINTF(m, "none\t\t\t\tvirtual\t\t%llu\t\t%llu\t\t0\n",
				  swaptotal * kb_per_page,
				  swapusage * kb_per_page); // in KB

	bpf_rcu_read_unlock();
	bpf_task_release(reaper);

	return RET_OK;
}
