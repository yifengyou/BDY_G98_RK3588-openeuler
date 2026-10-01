// SPDX-License-Identifier: GPL-2.0-only
/*
 * mem_sampling.c: declare the mem_sampling abstract layer and provide
 * unified pmu sampling for NUMA, DAMON, etc.
 *
 * Sample records are converted to mem_sampling_record, and then
 * mem_sampling_record_captured_cb_type invoke the callbacks to
 * pass the record.
 *
 * Copyright (c) 2024-2025, Huawei Technologies Ltd.
 */

#define pr_fmt(fmt) "mem_sampling: " fmt

#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mem_sampling.h>
#include <linux/mempolicy.h>
#include <linux/task_work.h>
#include <linux/migrate.h>
#include <trace/events/kmem.h>
#include <linux/sched/numa_balancing.h>

#define MEM_SAMPLING_DISABLED		0x0
#define MEM_SAMPLING_NORMAL		0x1
#define MEM_SAMPLING_MIN_VALUE		0
#define MEM_SAMPLING_MAX_VALUE		5

struct mem_sampling_ops_struct mem_sampling_ops;
static int mem_sampling_override __initdata;
static int sysctl_mem_sampling_mode;

static const int mem_sampling_min_value = MEM_SAMPLING_MIN_VALUE;
static const int mem_sampling_max_value = MEM_SAMPLING_MAX_VALUE;

/* keep track of who use the SPE */
DEFINE_PER_CPU(enum arm_spe_user_e, arm_spe_user);
EXPORT_PER_CPU_SYMBOL_GPL(arm_spe_user);

enum mem_sampling_saved_state_e {
	MEM_SAMPLING_STATE_DISABLE,
	MEM_SAMPLING_STATE_ENABLE,
	MEM_SAMPLING_STATE_NUMA_ENABLE,
	MEM_SAMPLING_STATE_DAMON_ENABLE,
	MEM_SAMPLING_STATE_NUM_DAMON_ENABLE,
	MEM_SAMPLING_STATE_EMPTY,
};
enum mem_sampling_saved_state_e mem_sampling_saved_state = MEM_SAMPLING_STATE_EMPTY;

/*
 * Callbacks should be registered using mem_sampling_record_cb_register()
 * by NUMA, DAMON and etc during their initialisation.
 * Callbacks will be invoked on new hardware pmu records caputured.
 */
typedef void (*mem_sampling_record_cb_type)(struct mem_sampling_record *record);

struct mem_sampling_record_cb_list_entry {
	struct list_head list;
	mem_sampling_record_cb_type cb;
};
LIST_HEAD(mem_sampling_record_cb_list);

struct mem_sampling_numa_access_work {
	struct callback_head work;
	u64 vaddr, paddr;
	int cpu;
};

void mem_sampling_record_cb_register(mem_sampling_record_cb_type cb)
{
	struct mem_sampling_record_cb_list_entry *cb_entry, *tmp;

	list_for_each_entry_safe(cb_entry, tmp, &mem_sampling_record_cb_list, list) {
		if (cb_entry->cb == cb)
			return;
	}

	cb_entry = kmalloc(sizeof(struct mem_sampling_record_cb_list_entry), GFP_KERNEL);
	if (!cb_entry)
		return;

	cb_entry->cb = cb;
	list_add(&(cb_entry->list), &mem_sampling_record_cb_list);
}

void mem_sampling_record_cb_unregister(mem_sampling_record_cb_type cb)
{
	struct mem_sampling_record_cb_list_entry *cb_entry, *tmp;

	list_for_each_entry_safe(cb_entry, tmp, &mem_sampling_record_cb_list, list) {
		if (cb_entry->cb == cb) {
			list_del(&cb_entry->list);
			kfree(cb_entry);
			return;
		}
	}
}

DEFINE_STATIC_KEY_FALSE(mem_sampling_access_hints);
void mem_sampling_sched_in(struct task_struct *prev, struct task_struct *curr)
{
	if (!static_branch_unlikely(&mem_sampling_access_hints))
		return;

	if (!mem_sampling_ops.sampling_start)
		return;

	if (curr->mm)
		mem_sampling_ops.sampling_start();
	else
		mem_sampling_ops.sampling_stop();
}

DEFINE_STATIC_KEY_FALSE(sched_numabalancing_mem_sampling);
#ifdef CONFIG_NUMABALANCING_MEM_SAMPLING
static int numa_migrate_prep(struct folio *folio, struct vm_area_struct *vma,
		      unsigned long addr, int page_nid, int *flags)
{
	folio_get(folio);

	/* Record the current PID acceesing VMA */
	vma_set_access_pid_bit(vma);

	count_vm_numa_event(NUMA_HINT_FAULTS);
	if (page_nid == numa_node_id()) {
		count_vm_numa_event(NUMA_HINT_FAULTS_LOCAL);
		*flags |= TNF_FAULT_LOCAL;
	}

	return mpol_misplaced(folio, vma, addr);
}

/*
 * Called from task_work context to act upon the page access.
 *
 * Physical address (provided by SPE) is used directly instead
 * of walking the page tables to get to the PTE/page. Hence we
 * don't check if PTE is writable for the TNF_NO_GROUP
 * optimization, which means RO pages are considered for grouping.
 */
static void do_numa_access(struct task_struct *p, u64 laddr, u64 paddr)
{
	struct mm_struct *mm = p->mm;
	struct vm_area_struct *vma;
	struct page *page = NULL;
	struct folio *folio;
	int page_nid = NUMA_NO_NODE;
	int last_cpupid;
	int target_nid;
	int flags = 0;

	if (!mm)
		return;

	if (!mmap_read_trylock(mm))
		return;

	vma = find_vma(mm, laddr);
	if (!vma)
		goto out_unlock;

	if (!vma_migratable(vma) || !vma_policy_mof(vma) ||
		is_vm_hugetlb_page(vma) || (vma->vm_flags & VM_MIXEDMAP))
		goto out_unlock;

	if (!vma->vm_mm ||
	    (vma->vm_file && (vma->vm_flags & (VM_READ|VM_WRITE)) == (VM_READ)))
		goto out_unlock;

	if (!vma_is_accessible(vma))
		goto out_unlock;

	page = pfn_to_online_page(PHYS_PFN(paddr));
	folio = page_folio(page);

	if (!folio || folio_is_zone_device(folio))
		goto out_unlock;

	if (unlikely(!PageLRU(page)))
		goto out_unlock;

	/* TODO: handle PTE-mapped THP or PMD-mapped THP*/
	if (folio_test_large(folio))
		goto out_unlock;

	/*
	 * Flag if the page is shared between multiple address spaces. This
	 * is later used when determining whether to group tasks together
	 */
	if (folio_likely_mapped_shared(folio) && (vma->vm_flags & VM_SHARED))
		flags |= TNF_SHARED;

	page_nid = folio_nid(folio);

	/*
	 * For memory tiering mode, cpupid of slow memory page is used
	 * to record page access time.  So use default value.
	 */
	if (folio_use_access_time(folio))
		last_cpupid = (-1 & LAST_CPUPID_MASK);
	else
		last_cpupid = folio_last_cpupid(folio);
	target_nid = numa_migrate_prep(folio, vma, laddr, page_nid, &flags);
	if (target_nid == NUMA_NO_NODE) {
		folio_put(folio);
		goto out;
	}

	/* Migrate to the requested node */
	if (migrate_misplaced_folio(folio, vma, target_nid)) {
		page_nid = target_nid;
		flags |= TNF_MIGRATED;
	} else {
		flags |= TNF_MIGRATE_FAIL;
	}

out:
	trace_mm_numa_migrating(laddr, page_nid, target_nid, flags&TNF_MIGRATED);
	if (page_nid != NUMA_NO_NODE)
		task_numa_fault(last_cpupid, page_nid, 1, flags);

out_unlock:
	mmap_read_unlock(mm);
}

static void task_mem_sampling_access_work(struct callback_head *work)
{
	struct mem_sampling_numa_access_work *iwork =
		container_of(work, struct mem_sampling_numa_access_work, work);

	if (iwork->cpu == smp_processor_id())
		do_numa_access(current, iwork->vaddr, iwork->paddr);
	kfree(iwork);
}

static void numa_create_taskwork(u64 vaddr, u64 paddr, int cpu)
{
	struct mem_sampling_numa_access_work *iwork = NULL;

	iwork = kzalloc(sizeof(*iwork), GFP_ATOMIC);
	if (!iwork)
		return;

	iwork->vaddr = vaddr;
	iwork->paddr = paddr;
	iwork->cpu = cpu;

	init_task_work(&iwork->work, task_mem_sampling_access_work);
	task_work_add(current, &iwork->work, TWA_RESUME);
}

static void numa_balancing_mem_sampling_cb(struct mem_sampling_record *record)
{
	struct task_struct *p = current;
	u64 vaddr = record->virt_addr;
	u64 paddr = record->phys_addr;

	/* Discard kernel address accesses */
	if (vaddr & (1UL << 63))
		return;

	if (p->pid != record->context_id)
		return;

	trace_mm_mem_sampling_access_record(vaddr, paddr, smp_processor_id(),
					current->pid);
	numa_create_taskwork(vaddr, paddr, smp_processor_id());
}

static void numa_balancing_mem_sampling_cb_register(void)
{
	mem_sampling_record_cb_register(numa_balancing_mem_sampling_cb);
}

static void numa_balancing_mem_sampling_cb_unregister(void)
{
	mem_sampling_record_cb_unregister(numa_balancing_mem_sampling_cb);
}
static void set_numabalancing_mem_sampling_state(bool enabled)
{
	if (!mem_sampling_ops.sampling_start || !mm_spe_enabled())
		return;

	if (enabled) {
		numa_balancing_mem_sampling_cb_register();
		static_branch_enable(&sched_numabalancing_mem_sampling);
	} else {
		numa_balancing_mem_sampling_cb_unregister();
		static_branch_disable(&sched_numabalancing_mem_sampling);
	}
}
#else
static inline void set_numabalancing_mem_sampling_state(bool enabled) { }
#endif /* CONFIG_NUMABALANCING_MEM_SAMPLING */

DEFINE_STATIC_KEY_FALSE(mm_damon_mem_sampling);
#ifdef CONFIG_DAMON_MEM_SAMPLING
static void damon_mem_sampling_record_cb(struct mem_sampling_record *record)
{
	struct damon_mem_sampling_fifo *damon_fifo;
	struct damon_mem_sampling_record domon_record;
	struct task_struct *task = NULL;
	struct mm_struct *mm;

	/* Discard kernel address accesses */
	if (record->virt_addr & (1UL << 63))
		return;

	task = find_get_task_by_vpid((pid_t)record->context_id);
	if (!task)
		return;

	mm = get_task_mm(task);
	put_task_struct(task);
	if (!mm)
		return;

	damon_fifo = mm->damon_fifo;
	mmput(mm);

	domon_record.vaddr = record->virt_addr;
	trace_mm_mem_sampling_damon_record(record->virt_addr, (pid_t)record->context_id);

	/* only the proc under monitor now has damon_fifo */
	if (damon_fifo) {
		if (kfifo_is_full(&damon_fifo->rx_kfifo))
			return;

		kfifo_in_locked(&damon_fifo->rx_kfifo, &domon_record,
				sizeof(struct damon_mem_sampling_record),
				&damon_fifo->rx_kfifo_lock);
		return;
	}
}

static void damon_mem_sampling_record_cb_register(void)
{
	mem_sampling_record_cb_register(damon_mem_sampling_record_cb);
}

static void damon_mem_sampling_record_cb_unregister(void)
{
	mem_sampling_record_cb_unregister(damon_mem_sampling_record_cb);
}

static void set_damon_mem_sampling_state(bool enabled)
{
	if (!mem_sampling_ops.sampling_start || !mm_spe_enabled())
		return;

	if (enabled) {
		damon_mem_sampling_record_cb_register();
		static_branch_enable(&mm_damon_mem_sampling);
	} else {
		damon_mem_sampling_record_cb_unregister();
		static_branch_disable(&mm_damon_mem_sampling);
	}
}

bool damon_use_mem_sampling(void)
{
	return static_branch_unlikely(&mem_sampling_access_hints) &&
			static_branch_unlikely(&mm_damon_mem_sampling);
}
#else
static inline void set_damon_mem_sampling_state(bool enabled) { }
#endif

void mem_sampling_process(void)
{
	int i, nr_records;
	struct mem_sampling_record *record;
	struct mem_sampling_record *record_base;
	struct mem_sampling_record_cb_list_entry *cb_entry, *tmp;

	mem_sampling_ops.sampling_decoding();

	record_base = (struct mem_sampling_record *)mem_sampling_ops.mm_spe_getbuf_addr();
	nr_records = mem_sampling_ops.mm_spe_getnum_record();

	if (list_empty(&mem_sampling_record_cb_list))
		goto out;

	for (i = 0; i < nr_records; i++) {
		record = record_base + i;
		list_for_each_entry_safe(cb_entry, tmp, &mem_sampling_record_cb_list, list) {
			cb_entry->cb(record);
		}
	}
out:
	/* if mem_sampling_access_hints is set to false, stop sampling */
	if (static_branch_unlikely(&mem_sampling_access_hints))
		mem_sampling_ops.sampling_continue();
	else
		mem_sampling_ops.sampling_stop();
}
EXPORT_SYMBOL_GPL(mem_sampling_process);

static inline enum mem_sampling_type_enum mem_sampling_get_type(void)
{
#ifdef CONFIG_ARM_SPE_MEM_SAMPLING
	return MEM_SAMPLING_ARM_SPE;
#else
	return MEM_SAMPLING_UNSUPPORTED;
#endif
}

static void __set_mem_sampling_state(bool enabled)
{
	if (enabled)
		static_branch_enable(&mem_sampling_access_hints);
	else {
		static_branch_disable(&mem_sampling_access_hints);
		set_numabalancing_mem_sampling_state(enabled);
		set_damon_mem_sampling_state(enabled);
	}
}

static enum mem_sampling_saved_state_e get_mem_sampling_saved_state(void)
{
	if (static_branch_likely(&mm_damon_mem_sampling) &&
		static_branch_likely(&sched_numabalancing_mem_sampling))
		return MEM_SAMPLING_STATE_NUM_DAMON_ENABLE;
	if (static_branch_likely(&mm_damon_mem_sampling))
		return MEM_SAMPLING_STATE_DAMON_ENABLE;
	if (static_branch_likely(&sched_numabalancing_mem_sampling))
		return MEM_SAMPLING_STATE_NUMA_ENABLE;
	if (static_branch_likely(&mem_sampling_access_hints))
		return MEM_SAMPLING_STATE_ENABLE;

	return MEM_SAMPLING_STATE_DISABLE;
}

void set_mem_sampling_state(bool enabled)
{
	if (!mem_sampling_ops.sampling_start || !mm_spe_enabled())
		return;

	if (enabled)
		sysctl_mem_sampling_mode = MEM_SAMPLING_NORMAL;
	else
		sysctl_mem_sampling_mode = MEM_SAMPLING_DISABLED;
	__set_mem_sampling_state(enabled);
}

static int set_state(int state)
{
	if (mem_sampling_saved_state != MEM_SAMPLING_STATE_EMPTY) {
		mem_sampling_saved_state = state;
		return -EINVAL;
	}
	switch (state) {
	case 0:
		set_mem_sampling_state(false);
		break;
	case 1:
		set_mem_sampling_state(false);
		set_mem_sampling_state(true);
		break;
	case 2:
		set_mem_sampling_state(false);
		set_mem_sampling_state(true);
		set_numabalancing_mem_sampling_state(true);
		break;
	case 3:
		set_mem_sampling_state(false);
		set_mem_sampling_state(true);
		set_damon_mem_sampling_state(true);
		break;
	case 4:
		set_mem_sampling_state(true);
		set_numabalancing_mem_sampling_state(true);
		set_damon_mem_sampling_state(true);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

void mem_sampling_user_switch_process(enum user_switch_type type)
{
	bool mm_spe_is_perf_user = false;
	int cpu, hints;

	if (type >= USER_SWITCH_TYPE_MAX) {
		pr_err("user switch type error.\n");
		return;
	}

	for_each_possible_cpu(cpu) {
		if (per_cpu(arm_spe_user, cpu) == SPE_USER_PERF) {
			mm_spe_is_perf_user = true;
			break;
		}
	}

	if (type == USER_SWITCH_AWAY_FROM_MEM_SAMPLING) {
		/* save state only the status when leave mem_sampling for the first time */
		if (mem_sampling_saved_state != MEM_SAMPLING_STATE_EMPTY)
			return;

		hints = get_mem_sampling_saved_state();
		set_state(0);

		if (hints)
			mem_sampling_saved_state = hints;
		else
			mem_sampling_saved_state = MEM_SAMPLING_STATE_DISABLE;

		pr_debug("user switch away from mem_sampling, %d is saved, set to disable.\n",
				mem_sampling_saved_state);

	} else {
		/* If the state is not backed up, do not restore it */
		if (mem_sampling_saved_state == MEM_SAMPLING_STATE_EMPTY || mm_spe_is_perf_user)
			return;

		hints = mem_sampling_saved_state;
		mem_sampling_saved_state = MEM_SAMPLING_STATE_EMPTY;
		set_state(hints);

		pr_debug("user switch back to mem_sampling, set to saved %d.\n", hints);
	}
}
EXPORT_SYMBOL_GPL(mem_sampling_user_switch_process);

#ifdef CONFIG_PROC_SYSCTL
static int proc_mem_sampling_enable(struct ctl_table *table, int write,
			  void *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table t;
	int err;
	int state = 0;

	state = get_mem_sampling_saved_state();

	if (write && !capable(CAP_SYS_ADMIN))
		return -EPERM;

	t = *table;
	t.data = &state;
	t.extra1 = (int *)&mem_sampling_min_value;
	t.extra2 = (int *)&mem_sampling_max_value;
	err = proc_dointvec_minmax(&t, write, buffer, lenp, ppos);
	if (err < 0)
		return err;
	if (write)
		err = set_state(state);
	return err;
}

static struct ctl_table mem_sampling_sysctls[] = {
	{
		.procname       = "mem_sampling_enable",
		.data           = NULL, /* filled in by handler */
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler   = proc_mem_sampling_enable,
		.extra1		= SYSCTL_ZERO,
		.extra2		= (int *)&mem_sampling_max_value,
	},
	{}
};

static void __init mem_sampling_sysctl_init(void)
{
	register_sysctl_init("kernel", mem_sampling_sysctls);
}
#else
#define mem_sampling_sysctl_init() do { } while (0)
#endif

static void __init check_mem_sampling_enable(void)
{
	bool mem_sampling_default = false;

	/* Parsed by setup_mem_sampling. override == 1 enables, -1 disables */
	if (mem_sampling_override)
		set_mem_sampling_state(mem_sampling_override == 1);
	else
		set_mem_sampling_state(mem_sampling_default);
}

static int __init setup_mem_sampling_enable(char *str)
{
	int ret = 0;

	if (!str)
		goto out;

	if (!strcmp(str, "enable")) {
		mem_sampling_override = 1;
		ret = 1;
	}
out:
	if (!ret)
		pr_warn("Unable to parse mem_sampling=\n");

	return ret;
}
__setup("mem_sampling=", setup_mem_sampling_enable);

static int __init mem_sampling_init(void)
{
	enum mem_sampling_type_enum mem_sampling_type = mem_sampling_get_type();
	int cpu;

	switch (mem_sampling_type) {
	case MEM_SAMPLING_ARM_SPE:
		mem_sampling_ops.sampling_start		= mm_spe_start;
		mem_sampling_ops.sampling_stop		= mm_spe_stop;
		mem_sampling_ops.sampling_continue	= mm_spe_continue;
		mem_sampling_ops.sampling_decoding	= mm_spe_decoding;
		mem_sampling_ops.mm_spe_getbuf_addr	= mm_spe_getbuf_addr;
		mem_sampling_ops.mm_spe_getnum_record	= mm_spe_getnum_record;

		break;

	default:
		pr_info("unsupport hardware pmu type(%d), disable access hint!\n",
			mem_sampling_type);
		set_mem_sampling_state(false);
		return -ENODEV;
	}
	check_mem_sampling_enable();
	mem_sampling_sysctl_init();

	for_each_possible_cpu(cpu)
		per_cpu(arm_spe_user, cpu) = SPE_USER_MEM_SAMPLING;

	return 0;
}
late_initcall(mem_sampling_init);
