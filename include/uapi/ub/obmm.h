/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
 */

#ifndef UAPI_OBMM_H
#define UAPI_OBMM_H

#include <linux/types.h>

#if defined(__cplusplus)
extern "C" {
#endif


#define OBMM_MAX_LOCAL_NUMA_NODES   16
#define MAX_NUMA_DIST               255
#define OBMM_MAX_PRIV_LEN           512
#define OBMM_MAX_VENDOR_LEN         128


#define OBMM_EXPORT_FLAG_ALLOW_MMAP 0x1UL
#define OBMM_EXPORT_FLAG_FAST       0x2UL
#define OBMM_EXPORT_FLAG_MASK       (OBMM_EXPORT_FLAG_ALLOW_MMAP | OBMM_EXPORT_FLAG_FAST)

struct obmm_cmd_export_pid {
	void *va;
	__u64 length;
	__u64 flags;
	__u64 uba;
	__u64 mem_id;
	__u32 tokenid;
	__s32 pid;
	__s32 pxm_numa;
	__u16 priv_len;
	__u16 vendor_len;
	__u8 deid[16];
	__u8 seid[16];
	const void *priv;
	const void *vendor_info;
} __attribute__((aligned(8)));

/* For ordinary register requests, @length and @flags are input arguments while
 * @tokenid, @uba and @mem_id are values set by obmm kernel module. For
 * register request, @length, @flags, @tokenid and @uba are input to obmm
 * kernel module. @mem_id is the only output.
 */
struct obmm_cmd_export {
	__u64 size[OBMM_MAX_LOCAL_NUMA_NODES];
	__u64 length;
	__u64 flags;
	__u64 uba;
	__u64 mem_id;
	__u32 tokenid;
	__s32 pxm_numa;
	__u16 priv_len;
	__u16 vendor_len;
	__u8 deid[16];
	__u8 seid[16];
	const void *vendor_info;
	const void *priv;
} __attribute__((aligned(8)));

#define OBMM_UNEXPORT_FLAG_MASK	(0UL)

struct obmm_cmd_unexport {
	__u64 mem_id;
	__u64 flags;
} __attribute__((aligned(8)));

enum obmm_query_key_type {
	OBMM_QUERY_BY_PA,
	OBMM_QUERY_BY_ID_OFFSET
};

struct obmm_cmd_addr_query {
	/* key type decides the input and output */
	enum obmm_query_key_type key_type;
	__u64 mem_id;
	__u64 offset;
	__u64 pa;
} __attribute__((aligned(8)));

#define OBMM_IMPORT_FLAG_ALLOW_MMAP	0x1UL
#define OBMM_IMPORT_FLAG_PREIMPORT	0x2UL
#define OBMM_IMPORT_FLAG_NUMA_REMOTE	0x4UL
#define OBMM_IMPORT_FLAG_MASK		(OBMM_IMPORT_FLAG_ALLOW_MMAP | \
					 OBMM_IMPORT_FLAG_PREIMPORT |  \
					 OBMM_IMPORT_FLAG_NUMA_REMOTE)


struct obmm_cmd_import {
	__u64 flags;
	__u64 mem_id;
	__u64 addr;
	__u64 length;
	__u32 tokenid;
	__u32 scna;
	__u32 dcna;
	__s32 numa_id;
	__u16 priv_len;
	__u8 base_dist;
	__u8 deid[16];
	__u8 seid[16];
	const void *priv;
} __attribute__((aligned(8)));

#define OBMM_UNIMPORT_FLAG_MASK	(0UL)

struct obmm_cmd_unimport {
	__u64 mem_id;
	__u64 flags;
} __attribute__((aligned(8)));


#define OBMM_CMD_EXPORT      _IOWR('x', 0, struct obmm_cmd_export)
#define OBMM_CMD_IMPORT      _IOWR('x', 1, struct obmm_cmd_import)
#define OBMM_CMD_UNEXPORT    _IOW('x', 2, struct obmm_cmd_unexport)
#define OBMM_CMD_UNIMPORT    _IOW('x', 3, struct obmm_cmd_unimport)
#define OBMM_CMD_ADDR_QUERY  _IOWR('x', 4, struct obmm_cmd_addr_query)
#define OBMM_CMD_EXPORT_PID  _IOWR('x', 5, struct obmm_cmd_export_pid)
#define OBMM_CMD_DECLARE_PREIMPORT   _IOWR('x', 6, struct obmm_cmd_preimport)
#define OBMM_CMD_UNDECLARE_PREIMPORT _IOW('x', 7, struct obmm_cmd_preimport)

/* 2bits */
#define OBMM_SHM_MEM_CACHE_RESV     0x0
#define OBMM_SHM_MEM_NORMAL         0x1
#define OBMM_SHM_MEM_NORMAL_NC      0x2
#define OBMM_SHM_MEM_DEVICE         0x3
#define OBMM_SHM_MEM_CACHE_MASK     0b11
/* 2bits */
#define OBMM_SHM_MEM_READONLY       0x0
#define OBMM_SHM_MEM_READEXEC       0x4
#define OBMM_SHM_MEM_READWRITE      0x8
#define OBMM_SHM_MEM_NO_ACCESS      0xc
#define OBMM_SHM_MEM_ACCESS_MASK    0b1100

/* cache maintenance operations (not states) */
/* no cache maintenance (nops) */
#define OBMM_SHM_CACHE_NONE             0x0
/* invalidate only (in-cache modifications may not be written back to DRAM) */
#define OBMM_SHM_CACHE_INVAL            0x1
/* write back and invalidate */
#define OBMM_SHM_CACHE_WB_INVAL         0x2
/* write back only */
#define OBMM_SHM_CACHE_WB_ONLY         0x3
/* Automatically choose the cache maintenance action depending on the memory
 * state. The resulting choice always make sure no data would be lost, and might
 * be more conservative than necessary.
 */
#define OBMM_SHM_CACHE_INFER            0x4

struct obmm_cmd_update_range {
	/* address range to manipulate: [start, end) */
	__u64 start;
	__u64 end;
	__u8  mem_state;
	__u8  cache_ops;
} __attribute__((aligned(8)));

#define OBMM_SHMDEV_UPDATE_RANGE	_IOW('X', 0, struct obmm_cmd_update_range)

struct obmm_cmd_preimport {
	__u64 pa;
	__u64 length;
	__u64 flags;
	__u32 scna;
	__u32 dcna;
	__s32 numa_id;
	__u16 priv_len;
	__u8 base_dist;
	__u8 deid[16];
	__u8 seid[16];
	const void *priv;
} __attribute__((aligned(16), packed));

#define OBMM_PREIMPORT_FLAG_MASK	(0UL)
#define OBMM_UNPREIMPORT_FLAG_MASK	(0UL)

#define OBMM_MMAP_FLAG_HUGETLB_PMD (1UL << 63)

#if defined(__cplusplus)
}
#endif

#endif /* UAPI_OBMM_H */
