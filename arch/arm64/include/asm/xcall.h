/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ARM64-specific xcall definitions and inline functions.
 *
 * Inspired by:
 *   1. XPC (ISCA 2019) - Dong Du, Zhichao Hua, Yubin Xia, Binyu Zang, Haibo Chen;
 *   2. Boosting IPC (TOCS 2022) - Yubin Xia, Dong Du, Zhichao Hua, Binyu Zang,
 *      Haibo Chen, Haibing Guan.
 */

#ifndef __ASM_XCALL_H
#define __ASM_XCALL_H

#include <linux/jump_label.h>
#include <linux/mm_types.h>
#include <linux/sched.h>
#include <linux/xcall.h>
#include <linux/refcount.h>

#include <asm/syscall.h>

#define SVC_0000	0xd4000001
#define SVC_FFFF	0xd41fffe1

/*
 * Only can switch by cmdline 'xcall=debug',
 * By default xcall init with XCALL_MODE_TASK.
 */
#define XCALL_MODE_TASK		0
#define XCALL_MODE_SYSTEM	1
extern int sw_xcall_mode;

struct xcall_comm {
	char *name;
	char *binary;
	struct path binary_path;
	char *module;
};

struct xcall {
	/* used for xcall_attach */
	struct list_head	list;
	refcount_t		ref;
	/* file attached xcall */
	struct inode		*binary;
	struct xcall_prog	*program;
	char			*name;
	struct xcall_comm	*info;
};

struct xcall_area {
	/*
	 * 0...NR_syscalls - 1: function pointers to hijack default syscall
	 * NR_syscalls...NR_syscalls * 2 - 1: function pointers in kernel module
	 */
	unsigned long		sys_call_table[NR_syscalls * 2];
	refcount_t		ref;
	struct xcall		*xcall;
	void			*sys_call_data[NR_syscalls];
};

extern const syscall_fn_t *default_sys_call_table(void);
#ifdef CONFIG_DYNAMIC_XCALL
extern void free_xcall_comm(struct xcall_comm *info);
extern void xcall_info_show(struct seq_file *m);
extern int xcall_attach(struct xcall_comm *info);
extern int xcall_detach(struct xcall_comm *info);
extern int xcall_pre_sstep_check(struct pt_regs *regs);
extern int set_xcall_insn(struct mm_struct *mm, unsigned long vaddr,
			  uprobe_opcode_t opcode);

#define mm_xcall_area(mm)	((struct xcall_area *)((mm)->xcall))

static inline long hijack_syscall(struct pt_regs *regs)
{
	struct xcall_area *area = mm_xcall_area(current->mm);
	unsigned int scno = (unsigned int)regs->regs[8];
	syscall_fn_t syscall_fn;

	if (likely(!area))
		return -EINVAL;

	if (unlikely(scno >= __NR_syscalls))
		return -EINVAL;

	syscall_fn = (syscall_fn_t)area->sys_call_table[scno];
	return syscall_fn(regs);
}

static inline const syscall_fn_t *real_syscall_table(void)
{
	struct xcall_area *area = mm_xcall_area(current->mm);

	if (likely(!area))
		return default_sys_call_table();

	return (syscall_fn_t *)(&(area->sys_call_table[__NR_syscalls]));
}
#else
#define mm_xcall_area(mm)	(NULL)
#define hijack_syscall(regs)	(NULL)
static inline const syscall_fn_t *real_syscall_table(void)
{
	return sys_call_table;
}
#endif /* CONFIG_DYNAMIC_XCALL */

DECLARE_STATIC_KEY_FALSE(xcall_enable);

struct xcall_info {
	/* Must be first! */
	u8 xcall_enable[__NR_syscalls + 1];
};

#define TASK_XINFO(p)	((struct xcall_info *)p->xinfo)

int xcall_init_task(struct task_struct *p, struct task_struct *orig);
void xcall_task_free(struct task_struct *p);
void xcall_info_switch(struct task_struct *p);
#endif /* __ASM_XCALL_H */
