/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024. Huawei Technologies Co., Ltd. All rights reserved.
 */
#ifndef __VIRTCCA_CVM_GUEST_H
#define __VIRTCCA_CVM_GUEST_H

#ifdef CONFIG_HISI_VIRTCCA_GUEST
#include <linux/percpu.h>

struct device;

DECLARE_PER_CPU(unsigned int, virtcca_unpark_idle_notify);
DECLARE_PER_CPU(unsigned int, virtcca_park_idle_state);

extern int set_cvm_memory_encrypted(unsigned long addr, int numpages);

extern int set_cvm_memory_decrypted(unsigned long addr, int numpages);

extern bool is_virtcca_cvm_world(void);

extern void __init swiotlb_cvm_update_mem_attributes(void);

extern void virtcca_cvm_tsi_init(void);

extern void swiotlb_unmap_notify(unsigned long paddr, unsigned long size);

extern void virtcca_its_init(void);

extern struct page *virtcca_its_alloc_shared_pages_node(int node, gfp_t gfp,
			unsigned int order);

extern void virtcca_its_free_shared_pages(void *addr, int order);

#else

static inline int set_cvm_memory_encrypted(unsigned long addr, int numpages)
{
	return 0;
}

static inline int set_cvm_memory_decrypted(unsigned long addr, int numpages)
{
	return 0;
}

static inline bool is_virtcca_cvm_world(void)
{
	return false;
}

static inline void __init swiotlb_cvm_update_mem_attributes(void) {}

static inline void virtcca_cvm_tsi_init(void) {}

static inline void swiotlb_unmap_notify(unsigned long paddr, unsigned long size) {}

#endif /* CONFIG_HISI_VIRTCCA_GUEST */
#endif /* __VIRTCCA_CVM_GUEST_H */
