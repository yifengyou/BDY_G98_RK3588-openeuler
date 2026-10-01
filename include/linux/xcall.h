/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * xcall data structures and function declarations.
 *
 * Copyright (C) 2025 Huawei.
 *
 * Inspired by:
 *   1. XPC (ISCA 2019) - Dong Du, Zhichao Hua, Yubin Xia, Binyu Zang, Haibo Chen;
 *   2. Boosting IPC (TOCS 2022) - Yubin Xia, Dong Du, Zhichao Hua, Binyu Zang,
 *      Haibo Chen, Haibing Guan.
 */

#ifndef _LINUX_XCALL_H
#define _LINUX_XCALL_H

#include <linux/module.h>

struct vm_area_struct;
struct mm_struct;
struct inode;

struct xcall_prog_object {
	unsigned long scno;
	unsigned long func;
};

#define PROG_NAME_LEN	64
#define MAX_NR_SCNO	32

struct xcall_prog {
	char name[PROG_NAME_LEN];
	struct module *owner;
	struct list_head list;
	struct xcall_prog_object objs[MAX_NR_SCNO];
	unsigned int nr_scno;
};

#ifdef CONFIG_DYNAMIC_XCALL
extern int xcall_prog_register(struct xcall_prog *prog);
extern void xcall_prog_unregister(struct xcall_prog *prog);
extern void mm_init_xcall_area(struct mm_struct *mm, struct task_struct *p);
extern struct proc_dir_entry *xcall_subdir_create(const char *name);
extern void clear_xcall_area(struct mm_struct *mm);
extern int xcall_mmap(struct vm_area_struct *vma, struct mm_struct *mm);
#else /* !CONFIG_DYNAMIC_XCALL */
static inline int xcall_prog_register(struct xcall_prog *prog)
{
	return -EINVAL;
}
static inline void xcall_prog_unregister(struct xcall_prog *prog) {}
static inline void mm_init_xcall_area(struct mm_struct *mm, struct task_struct *p) {}
static inline void clear_xcall_area(struct mm_struct *mm) {}
static inline int xcall_mmap(struct vm_area_struct *vma, struct mm_struct *mm)
{
	return 0;
}
#endif /* CONFIG_DYNAMIC_XCALL */

#endif /* _LINUX_XCALL_H */
