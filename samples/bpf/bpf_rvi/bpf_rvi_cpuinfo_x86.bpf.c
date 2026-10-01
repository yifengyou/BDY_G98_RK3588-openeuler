// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Huawei Technologies Co., Ltd */
#include <vmlinux.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>

#define ARRAY_SIZE(x)		(sizeof(x) / sizeof(*(x)))
#define BITS_PER_BYTE		8
#define BITS_PER_LONG		(sizeof(long) * BITS_PER_BYTE)
#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)


/* Check generic_test_bit() in include/asm-generic/bitops/generic-non-atomic.h */
static inline bool test_bit(unsigned long nr, const unsigned long *addr)
{
	return 1UL & (addr[BIT_WORD(nr)] >> (nr & (BITS_PER_LONG-1)));
}

const char *bpf_arch_flags(enum arch_flags_type t, int i) __ksym;

char _license[] SEC("license") = "GPL";

#define NCAPINTS		31	   /* N 32-bit words worth of info */
#define NBUGINTS		4	   /* N 32-bit bug flags */
#define X86_FEATURE_TSC		(0 * 32 + 4) /* Time Stamp Counter */
#define HZ			1000

/*
 * Reference: arch/x86/include/asm/cpufeature.h
 * Treats cpu_has(c, bit) == test_cpu_cap(c, bit)
 */
#define cpu_has(c, bit)		test_bit(bit, (unsigned long *)((c)->x86_capability))
#define cpu_has_bug(c, bit)	cpu_has((c), (bit))

#define MAX_CPUS	(sizeof(struct cpumask) * BITS_PER_BYTE)

#define RET_OK    0
#define RET_FAIL  1
/*
 * For bpf prog, -1 means to skip the current object,
 * so we can use ctx->meta->seq_num as new cpu index.
 */
#define RET_SKIP -1

/*
 * Relevant concepts:
 *  - CPU socket/chip, could be many on one motherboard
 *  - Physical CPU core
 *  - Logical processor, from e.g. SMT
 *
 * Field explanation:
 *  - physical id: id of CPU socket (i.e. CPU chip)
 *  - siblings: number of logical processors per CPU chip
 *  - core id: id of physical core
 *  - cpu cores: number of physical cores per CPU chip
 *  - (initial) apicid: something given by BIOS
 *
 * Use a designed pattern to print physical info that could leak the real number
 * of CPUs on the host.
 *
 */
static void show_cpuinfo_core(struct seq_file *m, struct cpuinfo_x86 *c,
			      unsigned int cpu, unsigned int siblings)
{
	BPF_SEQ_PRINTF(m, "physical id\t: %d\n", cpu);
	BPF_SEQ_PRINTF(m, "siblings\t: 1\n");
	BPF_SEQ_PRINTF(m, "core id\t\t: 0\n");
	BPF_SEQ_PRINTF(m, "cpu cores\t: 1\n");
	BPF_SEQ_PRINTF(m, "apicid\t\t: %d\n", cpu);
	BPF_SEQ_PRINTF(m, "initial apicid\t: %d\n", cpu);
}

/* this for CONFIG_X86 64 bit*/
static void show_cpuinfo_misc(struct seq_file *m, struct cpuinfo_x86 *c)
{
	BPF_SEQ_PRINTF(m,
		       "fpu\t\t: yes\n"
		       "fpu_exception\t: yes\n"
		       "cpuid level\t: %d\n"
		       "wp\t\t: yes\n",
		       c->cpuid_level);
}

/*
 * Reference: arch/x86/kernel/cpu/proc.c
 */
SEC("iter/cpuinfo_x86")
s64 dump_cpuinfo_x86(struct bpf_iter__cpuinfo_x86 *ctx)
{
	struct seq_file *m = ctx->meta->seq;
	struct cpuinfo_x86 *c = ctx->cpuinfo;
	struct cpuinfo_x86_bpf *c_bpf = ctx->cpuinfo_bpf;
	unsigned long x86_power_flags_size;
	unsigned int virtual_cpu;
	int i;

	virtual_cpu = ctx->meta->seq_num;
	BPF_SEQ_PRINTF(m, "processor\t: %u\n"
		       "vendor_id\t: %s\n"
		       "cpu family\t: %d\n"
		       "model\t\t: %u\n"
		       "model name\t: %s\n",
		       virtual_cpu,
		       c->x86_vendor_id[0] ? c->x86_vendor_id : "unknown",
		       c->x86,
		       c->x86_model,
		       c->x86_model_id[0] ? c->x86_model_id : "unknown");
	if (c->x86_stepping || c->cpuid_level >= 0)
		BPF_SEQ_PRINTF(m, "stepping\t: %d\n", c->x86_stepping);
	else
		BPF_SEQ_PRINTF(m, "stepping\t: unknown\n");
	if (c->microcode)
		BPF_SEQ_PRINTF(m, "microcode\t: 0x%x\n", c->microcode);

	if (cpu_has(c, X86_FEATURE_TSC)) {
		unsigned int freq = c_bpf->cpu_khz;//bpf_arch_freq_get_on_cpu(cpu);

		BPF_SEQ_PRINTF(m, "cpu MHz\t\t: %u.%03u\n", freq / 1000, (freq % 1000));
	}
	/* Cache size */
	if (c->x86_cache_size)
		BPF_SEQ_PRINTF(m, "cache size\t: %u KB\n", c->x86_cache_size);
	show_cpuinfo_core(m, c, virtual_cpu, c_bpf->siblings);
	show_cpuinfo_misc(m, c);

	BPF_SEQ_PRINTF(m, "flags\t\t:");
	for (i = 0; i < 32*NCAPINTS; i++)
		if (cpu_has(c, i) && bpf_arch_flags(X86_CAP, i))
			BPF_SEQ_PRINTF(m, " %s", bpf_arch_flags(X86_CAP, i));

	BPF_SEQ_PRINTF(m, "\nbugs\t\t:");
	for (i = 0; i < 32*NBUGINTS; i++) {
		unsigned int bug_bit = 32*NCAPINTS + i;

		if (cpu_has_bug(c, bug_bit) && bpf_arch_flags(X86_BUG, i))
			BPF_SEQ_PRINTF(m, " %s", bpf_arch_flags(X86_BUG, i));
	}

	BPF_SEQ_PRINTF(m, "\nbogomips\t: %lu.%02lu\n",
		       c->loops_per_jiffy/(500000/HZ),
		       (c->loops_per_jiffy/(5000/HZ)) % 100);

	//#ifdef CONFIG_X86_64
	if (c->x86_tlbsize > 0)
		BPF_SEQ_PRINTF(m, "TLB size\t: %d 4K pages\n", c->x86_tlbsize);

	BPF_SEQ_PRINTF(m, "clflush size\t: %u\n", c->x86_clflush_size);
	BPF_SEQ_PRINTF(m, "cache_alignment\t: %d\n", c->x86_cache_alignment);
	BPF_SEQ_PRINTF(m, "address sizes\t: %u bits physical, %u bits virtual\n",
		       c->x86_phys_bits, c->x86_virt_bits);

	BPF_SEQ_PRINTF(m, "power management:");
	x86_power_flags_size = (unsigned long)bpf_arch_flags(X86_POWER_SIZE, 0);
	for (i = 0; i < 32; i++) {
		if (c->x86_power & (1 << i)) {
			if (i < x86_power_flags_size &&
			    bpf_arch_flags(X86_POWER, i))
				BPF_SEQ_PRINTF(m, "%s%s",
					       bpf_arch_flags(X86_POWER, i)[0] ? " " : "",
					       bpf_arch_flags(X86_POWER, i));
			else
				BPF_SEQ_PRINTF(m, " [%d]", i);
		}
	}

	BPF_SEQ_PRINTF(m, "\n\n");

	return RET_OK;
}
