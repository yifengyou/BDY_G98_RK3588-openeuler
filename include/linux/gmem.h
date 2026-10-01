/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Generalized Memory Management.
 *
 * Copyright (C) 2023- Huawei, Inc.
 * Author: Weixi Zhu
 *
 */
#ifndef _GMEM_H
#define _GMEM_H

#ifdef CONFIG_GMEM
#include <linux/mm_types.h>
#include <linux/mutex.h>
#include <linux/nodemask.h>

/*
 * enum gm_ret - The return value of GMEM KPI that can be used to tell
 * the core VM or peripheral driver whether the GMEM KPI was
 * executed successfully.
 *
 * @GM_RET_SUCCESS:	The invoked GMEM KPI behaved as expected.
 * @GM_RET_FAILURE_UNKNOWN:	The GMEM KPI failed with unknown reason.
 * Any external status related to this KPI invocation changes must be rolled back.
 */
enum gm_ret {
	GM_RET_SUCCESS = 0,
	GM_RET_NOMEM,
	GM_RET_PAGE_EXIST,
	GM_RET_DMA_ERROR,
	GM_RET_MIGRATING,
	GM_RET_FAILURE_UNKNOWN,
	GM_RET_UNIMPLEMENTED,
};

/*
 * This is the parameter list of peer_map/unmap mmu operations.
 * if device should copy data to/from host, set copy and dma_addr
 */
struct gm_fault_t {
	struct mm_struct *mm;
	struct gm_dev *dev;
	unsigned long pfn;
	unsigned long va;
	unsigned long size;
	unsigned long prot;
	bool copy;
	dma_addr_t dma_addr;
	int behavior;
};

enum gm_memcpy_kind {
	GM_MEMCPY_H2D,
	GM_MEMCPY_D2H,
	GM_MEMCPY_KIND_INVALID,
};

struct gm_memcpy_t {
	struct mm_struct *mm;
	struct gm_dev *dev;
	dma_addr_t src;
	dma_addr_t dest;

	size_t size;
	enum gm_memcpy_kind kind;
};

/**
 * This struct defines a series of MMU functions registered by a peripheral
 * device that is to be invoked by GMEM.
 *
 */
struct gm_mmu {
	/* Synchronize VMA in a peer OS to interact with the host OS */
	enum gm_ret (*peer_va_alloc_fixed)(struct gm_fault_t *gmf);
	enum gm_ret (*peer_va_free)(struct gm_fault_t *gmf);

	/*
	 * Create physical mappings on peer host.
	 * If copy is set, copy data [dma_addr, dma_addr + size] to peer host
	 */
	enum gm_ret (*peer_map)(struct gm_fault_t *gmf);
	/*
	 * Destroy physical mappings on peer host.
	 * If copy is set, copy data back to [dma_addr, dma_addr + size]
	 */
	enum gm_ret (*peer_unmap)(struct gm_fault_t *gmf);

	/*
	 * Allocate gm_page with alloc_gm_page_struct()
	 * Allocate physical memory on device and save its location in gm_page
	 * attach gm_page to GMEM management by gm_add_pages() helper
	 */
	enum gm_ret (*import_phys_mem)(struct mm_struct *mm, int hnid, unsigned long page_cnt);

	/* copy one area of memory from device to host or from host to device */
	enum gm_ret (*peer_hmemcpy)(struct gm_memcpy_t *gmc);
};

#define NUM_IMPORT_PAGES   16 /* number of physical pages imported each time */

struct gm_context {
	struct gm_as *as;
	struct gm_dev *dev;
	/*
	 * consider a better container to maintain multiple ctx inside a device or multiple ctx
	 * inside a va space.
	 * A device may simultaneously have multiple contexts for time-sliced ctx switching
	 */
	struct list_head gm_dev_link;

	/* A va space may have multiple gm_context */
	struct list_head gm_as_link;
};

struct gm_dev {
	int id;

	struct gm_mmu *mmu;
	void *dev_data;
	/*
	 * A collection of device contexts. If the device does not support time-sliced context
	 * switch, then the size of the collection should never be greater than one.
	 * We need to think about what operators should the container be optimized for.
	 * A list, a radix-tree or what? What would gm_dev_activate require?
	 * Are there any accelerators that are really going to support time-sliced context switch?
	 */
	struct gm_context *current_ctx;

	struct list_head gm_ctx_list;

	/* Add tracking of registered device local physical memory. */
	nodemask_t registered_hnodes;
	struct device *dma_dev;

	struct gm_mapping *gm_mapping;
};

/* Records the status of a page-size physical page */
struct gm_mapping {
	unsigned int flag;

	union {
		struct page *page;		/* CPU node */
		struct gm_page *gm_page;	/* hetero-node */
	};

	struct gm_dev *dev;
	struct mutex lock;
};

/**
 * enum gm_as_alloc - defines different allocation policy for virtual addresses.
 *
 * @GM_AS_ALLOC_DEFAULT:		An object cache is applied to accelerate VA allocations.
 * @GM_AS_ALLOC_FIRSTFIT:		Prefer allocation efficiency.
 * @GM_AS_ALLOC_BESTFIT:		Prefer space efficiency.
 * @GM_AS_ALLOC_NEXTFIT:		Perform an address-ordered search for free addresses,
 * beginning where the previous search ended.
 */
enum gm_as_alloc {
	GM_AS_ALLOC_DEFAULT = 0,
	GM_AS_ALLOC_FIRSTFIT,
	GM_AS_ALLOC_BESTFIT,
	GM_AS_ALLOC_NEXTFIT,
};

/* Defines an address space. */
struct gm_as {
	spinlock_t rbtree_lock; /* spinlock of struct gm_as */
	struct rb_root rbroot; /*root of gm_region_t */
	enum gm_as_alloc policy;
	unsigned long start_va;
	unsigned long end_va;
	/* defines the VA unit size if an object cache is applied */
	unsigned long cache_quantum;
	/* tracks device contexts attached to this va space, using gm_as_link */
	struct list_head gm_ctx_list;
};

/* GMEM Device KPI */
int gm_dev_create(struct gm_mmu *mmu, void *dev_data,
				struct gm_dev **new_dev);
int gm_dev_register_hnode(struct gm_dev *dev);
enum gm_ret gm_dev_fault_locked(struct mm_struct *mm, unsigned long addr,
				struct gm_dev *dev, int behavior);

/* GMEM address space KPI */
int gm_as_create(unsigned long begin, unsigned long end, enum gm_as_alloc policy,
				unsigned long cache_quantum, struct gm_as **new_as);
int gm_as_destroy(struct gm_as *as);
int gm_as_attach(struct gm_as *as, struct gm_dev *dev,
				bool activate, struct gm_context **out_ctx);
int hmadvise_inner(int hnid, unsigned long start, size_t len_in, int behavior);
int hmemcpy(int hnid, unsigned long dest, unsigned long src, size_t size);

struct gm_page {
	struct list_head gm_page_list;

	unsigned long flags;
	unsigned long dev_pfn;
	unsigned long dev_dma_addr;
	unsigned int hnid;

	/*
	* The same functionality as rmap, we need know which process
	* maps to this gm_page with which virtual address.
	* */
	unsigned long va;
	struct mm_struct *mm;
	spinlock_t rmap_lock;

	unsigned int flag;
	atomic_t refcount;
};

/* For driver to add device pages */
int gm_add_pages(unsigned int hnid, struct list_head *pages);
struct gm_page *alloc_gm_page_struct(void);
enum gm_ret gm_dev_fault(struct mm_struct *mm, unsigned long addr,
				struct gm_dev *dev, int behavior);

#endif /* CONFIG_GMEM */

#define gmem_err(fmt, ...) \
	((void)pr_err("[gmem]" fmt "\n", ##__VA_ARGS__))

#endif /* _GMEM_H */
