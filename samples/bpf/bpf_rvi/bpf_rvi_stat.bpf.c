// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Huawei Technologies Co., Ltd */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define READ_ONCE(x)		(*(volatile typeof(x) *)&(x))

unsigned int bpf_kstat_softirqs_cpu(unsigned int irq, int cpu) __ksym;
unsigned long bpf_kstat_cpu_irqs_sum(unsigned int cpu) __ksym;
void bpf_kcpustat_cpu_fetch(struct kernel_cpustat *dst, int cpu) __ksym;
u64 bpf_get_idle_time(struct kernel_cpustat *kcs, int cpu) __ksym;
u64 bpf_get_iowait_time(struct kernel_cpustat *kcs, int cpu) __ksym;
struct task_struct *bpf_current_level1_reaper(void) __ksym;
void bpf_task_release(struct task_struct *p) __ksym;
struct cpumask *bpf_task_allowed_cpus(struct task_struct *p) __ksym;
u32 bpf_cpumask_next_idx(int n, const struct cpumask *mask) __ksym;
u64 bpf_cpuacct_stat_from_task(struct task_struct *p, int cpu, enum cpuacct_stat_index idx) __ksym;
void bpf_cpuacct_kcpustat_cpu_fetch(struct kernel_cpustat *dst, struct cpuacct *ca, int cpu) __ksym;
void bpf_seq_file_append(struct seq_file *dst, struct seq_file *src) __ksym;
void bpf_get_boottime_timens(struct task_struct *tsk, struct timespec64 *boottime) __ksym;
unsigned long bpf_get_total_forks(void) __ksym;
unsigned int bpf_nr_running(void) __ksym;
unsigned long long bpf_nr_context_switches(void) __ksym;
unsigned int bpf_nr_iowait(void) __ksym;
void bpf_show_all_irqs(struct seq_file *p) __ksym;

#define NSEC_PER_SEC		1000000000L
#define USER_HZ			100
/* Take the `#if (NSEC_PER_SEC % USER_HZ) == 0` case */
static inline u64 nsec_to_clock_t(u64 x)
{
	return x / (NSEC_PER_SEC / USER_HZ);
}

struct stat_sum_data {
	u64 user, nice, system, idle, iowait, irq, softirq, steal;
	u64 guest, guest_nice;
	u64 sum;
	u64 sum_softirq;
	unsigned int per_softirq_sums[NR_SOFTIRQS];
	u64 nr_context_switches, nr_running, nr_iowait;
};

struct stat_sum_data_map {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct stat_sum_data);
} collect_map SEC(".maps");

#define RET_OK    0
#define RET_FAIL  1

SEC("iter/stat")
s64 dump_stat(struct bpf_iter__stat *ctx)
{
	struct seq_file *m = ctx->meta->seq;
	u64 cpuid = ctx->cpuid;
	struct cpuacct *cpuacct = ctx->cpuacct;
	bool print_all = ctx->print_all;
	struct seq_file *seqf_pcpu = ctx->seqf_pcpu;
	struct task_struct *current = bpf_get_current_task_btf(); // just for bpf map management
	struct stat_sum_data *collect;
	int j;

	collect = bpf_task_storage_get(&collect_map, current, NULL, BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!collect)
		return RET_FAIL;

	if (!print_all) {
		u64 user, nice, system, idle, iowait, irq, softirq, steal;
		u64 guest, guest_nice;
		struct kernel_cpustat kcpustat = {};
		u64 *cpustat = kcpustat.cpustat;

		bpf_kcpustat_cpu_fetch(&kcpustat, cpuid);
		user		= cpustat[CPUTIME_USER];
		nice		= cpustat[CPUTIME_NICE];
		system		= cpustat[CPUTIME_SYSTEM];
		idle		= bpf_get_idle_time(&kcpustat, cpuid);
		iowait		= bpf_get_iowait_time(&kcpustat, cpuid);
		irq		= cpustat[CPUTIME_IRQ];
		softirq		= cpustat[CPUTIME_SOFTIRQ];
		steal		= cpustat[CPUTIME_STEAL];
		guest		= cpustat[CPUTIME_GUEST];
		guest_nice	= cpustat[CPUTIME_GUEST_NICE];

		collect->sum	+= bpf_kstat_cpu_irqs_sum(cpuid);
		collect->sum	+= ctx->arch_irq_stat_cpu;
		for (j = 0; j < NR_SOFTIRQS; j++) {
			unsigned int softirq_stat = bpf_kstat_softirqs_cpu(j, cpuid);

			collect->per_softirq_sums[j] += softirq_stat;
			collect->sum_softirq += softirq_stat;
		}

		// don't print cpuid to avoid leaking host info
		BPF_SEQ_PRINTF(seqf_pcpu, "cpu%d", ctx->meta->seq_num);

		if (cpuacct) {
			struct kernel_cpustat kcpustat = {};
			u64 *cpustat = kcpustat.cpustat;

			bpf_cpuacct_kcpustat_cpu_fetch(&kcpustat, cpuacct, cpuid);

			user		= cpustat[CPUTIME_USER];
			nice		= cpustat[CPUTIME_NICE];
			system		= cpustat[CPUTIME_SYSTEM];
			irq		= cpustat[CPUTIME_IRQ];
			softirq		= cpustat[CPUTIME_SOFTIRQ];
			idle		= cpustat[CPUTIME_IDLE];
			iowait		= cpustat[CPUTIME_IOWAIT];
			steal		= cpustat[CPUTIME_STEAL];
			guest		= cpustat[CPUTIME_GUEST];
			guest_nice	= cpustat[CPUTIME_GUEST_NICE];

			collect->user += user;
			collect->nice += nice;
			collect->system += system;
			collect->idle += idle;
			collect->iowait += iowait;
			collect->irq += irq;
			collect->softirq += softirq;
			collect->steal += steal;
			collect->guest += guest;
			collect->guest_nice += guest_nice;

			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(user));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(nice));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(system));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(idle));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(iowait));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(irq));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(softirq));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(steal));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(guest));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(guest_nice));
		} else {
			collect->user += user;
			collect->nice += nice;
			collect->system += system;
			collect->idle += idle;
			collect->iowait += iowait;
			collect->irq += irq;
			collect->softirq += softirq;
			collect->steal += steal;
			collect->guest += guest;
			collect->guest_nice += guest_nice;

			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(user));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(nice));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(system));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(idle));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(iowait));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(irq));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(softirq));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(steal));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(guest));
			BPF_SEQ_PRINTF(seqf_pcpu, " %ld", nsec_to_clock_t(guest_nice));
		}

		BPF_SEQ_PRINTF(seqf_pcpu, "\n");
	} else {
		struct timespec64 boottime;

		// Add only once
		collect->sum += ctx->arch_irq_stat;

		BPF_SEQ_PRINTF(m, "cpu  %ld", nsec_to_clock_t(collect->user));
		BPF_SEQ_PRINTF(m, " %ld", nsec_to_clock_t(collect->nice));
		BPF_SEQ_PRINTF(m, " %ld", nsec_to_clock_t(collect->system));
		BPF_SEQ_PRINTF(m, " %ld", nsec_to_clock_t(collect->idle));
		BPF_SEQ_PRINTF(m, " %ld", nsec_to_clock_t(collect->iowait));
		BPF_SEQ_PRINTF(m, " %ld", nsec_to_clock_t(collect->irq));
		BPF_SEQ_PRINTF(m, " %ld", nsec_to_clock_t(collect->softirq));
		BPF_SEQ_PRINTF(m, " %ld", nsec_to_clock_t(collect->steal));
		BPF_SEQ_PRINTF(m, " %ld", nsec_to_clock_t(collect->guest));
		BPF_SEQ_PRINTF(m, " %ld", nsec_to_clock_t(collect->guest_nice));
		BPF_SEQ_PRINTF(m, "\n");

		// ************************************
		// Dump percpu printing
		// Don't do this:
		//   BPF_SEQ_PRINTF(m, "%s", seqf_pcpu->buf);
		// as it prints at most 512 bytes each time
		bpf_seq_file_append(m, seqf_pcpu);
		// ************************************

		BPF_SEQ_PRINTF(m, "intr %ld\n", collect->sum);

		bpf_show_all_irqs(m);

		bpf_get_boottime_timens(current, &boottime);
		BPF_SEQ_PRINTF(m,
				"\nctxt %llu\n"
				"btime %llu\n"
				"processes %lu\n"
				"procs_running %u\n"
				"procs_blocked %u\n",
				bpf_nr_context_switches(),
				(unsigned long long)boottime.tv_sec,
				bpf_get_total_forks(),
				bpf_nr_running(),
				bpf_nr_iowait());

		BPF_SEQ_PRINTF(m, "softirq %ld", collect->sum_softirq);

		for (j = 0; j < NR_SOFTIRQS; j++)
			BPF_SEQ_PRINTF(m, " %d", collect->per_softirq_sums[j]);
		BPF_SEQ_PRINTF(m, "\n");

		bpf_task_storage_delete(&collect_map, current);
	}

	return RET_OK;
}

char _license[] SEC("license") = "GPL";
