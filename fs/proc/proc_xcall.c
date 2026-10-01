// SPDX-License-Identifier: GPL-2.0
/*
 * xcall related proc code
 *
 * Copyright (C) 2025 Huawei Ltd.
 */
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <asm/xcall.h>
#include "internal.h"

static int xcall_show(struct seq_file *m, void *v)
{
	struct inode *inode = m->private;
	int start = -1, first = 1;
	struct xcall_info *xinfo;
	struct task_struct *p;
	int scno = 0;

	if (!static_key_enabled(&xcall_enable))
		return -EACCES;

	p = get_proc_task(inode);
	if (!p)
		return -ESRCH;

	xinfo = TASK_XINFO(p);
	if (!xinfo)
		goto out;

	for (scno = 0; scno <= __NR_syscalls; scno++) {
		if (scno == __NR_syscalls || !xinfo->xcall_enable[scno]) {
			if (start == -1)
				continue;

			if (!first)
				seq_puts(m, ",");

			if (start == scno - 1)
				seq_printf(m, "%d", start);
			else
				seq_printf(m, "%d-%d", start, scno - 1);

			first = 0;
			start = -1;
		} else {
			if (start == -1)
				start = scno;
		}
	}

	seq_puts(m, "\n");
out:
	put_task_struct(p);

	return 0;
}

static int xcall_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, xcall_show, inode);
}

#define MAX_SYSNO_DIGITS	3
#define MAX_BUF_SIZE		(1 + MAX_SYSNO_DIGITS + 1) // "!" + digits + '\0'
static ssize_t xcall_write(struct file *file, const char __user *ubuf,
				      size_t count, loff_t *offset)
{
	unsigned int sc_no = __NR_syscalls;
	char buf[MAX_BUF_SIZE];
	struct task_struct *p;
	int is_clear = 0;
	int ret = 0;

	if (!static_key_enabled(&xcall_enable))
		return -EACCES;

	p = get_proc_task(file_inode(file));
	if (!p || !TASK_XINFO(p))
		return -ESRCH;

	memset(buf, '\0', MAX_BUF_SIZE);
	if (!count || (count > MAX_BUF_SIZE)) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_from_user(buf, ubuf, count > MAX_BUF_SIZE - 1 ? MAX_BUF_SIZE - 1 : count)) {
		ret = -EFAULT;
		goto out;
	}

	is_clear = (buf[0] == '!');
	if (kstrtouint((buf + is_clear), 10, &sc_no)) {
		ret = -EINVAL;
		goto out;
	}

	if (sc_no >= __NR_syscalls) {
		ret = -EINVAL;
		goto out;
	}

	if (!is_clear && !(TASK_XINFO(p))->xcall_enable[sc_no])
		(TASK_XINFO(p))->xcall_enable[sc_no] = 1;
	else if (is_clear && (TASK_XINFO(p))->xcall_enable[sc_no])
		(TASK_XINFO(p))->xcall_enable[sc_no] = 0;
	else
		ret = -EINVAL;
out:
	put_task_struct(p);

	return ret ? ret : count;
}

const struct file_operations proc_pid_xcall_operations = {
	.open		= xcall_open,
	.read		= seq_read,
	.write		= xcall_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};
