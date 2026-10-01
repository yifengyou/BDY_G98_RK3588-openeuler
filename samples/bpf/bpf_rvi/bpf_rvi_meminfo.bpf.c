// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Huawei Technologies Co., Ltd */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

#define READ_ONCE(x)		(*(volatile typeof(x) *)&(x))

void bpf_rcu_read_lock(void) __ksym;
void bpf_rcu_read_unlock(void) __ksym;
void bpf_task_release(struct task_struct *p) __ksym;
struct task_struct *bpf_current_level1_reaper(void) __ksym;
struct mem_cgroup *bpf_mem_cgroup_from_task(struct task_struct *p) __ksym;
void cgroup_rstat_flush_atomic(struct cgroup *cgrp) __ksym;
void bpf_si_memswinfo(struct bpf_sysinfo *si) __ksym;
unsigned long bpf_page_counter_read(struct page_counter *pc) __ksym;
unsigned long bpf_mem_committed(void) __ksym;
unsigned long bpf_mem_commit_limit(void) __ksym;
unsigned long bpf_mem_vmalloc_total(void) __ksym;
unsigned long bpf_mem_vmalloc_used(void) __ksym;
unsigned long bpf_mem_percpu(void) __ksym;
unsigned long bpf_mem_failure(void) __ksym;
unsigned long bpf_mem_totalcma(void) __ksym;
unsigned long bpf_mem_freecma(void) __ksym;
int bpf_hugetlb_report_meminfo(struct bpf_mem_hugepage *hugepage_info) __ksym;
void bpf_x86_direct_pages(unsigned long *p) __ksym;
unsigned long bpf_mem_file_hugepage(void) __ksym;
unsigned long bpf_mem_file_pmdmapped(void) __ksym;
unsigned long bpf_mem_kreclaimable(void) __ksym;

extern bool CONFIG_SWAP __kconfig __weak;
extern bool CONFIG_MEMCG_KMEM __kconfig __weak;
extern bool CONFIG_ZSWAP __kconfig __weak;
extern bool CONFIG_MEMORY_FAILURE __kconfig __weak;
extern bool CONFIG_TRANSPARENT_HUGEPAGE __kconfig __weak;
extern bool CONFIG_CMA __kconfig __weak;
extern bool CONFIG_X86 __kconfig __weak;
extern bool CONFIG_X86_64 __kconfig __weak;
extern bool CONFIG_X86_PAE __kconfig __weak;

/* Axiom */
#define PAGE_SHIFT		12
#define PMD_SHIFT		21
#define PAGE_SIZE		(1UL << PAGE_SHIFT)

/* include/linux/huge_mm.h */
#define HPAGE_PMD_SHIFT		PMD_SHIFT
#define HPAGE_PMD_ORDER		(HPAGE_PMD_SHIFT-PAGE_SHIFT)
#define HPAGE_PMD_NR		(1<<HPAGE_PMD_ORDER)

#define KB(pg)			((pg) * (PAGE_SIZE >> 10))
#define LONG_MAX		((long)(~0UL >> 1))
/* include/linux/page_counter.h */
#define PAGE_COUNTER_MAX	(LONG_MAX / PAGE_SIZE)

char _license[] SEC("license") = "GPL";

static inline unsigned long
memcg_page_state(struct mem_cgroup *memcg, int idx)
{
	long x = READ_ONCE(memcg->vmstats->state[idx]);

	return x < 0 ? 0 : x;
}

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
s64 dump_meminfo(struct bpf_iter__generic_single *ctx)
{
	struct seq_file *m = ctx->meta->seq;
	struct task_struct *reaper;
	struct mem_cgroup *memcg;
	struct bpf_sysinfo si = {};
	struct bpf_mem_hugepage hg_info = {};
	u64 usage, limit;
	unsigned long sreclaimable, sunreclaim;
	unsigned long memswlimit, memswusage;
	unsigned long cached, active_anon, inactive_anon;
	unsigned long active_file, inactive_file, unevicatable;
	u64 swaptotal = 0, swapusage = 0, swapfree;
	unsigned long committed;

	committed = bpf_mem_committed();
	bpf_si_memswinfo(&si);

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
	cgroup_rstat_flush_atomic(memcg->css.cgroup);
	limit = memcg->memory.max; // PAGE_COUNTER_MAX if not set
	if (limit > si.totalram) // "hard limit"
		limit = si.totalram;
	usage = bpf_page_counter_read(&memcg->memory);

	extract_swap_info(memcg, &si, &swapusage, &swaptotal);
	swapfree = swaptotal - swapusage;
	if (swapfree > si.freeswap)
		swapfree = si.freeswap;

	cached = memcg_page_state(memcg, NR_FILE_PAGES);
	active_anon = memcg_page_state(memcg, NR_ACTIVE_ANON);
	inactive_anon = memcg_page_state(memcg, NR_INACTIVE_ANON);
	active_file = memcg_page_state(memcg, NR_ACTIVE_FILE);
	inactive_file = memcg_page_state(memcg, NR_INACTIVE_FILE);
	unevicatable = memcg_page_state(memcg, NR_UNEVICTABLE);
	sreclaimable = memcg_page_state(memcg, NR_SLAB_RECLAIMABLE_B);
	sunreclaim = memcg_page_state(memcg, NR_SLAB_UNRECLAIMABLE_B);

	BPF_SEQ_PRINTF(m, "MemTotal:           %8llu kB\n", KB(limit));
	BPF_SEQ_PRINTF(m, "MemFree:            %8llu kB\n", KB(limit - usage));
	BPF_SEQ_PRINTF(m, "MemAvailable:       %8llu kB\n", KB(limit - usage + cached));
	BPF_SEQ_PRINTF(m, "Buffers:            %8llu kB\n", KB(0));
	BPF_SEQ_PRINTF(m, "Cached:             %8llu kB\n", KB(cached));

	if (CONFIG_SWAP)
		BPF_SEQ_PRINTF(m, "SwapCached:         %8llu kB\n", KB(memcg_page_state(memcg, NR_SWAPCACHE)));

	BPF_SEQ_PRINTF(m, "Active:             %8llu kB\n", KB(active_anon + active_file));
	BPF_SEQ_PRINTF(m, "Inactive:           %8llu kB\n", KB(inactive_anon + inactive_file));
	BPF_SEQ_PRINTF(m, "Active(anon):       %8llu kB\n", KB(active_anon));
	BPF_SEQ_PRINTF(m, "Inactive(anon):     %8llu kB\n", KB(inactive_anon));
	BPF_SEQ_PRINTF(m, "Active(file):       %8llu kB\n", KB(active_file));
	BPF_SEQ_PRINTF(m, "Inactive(file):     %8llu kB\n", KB(inactive_file));
	BPF_SEQ_PRINTF(m, "Unevictable:        %8llu kB\n", KB(unevicatable));
	BPF_SEQ_PRINTF(m, "Mlocked:            %8llu kB\n", KB(0));
	BPF_SEQ_PRINTF(m, "SwapTotal:          %8llu kB\n", KB(swaptotal));
	BPF_SEQ_PRINTF(m, "SwapFree:           %8llu kB\n", KB(swapfree));

	if (CONFIG_MEMCG_KMEM && CONFIG_ZSWAP) {
		BPF_SEQ_PRINTF(m, "Zswap:              %8llu kB\n", memcg_page_state(memcg, MEMCG_ZSWAP_B));
		BPF_SEQ_PRINTF(m, "Zswapped:           %8llu kB\n", memcg_page_state(memcg, MEMCG_ZSWAPPED));
	}

	BPF_SEQ_PRINTF(m, "Dirty:              %8llu kB\n", KB(memcg_page_state(memcg, NR_FILE_DIRTY)));
	BPF_SEQ_PRINTF(m, "Writeback:          %8llu kB\n", KB(memcg_page_state(memcg, NR_WRITEBACK)));
	BPF_SEQ_PRINTF(m, "AnonPages:          %8llu kB\n", KB(memcg_page_state(memcg, NR_ANON_MAPPED)));
	BPF_SEQ_PRINTF(m, "Mapped:             %8llu kB\n", KB(memcg_page_state(memcg, NR_FILE_MAPPED)));
	BPF_SEQ_PRINTF(m, "Shmem:              %8llu kB\n", KB(memcg_page_state(memcg, NR_SHMEM)));
	BPF_SEQ_PRINTF(m, "KReclaimable:       %8llu kB\n", KB(bpf_mem_kreclaimable()));
	BPF_SEQ_PRINTF(m, "Slab:               %8llu kB\n", KB(sreclaimable + sunreclaim));
	BPF_SEQ_PRINTF(m, "SReclaimable:       %8llu kB\n", KB(sreclaimable));
	BPF_SEQ_PRINTF(m, "SUnreclaim:         %8llu kB\n", KB(sunreclaim));
	BPF_SEQ_PRINTF(m, "KernelStack:        %8llu kB\n", memcg_page_state(memcg, NR_KERNEL_STACK_KB));
	BPF_SEQ_PRINTF(m, "PageTables:         %8llu kB\n", KB(memcg_page_state(memcg, NR_PAGETABLE)));
	BPF_SEQ_PRINTF(m, "SecPageTables       %8llu kB\n", KB(memcg_page_state(memcg, NR_SECONDARY_PAGETABLE)));
	BPF_SEQ_PRINTF(m, "NFS_Unstable:       %8llu kB\n", KB(0));
	BPF_SEQ_PRINTF(m, "Bounce:             %8llu kB\n", KB(0));
	BPF_SEQ_PRINTF(m, "WritebackTmp:       %8llu kB\n", KB(memcg_page_state(memcg, NR_WRITEBACK_TEMP)));
	BPF_SEQ_PRINTF(m, "CommitLimit:        %8llu kB\n", KB(bpf_mem_commit_limit()));
	BPF_SEQ_PRINTF(m, "Committed_AS:       %8llu kB\n", KB(committed));
	BPF_SEQ_PRINTF(m, "VmallocTotal:       %8llu kB\n", bpf_mem_vmalloc_total());
	BPF_SEQ_PRINTF(m, "VmallocUsed:        %8llu kB\n", KB(bpf_mem_vmalloc_used()));
	BPF_SEQ_PRINTF(m, "VmallocChunk:       %8llu kB\n", KB(0));
	BPF_SEQ_PRINTF(m, "Percpu:             %8llu kB\n", KB(bpf_mem_percpu()));

	if (CONFIG_MEMORY_FAILURE)
		BPF_SEQ_PRINTF(m, "HardwareCorrupted:  %8llu kB\n", bpf_mem_failure());

	if (CONFIG_TRANSPARENT_HUGEPAGE) {
		BPF_SEQ_PRINTF(m, "AnonHugePages:      %8llu kB\n", KB(memcg_page_state(memcg, NR_ANON_THPS) *
								       HPAGE_PMD_NR));
		BPF_SEQ_PRINTF(m, "ShmemHugePages:     %8llu kB\n", KB(memcg_page_state(memcg, NR_SHMEM_THPS) *
								       HPAGE_PMD_NR));
		BPF_SEQ_PRINTF(m, "ShmemPmdMapped:     %8llu kB\n", KB(memcg_page_state(memcg, NR_SHMEM_PMDMAPPED) *
								       HPAGE_PMD_NR));
		BPF_SEQ_PRINTF(m, "FileHugePages:      %8llu kB\n", KB(bpf_mem_file_hugepage()));
		BPF_SEQ_PRINTF(m, "FilePmdMapped:      %8llu kB\n", KB(bpf_mem_file_pmdmapped()));
	}
	if (CONFIG_CMA) {
		BPF_SEQ_PRINTF(m, "CmaTotal:           %8llu kB\n", KB(bpf_mem_totalcma()));
		BPF_SEQ_PRINTF(m, "CmaFree:            %8llu kB\n", KB(bpf_mem_freecma()));
	}
	BPF_SEQ_PRINTF(m, "Unaccepted:         %8llu kB\n", KB(0));

	if (bpf_hugetlb_report_meminfo(&hg_info) != -1) {
		BPF_SEQ_PRINTF(m, "HugePages_Total:    %8llu\n", hg_info.total);
		BPF_SEQ_PRINTF(m, "HugePages_Free:     %8llu\n", hg_info.free);
		BPF_SEQ_PRINTF(m, "HugePages_Rsvd:     %8llu\n", hg_info.rsvd);
		BPF_SEQ_PRINTF(m, "HugePages_Surp:     %8llu\n", hg_info.surp);
		BPF_SEQ_PRINTF(m, "Hugepagesize:       %8llu kB\n", hg_info.size);
		BPF_SEQ_PRINTF(m, "Hugetlb:            %8llu kB\n", hg_info.hugetlb);
	}

	/* Reference: x86's arch_report_meminfo() */
	if (CONFIG_X86) {
		/* For successful compilation on other arch */
		enum { PG_LEVEL_NONE, PG_LEVEL_4K, PG_LEVEL_2M,
			PG_LEVEL_1G, PG_LEVEL_512G, PG_LEVEL_NUM };
		unsigned long x86_direct_pages[PG_LEVEL_NUM] = {};

		bpf_x86_direct_pages(x86_direct_pages);
		BPF_SEQ_PRINTF(m, "DirectMap4k:        %8llu kB\n",
				x86_direct_pages[PG_LEVEL_4K] << 2);
		if (CONFIG_X86_64 || CONFIG_X86_PAE)
			BPF_SEQ_PRINTF(m, "DirectMap2M:        %8llu kB\n",
					x86_direct_pages[PG_LEVEL_2M] << 11);
		else
			BPF_SEQ_PRINTF(m, "DirectMap4M:        %8llu kB\n",
					x86_direct_pages[PG_LEVEL_2M] << 12);
		BPF_SEQ_PRINTF(m, "DirectMap1G:        %8llu kB\n",
				x86_direct_pages[PG_LEVEL_1G] << 20);
	}

	bpf_rcu_read_unlock();
	bpf_task_release(reaper);

	return RET_OK;
}
