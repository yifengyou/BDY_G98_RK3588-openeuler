// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Huawei Technologies Co., Ltd */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

/* Copied from kdev_t.h */
#define MINORBITS	20
#define MINORMASK	((1U << MINORBITS) - 1)
#define MAJOR(dev)	((unsigned int) ((dev) >> MINORBITS))
#define MINOR(dev)	((unsigned int) ((dev) & MINORMASK))

static inline sector_t bdev_nr_sectors(struct block_device *bdev)
{
	return bdev->bd_nr_sectors;
}

/* Reference: show_partition() in block/genh.c */
SEC("iter/partitions")
s64 dump_partitions(struct bpf_iter__partitions *ctx)
{
	struct seq_file *m = ctx->meta->seq;
	struct block_device *part = ctx->part;

	if (!bdev_nr_sectors(part))
		return 0;
	BPF_SEQ_PRINTF(m, "%4d  %7d %10llu ",
		       MAJOR(part->bd_dev), MINOR(part->bd_dev),
		       bdev_nr_sectors(part) >> 1);

	/*
	 * Mimic %pg format of printk.
	 * Reference: bdev_name() in lib/vsprintf.c
	 */
	if (part->bd_partno)
		BPF_SEQ_PRINTF(m, "%s%d\n", part->bd_disk->disk_name, part->bd_partno);
	else
		BPF_SEQ_PRINTF(m, "%s\n", part->bd_disk->disk_name);

	return 0;
}
