// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Huawei Technologies Co., Ltd */
#include <vmlinux.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>

void bpf_rcu_read_lock(void) __ksym;
void bpf_rcu_read_unlock(void) __ksym;
void bpf_task_release(struct task_struct *p) __ksym;
struct task_struct *bpf_current_level1_reaper(void) __ksym;
struct pid_namespace *bpf_task_active_pid_ns(struct task_struct *task) __ksym;
u64 bpf_pidns_nr_tasks(struct pid_namespace *ns) __ksym;
u32 bpf_pidns_last_pid(struct pid_namespace *ns) __ksym;

char _license[] SEC("license") = "GPL";

#define FSHIFT		11		/* nr of bits of precision */
#define FIXED_1		(1<<FSHIFT)	/* 1.0 as fixed-point */
#define LOAD_INT(x)	((x) >> FSHIFT)
#define LOAD_FRAC(x)	LOAD_INT(((x) & (FIXED_1-1)) * 100)

#define RET_OK    0
#define RET_FAIL  1

SEC("iter/generic_single")
s64 dump_loadavg(struct bpf_iter__generic_single *ctx)
{
	struct seq_file *m = ctx->meta->seq;
	struct task_struct *reaper;
	struct pid_namespace *pidns;
	u64 nr_mix;
	u32 nr_running, nr_threads, last_pid;
	unsigned long avenrun[3];
	int ret = RET_OK;

	reaper = bpf_current_level1_reaper();
	if (!reaper)
		return RET_FAIL;
	bpf_rcu_read_lock();

	pidns = bpf_task_active_pid_ns(reaper);
	// ~= memcpy(avenrun, pidns->loadavg->avenrun, sizeof(avenrun))
	BPF_CORE_READ_INTO(&avenrun, pidns, loadavg, avenrun);

	nr_mix = bpf_pidns_nr_tasks(pidns);
	nr_running = nr_mix >> 32;
	nr_threads = (u32)nr_mix;
	last_pid = bpf_pidns_last_pid(pidns);

	BPF_SEQ_PRINTF(m, "%lu.%02lu %lu.%02lu %lu.%02lu %u/%d %d\n",
		LOAD_INT(avenrun[0]), LOAD_FRAC(avenrun[0]),
		LOAD_INT(avenrun[1]), LOAD_FRAC(avenrun[1]),
		LOAD_INT(avenrun[2]), LOAD_FRAC(avenrun[2]),
		nr_running, nr_threads, last_pid);

	bpf_rcu_read_unlock();
	bpf_task_release(reaper);
	return ret;
}
