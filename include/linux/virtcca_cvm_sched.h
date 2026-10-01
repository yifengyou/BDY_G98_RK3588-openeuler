/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024. Huawei Technologies Co., Ltd. All rights reserved.
 */

#ifndef __VIRTCCA_CVM_SCHED_H
#define __VIRTCCA_CVM_SCHED_H

enum cvm_park_idle_state {
	CVM_PARK_RUNNING,
	CVM_PARK_IDLE
};

#ifdef CONFIG_HISI_VIRTCCA_GUEST
#include <asm/virtcca_cvm_guest.h>

static inline bool is_cvm_in_park_idle_state(int cpu)
{
	return (per_cpu(virtcca_park_idle_state, cpu) == CVM_PARK_IDLE);
}

static inline bool virtcca_cvm_domain(void)
{
	return is_virtcca_cvm_world();
}


extern bool virtcca_spin_cpumask_test_cpu(int cpu);

static inline void virtcca_set_park_idle_state(int cpu,
					enum cvm_park_idle_state state)
{
	per_cpu(virtcca_park_idle_state, cpu) = state;
}

static inline bool is_virtcca_unpark_idle_notify_set(int cpu)
{
	return (per_cpu(virtcca_unpark_idle_notify, cpu) == 1);
}

static inline void virtcca_set_unpark_idle_notify(int cpu)
{
	per_cpu(virtcca_unpark_idle_notify, cpu) = 1;
}

static inline void virtcca_clear_unpark_idle_notify(int cpu)
{
	per_cpu(virtcca_unpark_idle_notify, cpu) = 0;
}

#else
static inline bool is_cvm_in_park_idle_state(int cpu)
{
	return false;
}

static inline bool virtcca_cvm_domain(void)
{
	return false;
}

static inline bool virtcca_spin_cpumask_test_cpu(int cpu)
{
	return false;
}

static inline void virtcca_set_park_idle_state(int cpu,
					enum cvm_park_idle_state state) {}

static inline bool is_virtcca_unpark_idle_notify_set(int cpu)
{
	return false;
}

static inline void virtcca_set_unpark_idle_notify(int cpu) {}

static inline void virtcca_clear_unpark_idle_notify(int cpu) {}

#endif
#endif /* __VIRTCCA_CVM_SCHED_H */
