/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _VM_OBJECT_H
#define _VM_OBJECT_H

#ifdef CONFIG_GMEM
#include <linux/mm_types.h>

/*
 * Defines a centralized logical mapping table that reflects the mapping information
 * regardless of the underlying arch-specific MMUs.
 * The implementation of this data structure borrows the VM_OBJECT from FreeBSD as well
 * as the filemap address_space struct from Linux page cache.
 * Only VMAs point to VM_OBJECTs and maintain logical mappings, because we assume that
 * the coordiantion between page tables must happen with CPU page table involved. That
 * is to say, a generalized process unit must involve in a UVA-programming model, otherwise
 * there is no point to support UVA programming.
 * However, a VMA only needs to maintain logical mappings if the process has been
 * attached to a GMEM VA space. In normal cases, a CPU process does not need it. (unless
 * we later build a reservation system on top of the logical mapping tables to support
 * reservation-based superpages and rangeTLBs).
 * A GM_REGION does not need to maintain logical mappings. In the case that a device wants
 * to support its private address space with local physical memory, GMEM should forward address
 * space management to the core VM, using VMAs, instead of using GM_REGIONs.
 */
struct vm_object {
	spinlock_t lock;
	struct vm_area_struct *vma;

	/*
	 * The logical_page_table is a container that holds the mapping
	 * information between a VA and a struct page.
	 */
	struct xarray *logical_page_table;
	atomic_t nr_pages;

	/*
	 * a vm object might be referred by multiple VMAs to share
	 * memory.
	 */
	atomic_t ref_count;
};

/* vm_object KPI */
int __init vm_object_init(void);
void vm_object_destroy(void);
struct vm_object *vm_object_create(struct vm_area_struct *vma);
void vm_object_drop_locked(struct vm_area_struct *vma);
void dup_vm_object(struct vm_area_struct *dst, struct vm_area_struct *src,
				bool dst_peer_shared);
void vm_object_adjust(struct vm_area_struct *vma, unsigned long start,
	unsigned long end);
void vm_object_merge(struct vm_area_struct *vma, unsigned long addr);
void vm_object_split(struct vm_area_struct *old_vma,
				struct vm_area_struct *new_vma);
void dup_peer_shared_vma(struct vm_area_struct *vma);

/* gm_mapping KPI for logical pgtable operation */
struct gm_mapping *vma_prepare_gm_mapping(struct vm_area_struct *vma,
				unsigned long haddr);
struct gm_mapping *vm_object_lookup(struct vm_object *obj, unsigned long va);
void zap_logic_pmd_range(struct vm_area_struct *vma,
				unsigned long addr, unsigned long end, bool verify_pmd,
				pmd_t *pmd);
void zap_logic_pud_range(struct vm_area_struct *vma,
				unsigned long addr, unsigned long end);
void unmap_single_peer_shared_vma(struct mm_struct *mm,
				struct vm_area_struct *vma, unsigned long start_addr,
				unsigned long end_addr);
void free_gm_mappings(struct vm_area_struct *vma);
#else
static inline void vm_object_drop_locked(struct vm_area_struct *vma) {}
static inline void dup_vm_object(struct vm_area_struct *dst,
				struct vm_area_struct *src, bool dst_peer_shared) {}
static inline void dup_peer_shared_vma(struct vm_area_struct *vma) {}
static inline void vm_object_adjust(struct vm_area_struct *vma,
				unsigned long start, unsigned long end) {}
static inline void vm_object_merge(struct vm_area_struct *vma,
				unsigned long addr) {}
static inline void vm_object_split(struct vm_area_struct *old_vma,
				struct vm_area_struct *new_vma) {}
static inline void zap_logic_pmd_range(struct vm_area_struct *vma,
				unsigned long addr, unsigned long end, bool verify_pmd,
				pmd_t *pmd) {}
static inline void zap_logic_pud_range(struct vm_area_struct *vma,
				unsigned long addr, unsigned long end) {}
static inline void unmap_single_peer_shared_vma(struct mm_struct *mm,
				struct vm_area_struct *vma, unsigned long start_addr,
				unsigned long end_addr) {}
#endif

#endif /* _VM_OBJECT_H */
