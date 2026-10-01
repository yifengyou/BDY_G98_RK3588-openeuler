// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * choose a prefer numa node
 *
 * Copyright (C) 2025 Huawei Limited.
 */
#include <linux/nodemask.h>
#include <linux/topology.h>
#include <asm/prefer_numa.h>

static atomic_t paral_nid_last = ATOMIC_INIT(-1);

int is_prefer_numa(void)
{
	if (num_possible_nodes() <= 1)
		return 0;

	return 1;
}

static inline unsigned int update_sched_paral_nid(void)
{
	return (unsigned int)atomic_inc_return(&paral_nid_last);
}

void set_task_paral_node(struct task_struct *p)
{
	int nid;
	int i = 0;
	const cpumask_t *cpus_mask;

	if (is_global_init(current))
		return;

	if (p->flags & PF_KTHREAD || p->tgid != p->pid)
		return;

	while (i < nr_node_ids) {
		nid = update_sched_paral_nid() % nr_node_ids;
		cpus_mask = cpumask_of_node(nid);

		if (cpumask_empty(cpus_mask) ||
			!cpumask_subset(cpus_mask, p->cpus_ptr)) {
			i++;
			continue;
		}

		cpumask_copy(p->prefer_cpus, cpus_mask);
		break;
	}
}
