// SPDX-License-Identifier: GPL-2.0-only
/*
 * xcall task lifecycle management and context switch implementation.
 *
 * Copyright (C) 2025 Huawei Ltd.
 *
 * Inspired by:
 *   1. XPC (ISCA 2019) - Dong Du, Zhichao Hua, Yubin Xia, Binyu Zang, Haibo Chen;
 *   2. Boosting IPC (TOCS 2022) - Yubin Xia, Dong Du, Zhichao Hua, Binyu Zang,
 *      Haibo Chen, Haibing Guan.
 */

#include <linux/bitmap.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <asm/xcall.h>

// Only can switch by cmdline 'xcall=debug'
int sw_xcall_mode = XCALL_MODE_TASK;

static inline int sw_xcall_init_task(struct task_struct *p, struct task_struct *orig)
{
	p->xinfo = kzalloc(sizeof(struct xcall_info), GFP_KERNEL);
	if (!p->xinfo)
		return -ENOMEM;

	if (!orig->xinfo)
		return 0;

	/* In xcall debug mode, all syscalls are enabled by default! */
	if (sw_xcall_mode == XCALL_MODE_SYSTEM)
		memset(TASK_XINFO(p)->xcall_enable, 1, (__NR_syscalls + 1) * sizeof(u8));
	else
		memcpy(TASK_XINFO(p)->xcall_enable,
		       TASK_XINFO(orig)->xcall_enable,
		       (__NR_syscalls + 1) * sizeof(u8));

	return 0;
}

int xcall_init_task(struct task_struct *p, struct task_struct *orig)
{
	if (static_branch_unlikely(&xcall_enable))
		return sw_xcall_init_task(p, orig);

	return 0;
}

void xcall_task_free(struct task_struct *p)
{
	if (static_branch_unlikely(&xcall_enable))
		kfree(p->xinfo);
}

static u8 default_xcall_info[__NR_syscalls + 1] = {
	[0 ... __NR_syscalls] = 0,
};
DEFINE_PER_CPU(u8*, __xcall_info) = default_xcall_info;

void xcall_info_switch(struct task_struct *task)
{
	if (TASK_XINFO(task)->xcall_enable)
		__this_cpu_write(__xcall_info, TASK_XINFO(task)->xcall_enable);
	else
		__this_cpu_write(__xcall_info, default_xcall_info);
}
