// SPDX-License-Identifier: GPL-2.0-only
/*
 * Logical Mapping Management
 *
 * Copyright (C) 2023- Huawei, Inc.
 * Author: Weixi zhu, chao Liu
 *
 */
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/mman.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/coredump.h>
#include <linux/rwsem.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/spinlock.h>
#include <linux/xxhash.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/rbtree.h>
#include <linux/memory.h>
#include <linux/mmu_notifier.h>
#include <linux/swap.h>
#include <linux/ksm.h>
#include <linux/hashtable.h>
#include <linux/freezer.h>
#include <linux/oom.h>
#include <linux/numa.h>
#include <linux/mempolicy.h>
#include <linux/gmem.h>
#include <linux/xarray.h>
#include <linux/vm_object.h>

#include "gmem-internal.h"

/*
 * Sine VM_OBJECT maintains the logical page table under each VMA, and each VMA
 * points to a VM_OBJECT. Ultimately VM_OBJECTs must be maintained as long as VMA
 * gets changed: merge, split, adjust
 */
static struct kmem_cache *vm_object_cachep;
static struct kmem_cache *gm_mapping_cachep;

/* gm_mapping will not be release dynamically */
static inline struct gm_mapping *alloc_gm_mapping(void)
{
	struct gm_mapping *gm_mapping = kmem_cache_zalloc(gm_mapping_cachep, GFP_KERNEL);

	if (!gm_mapping)
		return NULL;

	gm_mapping_flags_set(gm_mapping, GM_MAPPING_NOMAP);
	mutex_init(&gm_mapping->lock);

	return gm_mapping;
}

static inline void release_gm_mapping(struct gm_mapping *mapping)
{
	kmem_cache_free(gm_mapping_cachep, mapping);
}

static inline struct gm_mapping *lookup_gm_mapping(struct vm_object *obj, unsigned long pindex)
{
	return xa_load(obj->logical_page_table, pindex);
}

int __init vm_object_init(void)
{
	vm_object_cachep = KMEM_CACHE(vm_object, 0);
	if (!vm_object_cachep)
		goto out;

	gm_mapping_cachep = KMEM_CACHE(gm_mapping, 0);
	if (!gm_mapping_cachep)
		goto free_vm_object;

	return 0;
free_vm_object:
	vm_object_destroy();
out:
	return -ENOMEM;
}

void vm_object_destroy(void)
{
	kmem_cache_destroy(gm_mapping_cachep);
	gm_mapping_cachep = NULL;

	kmem_cache_destroy(vm_object_cachep);
	vm_object_cachep = NULL;
}

/*
 * Create a VM_OBJECT and attach it to a VMA
 * This should be called when a VMA is created.
 */
struct vm_object *vm_object_create(struct vm_area_struct *vma)
{
	struct vm_object *obj = kmem_cache_alloc(vm_object_cachep, GFP_KERNEL);

	if (!obj)
		return NULL;

	spin_lock_init(&obj->lock);
	obj->vma = vma;

	/*
	 * The logical page table maps linear_page_index(obj->vma, va)
	 * to pointers of struct gm_mapping.
	 */
	obj->logical_page_table = kmalloc(sizeof(struct xarray), GFP_KERNEL);
	if (!obj->logical_page_table) {
		kmem_cache_free(vm_object_cachep, obj);
		return NULL;
	}

	xa_init(obj->logical_page_table);
	atomic_set(&obj->nr_pages, 0);
	atomic_set(&obj->ref_count, 1);

	return obj;
}

/* This should be called when a VMA no longer refers to a VM_OBJECT */
void vm_object_drop_locked(struct vm_area_struct *vma)
{
	struct vm_object *obj = vma->vm_obj;

	if (!obj) {
		pr_err("vm_object: vm_obj of the vma is NULL\n");
		return;
	}

	free_gm_mappings(vma);
	mmap_assert_write_locked(vma->vm_mm);
	vma->vm_obj = NULL;

	if (atomic_dec_and_test(&obj->ref_count)) {
		xa_destroy(obj->logical_page_table);
		kfree(obj->logical_page_table);
		kmem_cache_free(vm_object_cachep, obj);
	}
}

void dup_vm_object(struct vm_area_struct *dst, struct vm_area_struct *src, bool dst_peer_shared)
{
	unsigned long index;
	struct gm_mapping *mapping;
	unsigned long moved_pages = 0;

	if (dst_peer_shared) {
		if (!vma_is_peer_shared(dst))
			return;
	} else {
		if (!vma_is_peer_shared(src))
			return;
	}

	XA_STATE(xas, src->vm_obj->logical_page_table, linear_page_index(src, src->vm_start));

	xa_lock(dst->vm_obj->logical_page_table);
	rcu_read_lock();
	xas_for_each(&xas, mapping, linear_page_index(src, src->vm_end)) {
		index = xas.xa_index - src->vm_pgoff + dst->vm_pgoff +
			((src->vm_start - dst->vm_start) >> PAGE_SHIFT);
		__xa_store(dst->vm_obj->logical_page_table, index, mapping, GFP_KERNEL);
		moved_pages++;
	}
	rcu_read_unlock();
	atomic_add(moved_pages, &dst->vm_obj->nr_pages);
	xa_unlock(dst->vm_obj->logical_page_table);
}

void dup_peer_shared_vma(struct vm_area_struct *vma)
{
	if (vma_is_peer_shared(vma)) {
		pr_debug("gmem: peer-shared vma should not be dup\n");
		vma->vm_obj = vm_object_create(vma);
	}
}

/**
 * new_vma is part of old_vma, so old_vma->vm_start <= new_vma->vm_start
 * and new_vma->vm_end < old_vma->vm_end
 */
void vm_object_split(struct vm_area_struct *old_vma, struct vm_area_struct *new_vma)
{
	unsigned long index;
	struct gm_mapping *page;
	unsigned long transferred_pages = 0;

	if (!old_vma->vm_obj || !new_vma->vm_obj)
		return;

	XA_STATE(xas, old_vma->vm_obj->logical_page_table,
		linear_page_index(old_vma, new_vma->vm_start));

	xa_lock(old_vma->vm_obj->logical_page_table);
	xa_lock(new_vma->vm_obj->logical_page_table);
	xas_for_each(&xas, page, linear_page_index(old_vma, new_vma->vm_end - SZ_2M)) {
		index = xas.xa_index - old_vma->vm_pgoff + new_vma->vm_pgoff -
				((new_vma->vm_start - old_vma->vm_start) >> PAGE_SHIFT);
		__xa_store(new_vma->vm_obj->logical_page_table, index, page, GFP_KERNEL);
		xas_store(&xas, NULL);
		transferred_pages++;
	}

	atomic_sub(transferred_pages, &old_vma->vm_obj->nr_pages);
	atomic_add(transferred_pages, &new_vma->vm_obj->nr_pages);
	xa_unlock(new_vma->vm_obj->logical_page_table);
	xa_unlock(old_vma->vm_obj->logical_page_table);
}

void vm_object_merge(struct vm_area_struct *vma, unsigned long addr)
{
	unsigned long index;
	struct gm_mapping *page;
	struct vm_area_struct *next, *n_next;
	unsigned long moved_pages = 0;

	VMA_ITERATOR(vmi, vma->vm_mm, vma->vm_start);
	next = vma_next(&vmi);
	next = vma_next(&vmi);
	if (!next)
		return;

	if (addr < vma->vm_end) {
		/* case 4: move logical mapping in [end, vma->vm_end) from vma to next */
		XA_STATE(xas, vma->vm_obj->logical_page_table,
			linear_page_index(vma, addr));

		xa_lock(vma->vm_obj->logical_page_table);
		xa_lock(next->vm_obj->logical_page_table);
		xas_for_each(&xas, page, linear_page_index(vma, vma->vm_end - SZ_2M)) {
			index = xas.xa_index - vma->vm_pgoff + next->vm_pgoff -
					((next->vm_start - vma->vm_start) >> PAGE_SHIFT);
			__xa_store(next->vm_obj->logical_page_table, index, page, GFP_KERNEL);
			xas_store(&xas, NULL);
			moved_pages++;
		}
		atomic_sub(moved_pages, &vma->vm_obj->nr_pages);
		atomic_add(moved_pages, &next->vm_obj->nr_pages);
		xa_unlock(next->vm_obj->logical_page_table);
		xa_unlock(vma->vm_obj->logical_page_table);
	} else {
		n_next = vma_next(&vmi);

		if (addr == next->vm_end) {
			/* case 1, 7, 8: copy all logical mappings from next to vma */
			XA_STATE(xas, next->vm_obj->logical_page_table,
				linear_page_index(next, next->vm_start));

			xa_lock(vma->vm_obj->logical_page_table);
			rcu_read_lock();
			xas_for_each(&xas, page, linear_page_index(next, next->vm_end - SZ_2M)) {
				index = xas.xa_index - next->vm_pgoff + vma->vm_pgoff +
						((next->vm_start - vma->vm_start) >> PAGE_SHIFT);
				__xa_store(vma->vm_obj->logical_page_table,
					index, page, GFP_KERNEL);
				xas_store(&xas, NULL);
				moved_pages++;
			}
			rcu_read_unlock();
			atomic_add(moved_pages, &vma->vm_obj->nr_pages);
			xa_unlock(vma->vm_obj->logical_page_table);
		} else if (next->vm_start < addr && addr < next->vm_end) {
			/* case 5: move logical mapping in [next->vm_start, end) from next to vma */
			XA_STATE(xas, next->vm_obj->logical_page_table,
				linear_page_index(next, next->vm_start));

			xa_lock(vma->vm_obj->logical_page_table);
			xa_lock(next->vm_obj->logical_page_table);
			xas_for_each(&xas, page, linear_page_index(next, addr - SZ_2M)) {
				index = xas.xa_index - next->vm_pgoff + vma->vm_pgoff +
						((next->vm_start - vma->vm_start) >> PAGE_SHIFT);
				__xa_store(vma->vm_obj->logical_page_table,
					index, page, GFP_KERNEL);
				xas_store(&xas, NULL);
				moved_pages++;
			}
			atomic_add(moved_pages, &vma->vm_obj->nr_pages);
			atomic_sub(moved_pages, &next->vm_obj->nr_pages);
			xa_unlock(next->vm_obj->logical_page_table);
			xa_unlock(vma->vm_obj->logical_page_table);
		} else if (n_next && addr == n_next->vm_end) {
			/* case 6: copy all logical mappings from next and n_next to vma */
			XA_STATE(xas_next, next->vm_obj->logical_page_table,
				linear_page_index(next, next->vm_start));
			XA_STATE(xas_n_next, n_next->vm_obj->logical_page_table,
				linear_page_index(n_next, n_next->vm_start));

			xa_lock(vma->vm_obj->logical_page_table);
			rcu_read_lock();

			xas_for_each(&xas_next, page,
				linear_page_index(next, next->vm_end - SZ_2M)) {
				index = xas_next.xa_index - next->vm_pgoff + vma->vm_pgoff +
						((next->vm_start - vma->vm_start) >> PAGE_SHIFT);
				__xa_store(vma->vm_obj->logical_page_table,
					index, page, GFP_KERNEL);
				xas_store(&xas_next, NULL);
				moved_pages++;
			}

			xas_for_each(&xas_n_next, page,
				linear_page_index(n_next, n_next->vm_end - SZ_2M)) {
				index = xas_n_next.xa_index - n_next->vm_pgoff + vma->vm_pgoff +
					((n_next->vm_start - vma->vm_start) >> PAGE_SHIFT);
				__xa_store(vma->vm_obj->logical_page_table,
					index, page, GFP_KERNEL);
				xas_store(&xas_n_next, NULL);
				moved_pages++;
			}

			rcu_read_unlock();
			atomic_add(moved_pages, &vma->vm_obj->nr_pages);
			xa_unlock(vma->vm_obj->logical_page_table);
		}
	}
	/* case 2, 3: do nothing */
}

void vm_object_adjust(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	/* remove logical mapping in [vma->vm_start, start) and [end, vm->vm_end) */
	unsigned long removed_pages = 0;
	struct gm_mapping *mapping;

	XA_STATE(xas, vma->vm_obj->logical_page_table,
		linear_page_index(vma, vma->vm_start));

	xas_lock(&xas);
	if (vma->vm_start < start) {
		xas_for_each(&xas, mapping, linear_page_index(vma, start)) {
			xas_store(&xas, NULL);
			removed_pages++;
		}
	}

	if (vma->vm_end > end) {
		xas_set(&xas, linear_page_index(vma, end));

		xas_for_each(&xas, mapping, linear_page_index(vma, vma->vm_end)) {
			xas_store(&xas, NULL);
			removed_pages++;
		}
	}
	atomic_sub(removed_pages, &vma->vm_obj->nr_pages);
	xas_unlock(&xas);
}

/*
 * Given a VA, the page_index is computed by
 * page_index = linear_page_index(struct vm_area_struct *vma, unsigned long address)
 */
struct gm_mapping *vm_object_lookup(struct vm_object *obj, unsigned long va)
{
	return lookup_gm_mapping(obj, linear_page_index(obj->vma, va));
}
EXPORT_SYMBOL_GPL(vm_object_lookup);

static inline void vm_object_mapping_create(struct vm_object *obj, unsigned long start)
{
	pgoff_t index = linear_page_index(obj->vma, start);
	struct gm_mapping *gm_mapping;

	gm_mapping = alloc_gm_mapping();
	if (!gm_mapping)
		return;

	__xa_store(obj->logical_page_table, index, gm_mapping, GFP_KERNEL);
}

void free_gm_mappings(struct vm_area_struct *vma)
{
	struct vm_object *obj = vma->vm_obj;
	struct gm_mapping *gm_mapping;

	if (!obj)
		return;

	XA_STATE(xas, obj->logical_page_table,
		linear_page_index(vma, vma->vm_start));

	xa_lock(obj->logical_page_table);
	xas_for_each(&xas, gm_mapping, linear_page_index(vma, vma->vm_end - SZ_2M)) {
		release_gm_mapping(gm_mapping);
		xas_store(&xas, NULL);
	}
	xa_unlock(obj->logical_page_table);
}

struct gm_mapping *vma_prepare_gm_mapping(struct vm_area_struct *vma,
					unsigned long haddr)
{
	struct vm_object *obj = vma->vm_obj;
	struct gm_mapping *gm_mapping;

	if (!obj)
		return NULL;

	xa_lock(obj->logical_page_table);
	gm_mapping = vm_object_lookup(obj, haddr);
	if (!gm_mapping) {
		vm_object_mapping_create(obj, haddr);
		gm_mapping = vm_object_lookup(obj, haddr);
	}
	xa_unlock(obj->logical_page_table);

	return gm_mapping;
}

void zap_logic_pmd_range(struct vm_area_struct *vma, unsigned long addr,
					unsigned long end, bool verify_pmd, pmd_t *pmd)
{
	struct vm_object *obj = vma->vm_obj;
	struct gm_mapping *gm_mapping = NULL;
	struct page *page = NULL;

	if (!vma_is_peer_shared(vma))
		return;
	if (verify_pmd && !pmd_none_or_clear_bad(pmd) && !pmd_trans_huge(*pmd))
		return;
	if (!obj)
		return;

	xa_lock(obj->logical_page_table);
	gm_mapping = vm_object_lookup(obj, addr);

	if (gm_mapping && gm_mapping_cpu(gm_mapping)) {
		page = gm_mapping->page;
		if (page && (page_ref_count(page) != 0)) {
			put_page(page);
			gm_mapping->page = NULL;
		}
	}
	xa_unlock(obj->logical_page_table);
}

void zap_logic_pud_range(struct vm_area_struct *vma,
					unsigned long addr,
					unsigned long end)
{
	unsigned long next;

	if (!vma_is_peer_shared(vma))
		return;
	do {
		next = pmd_addr_end(addr, end);
		zap_logic_pmd_range(vma, addr, next, false, NULL);
	} while (addr = next, addr != end);
}

void unmap_single_peer_shared_vma(struct mm_struct *mm, struct vm_area_struct *vma,
					 unsigned long start_addr, unsigned long end_addr)
{
	unsigned long start, end, addr;
	struct vm_object *obj = vma->vm_obj;
	struct gm_mapping *gm_mapping;
	struct hnode *hnode;

	start = max(vma->vm_start, start_addr);
	if (start >= vma->vm_end)
		return;
	addr = start;
	end = min(vma->vm_end, end_addr);
	if (end <= vma->vm_start)
		return;

	if (!obj)
		return;

	if (!mm->gm_as)
		return;

	do {
		xa_lock(obj->logical_page_table);
		gm_mapping = vm_object_lookup(obj, addr);
		if (!gm_mapping) {
			xa_unlock(obj->logical_page_table);
			continue;
		}
		xa_unlock(obj->logical_page_table);

		mutex_lock(&gm_mapping->lock);
		if (!gm_mapping_device(gm_mapping)) {
			mutex_unlock(&gm_mapping->lock);
			continue;
		}

		/*
		 * Regardless of whether the gm_page is unmapped, we should release it.
		 */
		hnode = get_hnode(gm_mapping->gm_page->hnid);
		if (!hnode) {
			mutex_unlock(&gm_mapping->lock);
			continue;
		}
		gm_page_remove_rmap(gm_mapping->gm_page);
		hnode_activelist_del(hnode, gm_mapping->gm_page);
		hnode_active_pages_dec(hnode);
		put_gm_page(gm_mapping->gm_page);
		gm_mapping->gm_page = NULL;
		mutex_unlock(&gm_mapping->lock);
	} while (addr += HPAGE_SIZE, addr != end);
}
