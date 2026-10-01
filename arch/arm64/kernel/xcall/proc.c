// SPDX-License-Identifier: GPL-2.0-only
/*
 * xcall proc interface for attach/detach operations.
 *
 * Copyright (C) 2025 Huawei Limited.
 *
 * Inspired by:
 *   1. XPC (ISCA 2019) - Dong Du, Zhichao Hua, Yubin Xia, Binyu Zang, Haibo Chen;
 *   2. Boosting IPC (TOCS 2022) - Yubin Xia, Dong Du, Zhichao Hua, Binyu Zang,
 *      Haibo Chen, Haibing Guan.
 */

#include <linux/namei.h>
#include <linux/path.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/xcall.h>

#include <asm/xcall.h>

static struct proc_dir_entry *root_xcall_dir;

void free_xcall_comm(struct xcall_comm *info)
{
	if (!info)
		return;
	kfree(info->name);
	kfree(info->binary);
	kfree(info->module);
	path_put(&info->binary_path);
	kfree(info);
}

static int is_absolute_path(const char *path)
{
	return path[0] == '/';
}

static int parse_xcall_command(int argc, char **argv,
			       struct xcall_comm *info)
{
	struct dentry *dentry;

	if (strlen(argv[0]) < 3)
		return -ECANCELED;

	if (argv[0][0] != '+' && argv[0][0] != '-')
		return -ECANCELED;

	if (argv[0][1] != ':')
		return -ECANCELED;

	if (argv[0][0] == '+' && argc != 3)
		return -ECANCELED;

	if (argv[0][0] == '-' && argc != 1)
		return -ECANCELED;

	info->name = kstrdup(&argv[0][2], GFP_KERNEL);
	if (!info->name)
		return -ENOMEM;

	if (argv[0][0] == '-')
		return '-';

	info->binary = kstrdup(argv[1], GFP_KERNEL);
	if (!info->binary)
		goto free_name;

	if (!is_absolute_path(info->binary))
		goto free_binary;

	if (kern_path(info->binary, LOOKUP_FOLLOW, &info->binary_path))
		goto free_binary;

	dentry = info->binary_path.dentry;
	if (!dentry || !S_ISREG(d_inode(dentry)->i_mode) ||
	    !(d_inode(dentry)->i_mode & 0111))
		goto put_path;

	info->module = kstrdup(argv[2], GFP_KERNEL);
	if (!info->module)
		goto put_path;

	return argv[0][0];

put_path:
	path_put(&info->binary_path);
free_binary:
	kfree(info->binary);
free_name:
	kfree(info->name);
	return -EINVAL;
}

/*
 * /proc/xcall/comm
 * Argument syntax:
 *   +:COMM ELF_FILE [KERNEL_MODULE] : Attach a xcall
 *   -:COMM							 : Detach a xcall
 *
 *   COMM:		: Unique string for attached xcall.
 *   ELF_FILE		: Path to an executable or library.
 *   KERNEL_MODULE	: Module name listed in /proc/modules provide xcall program.
 */
int proc_xcall_command(int argc, char **argv)
{
	struct xcall_comm *info;
	int ret, op;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	op = parse_xcall_command(argc, argv, info);
	switch (op) {
	case '+':
		ret = xcall_attach(info);
		if (ret)
			free_xcall_comm(info);
		break;
	case '-':
		ret = xcall_detach(info);
		free_xcall_comm(info);
		break;
	default:
		kfree(info);
		return -EINVAL;
	}

	return ret;
}

static int xcall_comm_show(struct seq_file *m, void *v)
{
	xcall_info_show(m);

	return 0;
}

static int xcall_comm_open(struct inode *inode, struct file *file)
{
	return single_open(file, xcall_comm_show, NULL);
}

static ssize_t xcall_comm_write(struct file *file,
				const char __user *user_buf,
				size_t nbytes, loff_t *ppos)
{
	int argc = 0, ret = 0;
	char *raw_comm;
	char **argv;

	if (nbytes <= 1)
		return -EINVAL;

	raw_comm = memdup_user_nul(user_buf, nbytes - 1);
	if (IS_ERR(raw_comm))
		return PTR_ERR(raw_comm);

	argv = argv_split(GFP_KERNEL, raw_comm, &argc);
	if (!argv) {
		kfree(raw_comm);
		return -ENOMEM;
	}

	ret = proc_xcall_command(argc, argv);

	argv_free(argv);

	kfree(raw_comm);

	return ret ? ret : nbytes;
}

struct proc_dir_entry *xcall_subdir_create(const char *name)
{
	return proc_mkdir(name, root_xcall_dir);
}
EXPORT_SYMBOL(xcall_subdir_create);

static const struct proc_ops xcall_comm_ops = {
	.proc_open	= xcall_comm_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_write	= xcall_comm_write,
	.proc_release	= single_release,
};

static int __init xcall_proc_init(void)
{
	if (!static_key_enabled(&xcall_enable))
		return 0;

	root_xcall_dir = proc_mkdir("xcall", NULL);
	if (!root_xcall_dir)
		return 0;

	proc_create("comm", 0640, root_xcall_dir, &xcall_comm_ops);
	return 0;
}
module_init(xcall_proc_init);
