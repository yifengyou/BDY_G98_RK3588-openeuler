// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A simple dummy xcall for syscall testing
 *
 * The data struct and functions marked as MANDATORY have to
 * be includes in all of kernel xcall modules.
 *
 * Copyright (C) 2025 Huawei Limited.
 */

#define pr_fmt(fmt)	"dummy_xcall: " fmt

#include <linux/module.h>
#include <linux/xcall.h>
#include <linux/fs.h>

#include <asm/xcall.h>

static long __do_sys_close(struct pt_regs *regs)
{
	return default_sys_call_table()[__NR_close](regs);
}

static long __do_sys_getpid(struct pt_regs *regs)
{
	return default_sys_call_table()[__NR_getpid](regs);
}

static long __do_sys_getuid(struct pt_regs *regs)
{
	return default_sys_call_table()[__NR_getuid](regs);
}

static long __do_sys_unmask(struct pt_regs *regs)
{
	return default_sys_call_table()[__NR_umask](regs);
}

static long __do_sys_dup(struct pt_regs *regs)
{
	return default_sys_call_table()[__NR_dup](regs);
}

/* MANDATORY */
static struct xcall_prog dummy_xcall_prog = {
	.name		= "dummy_xcall",
	.owner		= THIS_MODULE,
	.objs		= {
		{
			.scno = (unsigned long)__NR_getpid,
			.func = (unsigned long)__do_sys_getpid,
		},
		{
			.scno = (unsigned long)__NR_getuid,
			.func = (unsigned long)__do_sys_getuid,
		},
		{
			.scno = (unsigned long)__NR_close,
			.func = (unsigned long)__do_sys_close,
		},
		{
			.scno = (unsigned long)__NR_umask,
			.func = (unsigned long)__do_sys_unmask,
		},
		{
			.scno = (unsigned long)__NR_dup,
			.func = (unsigned long)__do_sys_dup,
		},
		{}
	}
};

/* MANDATORY */
static int __init dummy_xcall_init(void)
{
	return xcall_prog_register(&dummy_xcall_prog);
}

/* MANDATORY */
static void __exit dummy_xcall_exit(void)
{
	xcall_prog_unregister(&dummy_xcall_prog);
}

module_init(dummy_xcall_init);
module_exit(dummy_xcall_exit);
MODULE_AUTHOR("Liao Chang <liaochang1@huawei.com>");
MODULE_DESCRIPTION("Dummy Xcall");
MODULE_LICENSE("GPL");
