// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025. Huawei Technologies Co., Ltd. All rights reserved.
 */
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/slab.h>

#include <linux/printk.h>
#include <linux/module.h>

#include <linux/kthread.h>
#include <linux/cpu.h>
#include <linux/ktime.h>

#include "ib_core_cq_poll.h"
#include "ib_core_cq.h"

#define INVALID_IB_POLL_CORE -1

static DEFINE_SPINLOCK(polling_kthread_spinlock);
static unsigned int poll_core;

static struct task_struct *poll_cq_thread;
static struct task_struct *waker_polling_thread;
static struct polling_kthread ib_cq_polling_kthread;

static LIST_HEAD(ib_cq_poll_list);
static DEFINE_SPINLOCK(cq_list_lock);

static void set_kthread_polling_ctx(struct polling_kthread *polling_ctx)
{
	struct polling_kthread *ctx = get_kthread_polling_ctx();

	memcpy(ctx, polling_ctx, sizeof(struct polling_kthread));
}

void add_cq_to_poll_list(void *cq)
{
	unsigned long flags;
	struct cq_poll_node *cq_node;

	cq_node = kmalloc(sizeof(struct cq_poll_node), GFP_ATOMIC);
	if (!cq_node)
		return;

	cq_node->cq = cq;
	cq_node->time_used_ns = 0;
	cq_node->poll_cq_cnt = 0;
	cq_node->max_time_ns = 0;

	spin_lock_irqsave(&cq_list_lock, flags);
	list_add_tail(&cq_node->list, &ib_cq_poll_list);
	spin_unlock_irqrestore(&cq_list_lock, flags);
}

void del_cq_from_poll_list(void *del_cq)
{
	struct cq_poll_node *poll_node_entry, *poll_node_next;
	void *curr_cq;
	void *cq = del_cq;
	unsigned long flags;

	spin_lock_irqsave(&cq_list_lock, flags);
	list_for_each_entry_safe(poll_node_entry, poll_node_next,
				&ib_cq_poll_list, list) {
		curr_cq = poll_node_entry->cq;
		if (curr_cq == cq) {
			list_del(&poll_node_entry->list);
			kfree(poll_node_entry);
			break;
		}
	}
	spin_unlock_irqrestore(&cq_list_lock, flags);
}

void clear_cq_poll_list(void)
{
	unsigned long flags;
	struct cq_poll_node *entry, *next;

	spin_lock_irqsave(&cq_list_lock, flags);
	list_for_each_entry_safe(entry, next, &ib_cq_poll_list, list) {
		list_del(&entry->list);
		kfree(entry);
	}
	spin_unlock_irqrestore(&cq_list_lock, flags);

	INIT_LIST_HEAD(&ib_cq_poll_list);
}

void cq_polling(void *data)
{
	void *cq;
	int completed = 0;
	unsigned long flags;
	u64 time_interval;
	ktime_t start_time_stamp, end_time_stamp;
	struct cq_poll_node *poll_node_entry, *poll_node_next;

	spin_lock_irqsave(&cq_list_lock, flags);
	list_for_each_entry_safe(poll_node_entry, poll_node_next,
					&ib_cq_poll_list, list) {
		cq = poll_node_entry->cq;
		if (!cq) {
			WARN_ONCE(1, "got NULL CQ 0x%p in poll list\n", cq);
			continue;
		}
		start_time_stamp = ktime_get();
		completed = ib_poll_cq_thread(cq);
		end_time_stamp = ktime_get();
		if (ib_cq_polling_kthread.debug_cq_poll_stat && completed) {
			time_interval = ktime_to_ns(ktime_sub(end_time_stamp, start_time_stamp));
			poll_node_entry->time_used_ns += time_interval;
			poll_node_entry->poll_cq_cnt++;
			if (poll_node_entry->max_time_ns < time_interval)
				poll_node_entry->max_time_ns = time_interval;
		}
	}
	spin_unlock_irqrestore(&cq_list_lock, flags);
}

void wakeup_and_poll(struct task_struct *awakened_thread)
{
	wake_up_process(awakened_thread);

	cq_polling(NULL);
}

int polling_thread(void *data)
{
	while (true) {
		if (ib_cq_polling_kthread.use_polling_kthread)
			wakeup_and_poll(waker_polling_thread);

		set_current_state(TASK_INTERRUPTIBLE);
		if (unlikely(kthread_should_stop())) {
			set_current_state(TASK_RUNNING);
			break;
		}
		schedule();
	}

	return 0;
}

int polling_awaken_thread(void *data)
{
	while (true) {
		if (ib_cq_polling_kthread.use_polling_kthread)
			wakeup_and_poll(poll_cq_thread);

		set_current_state(TASK_INTERRUPTIBLE);
		if (unlikely(kthread_should_stop())) {
			set_current_state(TASK_RUNNING);
			break;
		}
		schedule();
	}

	return 0;
}

static ssize_t ib_core_poll_cpu_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	int ret;

	if (ib_cq_polling_kthread.use_polling_kthread)
		ret = sprintf(buf, "Current polling core is: %u\n", poll_core);
	else
		ret = sprintf(buf, "Current polling thread is down\n");

	return ret;
}

static ssize_t ib_core_poll_cpu_store(struct kobject *kobj,
					struct kobj_attribute *attr, const char *buf,
					size_t count)
{
	int input_poll_core;
	unsigned int max_nr_cores = num_possible_cpus();

	if (kstrtoint(buf, 10, &input_poll_core) < 0) {
		pr_err("Invalid input, input format: <bind_core> e.g. 8\n");
		return -EINVAL;
	}

	spin_lock(&polling_kthread_spinlock);
	if (input_poll_core == INVALID_IB_POLL_CORE) {
		ib_cq_polling_kthread.use_polling_kthread = 0;
		set_kthread_polling_ctx(&ib_cq_polling_kthread);
		clear_cq_poll_list();
		goto out;
	}
	if (input_poll_core < 0 || input_poll_core >= max_nr_cores) {
		pr_err("Invalid CPU core ID. Valid range is 0 to %u.\n", max_nr_cores - 1);
		goto err_inval;
	}
	ib_cq_polling_kthread.use_polling_kthread = 1;
	set_kthread_polling_ctx(&ib_cq_polling_kthread);
	poll_core = (unsigned int)input_poll_core;
	set_cpus_allowed_ptr(poll_cq_thread, cpumask_of(poll_core));
	set_cpus_allowed_ptr(waker_polling_thread, cpumask_of(poll_core));
	wake_up_process(waker_polling_thread);

out:
	spin_unlock(&polling_kthread_spinlock);
	return count;

err_inval:
	spin_unlock(&polling_kthread_spinlock);
	return -EINVAL;
}

static ssize_t ib_core_poll_stat_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	int ret;
	unsigned long flags;
	unsigned long avg_poll_time;
	struct cq_poll_node *poll_node_entry, *poll_node_next;
	struct polling_kthread *ctx = get_kthread_polling_ctx();

	ret = sprintf(buf, "kthread polled cqes cnt: %lu\n",
				ctx->cqe_polling_cnt);

	spin_lock_irqsave(&cq_list_lock, flags);
	pr_info("cq\t\tpoll cnt\tavg poll time(ns)\tmax poll time(ns)\n");
	list_for_each_entry_safe(poll_node_entry, poll_node_next,
					&ib_cq_poll_list, list) {
		avg_poll_time = poll_node_entry->time_used_ns / poll_node_entry->poll_cq_cnt;
		pr_info("%p\t%lu\t\t%lu\t\t\t%lu", poll_node_entry->cq,
				poll_node_entry->poll_cq_cnt, avg_poll_time,
				poll_node_entry->max_time_ns);
	}
	spin_unlock_irqrestore(&cq_list_lock, flags);

	return ret;
}

static ssize_t ib_core_poll_stat_store(struct kobject *kobj,
					struct kobj_attribute *attr, const char *buf,
					size_t count)
{
	unsigned int input_debug_cq_poll_stat;

	if (kstrtouint(buf, 10, &input_debug_cq_poll_stat) < 0) {
		pr_err("Invalid input, input format: <0/1> for on/off\n");
		return -EINVAL;
	}
	spin_lock(&polling_kthread_spinlock);
	ib_cq_polling_kthread.debug_cq_poll_stat = input_debug_cq_poll_stat;
	set_kthread_polling_ctx(&ib_cq_polling_kthread);
	spin_unlock(&polling_kthread_spinlock);

	return count;
}

static struct kobj_attribute ib_core_poll_cpu_attr = __ATTR_RW(ib_core_poll_cpu);
static struct kobj_attribute ib_core_poll_stat_attr = __ATTR_RW(ib_core_poll_stat);

static int __init mod_poll_cq_kthread_init(void)
{
	int ret;

	ret = sysfs_create_file(kernel_kobj, &ib_core_poll_cpu_attr.attr);
	if (ret)
		return ret;
	ret = sysfs_create_file(kernel_kobj, &ib_core_poll_stat_attr.attr);
	if (ret)
		return ret;

	/* init poll thread */
	if (!poll_cq_thread)
		poll_cq_thread = kthread_create(polling_thread, NULL, "polling_thread");
	if (IS_ERR(poll_cq_thread)) {
		ret = PTR_ERR(poll_cq_thread);
		poll_cq_thread = NULL;
		goto out;
	}
	if (!waker_polling_thread)
		waker_polling_thread = kthread_create(polling_awaken_thread,
					NULL, "polling_awaken_thread");
	if (IS_ERR(waker_polling_thread)) {
		ret = PTR_ERR(waker_polling_thread);
		waker_polling_thread = NULL;
		goto out;
	}
	ib_cq_polling_kthread.add_to_poll_list = add_cq_to_poll_list;
	ib_cq_polling_kthread.del_from_poll_list = del_cq_from_poll_list;
	set_kthread_polling_ctx(&ib_cq_polling_kthread);

out:
	return 0;
}
late_initcall(mod_poll_cq_kthread_init);

static void mod_poll_cq_kthread_exit(void)
{
	if (poll_cq_thread)
		kthread_stop(poll_cq_thread);
	if (waker_polling_thread)
		kthread_stop(waker_polling_thread);
	if (ib_cq_polling_kthread.use_polling_kthread) {
		ib_cq_polling_kthread.use_polling_kthread = 0;
		ib_cq_polling_kthread.debug_cq_poll_stat = 0;
		set_kthread_polling_ctx(&ib_cq_polling_kthread);
		clear_cq_poll_list();
	}

	sysfs_remove_file(kernel_kobj, &ib_core_poll_cpu_attr.attr);
	sysfs_remove_file(kernel_kobj, &ib_core_poll_stat_attr.attr);
}
module_exit(mod_poll_cq_kthread_exit);

MODULE_DESCRIPTION("cq polling kthread init and config");
MODULE_LICENSE("GPL");

