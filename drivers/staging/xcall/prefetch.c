// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * xcall epollwait prefetch module
 *
 * The data struct and functions marked as MANDATORY have to
 * be includes in all of kernel xcall modules.
 *
 * Copyright (C) 2025 Huawei Limited.
 */

#define pr_fmt(fmt)	"xcall_prefetch: " fmt

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/xcall.h>
#include <net/sock.h>

#include <asm/xcall.h>

#define MAX_FD 1024

#define XCALL_CACHE_PAGE_ORDER 2
#define XCALL_CACHE_BUF_SIZE ((1 << XCALL_CACHE_PAGE_ORDER) * PAGE_SIZE)

#define current_prefetch_mm_data() \
	((struct prefetch_mm_data *) \
	((((struct xcall_area *)(current->mm->xcall))->sys_call_data)[__NR_epoll_pwait]))

static DEFINE_PER_CPU_ALIGNED(unsigned long, xcall_cache_hit);
static DEFINE_PER_CPU_ALIGNED(unsigned long, xcall_cache_miss);

static struct workqueue_struct *rc_work;
static struct cpumask xcall_mask;
struct proc_dir_entry *xcall_proc_dir, *prefetch_dir, *xcall_mask_dir;

static struct list_head prefetch_mm_data_to_delete;
static spinlock_t prefetch_mm_delete_lock;

enum cache_state {
	XCALL_CACHE_NONE = 0,
	XCALL_CACHE_PREFETCH,
	XCALL_CACHE_READY,
	XCALL_CACHE_CANCEL
};

struct prefetch_item {
	struct file *file;
	struct work_struct work;
	int cpu;
	cpumask_t related_cpus;
	struct page *cache_pages;
	char *cache;
	ssize_t len;
	/* cache state in epoll_wait */
	atomic_t state;
	loff_t pos;
};

struct prefetch_mm_data {
	struct prefetch_item items[MAX_FD];
	struct epoll_event events[MAX_FD];
	struct mmu_notifier mmu_notifier;
	struct list_head list;
};

static ssize_t xcall_mask_proc_write(struct file *file, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct cpumask tmp;
	int err;

	err = cpumask_parselist_user(buf, count, &tmp);
	if (err)
		return err;

	if (cpumask_empty(&tmp))
		return -EINVAL;

	if (!cpumask_intersects(&tmp, cpu_online_mask)) {
		pr_warn("cpu %*pbl is not online.\n", cpumask_pr_args(&tmp));
		return -EINVAL;
	}

	cpumask_copy(&xcall_mask, &tmp);
	return count;
}

static int xcall_mask_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%*pbl\n", cpumask_pr_args(&xcall_mask));
	return 0;
}

static int xcall_mask_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, xcall_mask_proc_show, pde_data(inode));
}

static const struct proc_ops xcall_mask_fops = {
	.proc_open	= xcall_mask_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= xcall_mask_proc_write,
};

static ssize_t xcall_prefetch_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *pos)
{
	int cpu;

	for_each_cpu(cpu, cpu_online_mask) {
		*per_cpu_ptr(&xcall_cache_hit, cpu) = 0;
		*per_cpu_ptr(&xcall_cache_miss, cpu) = 0;
	}

	return count;
}

static int xcall_prefetch_show(struct seq_file *m, void *v)
{
	unsigned long hit = 0, miss = 0;
	unsigned int cpu;
	u64 percent;

	for_each_cpu(cpu, cpu_online_mask) {
		hit = *per_cpu_ptr(&xcall_cache_hit, cpu);
		miss = *per_cpu_ptr(&xcall_cache_miss, cpu);

		if (hit == 0 && miss == 0)
			continue;

		percent = DIV_ROUND_CLOSEST(hit * 100ULL, hit + miss);
		seq_printf(m, "cpu%u epoll cache_{hit,miss}: %lu,%lu, hit ratio: %llu%%\n",
			   cpu, hit, miss, percent);
	}
	return 0;
}

static int xcall_prefetch_open(struct inode *inode, struct file *file)
{
	return single_open(file, xcall_prefetch_show, NULL);
}

static const struct proc_ops xcall_prefetch_fops = {
	.proc_open = xcall_prefetch_open,
	.proc_read = seq_read,
	.proc_write = xcall_prefetch_write,
	.proc_lseek = seq_lseek,
	.proc_release = single_release
};

static inline struct prefetch_item *get_pfi(unsigned int fd)
{
	struct prefetch_item *pfis = NULL;

	pfis = (struct prefetch_item *)current_prefetch_mm_data();
	if (fd >= MAX_FD || !pfis)
		return NULL;

	return pfis + fd;
}

static inline bool transition_state(struct prefetch_item *pfi,
				    enum cache_state old, enum cache_state new)
{
	return atomic_cmpxchg(&pfi->state, old, new) == old;
}

static void prefetch_work_fn(struct work_struct *work)
{
	struct prefetch_item *pfi = container_of(work, struct prefetch_item, work);

	if (!transition_state(pfi, XCALL_CACHE_NONE, XCALL_CACHE_PREFETCH))
		return;

	if (!pfi->cache)
		return;

	pfi->pos = 0;
	pfi->len = kernel_read(pfi->file, pfi->cache,
			       XCALL_CACHE_BUF_SIZE, &pfi->file->f_pos);
	transition_state(pfi, XCALL_CACHE_PREFETCH, XCALL_CACHE_READY);
}

static int prefetch_cpus[MAX_NUMNODES] = { [0 ... MAX_NUMNODES - 1] = -1 };

static void set_prefetch_numa_cpu(struct prefetch_item *pfi)
{
	int cur_cpu = smp_processor_id();
	int cur_nid = numa_node_id();
	int old_cpu, new_cpu;
	struct cpumask tmp;

	cpumask_copy(&tmp, &xcall_mask);
	cpumask_and(&pfi->related_cpus, cpu_cpu_mask(cur_cpu), cpu_online_mask);
	if (cpumask_intersects(&tmp, &pfi->related_cpus))
		cpumask_and(&pfi->related_cpus, &pfi->related_cpus, &tmp);

	do {
		old_cpu = prefetch_cpus[cur_nid];
		new_cpu = cpumask_next(old_cpu, &pfi->related_cpus);
		if (new_cpu > cpumask_last(&pfi->related_cpus))
			new_cpu = cpumask_first(&pfi->related_cpus);
	} while (cmpxchg(&prefetch_cpus[cur_nid], old_cpu, new_cpu) != old_cpu);

	pfi->cpu = new_cpu;
}

static int get_async_prefetch_cpu(struct prefetch_item *pfi)
{
	int cpu;

	if (pfi->cpu != smp_processor_id())
		return pfi->cpu;

	cpu = cpumask_next(pfi->cpu, &pfi->related_cpus);
	if (cpu > cpumask_last(&pfi->related_cpus))
		cpu = cpumask_first(&pfi->related_cpus);
	pfi->cpu = cpu;
	return pfi->cpu;
}

static void prefetch_pfi_release(struct mmu_notifier *mn, struct mm_struct *mm)
{
	struct prefetch_mm_data *private_data =
				container_of(mn, struct prefetch_mm_data, mmu_notifier);
	struct xcall_area *area = mm_xcall_area(mm);
	struct prefetch_item *prefetch_items = NULL;
	int i;

	private_data = xchg(&area->sys_call_data[__NR_epoll_pwait], NULL);
	prefetch_items = (struct prefetch_item *)private_data->items;
	for (i = 0; i < MAX_FD; i++) {
		cancel_work_sync(&prefetch_items[i].work);
		if (prefetch_items[i].file)
			fput(prefetch_items[i].file);
		if (prefetch_items[i].cache_pages)
			__free_pages(prefetch_items[i].cache_pages, XCALL_CACHE_PAGE_ORDER);
		prefetch_items[i].cache = NULL;
	}
	spin_lock(&prefetch_mm_delete_lock);
	list_add_tail(&private_data->list, &prefetch_mm_data_to_delete);
	spin_unlock(&prefetch_mm_delete_lock);
}

static void xcall_mmu_notifier_free(struct mmu_notifier *mn)
{
	kfree(container_of(mn, struct prefetch_mm_data, mmu_notifier));
}

static void xcall_prefetch_mm_free(void)
{
	struct prefetch_mm_data *private_data, *tmp;

	spin_lock(&prefetch_mm_delete_lock);
	list_for_each_entry_safe(private_data, tmp, &prefetch_mm_data_to_delete, list) {
		list_del(&private_data->list);
		mmu_notifier_put(&private_data->mmu_notifier);
	}
	spin_unlock(&prefetch_mm_delete_lock);
}

static struct mmu_notifier_ops xcall_mmu_notifier_ops = {
	.release = prefetch_pfi_release,
	.free_notifier = xcall_mmu_notifier_free,
};

static void xcall_cancel_work(unsigned int fd)
{
	struct prefetch_item *pfi = NULL;

	pfi = get_pfi(fd);
	if (pfi && pfi->file) {
		cancel_work_sync(&pfi->work);
		fput(pfi->file);
		xchg(&pfi->file, NULL);
	}
}

#define MAX_READY_WAIT_TIME  msecs_to_jiffies(2)
static int xcall_read(struct prefetch_item *pfi, char __user *buf, size_t count)
{
	unsigned long end = jiffies + MAX_READY_WAIT_TIME;
	ssize_t copy_len = 0;

	/*
	 * Everytime it does the memcpy on prefetch buffer, it has to keep
	 * the state of pfi is "CANCEL" to avoid the race on the prefetch
	 * buffer from both the prefetch thread calling kernel_read() and
	 * other threads calling copy_to_user(), also avoid race on the
	 * prefetch file from both the prefetch thread calling kernel_read()
	 * and other threads calling vfs_read().
	 */
	while (!transition_state(pfi, XCALL_CACHE_READY, XCALL_CACHE_CANCEL)) {
		/*
		 * Once the prefetch thread read return error code or prefetch
		 * has not start, no need to waste CPU on waiting right here,
		 * it should do a slow vfs_read() to ensure no new arrival data.
		 */
		if (transition_state(pfi, XCALL_CACHE_NONE, XCALL_CACHE_CANCEL))
			goto slow_read;

		if (time_after(jiffies, end)) {
			pr_warn("xcall read wait prefetch state %d more than 2ms\n",
				atomic_read(&pfi->state));
			cond_resched();
			end = jiffies + MAX_READY_WAIT_TIME;
		}
	}

	copy_len = pfi->len;
	if (unlikely(copy_len < 0))
		goto slow_read;

	if (copy_len == 0) {
		this_cpu_inc(xcall_cache_hit);
		transition_state(pfi, XCALL_CACHE_CANCEL, XCALL_CACHE_NONE);
		return 0;
	}

	copy_len = (copy_len >= count) ? count : copy_len;
	copy_len -= copy_to_user(buf, (void *)(pfi->cache + pfi->pos), copy_len);
	pfi->len -= copy_len;
	pfi->pos += copy_len;
	if (pfi->len == 0)
		transition_state(pfi, XCALL_CACHE_CANCEL, XCALL_CACHE_NONE);
	else
		transition_state(pfi, XCALL_CACHE_CANCEL, XCALL_CACHE_READY);

	this_cpu_inc(xcall_cache_hit);
	return copy_len;

slow_read:
	this_cpu_inc(xcall_cache_miss);
	pfi->len = 0;
	pfi->pos = 0;
	cancel_work(&pfi->work);

	return -EAGAIN;
}

static inline int xcall_read_begin(unsigned int fd, char __user *buf, size_t count)
{
	struct prefetch_item *pfi = NULL;

	pfi = get_pfi(fd);
	if (!pfi || !pfi->file)
		return -EAGAIN;

	return xcall_read(pfi, buf, count);
}

static inline void xcall_read_end(unsigned int fd)
{
	struct prefetch_item *pfi = NULL;

	pfi = get_pfi(fd);
	if (!pfi || !pfi->file)
		return;

	transition_state(pfi, XCALL_CACHE_CANCEL, XCALL_CACHE_NONE);
}

static long __do_sys_epoll_create(struct pt_regs *regs)
{
	long ret;
	int i;
	struct xcall_area *area = mm_xcall_area(current->mm);
	struct prefetch_mm_data *private_data = NULL;
	struct prefetch_item *items = NULL;

	ret = default_sys_call_table()[__NR_epoll_create1](regs);
	if (ret < 0)
		return ret;
	if (current_prefetch_mm_data())
		return ret;

	private_data = kmalloc(sizeof(struct prefetch_mm_data), GFP_KERNEL);
	if (!private_data)
		return ret;
	if (cmpxchg(&area->sys_call_data[__NR_epoll_pwait], NULL, private_data)) {
		kfree(private_data);
		return ret;
	}

	items = private_data->items;
	for (i = 0; i < MAX_FD; i++) {
		items[i].cache_pages = alloc_pages(GFP_KERNEL_ACCOUNT | __GFP_ZERO,
						   XCALL_CACHE_PAGE_ORDER);
		if (!items[i].cache_pages) {
			items[i].cache = NULL;
			continue;
		}

		INIT_WORK(&items[i].work, prefetch_work_fn);
		atomic_set(&items[i].state, XCALL_CACHE_NONE);
		items[i].cache = page_address(items[i].cache_pages);
		items[i].len = 0;
		items[i].pos = 0;
		items[i].file = NULL;
		set_prefetch_numa_cpu(&items[i]);
	}

	memset(private_data->events, 0, sizeof(private_data->events));
	INIT_LIST_HEAD(&private_data->list);
	private_data->mmu_notifier.ops = &xcall_mmu_notifier_ops;
	mmu_notifier_register(&private_data->mmu_notifier, current->mm);
	xcall_prefetch_mm_free();
	return ret;
}

static long __do_sys_epoll_ctl(struct pt_regs *regs)
{
	struct prefetch_item *pfi = NULL;
	unsigned int fd = regs->regs[2];
	int op = regs->regs[1];
	struct file *file;
	long ret;

	ret = default_sys_call_table()[__NR_epoll_ctl](regs);
	if (ret)
		return ret;

	pfi = get_pfi(fd);
	if (!pfi)
		return ret;

	switch (op) {
	case EPOLL_CTL_ADD:
		file = fget(fd);
		if (!file)
			return ret;
		if (!sock_from_file(file)) {
			fput(file);
			return ret;
		}
		if (cmpxchg(&pfi->file, NULL, file))
			fput(file);
		break;
	case EPOLL_CTL_DEL:
		xcall_cancel_work(fd);
		break;
	}

	return ret;
}

static long __do_sys_epoll_pwait(struct pt_regs *regs)
{
	void __user *buf = (void *)regs->regs[1];
	struct prefetch_item *pfi = NULL;
	struct prefetch_mm_data *private_data = NULL;
	struct epoll_event *events = NULL;
	int i, fd, cpu, prefetch_task_num;
	long ret;

	ret = default_sys_call_table()[__NR_epoll_pwait](regs);
	if (ret <= 0)
		return ret;

	if (!current_prefetch_mm_data())
		return ret;

	private_data = current_prefetch_mm_data();
	events = private_data->events;

	prefetch_task_num = ret > MAX_FD ? MAX_FD : ret;
	if (copy_from_user(events, buf, prefetch_task_num * sizeof(struct epoll_event)))
		return ret;

	for (i = 0; i < prefetch_task_num; i++) {
		fd = events[i].data;
		if (!(events[i].events & EPOLLIN) || fd >= MAX_FD)
			continue;

		pfi = get_pfi(fd);
		if (!pfi || !(pfi->file) || !(pfi->file->f_mode & FMODE_READ))
			continue;
		if (atomic_read(&pfi->state) != XCALL_CACHE_NONE)
			continue;

		cpu = get_async_prefetch_cpu(pfi);
		queue_work_on(cpu, rc_work, &pfi->work);
	}
	return ret;
}

static long __do_sys_close(struct pt_regs *regs)
{
	unsigned int fd = regs->regs[0];
	struct prefetch_item *pfi = NULL;
	struct file *pfi_old_file = NULL;
	struct file *pfi_new_file = NULL;

	pfi = get_pfi(fd);
	if (!pfi)
		return default_sys_call_table()[__NR_close](regs);

	if (pfi && pfi->file) {
		pfi_old_file = pfi->file;
		pfi_new_file = cmpxchg(&pfi->file, pfi_old_file, NULL);
		if (pfi_new_file == pfi_old_file) {
			fput(pfi_old_file);
			atomic_set(&pfi->state, XCALL_CACHE_NONE);
			pfi->len = 0;
			pfi->pos = 0;
		}
	}
	return default_sys_call_table()[__NR_close](regs);
}

static long __do_sys_read(struct pt_regs *regs)
{
	unsigned int fd = regs->regs[0];
	void __user *buf = (void *)regs->regs[1];
	size_t count = regs->regs[2];
	size_t ret = -EBADF;

	ret = xcall_read_begin(fd, buf, count);
	if (ret != -EAGAIN)
		return ret;

	ret = default_sys_call_table()[__NR_read](regs);

	xcall_read_end(fd);
	return ret;
}

/* MANDATORY */
struct xcall_prog xcall_prefetch_prog = {
	.name		= "prefetch",
	.owner		= THIS_MODULE,
	.objs		= {
		{
			.scno = (unsigned long)__NR_epoll_create1,
			.func = (unsigned long)__do_sys_epoll_create,
		},
		{
			.scno = (unsigned long)__NR_epoll_ctl,
			.func = (unsigned long)__do_sys_epoll_ctl,
		},
		{
			.scno = (unsigned long)__NR_epoll_pwait,
			.func = (unsigned long)__do_sys_epoll_pwait,
		},
		{
			.scno = (unsigned long)__NR_close,
			.func = (unsigned long)__do_sys_close,
		},
		{
			.scno = (unsigned long)__NR_read,
			.func = (unsigned long)__do_sys_read,
		},
		{}
	}
};

static int __init init_xcall_prefetch_procfs(void)
{
	xcall_proc_dir = xcall_subdir_create("prefetch");
	if (!xcall_proc_dir)
		return -ENOMEM;
	prefetch_dir = proc_create("prefetch", 0640, xcall_proc_dir,
				   &xcall_prefetch_fops);
	if (!prefetch_dir)
		goto rm_xcall_proc_dir;
	xcall_mask_dir = proc_create("cpu_list", 0640, xcall_proc_dir,
				     &xcall_mask_fops);
	if (!xcall_mask_dir)
		goto rm_prefetch_dir;

	cpumask_copy(&xcall_mask, cpu_online_mask);
	return 0;

rm_prefetch_dir:
	proc_remove(prefetch_dir);
rm_xcall_proc_dir:
	proc_remove(xcall_proc_dir);
	return -ENOMEM;
}

/* MANDATORY */
static int __init xcall_prefetch_init(void)
{
	int ret;

	rc_work = alloc_workqueue("eventpoll_rc", 0, 0);
	if (!rc_work) {
		pr_warn("alloc eventpoll_rc workqueue failed.\n");
		return -ENOMEM;
	}

	ret = init_xcall_prefetch_procfs();
	if (ret)
		goto destroy_queue;

	ret = xcall_prog_register(&xcall_prefetch_prog);
	if (ret)
		goto remove_dir;

	INIT_LIST_HEAD(&prefetch_mm_data_to_delete);
	spin_lock_init(&prefetch_mm_delete_lock);
	return ret;

remove_dir:
	proc_remove(prefetch_dir);
	proc_remove(xcall_mask_dir);
	proc_remove(xcall_proc_dir);
destroy_queue:
	destroy_workqueue(rc_work);
	return ret;
}

/* MANDATORY */
static void __exit xcall_prefetch_exit(void)
{
	if (rc_work)
		destroy_workqueue(rc_work);
	if (prefetch_dir)
		proc_remove(prefetch_dir);
	if (xcall_mask_dir)
		proc_remove(xcall_mask_dir);
	if (xcall_proc_dir)
		proc_remove(xcall_proc_dir);

	xcall_prog_unregister(&xcall_prefetch_prog);
	xcall_prefetch_mm_free();
	mmu_notifier_synchronize();
}

module_init(xcall_prefetch_init);
module_exit(xcall_prefetch_exit);
MODULE_LICENSE("GPL");
