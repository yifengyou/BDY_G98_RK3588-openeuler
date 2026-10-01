// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Huawei Technologies Co., Ltd */
#include <vmlinux.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>

#define MIDR_REVISION_MASK	0xf
#define MIDR_REVISION(midr)	((midr) & MIDR_REVISION_MASK)
#define MIDR_PARTNUM_SHIFT	4
#define MIDR_PARTNUM_MASK	(0xfff << MIDR_PARTNUM_SHIFT)
#define MIDR_PARTNUM(midr)	(((midr) & MIDR_PARTNUM_MASK) >> MIDR_PARTNUM_SHIFT)
#define MIDR_VARIANT_SHIFT	20
#define MIDR_VARIANT_MASK	(0xf << MIDR_VARIANT_SHIFT)
#define MIDR_VARIANT(midr)	(((midr) & MIDR_VARIANT_MASK) >> MIDR_VARIANT_SHIFT)
#define MIDR_IMPLEMENTOR_SHIFT	24
#define MIDR_IMPLEMENTOR_MASK	(0xff << MIDR_IMPLEMENTOR_SHIFT)
#define MIDR_IMPLEMENTOR(midr)	(((midr) & MIDR_IMPLEMENTOR_MASK) >> MIDR_IMPLEMENTOR_SHIFT)

#define ARRAY_SIZE(arr)		(sizeof(arr) / sizeof((arr)[0]))
#define BITS_PER_BYTE		8UL
#define __KERNEL_DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#define DIV_ROUND_UP			__KERNEL_DIV_ROUND_UP
#define BITS_PER_TYPE(type)		(sizeof(type) * BITS_PER_BYTE)
#define BITS_TO_LONGS(nr)		DIV_ROUND_UP(nr, BITS_PER_TYPE(long))

extern bool bpf_arm64_cpu_have_feature(unsigned int num) __ksym;
extern const char *bpf_arch_flags(enum arch_flags_type t, int i) __ksym;

/* Reference: https://docs.ebpf.io/ebpf-library/libbpf/ebpf/__ksym/ */
extern void loops_per_jiffy __ksym;
extern void a32_elf_hwcap __ksym;
extern void a32_elf_hwcap2 __ksym;

extern int CONFIG_HZ __kconfig __weak;
extern int CONFIG_NR_CPUS __kconfig __weak;
extern bool CONFIG_CPU_BIG_ENDIAN __kconfig __weak;
extern bool CONFIG_AARCH32_EL0 __kconfig __weak;

#define PER_LINUX32		0x0008
#define PER_MASK		0x00ff
#define personality(pers)	(pers & PER_MASK)

#define RET_OK    0
#define RET_FAIL  1

SEC("iter/cpuinfo_arm64")
int dump_cpuinfo_arm64(struct bpf_iter__cpuinfo_arm64 *ctx)
{
	struct seq_file *m = ctx->meta->seq;
	struct cpuinfo_arm64 *cpuinfo = ctx->cpuinfo;
	unsigned int midr = cpuinfo->reg_midr;
	struct task_struct *current = bpf_get_current_task_btf();
	bool aarch32 = personality(current->personality) == PER_LINUX32;
	unsigned long out_loops_per_jiffy;
	unsigned int out_a32_elf_hwcap, out_a32_elf_hwcap2;
	int err = 0;
	int j;
	const char *COMPAT_ELF_PLATFORM = CONFIG_CPU_BIG_ENDIAN ? "v8b" : "v8l";

	BPF_SEQ_PRINTF(m, "processor\t: %ld\n", ctx->meta->seq_num);

	if (aarch32)
		BPF_SEQ_PRINTF(m, "model name\t: ARMv8 Processor rev %d (%s)\n",
				MIDR_REVISION(midr), COMPAT_ELF_PLATFORM);

	err = bpf_core_read(&out_loops_per_jiffy, sizeof(unsigned long), &loops_per_jiffy);
	if (err)
		return RET_FAIL;
	BPF_SEQ_PRINTF(m, "BogoMIPS\t: %lu.%02lu\n",
		       out_loops_per_jiffy / (500000UL/CONFIG_HZ),
		       out_loops_per_jiffy / (5000UL/CONFIG_HZ) % 100);

	BPF_SEQ_PRINTF(m, "Features\t:");
	if (aarch32 && CONFIG_AARCH32_EL0) {
		unsigned long compat_hwcap_str_size = 0x1c, compat_hwcap2_str_size = 0x7;

		bpf_core_read(&out_a32_elf_hwcap, sizeof(unsigned int), &a32_elf_hwcap);
		for (j = 0; j < compat_hwcap_str_size; j++) {
			if (out_a32_elf_hwcap & (1 << j)) {
				if (!bpf_arch_flags(ARM64_COMPAT_HWCAP, j))
					continue;
				BPF_SEQ_PRINTF(m, " %s", bpf_arch_flags(ARM64_COMPAT_HWCAP, j));
			}
		}

		bpf_core_read(&out_a32_elf_hwcap2, sizeof(unsigned int), &a32_elf_hwcap2);
		for (j = 0; j < compat_hwcap2_str_size; j++)
			if (out_a32_elf_hwcap2 & (1 << j))
				BPF_SEQ_PRINTF(m, " %s", bpf_arch_flags(ARM64_COMPAT_HWCAP2, j));
	} else {
		unsigned long hwcap_str_size = 0x82;

		for (j = 0; j < hwcap_str_size; j++)
			if (bpf_arm64_cpu_have_feature(j))
				BPF_SEQ_PRINTF(m, " %s", bpf_arch_flags(ARM64_HWCAP, j));
	}

	BPF_SEQ_PRINTF(m, "\n");

	BPF_SEQ_PRINTF(m, "CPU implementer\t: 0x%02x\n", MIDR_IMPLEMENTOR(midr));
	BPF_SEQ_PRINTF(m, "CPU architecture: 8\n");
	BPF_SEQ_PRINTF(m, "CPU variant\t: 0x%x\n", MIDR_VARIANT(midr));
	BPF_SEQ_PRINTF(m, "CPU part\t: 0x%03x\n", MIDR_PARTNUM(midr));
	BPF_SEQ_PRINTF(m, "CPU revision\t: %d\n\n", MIDR_REVISION(midr));

	return RET_OK;
}

char _license[] SEC("license") = "GPL";
