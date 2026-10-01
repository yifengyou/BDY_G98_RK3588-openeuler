// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2024 Huawei Technologies Co., Ltd. */

#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <linux/thp_ctl.h>

enum thp_ops {
	THP_OPS_SET,
	THP_OPS_GET
};

static inline struct mm_struct *get_mm_by_pid(pid_t pid)
{
	struct task_struct *task;
	struct mm_struct *mm;
	int ret;

	task = find_get_task_by_vpid(pid);
	if (!task)
		return ERR_PTR(-ESRCH);

	/* Require PTRACE_MODE_READ to avoid leaking ASLR metadata. */
	mm = mm_access(task, PTRACE_MODE_READ_FSCREDS);
	if (IS_ERR_OR_NULL(mm)) {
		ret = -ESRCH;
		goto release_task;
	}

	if (mm != current->mm && !capable(CAP_SYS_NICE)) {
		ret = -EPERM;
		goto release_mm;
	}

	put_task_struct(task);
	return mm;

release_mm:
	mmput(mm);
release_task:
	put_task_struct(task);
	return ERR_PTR(ret);
}

static int mm_get_thp_status(struct mm_struct *mm)
{
	return !test_bit(MMF_DISABLE_THP, &mm->flags);
}

static int mm_set_thp_status(struct mm_struct *mm, bool enable)
{
	if (enable == !test_bit(MMF_DISABLE_THP, &mm->flags))
		return 0;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	if (enable)
		clear_bit(MMF_DISABLE_THP, &mm->flags);
	else
		set_bit(MMF_DISABLE_THP, &mm->flags);

	mmap_write_unlock(mm);

	return 0;
}

/* if ops is THP_OPS_GET, #enable is ignore. */
static int thp_status_op(pid_t pid, enum thp_ops ops, bool enable)
{
	struct mm_struct *mm;
	int ret;

	mm = get_mm_by_pid(pid);
	if (IS_ERR(mm))
		return PTR_ERR(mm);

	if (ops == THP_OPS_SET)
		ret = mm_set_thp_status(mm, enable);
	else
		ret = mm_get_thp_status(mm);
	mmput(mm);

	return ret;
}

static long get_thp_status(struct get_thp_status_arg __user *ubuf)
{
	pid_t pid;
	int ret;

	if (get_user(pid, &ubuf->pid))
		return -EFAULT;

	if (pid < 0)
		return -EINVAL;

	ret = thp_status_op(pid, THP_OPS_GET, false);
	if (ret < 0)
		return ret;

	if (put_user((unsigned long)ret, &ubuf->thp_enable))
		return -EFAULT;

	return 0;
}

static long set_thp_status(pid_t __user *pidp, bool enable)
{
	pid_t pid;

	if (get_user(pid, pidp))
		return -EFAULT;

	if (pid < 0)
		return -EINVAL;

	return (long)thp_status_op(pid, THP_OPS_SET, enable);
}

static long thp_ctl_ioctl(struct file *f, unsigned int ioctl,
			  unsigned long arg)
{
	void __user *argp = (void __user *)arg;

	switch (ioctl) {
	case IOC_THP_STATUS_GET:
		return get_thp_status(argp);
	case IOC_THP_SET_ENABLE:
		return set_thp_status(argp, true);
	case IOC_THP_SET_DISABLE:
		return set_thp_status(argp, false);
	default:
		return -EINVAL;
	}
}

static const struct file_operations thp_ctl_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = thp_ctl_ioctl,
};

static struct miscdevice thp_ctl_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "thp_ctl",
	.fops = &thp_ctl_fops,
	.mode = 0600,
};
module_misc_device(thp_ctl_misc);

MODULE_VERSION("0.0.1");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nanyong Sun");
MODULE_DESCRIPTION("Control process thp policy");
