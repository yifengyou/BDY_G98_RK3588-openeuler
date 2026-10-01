// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Huawei Technologies Co., Ltd */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

void bpf_blkcg_get_dev_iostat(struct blkcg *blkcg, int major, int minor,
				struct blkg_rw_iostat *iostat, bool is_v2) __ksym;

char _license[] SEC("license") = "GPL";

#define MINORBITS	20
#define MINORMASK	((1U << MINORBITS) - 1)
#define MAJOR(dev)	((unsigned int) ((dev) >> MINORBITS))
#define MINOR(dev)	((unsigned int) ((dev) & MINORMASK))
#define MKDEV(ma, mi)	(((ma) << MINORBITS) | (mi))

/*
 * The current implementation of bpf prog for diskstats retrieves I/O stat and
 * limit of a container from either cgroup V1 or V2, and it's decided at compile
 * time. In the future we may make this decision dynamically.
 */
#define USE_CGROUP_V1

#define anyof(seg)	(r##seg || w##seg || d##seg)

#ifdef USE_CGROUP_V1
static bool throttle_stat_available(struct blkg_rw_iostat *iostat)
{
	u64 rbytes, wbytes, dbytes;
	u64 rios, wios, dios;

	rbytes = iostat->throttle_bytes.cnt[BLKG_RWSTAT_READ];
	wbytes = iostat->throttle_bytes.cnt[BLKG_RWSTAT_WRITE];
	dbytes = iostat->throttle_bytes.cnt[BLKG_RWSTAT_DISCARD];
	rios   = iostat->throttle_ios.cnt[BLKG_RWSTAT_READ];
	wios   = iostat->throttle_ios.cnt[BLKG_RWSTAT_WRITE];
	dios   = iostat->throttle_ios.cnt[BLKG_RWSTAT_DISCARD];

	if (anyof(bytes) || anyof(ios))
		return true;
	return false;
}

static bool bfq_stat_available(struct blkg_rw_iostat *iostat)
{
	u64 rbytes, wbytes, dbytes;
	u64 rios, wios, dios;
	u64 rserv, wserv, dserv;
	u64 rwait, wwait, dwait;
	u64 rmerge, wmerge, dmerge;

	rbytes = iostat->bfq_bytes.cnt[BLKG_RWSTAT_READ];
	wbytes = iostat->bfq_bytes.cnt[BLKG_RWSTAT_WRITE];
	dbytes = iostat->bfq_bytes.cnt[BLKG_RWSTAT_DISCARD];
	rios   = iostat->bfq_ios.cnt[BLKG_RWSTAT_READ];
	wios   = iostat->bfq_ios.cnt[BLKG_RWSTAT_WRITE];
	dios   = iostat->bfq_ios.cnt[BLKG_RWSTAT_DISCARD];
	rserv  = iostat->bfq_service_time.cnt[BLKG_RWSTAT_READ];
	wserv  = iostat->bfq_service_time.cnt[BLKG_RWSTAT_WRITE];
	dserv  = iostat->bfq_service_time.cnt[BLKG_RWSTAT_DISCARD];
	rwait  = iostat->bfq_wait_time.cnt[BLKG_RWSTAT_READ];
	wwait  = iostat->bfq_wait_time.cnt[BLKG_RWSTAT_WRITE];
	dwait  = iostat->bfq_wait_time.cnt[BLKG_RWSTAT_DISCARD];
	rmerge = iostat->bfq_merged.cnt[BLKG_RWSTAT_READ];
	wmerge = iostat->bfq_merged.cnt[BLKG_RWSTAT_WRITE];
	dmerge = iostat->bfq_merged.cnt[BLKG_RWSTAT_DISCARD];

	if (anyof(bytes) || anyof(ios) || anyof(serv) || anyof(wait) || anyof(merge))
		return true;
	return false;
}
#else
static bool v2_stat_available(struct blkg_rw_iostat *iostat)
{
	u64 rbytes, wbytes, dbytes;
	u64 rios, wios, dios;

	rbytes = iostat->v2_iostat.bytes[BLKG_IOSTAT_READ];
	wbytes = iostat->v2_iostat.bytes[BLKG_IOSTAT_WRITE];
	dbytes = iostat->v2_iostat.bytes[BLKG_IOSTAT_DISCARD];
	rios   = iostat->v2_iostat.ios[BLKG_IOSTAT_READ];
	wios   = iostat->v2_iostat.ios[BLKG_IOSTAT_WRITE];
	dios   = iostat->v2_iostat.ios[BLKG_IOSTAT_DISCARD];

	if (anyof(bytes) || anyof(ios))
		return true;
	return false;
}
#endif

#define MSEC_PER_SEC	1000L
#define NSEC_PER_MSEC	1000000L
#define HZ		1000
static inline u32 jiffies_to_msecs(const unsigned long j)
{
	return (MSEC_PER_SEC / HZ) * j;
}

static inline u64 div_u64(u64 dividend, u32 divisor)
{
	return dividend / divisor;
}

static void native_diskstats_show(struct seq_file *m, struct block_device *hd,
				  struct disk_stats *stat, unsigned int inflight)
{
	BPF_SEQ_PRINTF(m, "%4d %7d ", MAJOR(hd->bd_dev), MINOR(hd->bd_dev));
	/* Reference: bdev_name() in lib/vsprintf.c */
	if (hd->bd_partno)
		BPF_SEQ_PRINTF(m, "%s%d ", hd->bd_disk->disk_name, hd->bd_partno);
	else
		BPF_SEQ_PRINTF(m, "%s ", hd->bd_disk->disk_name);

	BPF_SEQ_PRINTF(m, "%lu %lu %lu %u %lu %lu %lu %u ",
		       stat->ios[STAT_READ],
		       stat->merges[STAT_READ],
		       stat->sectors[STAT_READ],
		       (unsigned int)div_u64(stat->nsecs[STAT_READ],
						NSEC_PER_MSEC),
		       stat->ios[STAT_WRITE],
		       stat->merges[STAT_WRITE],
		       stat->sectors[STAT_WRITE],
		       (unsigned int)div_u64(stat->nsecs[STAT_WRITE],
						NSEC_PER_MSEC)
		);
	BPF_SEQ_PRINTF(m, "%u %u %u ",
		       inflight,
		       jiffies_to_msecs(stat->io_ticks),
		       (unsigned int)div_u64(stat->nsecs[STAT_READ] +
					     stat->nsecs[STAT_WRITE] +
					     stat->nsecs[STAT_DISCARD] +
					     stat->nsecs[STAT_FLUSH],
						NSEC_PER_MSEC)
		);
	BPF_SEQ_PRINTF(m, "%lu %lu %lu %u %lu %u\n",
		       stat->ios[STAT_DISCARD],
		       stat->merges[STAT_DISCARD],
		       stat->sectors[STAT_DISCARD],
		       (unsigned int)div_u64(stat->nsecs[STAT_DISCARD],
						NSEC_PER_MSEC),
		       stat->ios[STAT_FLUSH],
		       (unsigned int)div_u64(stat->nsecs[STAT_FLUSH],
						NSEC_PER_MSEC)
		);
}

enum iostat_choice {
	IOSTAT_CHOICE_NONE,
#ifdef USE_CGROUP_V1
	IOSTAT_CHOICE_BFQ,
	IOSTAT_CHOICE_THROTTLE,
#else
	IOSTAT_CHOICE_V2,
#endif
};

/* Reference: diskstats_show() in block/genhd.c */
SEC("iter/diskstats")
s64 dump_diskstats(struct bpf_iter__diskstats *ctx)
{
	struct seq_file *m = ctx->meta->seq;
	struct block_device *bd = ctx->bd;
	struct blkcg *blkcg = ctx->task_blkcg;
	struct blkg_rw_iostat iostat = {};
	int major, minor;
	enum iostat_choice choice = IOSTAT_CHOICE_NONE;
#ifdef USE_CGROUP_V1
	bool use_v2 = false;
#else
	bool use_v2 = true;
#endif

	major = MAJOR(bd->bd_dev);
	minor = MINOR(bd->bd_dev);

	bpf_blkcg_get_dev_iostat(blkcg, major, minor, &iostat, use_v2);
#ifdef USE_CGROUP_V1
	if (bfq_stat_available(&iostat))
		choice = IOSTAT_CHOICE_BFQ;
	else if (throttle_stat_available(&iostat))
		choice = IOSTAT_CHOICE_THROTTLE;
#else
	if (v2_stat_available(&iostat))
		choice = IOSTAT_CHOICE_V2;
#endif

	if (choice == IOSTAT_CHOICE_NONE) {
		native_diskstats_show(m, bd, ctx->native_stat, ctx->inflight);
		return 0;
	}

	BPF_SEQ_PRINTF(m, "%4d %7d ", major, minor);
	/* Reference: bdev_name() in lib/vsprintf.c */
	if (bd->bd_partno)
		BPF_SEQ_PRINTF(m, "%s%d ", bd->bd_disk->disk_name, bd->bd_partno);
	else
		BPF_SEQ_PRINTF(m, "%s ", bd->bd_disk->disk_name);

	/*
	 * Long fmt needs to be split, as BPF_SEQ_PRINTF accepts limited
	 * number of arguments via macro expansion.
	 */
#ifdef USE_CGROUP_V1
	if (choice == IOSTAT_CHOICE_BFQ) {
		BPF_SEQ_PRINTF(m, "%lu %lu %lu %lu ",
				  /* 4-7: read {ios,*merges*,sectors,*nsecs*} */
				  iostat.bfq_ios.cnt[BLKG_RWSTAT_READ],
				  iostat.bfq_merged.cnt[BLKG_RWSTAT_READ],
				  iostat.bfq_bytes.cnt[BLKG_RWSTAT_READ] >> 9,
				  iostat.bfq_service_time.cnt[BLKG_RWSTAT_READ] / 1000000 +
				  iostat.bfq_wait_time.cnt[BLKG_RWSTAT_READ] / 1000000);
		BPF_SEQ_PRINTF(m, "%lu %lu %lu %lu ",
				  /* 8-11: write {ios,*merges*,sectors,*nsecs*} */
				  iostat.bfq_ios.cnt[BLKG_RWSTAT_WRITE],
				  iostat.bfq_merged.cnt[BLKG_RWSTAT_WRITE],
				  iostat.bfq_bytes.cnt[BLKG_RWSTAT_WRITE] >> 9,
				  iostat.bfq_service_time.cnt[BLKG_RWSTAT_WRITE] / 1000000 +
				  iostat.bfq_wait_time.cnt[BLKG_RWSTAT_WRITE] / 1000000);
		BPF_SEQ_PRINTF(m, "%u %u %u ",
				  // 12: I/Os currently in progress (inflight)
				  ctx->inflight,
				  // 13: time spent doing I/Os (ms) (io_ticks) TODO
				  0,
				  // 14: weighted time doing I/Os (ns) (rd + wr + discard + flush)
				  0);
		BPF_SEQ_PRINTF(m, "%lu %lu %lu %lu ",
				  /* 15-18: discard {ios,*merges*,sectors,*nsecs*} */
				  iostat.bfq_ios.cnt[BLKG_RWSTAT_DISCARD],
				  iostat.bfq_merged.cnt[BLKG_RWSTAT_DISCARD],
				  iostat.bfq_bytes.cnt[BLKG_RWSTAT_DISCARD] >> 9,
				  iostat.bfq_service_time.cnt[BLKG_RWSTAT_DISCARD] / 1000000 +
				  iostat.bfq_wait_time.cnt[BLKG_RWSTAT_DISCARD] / 1000000);
		BPF_SEQ_PRINTF(m, "%lu %lu\n",
				  /* 19-20: flush {ios,nsec} */
				  0, 0);
	} else if (choice == IOSTAT_CHOICE_THROTTLE) {
		BPF_SEQ_PRINTF(m, "%lu %lu %lu %lu ",
				  /* 4-7: read {ios,*merges*,sectors,*nsecs*} */
				  iostat.throttle_ios.cnt[BLKG_RWSTAT_READ],
				  0,
				  iostat.throttle_bytes.cnt[BLKG_RWSTAT_READ] >> 9,
				  0);
		BPF_SEQ_PRINTF(m, "%lu %lu %lu %lu ",
				  /* 8-11: write {ios,*merges*,sectors,*nsecs*} */
				  iostat.throttle_ios.cnt[BLKG_RWSTAT_WRITE],
				  0,
				  iostat.throttle_bytes.cnt[BLKG_RWSTAT_WRITE] >> 9,
				  0);
		BPF_SEQ_PRINTF(m, "%u %u %u ",
				  // 12: I/Os currently in progress (inflight)
				  ctx->inflight,
				  // 13: time spent doing I/Os (ms) (io_ticks) TODO
				  0,
				  // 14: weighted time doing I/Os (ns) (rd + wr + discard + flush)
				  0);
		BPF_SEQ_PRINTF(m, "%lu %lu %lu %lu ",
				  /* 15-18: discard {ios,*merges*,sectors,*nsecs*} */
				  iostat.throttle_ios.cnt[BLKG_RWSTAT_DISCARD],
				  0,
				  iostat.throttle_bytes.cnt[BLKG_RWSTAT_DISCARD] >> 9,
				  0);
		BPF_SEQ_PRINTF(m, "%lu %lu\n",
				  /* 19-20: flush {ios,nsec} */
				  0, 0);
	}
#else
	if (choice == IOSTAT_CHOICE_V2) {
		BPF_SEQ_PRINTF(m, "%lu %lu %lu %lu ",
				  /* 4-7: read {ios,*merges*,sectors,*nsecs*} */
				  iostat.v2_iostat.ios[BLKG_IOSTAT_READ],
				  0,
				  iostat.v2_iostat.bytes[BLKG_IOSTAT_READ] >> 9,
				  0);
		BPF_SEQ_PRINTF(m, "%lu %lu %lu %lu ",
				  /* 8-11: write {ios,*merges*,sectors,*nsecs*} */
				  iostat.v2_iostat.ios[BLKG_IOSTAT_WRITE],
				  0,
				  iostat.v2_iostat.bytes[BLKG_IOSTAT_WRITE] >> 9,
				  0);
		BPF_SEQ_PRINTF(m, "%u %u %u ",
				  // 12: I/Os currently in progress (inflight)
				  ctx->inflight,
				  // 13: time spent doing I/Os (ms) (io_ticks) TODO
				  0,
				  // 14: weighted time doing I/Os (ns) (rd + wr + discard + flush)
				  0);
		BPF_SEQ_PRINTF(m, "%lu %lu %lu %lu ",
				  /* 15-18: discard {ios,*merges*,sectors,*nsecs*} */
				  iostat.v2_iostat.ios[BLKG_IOSTAT_DISCARD],
				  0,
				  iostat.v2_iostat.bytes[BLKG_IOSTAT_DISCARD] >> 9,
				  0);
		BPF_SEQ_PRINTF(m, "%lu %lu\n",
				  /* 19-20: flush {ios,nsec} */
				  0, 0);
	}
#endif
	return 0;
}
