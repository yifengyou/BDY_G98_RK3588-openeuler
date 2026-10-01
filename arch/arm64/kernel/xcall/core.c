// SPDX-License-Identifier: GPL-2.0-only
/*
 * xcall core implementation: syscall interception and xcall_area management.
 *
 * Copyright (C) 2025 Huawei Limited.
 *
 * Inspired by:
 *   1. XPC (ISCA 2019) - Dong Du, Zhichao Hua, Yubin Xia, Binyu Zang, Haibo Chen;
 *   2. Boosting IPC (TOCS 2022) - Yubin Xia, Dong Du, Zhichao Hua, Binyu Zang,
 *      Haibo Chen, Haibing Guan.
 */

#define pr_fmt(fmt)	"xcall: " fmt

#include <linux/mmap_lock.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/xcall.h>

#include <asm/xcall.h>

static DEFINE_SPINLOCK(xcall_list_lock);
static LIST_HEAD(xcalls_list);
static DEFINE_SPINLOCK(prog_list_lock);
static LIST_HEAD(progs_list);

/*
 * Travel the list of all registered xcall_prog during module installation
 * to find the xcall_prog.
 */
static struct xcall_prog *get_xcall_prog(const char *module)
{
	struct xcall_prog *p;

	list_for_each_entry(p, &progs_list, list) {
		if (!strcmp(module, p->name))
			return p;
	}
	return NULL;
}

static struct xcall_prog *get_xcall_prog_locked(const char *module)
{
	struct xcall_prog *ret;

	spin_lock(&prog_list_lock);
	ret = get_xcall_prog(module);
	spin_unlock(&prog_list_lock);

	return ret;
}

#define inv_xcall_syscall ((unsigned long)__arm64_sys_ni_syscall)

static long patch_syscall(struct pt_regs *regs);

static long filter_ksyscall(struct pt_regs *regs)
{
	struct xcall_area *area = mm_xcall_area(current->mm);
	unsigned int scno = (unsigned int)regs->regs[8];

	cmpxchg(&(area->sys_call_table[scno]), filter_ksyscall, patch_syscall);
	regs->pc -= AARCH64_INSN_SIZE;
	return 0;
}

static long replay_syscall(struct pt_regs *regs)
{
	regs->pc -= AARCH64_INSN_SIZE;
	return 0;
}

static long patch_syscall(struct pt_regs *regs)
{
	struct xcall_area *area = mm_xcall_area(current->mm);
	unsigned int scno = (unsigned int)regs->regs[8];
	syscall_fn_t syscall_fn;
	unsigned long old;
	int ret;

	old = cmpxchg(&(area->sys_call_table[scno]), patch_syscall, replay_syscall);
	if (old != (unsigned long)patch_syscall) {
		syscall_fn = (syscall_fn_t)area->sys_call_table[scno];
		return syscall_fn(regs);
	}

	regs->pc -= AARCH64_INSN_SIZE;

	mmap_write_lock(current->mm);
	ret = set_xcall_insn(current->mm, regs->pc, SVC_FFFF);
	mmap_write_unlock(current->mm);

	if (!ret) {
		xchg(&(area->sys_call_table[scno]), filter_ksyscall);
		return 0;
	}

	regs->pc += AARCH64_INSN_SIZE;
	xchg(&(area->sys_call_table[scno]), patch_syscall);
	pr_info("patch xcall insn failed for scno %u at %s.\n",
		scno, ret > 0 ? "UPROBE_BRK" : "SVC_FFFF");

	return ret;
}

int xcall_pre_sstep_check(struct pt_regs *regs)
{
	struct xcall_area *area = mm_xcall_area(current->mm);
	unsigned int scno = (unsigned int)regs->regs[8];

	return area && (scno < NR_syscalls) &&
		(area->sys_call_table[scno] != inv_xcall_syscall);
}

static struct xcall *get_xcall(struct xcall *xcall)
{
	refcount_inc(&xcall->ref);
	return xcall;
}

static void put_xcall(struct xcall *xcall)
{
	if (!refcount_dec_and_test(&xcall->ref))
		return;

	kfree(xcall->name);
	free_xcall_comm(xcall->info);

	if (xcall->program)
		module_put(xcall->program->owner);

	kfree(xcall);
}

static struct xcall *find_xcall(const char *name, struct inode *binary)
{
	struct xcall *xcall;

	list_for_each_entry(xcall, &xcalls_list, list) {
		if ((name && !strcmp(name, xcall->name)) ||
		    (binary && xcall->binary == binary))
			return get_xcall(xcall);
	}
	return NULL;
}

static struct xcall *insert_xcall_locked(struct xcall *xcall, struct xcall_comm *info)
{
	struct xcall *ret = NULL;

	spin_lock(&xcall_list_lock);
	ret = find_xcall(xcall->name, xcall->binary);
	if (!ret) {
		xcall->info = info;
		list_add(&xcall->list, &xcalls_list);
	} else
		put_xcall(ret);

	spin_unlock(&xcall_list_lock);
	return ret;
}

static void delete_xcall(struct xcall *xcall)
{
	spin_lock(&xcall_list_lock);
	list_del(&xcall->list);
	spin_unlock(&xcall_list_lock);

	put_xcall(xcall);
}

/* Init xcall with a given inode */
static int init_xcall(struct xcall *xcall, struct xcall_comm *comm)
{
	struct xcall_prog *program = get_xcall_prog_locked(comm->module);

	if (!program || !try_module_get(program->owner))
		return -EINVAL;

	xcall->binary = d_real_inode(comm->binary_path.dentry);
	xcall->program = program;
	refcount_set(&xcall->ref, 1);
	INIT_LIST_HEAD(&xcall->list);

	return 0;
}

static int fill_xcall_syscall(struct xcall_area *area, struct xcall *xcall)
{
	unsigned int scno_offset, scno_count = 0;
	struct xcall_prog_object *obj;

	obj = xcall->program->objs;
	while (scno_count < xcall->program->nr_scno && obj->func) {
		scno_offset = NR_syscalls + obj->scno;
		if (area->sys_call_table[scno_offset] != inv_xcall_syscall) {
			pr_err("Process can not mount more than one xcall.\n");
			return -EINVAL;
		}

		area->sys_call_table[scno_offset] = obj->func;
		area->sys_call_table[obj->scno] = (unsigned long)patch_syscall;
		obj += 1;
		scno_count++;
	}

	return 0;
}

static struct xcall_area *create_xcall_area(struct mm_struct *mm)
{
	struct xcall_area *area;
	int i;

	area = kzalloc(sizeof(*area), GFP_KERNEL);
	if (!area)
		return NULL;

	refcount_set(&area->ref, 1);

	for (i = 0; i < NR_syscalls; i++) {
		area->sys_call_table[i] = inv_xcall_syscall;
		area->sys_call_table[i + NR_syscalls] = inv_xcall_syscall;
	}

	/*
	 * Pairs with the READ_ONCE(mm->xcall) in xcall_map() to avoid
	 * the reord of the access on mm->xcall and filling mm->xcall.
	 */
	smp_store_release(&mm->xcall, area);
	return area;
}

/*
 * Initialize the xcall data of mm_struct data.
 * And register xcall into one address space, which includes create
 * the mm_struct associated xcall_area data
 */
int xcall_mmap(struct vm_area_struct *vma, struct mm_struct *mm)
{
	struct xcall_area *area;
	struct xcall *xcall;
	int ret = -EINVAL;

	if (list_empty(&xcalls_list))
		return 0;

	spin_lock(&xcall_list_lock);
	xcall = find_xcall(NULL, file_inode(vma->vm_file));
	spin_unlock(&xcall_list_lock);
	if (!xcall)
		return ret;

	if (!xcall->program)
		goto put_xcall;

	area = mm_xcall_area(mm);
	if (!area && !create_xcall_area(mm)) {
		ret = -ENOMEM;
		goto put_xcall;
	}

	area = (struct xcall_area *)READ_ONCE(mm->xcall);
	// Each process is allowed to be associated with only one xcall.
	if (!cmpxchg(&area->xcall, NULL, xcall) && !fill_xcall_syscall(area, xcall))
		return 0;

put_xcall:
	put_xcall(xcall);
	return ret;
}

void mm_init_xcall_area(struct mm_struct *mm, struct task_struct *p)
{
	struct xcall_area *area = mm_xcall_area(mm);

	if (area)
		refcount_inc(&area->ref);
}

void clear_xcall_area(struct mm_struct *mm)
{
	struct xcall_area *area = mm_xcall_area(mm);

	if (!area)
		return;

	if (!refcount_dec_and_test(&area->ref))
		return;

	if (area->xcall)
		put_xcall(area->xcall);

	kfree(area);
	mm->xcall = NULL;
}

void xcall_info_show(struct seq_file *m)
{
	struct xcall *xcall;

	spin_lock(&xcall_list_lock);
	list_for_each_entry(xcall, &xcalls_list, list) {
		seq_printf(m, "+:%s %s %s\n",
			   xcall->info->name, xcall->info->binary,
			   xcall->info->module);
	}
	spin_unlock(&xcall_list_lock);
}

int xcall_attach(struct xcall_comm *comm)
{
	struct xcall *xcall;
	int ret;

	xcall = kzalloc(sizeof(struct xcall), GFP_KERNEL);
	if (!xcall)
		return -ENOMEM;

	ret = init_xcall(xcall, comm);
	if (ret) {
		kfree(xcall);
		return ret;
	}

	xcall->name = kstrdup(comm->name, GFP_KERNEL);
	if (!xcall->name) {
		delete_xcall(xcall);
		return -ENOMEM;
	}

	if (insert_xcall_locked(xcall, comm)) {
		delete_xcall(xcall);
		return -EINVAL;
	}

	return 0;
}

int xcall_detach(struct xcall_comm *comm)
{
	struct xcall *xcall;

	spin_lock(&xcall_list_lock);
	xcall = find_xcall(comm->name, NULL);
	if (!xcall) {
		spin_unlock(&xcall_list_lock);
		return -EINVAL;
	}

	list_del(&xcall->list);
	spin_unlock(&xcall_list_lock);

	// this put_xcall pairs with list_del() above
	put_xcall(xcall);
	// this put_xcall pairs with find_xcall() above
	put_xcall(xcall);
	return 0;
}

static int check_prog(struct xcall_prog *prog)
{
	int prog_len = strnlen(prog->name, PROG_NAME_LEN);
	struct xcall_prog_object *obj = prog->objs;
	DECLARE_BITMAP(hijack_map, NR_syscalls);

	if (!prog->owner)
		return -EINVAL;

	if (prog_len <= 0 || prog_len >= PROG_NAME_LEN)
		return -EINVAL;

	prog->nr_scno = 0;
	bitmap_zero(hijack_map, NR_syscalls);
	while (prog->nr_scno < MAX_NR_SCNO && obj && obj->func) {
		if (obj->scno >= __NR_syscalls)
			return -EINVAL;

		if (test_and_set_bit(obj->scno, hijack_map))
			return -EINVAL;

		prog->nr_scno++;
		obj++;
	}

	if (!prog->nr_scno)
		return -EINVAL;

	return 0;
}

int xcall_prog_register(struct xcall_prog *prog)
{
	if (!static_key_enabled(&xcall_enable))
		return -EACCES;

	if (check_prog(prog))
		return -EINVAL;

	spin_lock(&prog_list_lock);
	if (get_xcall_prog(prog->name)) {
		spin_unlock(&prog_list_lock);
		return -EBUSY;
	}

	INIT_LIST_HEAD(&prog->list);
	list_add(&prog->list, &progs_list);
	spin_unlock(&prog_list_lock);
	return 0;
}
EXPORT_SYMBOL(xcall_prog_register);

void xcall_prog_unregister(struct xcall_prog *prog)
{
	if (!static_key_enabled(&xcall_enable))
		return;

	if (check_prog(prog))
		return;

	spin_lock(&prog_list_lock);
	if (get_xcall_prog(prog->name))
		list_del(&prog->list);
	spin_unlock(&prog_list_lock);
}
EXPORT_SYMBOL(xcall_prog_unregister);

const syscall_fn_t *default_sys_call_table(void)
{
	return sys_call_table;
}
EXPORT_SYMBOL(default_sys_call_table);
