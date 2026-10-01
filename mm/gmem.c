// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generalized Memory Management.
 *
 * Copyright (C) 2023- Huawei, Inc.
 * Author: Weixi Zhu
 *
 */

#include <linux/dma-mapping.h>
#include <linux/dma-direct.h>
#include <linux/gmem.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/vm_object.h>
#include <linux/xarray.h>

#include "gmem-internal.h"

DEFINE_STATIC_KEY_FALSE(gmem_status);
EXPORT_SYMBOL_GPL(gmem_status);

static struct kmem_cache *gm_as_cache;
static struct kmem_cache *gm_dev_cache;
static struct kmem_cache *gm_ctx_cache;
static DEFINE_XARRAY_ALLOC(gm_dev_id_pool);

static bool enable_gmem __ro_after_init;

static inline unsigned long pe_mask(unsigned int order)
{
	if (order == 0)
		return PAGE_MASK;
	if (order == PMD_ORDER)
		return HPAGE_PMD_MASK;
	if (order == PUD_ORDER)
		return HPAGE_PUD_MASK;
	return 0;
}

static struct workqueue_struct *prefetch_wq;

#define GM_WORK_CONCURRENCY 4

static int __init gmem_init(void)
{
	int err = -ENOMEM;

	if (!enable_gmem)
		return 0;

	hnuma_init();

	gm_as_cache = KMEM_CACHE(gm_as, 0);
	if (!gm_as_cache)
		goto out;

	gm_dev_cache = KMEM_CACHE(gm_dev, 0);
	if (!gm_dev_cache)
		goto free_as;

	gm_ctx_cache = KMEM_CACHE(gm_context, 0);
	if (!gm_ctx_cache)
		goto free_dev;

	err = gm_page_cachep_init();
	if (err)
		goto free_ctx;

	err = vm_object_init();
	if (err)
		goto free_gm_page;

	err = gm_init_sysfs();
	if (err)
		goto free_vm_object;

	prefetch_wq = alloc_workqueue("prefetch",
		__WQ_LEGACY | WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE, GM_WORK_CONCURRENCY);
	if (!prefetch_wq) {
		gmem_err("fail to alloc workqueue prefetch_wq\n");
		err = -EFAULT;
		goto free_gm_sysfs;
	}

	static_branch_enable(&gmem_status);

	return 0;

free_gm_sysfs:
	gm_deinit_sysfs();
free_vm_object:
	vm_object_destroy();
free_gm_page:
	gm_page_cachep_destroy();
free_ctx:
	kmem_cache_destroy(gm_ctx_cache);
free_dev:
	kmem_cache_destroy(gm_dev_cache);
free_as:
	kmem_cache_destroy(gm_as_cache);
out:
	return -ENOMEM;
}
subsys_initcall(gmem_init);

static int __init setup_gmem(char *str)
{
	strtobool(str, &enable_gmem);

	return 1;
}
__setup("gmem=", setup_gmem);

/*
 * Create a GMEM device, register its MMU function and the page table.
 * The returned device pointer will be passed by new_dev.
 * A unique id will be assigned to the GMEM device, using Linux's xarray.
 */
int gm_dev_create(struct gm_mmu *mmu, void *dev_data,
				struct gm_dev **new_dev)
{
	struct gm_dev *dev;

	if (!gmem_is_enabled())
		return -EINVAL;

	dev = kmem_cache_alloc(gm_dev_cache, GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	if (xa_alloc(&gm_dev_id_pool, &dev->id, dev, xa_limit_32b,
			 GFP_KERNEL)) {
		kmem_cache_free(gm_dev_cache, dev);
		return -EAGAIN;
	}

	dev->mmu = mmu;
	dev->dev_data = dev_data;
	dev->current_ctx = NULL;
	INIT_LIST_HEAD(&dev->gm_ctx_list);
	*new_dev = dev;
	nodes_clear(dev->registered_hnodes);
	return 0;
}
EXPORT_SYMBOL_GPL(gm_dev_create);

/* Handle the page fault triggered by a given device with mmap lock*/
enum gm_ret gm_dev_fault_locked(struct mm_struct *mm, unsigned long  addr, struct gm_dev *dev,
				int behavior)
{
	enum gm_ret ret = GM_RET_SUCCESS;
	struct gm_mmu *mmu = dev->mmu;
	struct hnode *hnode;
	struct device *dma_dev = dev->dma_dev;
	struct vm_area_struct *vma;
	struct gm_mapping *gm_mapping;
	struct gm_page *gm_page;
	unsigned long size = HPAGE_SIZE;
	struct gm_fault_t gmf = {
		.mm = mm,
		.va = addr,
		.dev = dev,
		.size = size,
		.copy = false,
		.behavior = behavior
	};
	struct page *page = NULL;

	hnode = get_hnode(get_hnuma_id(dev));
	if (!hnode) {
		gmem_err("gmem device should correspond to a hnuma node");
		ret = -EINVAL;
		goto out;
	}

	vma = find_vma(mm, addr);
	if (!vma || vma->vm_start > addr) {
		gmem_err("%s failed to find vma", __func__);
		ret = GM_RET_FAILURE_UNKNOWN;
		goto out;
	}

	gm_mapping = vma_prepare_gm_mapping(vma, addr);
	if (unlikely(!gm_mapping)) {
		if (!vma->vm_obj) {
			gmem_err("%s no vm_obj", __func__);
			ret = GM_RET_FAILURE_UNKNOWN;
		} else {
			gmem_err("%s OOM when creating vm_obj!", __func__);
			ret = GM_RET_NOMEM;
		}
		goto out;
	}

	mutex_lock(&gm_mapping->lock);
	if (gm_mapping_nomap(gm_mapping)) {
		goto peer_map;
	} else if (gm_mapping_device(gm_mapping)) {
		switch (behavior) {
		case MADV_PINNED:
			mark_gm_page_pinned(gm_mapping->gm_page);
			fallthrough;
		case MADV_WILLNEED:
			mark_gm_page_active(gm_mapping->gm_page);
			goto unlock;
		case MADV_UNPINNED:
			mark_gm_page_unpinned(gm_mapping->gm_page);
			goto unlock;
		default:
			ret = 0;
			goto unlock;
		}
	} else if (gm_mapping_cpu(gm_mapping)) {
		page = gm_mapping->page;
		if (!page) {
			gmem_err("host gm_mapping page is NULL. Set nomap");
			gm_mapping_flags_set(gm_mapping, GM_MAPPING_NOMAP);
			goto unlock;
		}
		get_page(page);
		/* zap_page_range_single can be used in Linux 6.4 and later versions. */
		zap_page_range_single(vma, addr, size, NULL);
		gmf.dma_addr =
			dma_map_page(dma_dev, page, 0, size, DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dma_dev, gmf.dma_addr))
			gmem_err("dma map failed");

		gmf.copy = true;
	}

peer_map:
	gm_page = gm_alloc_page(mm, hnode);
	if (!gm_page) {
		gmem_err("Alloc gm_page for device fault failed.");
		ret = -ENOMEM;
		goto unlock;
	}

	gmf.pfn = gm_page->dev_pfn;

	ret = mmu->peer_map(&gmf);
	if (ret != GM_RET_SUCCESS) {
		gmem_err("peer map failed");
		if (page)
			gm_mapping_flags_set(gm_mapping, GM_MAPPING_CPU);
		put_gm_page(gm_page);
		goto unlock;
	}

	if (page) {
		dma_unmap_page(dma_dev, gmf.dma_addr, size, DMA_BIDIRECTIONAL);
		folio_put(page_folio(page));
	}

	gm_mapping_flags_set(gm_mapping, GM_MAPPING_DEVICE);
	gm_mapping->dev = dev;
	gm_page_add_rmap(gm_page, mm, addr);
	gm_mapping->gm_page = gm_page;

	if (behavior == MADV_PINNED)
		mark_gm_page_pinned(gm_page);
	else if (behavior == MADV_UNPINNED)
		mark_gm_page_unpinned(gm_page);

	hnode_activelist_add(hnode, gm_page);
	hnode_active_pages_inc(hnode);
unlock:
	mutex_unlock(&gm_mapping->lock);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(gm_dev_fault_locked);

vm_fault_t gm_host_fault_locked(struct vm_fault *vmf,
				unsigned int order)
{
	vm_fault_t ret = 0;
	struct vm_area_struct *vma = vmf->vma;
	unsigned long addr = vmf->address & pe_mask(order);
	struct vm_object *obj = vma->vm_obj;
	struct gm_mapping *gm_mapping;
	unsigned long size = HPAGE_SIZE;
	struct gm_dev *dev;
	struct hnode *hnode;
	struct device *dma_dev;
	struct gm_fault_t gmf = {
		.mm = vma->vm_mm,
		.va = addr,
		.size = size,
		.copy = true,
	};

	gm_mapping = vm_object_lookup(obj, addr);
	if (!gm_mapping) {
		gmem_err("host fault gm_mapping should not be NULL\n");
		return VM_FAULT_SIGBUS;
	}

	dev = gm_mapping->dev;
	gmf.dev = dev;
	gmf.pfn = gm_mapping->gm_page->dev_pfn;
	dma_dev = dev->dma_dev;
	gmf.dma_addr =
		dma_map_page(dma_dev, vmf->page, 0, size, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dma_dev, gmf.dma_addr)) {
		gmem_err("host fault dma mapping error\n");
		return VM_FAULT_SIGBUS;
	}
	if (dev->mmu->peer_unmap(&gmf) != GM_RET_SUCCESS) {
		gmem_err("peer unmap failed\n");
		dma_unmap_page(dma_dev, gmf.dma_addr, size, DMA_BIDIRECTIONAL);
		return VM_FAULT_SIGBUS;
	}

	dma_unmap_page(dma_dev, gmf.dma_addr, size, DMA_BIDIRECTIONAL);
	hnode = get_hnode(gm_mapping->gm_page->hnid);
	gm_page_remove_rmap(gm_mapping->gm_page);
	hnode_activelist_del(hnode, gm_mapping->gm_page);
	hnode_active_pages_dec(hnode);
	put_gm_page(gm_mapping->gm_page);
	return ret;
}

enum gm_ret gm_dev_fault(struct mm_struct *mm, unsigned long  addr,
				struct gm_dev *dev, int behavior)
{
	int ret;

	mmap_read_lock(mm);
	ret = gm_dev_fault_locked(mm, addr, dev, behavior);
	mmap_read_unlock(mm);

	return ret;
}
EXPORT_SYMBOL_GPL(gm_dev_fault);

/* GMEM Virtual Address Space API */
int gm_as_create(unsigned long begin, unsigned long end, enum gm_as_alloc policy,
			unsigned long cache_quantum, struct gm_as **new_as)
{
	struct gm_as *as;

	if (!new_as)
		return -EINVAL;

	as = kmem_cache_alloc(gm_as_cache, GFP_ATOMIC);
	if (!as)
		return -ENOMEM;

	spin_lock_init(&as->rbtree_lock);
	as->rbroot = RB_ROOT;
	as->start_va = begin;
	as->end_va = end;
	as->policy = policy;

	INIT_LIST_HEAD(&as->gm_ctx_list);

	*new_as = as;
	return 0;
}
EXPORT_SYMBOL_GPL(gm_as_create);

int gm_as_destroy(struct gm_as *as)
{
	struct gm_context *ctx, *tmp_ctx;

	list_for_each_entry_safe(ctx, tmp_ctx, &as->gm_ctx_list, gm_as_link)
		kfree(ctx);

	kmem_cache_free(gm_as_cache, as);

	return 0;
}
EXPORT_SYMBOL_GPL(gm_as_destroy);

int gm_as_attach(struct gm_as *as, struct gm_dev *dev,
			bool activate, struct gm_context **out_ctx)
{
	struct gm_context *ctx;
	int nid;

	ctx = kmem_cache_alloc(gm_ctx_cache, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->as = as;
	ctx->dev = dev;

	INIT_LIST_HEAD(&ctx->gm_dev_link);
	INIT_LIST_HEAD(&ctx->gm_as_link);

	if (!list_empty(&as->gm_ctx_list)) {
		struct list_head *old_node;
		struct gm_context *old_ctx;

		old_node = as->gm_ctx_list.prev;
		list_del_init(old_node);
		old_ctx = list_entry(old_node, struct gm_context, gm_as_link);
		kfree(old_ctx);
	}

	list_add_tail(&dev->gm_ctx_list, &ctx->gm_dev_link);
	list_add_tail(&ctx->gm_as_link, &as->gm_ctx_list);

	if (activate) {
		/*
		 * Here we should really have a callback function to perform the context switch
		 * for the hardware. E.g. in x86 this function is effectively
		 * flushing the CR3 value. Currently we do not care time-sliced context switch,
		 * unless someone wants to support it.
		 */
		dev->current_ctx = ctx;
	}
	*out_ctx = ctx;

	/*
	 * gm_as_attach will be used to attach device to process address space.
	 * Handle this case and add hnodes registered by device to process mems_allowed.
	 */
#ifdef CONFIG_CPUSETS
	for_each_node_mask(nid, dev->registered_hnodes)
		node_set(nid, current->mems_allowed);
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(gm_as_attach);

struct prefetch_data {
	struct mm_struct *mm;
	struct gm_dev *dev;
	unsigned long addr;
	size_t size;
	struct work_struct work;
	int behavior;
	int *res;
};

static void prefetch_work_cb(struct work_struct *work)
{
	struct prefetch_data *d =
		container_of(work, struct prefetch_data, work);
	unsigned long addr = d->addr, end = d->addr + d->size;
	int page_size = HPAGE_SIZE;
	int ret;

	do {
		/* MADV_WILLNEED: dev will soon access this addr. */
		mmap_read_lock(d->mm);
		ret = gm_dev_fault_locked(d->mm, addr, d->dev, d->behavior);
		mmap_read_unlock(d->mm);
		if (ret == GM_RET_PAGE_EXIST) {
			gmem_err("%s: device has done page fault, ignore prefetch\n",
				__func__);
		} else if (ret != GM_RET_SUCCESS) {
			*d->res = -EFAULT;
			gmem_err("%s: call dev fault error %d\n", __func__, ret);
		}
	} while (addr += page_size, addr != end);

	kfree(d);
}

static int hmadvise_do_prefetch(struct gm_dev *dev, unsigned long addr, size_t size, int behavior)
{
	unsigned long start, end, per_size;
	int page_size = HPAGE_SIZE;
	struct prefetch_data *data;
	struct vm_area_struct *vma;
	int res = GM_RET_SUCCESS;
	unsigned long old_start;

	/* overflow */
	if (check_add_overflow(addr, size, &end)) {
		gmem_err("addr plus size will cause overflow!\n");
		return -EINVAL;
	}

	old_start = end;

	/* Align addr by rounding outward to make page cover addr. */
	end = round_up(end, page_size);
	start = round_down(addr, page_size);
	size = end - start;

	if (!end && old_start) {
		gmem_err("end addr align up 2M causes invalid addr\n");
		return -EINVAL;
	}

	if (size == 0)
		return 0;

	mmap_read_lock(current->mm);
	vma = find_vma(current->mm, start);
	if (!vma || start < vma->vm_start || end > vma->vm_end) {
		mmap_read_unlock(current->mm);
		gmem_err("failed to find vma by invalid start or size.\n");
		return GM_RET_FAILURE_UNKNOWN;
	}  else if (!vma_is_peer_shared(vma)) {
		mmap_read_unlock(current->mm);
		gmem_err("%s the vma does not use VM_PEER_SHARED\n", __func__);
		return GM_RET_FAILURE_UNKNOWN;
	}
	mmap_read_unlock(current->mm);

	per_size = (size / GM_WORK_CONCURRENCY) & ~(page_size - 1);

	while (start < end) {
		data = kzalloc(sizeof(struct prefetch_data), GFP_KERNEL);
		if (!data) {
			flush_workqueue(prefetch_wq);
			return GM_RET_NOMEM;
		}

		INIT_WORK(&data->work, prefetch_work_cb);
		data->mm = current->mm;
		data->dev = dev;
		data->addr = start;
		data->behavior = behavior;
		data->res = &res;
		if (per_size == 0)
			data->size = size;
		else
			/* Process (1.x * per_size) for the last time */
			data->size = (end - start < 2 * per_size) ?
					     (end - start) :
					     per_size;
		queue_work(prefetch_wq, &data->work);
		start += data->size;
	}

	flush_workqueue(prefetch_wq);
	return res;
}

static int gmem_unmap_vma_pages(struct vm_area_struct *vma, unsigned long start,
				unsigned long end, int page_size)
{
	struct gm_fault_t gmf = {
		.mm = current->mm,
		.size = page_size,
		.copy = false,
	};
	struct gm_mapping *gm_mapping;
	struct vm_object *obj;
	struct hnode *hnode;
	enum gm_ret gm_ret;

	obj = vma->vm_obj;
	if (!obj) {
		gmem_err("peer-shared vma should have vm_object\n");
		return -EINVAL;
	}

	for (; start < end; start += page_size) {
		xa_lock(obj->logical_page_table);
		gm_mapping = vm_object_lookup(obj, start);
		if (!gm_mapping) {
			xa_unlock(obj->logical_page_table);
			continue;
		}
		xa_unlock(obj->logical_page_table);
		mutex_lock(&gm_mapping->lock);
		if (gm_mapping_nomap(gm_mapping)) {
			mutex_unlock(&gm_mapping->lock);
			continue;
		} else if (gm_mapping_cpu(gm_mapping)) {
			zap_page_range_single(vma, start, page_size, NULL);
		} else {
			gmf.va = start;
			gmf.dev = gm_mapping->dev;
			gm_ret = gm_mapping->dev->mmu->peer_unmap(&gmf);
			if (gm_ret) {
				gmem_err("peer_unmap failed. ret %d\n", gm_ret);
				mutex_unlock(&gm_mapping->lock);
				continue;
			}
			hnode = get_hnode(gm_mapping->gm_page->hnid);
			gm_page_remove_rmap(gm_mapping->gm_page);
			hnode_activelist_del(hnode, gm_mapping->gm_page);
			hnode_active_pages_dec(hnode);
			put_gm_page(gm_mapping->gm_page);
		}
		gm_mapping_flags_set(gm_mapping, GM_MAPPING_NOMAP);
		mutex_unlock(&gm_mapping->lock);
	}

	return 0;
}

static int hmadvise_do_eagerfree(unsigned long addr, size_t size)
{
	unsigned long start, end, i_start, i_end;
	int page_size = HPAGE_SIZE;
	struct vm_area_struct *vma;
	int ret = GM_RET_SUCCESS;
	unsigned long old_start;

	/* overflow */
	if (check_add_overflow(addr, size, &end)) {
		gmem_err("addr plus size will cause overflow!\n");
		return -EINVAL;
	}

	old_start = addr;

	/* Align addr by rounding inward to avoid excessive page release. */
	end = round_down(end, page_size);
	start = round_up(addr, page_size);
	if (start >= end) {
		pr_debug("gmem:start align up 2M >= end align down 2M.\n");
		return ret;
	}

	/* Check to see whether len was rounded up from small -ve to zero */
	if (old_start && !start) {
		gmem_err("start addr align up 2M causes invalid addr");
		return -EINVAL;
	}

	mmap_read_lock(current->mm);
	do {
		vma = find_vma_intersection(current->mm, start, end);
		if (!vma) {
			gmem_err("gmem: there is no valid vma\n");
			break;
		}

		if (!vma_is_peer_shared(vma)) {
			pr_debug("gmem:not peer-shared vma, skip dontneed\n");
			start = vma->vm_end;
			continue;
		}

		i_start = start > vma->vm_start ? start : vma->vm_start;
		i_end = end < vma->vm_end ? end : vma->vm_end;
		ret = gmem_unmap_vma_pages(vma, i_start, i_end, page_size);
		if (ret)
			break;

		start = vma->vm_end;
	} while (start < end);

	mmap_read_unlock(current->mm);
	return ret;
}

static bool check_hmadvise_behavior(int behavior)
{
	return behavior == MADV_DONTNEED;
}

int hmadvise_inner(int hnid, unsigned long start, size_t len_in, int behavior)
{
	int error = -EINVAL;
	struct gm_dev *dev = NULL;

	if (hnid == -1) {
		if (check_hmadvise_behavior(behavior)) {
			goto no_hnid;
		} else {
			gmem_err("hmadvise: behavior %d need hnid or is invalid\n",
				behavior);
			return error;
		}
	}

	if (hnid < 0) {
		gmem_err("hmadvise: invalid hnid %d < 0\n", hnid);
		return error;
	}

	if (!is_hnode(hnid)) {
		gmem_err("hmadvise: can't find hnode by hnid:%d or hnode is not allowed\n", hnid);
		return error;
	}

	dev = get_gm_dev(hnid);
	if (!dev) {
		gmem_err("hmadvise: hnode id %d is invalid\n", hnid);
		return error;
	}

no_hnid:
	switch (behavior) {
	case MADV_PREFETCH:
		behavior = MADV_WILLNEED;
		fallthrough;
	case MADV_UNPINNED:
		fallthrough;
	case MADV_PINNED:
		return hmadvise_do_prefetch(dev, start, len_in, behavior);
	case MADV_DONTNEED:
		return hmadvise_do_eagerfree(start, len_in);
	default:
		gmem_err("hmadvise: unsupported behavior %d\n", behavior);
	}

	return error;
}
EXPORT_SYMBOL_GPL(hmadvise_inner);

static bool hnid_match_dest(int hnid, struct gm_mapping *dest)
{
	return (hnid < 0) ? gm_mapping_cpu(dest) : gm_mapping_device(dest);
}

static void do_hmemcpy(struct mm_struct *mm, int hnid, unsigned long dest,
		unsigned long src, size_t size)
{
	enum gm_ret ret;
	int page_size = HPAGE_SIZE;
	struct vm_area_struct *vma_dest, *vma_src;
	struct gm_mapping *gm_mapping_dest, *gm_mapping_src;
	struct gm_dev *dev = NULL;
	struct gm_memcpy_t gmc = {0};

	if (size == 0)
		return;

	mmap_read_lock(mm);
	vma_dest = find_vma(mm, dest);
	vma_src = find_vma(mm, src);

	if (!vma_src || vma_src->vm_start > src || !vma_dest || vma_dest->vm_start > dest) {
		gmem_err("hmemcpy: the vma find by src/dest is NULL!");
		goto unlock_mm;
	}

	gm_mapping_dest = vm_object_lookup(vma_dest->vm_obj, dest & ~(page_size - 1));
	gm_mapping_src = vm_object_lookup(vma_src->vm_obj, src & ~(page_size - 1));

	if (!gm_mapping_src) {
		gmem_err("hmemcpy: gm_mapping_src is NULL");
		goto unlock_mm;
	}

	if (gm_mapping_nomap(gm_mapping_src)) {
		gmem_err("hmemcpy: src address is not mapping to CPU or device");
		goto unlock_mm;
	}

	if (hnid != -1) {
		dev = get_gm_dev(hnid);
		if (!dev) {
			gmem_err("hmemcpy: hnode's dev is NULL");
			goto unlock_mm;
		}
	}

	// Trigger dest page fault on host or device
	if (!gm_mapping_dest || gm_mapping_nomap(gm_mapping_dest)
		|| !hnid_match_dest(hnid, gm_mapping_dest)) {
		if (hnid == -1) {
			ret = handle_mm_fault(vma_dest, dest & ~(page_size - 1), FAULT_FLAG_USER |
						FAULT_FLAG_INSTRUCTION | FAULT_FLAG_WRITE, NULL);
			if (ret) {
				gmem_err("%s: failed to execute host page fault, ret:%d",
					__func__, ret);
				goto unlock_mm;
			}
		} else {
			ret = gm_dev_fault_locked(mm, dest & ~(page_size - 1), dev, MADV_WILLNEED);
			if (ret != GM_RET_SUCCESS) {
				gmem_err("%s: failed to excecute dev page fault.", __func__);
				goto unlock_mm;
			}
		}
	}
	if (!gm_mapping_dest)
		gm_mapping_dest = vm_object_lookup(vma_dest->vm_obj, round_down(dest, page_size));

	if (gm_mapping_dest && gm_mapping_dest != gm_mapping_src)
		mutex_lock(&gm_mapping_dest->lock);

	if (!mutex_trylock(&gm_mapping_src->lock)) {
		gmem_err("hmemcpy failed due to src/dest conflict");
		if (gm_mapping_dest && gm_mapping_dest != gm_mapping_src)
			mutex_unlock(&gm_mapping_dest->lock);
		goto unlock_mm;
	}
	if (gm_mapping_nomap(gm_mapping_src)) {
		gmem_err("hmemcpy: src address is not mapping to CPU or device");
		goto unlock_gm_mapping;
	}

	// Use memcpy when there is no device address, otherwise use peer_memcpy
	if (hnid == -1) {
		if (gm_mapping_cpu(gm_mapping_src)) { // host to host
			gmem_err("hmemcpy: host to host is unimplemented\n");
			goto unlock_gm_mapping;
		} else { // device to host
			dev = gm_mapping_src->dev;
			gmc.dest = dma_map_page(dev->dma_dev, gm_mapping_dest->page,
				 (dest & (page_size - 1)), page_size, DMA_BIDIRECTIONAL);
			if (dma_mapping_error(dev->dma_dev, gmc.dest)) {
				gmem_err("hmemcpy dma map failed");
				goto unlock_gm_mapping;
			}
			gmc.src = gm_mapping_src->gm_page->dev_dma_addr + (src & (page_size - 1));
			gmc.kind = GM_MEMCPY_D2H;
		}
	} else {
		if (gm_mapping_cpu(gm_mapping_src)) { // host to device
			gmc.dest = gm_mapping_dest->gm_page->dev_dma_addr +
						(dest & (page_size - 1));
			gmc.src = dma_map_page(dev->dma_dev, gm_mapping_src->page,
				 (src & (page_size - 1)), page_size, DMA_BIDIRECTIONAL);
			if (dma_mapping_error(dev->dma_dev, gmc.src)) {
				gmem_err("hmemcpy dma map failed");
				goto unlock_gm_mapping;
			}
			gmc.kind = GM_MEMCPY_H2D;
		} else { // device to device
			gmem_err("hmemcpy: device to device is unimplemented\n");
			goto unlock_gm_mapping;
		}
	}
	gmc.mm = mm;
	gmc.dev = dev;
	gmc.size = size;
	dev->mmu->peer_hmemcpy(&gmc);

	if (hnid == -1)
		dma_unmap_page(dev->dma_dev, gmc.dest, page_size, DMA_BIDIRECTIONAL);
	else
		dma_unmap_page(dev->dma_dev, gmc.src, page_size, DMA_BIDIRECTIONAL);

unlock_gm_mapping:
	mutex_unlock(&gm_mapping_src->lock);
	if (gm_mapping_dest && gm_mapping_dest != gm_mapping_src)
		mutex_unlock(&gm_mapping_dest->lock);
unlock_mm:
	mmap_read_unlock(mm);
}

/*
 * Each page needs to be copied in three parts when the address is not aligned.
 * |      ml <--0-->|<1><--2->       |
 * |         -------|---------       |
 * |        /      /|  /     /       |
 * |       /      / | /     /        |
 * |      /      /  |/     /         |
 * |      ----------|------          |
 * |                |                |
 * |<----page x---->|<----page y---->|
 */

static void __hmemcpy(int hnid, unsigned long dest, unsigned long src, size_t size)
{
	int i = 0;
	// offsets within the huge page for the source and destination addresses
	int src_offset = src & (HPAGE_SIZE - 1);
	int dst_offset = dest & (HPAGE_SIZE - 1);
	// Divide each page into three parts according to the align
	int ml[3] = {
		HPAGE_SIZE - (src_offset < dst_offset ? dst_offset : src_offset),
		src_offset < dst_offset ? (dst_offset - src_offset) : (src_offset - dst_offset),
		src_offset < dst_offset ? src_offset : dst_offset
	};
	struct mm_struct *mm = current->mm;

	if (size == 0)
		return;

	while (size >= ml[i]) {
		if (ml[i] > 0) {
			do_hmemcpy(mm, hnid, dest, src, ml[i]);
			src += ml[i];
			dest += ml[i];
			size -= ml[i];
		}
		i = (i + 1) % 3;
	}

	if (size > 0)
		do_hmemcpy(mm, hnid, dest, src, size);
}

int hmemcpy(int hnid, unsigned long dest, unsigned long src, size_t size)
{
	struct vm_area_struct *vma_dest, *vma_src;
	struct mm_struct *mm = current->mm;

	if (hnid < 0) {
		if (hnid != -1) {
			gmem_err("%s: invalid hnid %d < 0\n", __func__, hnid);
			return -EINVAL;
		}
	} else if (!is_hnode(hnid)) {
		gmem_err("%s: can't find hnode by hnid:%d or hnode is not allowed\n",
			__func__, hnid);
		return -EINVAL;
	}

	mmap_read_lock(mm);
	vma_dest = find_vma(mm, dest);
	vma_src = find_vma(mm, src);

	if ((ULONG_MAX - size < src) || !vma_src || vma_src->vm_start > src ||
		!vma_is_peer_shared(vma_src) || vma_src->vm_end < (src + size)) {
		gmem_err("failed to find peer_shared vma by invalid src or size\n");
		goto unlock;
	}

	if ((ULONG_MAX - size < dest) || !vma_dest || vma_dest->vm_start > dest ||
		!vma_is_peer_shared(vma_dest) || vma_dest->vm_end < (dest + size)) {
		gmem_err("failed to find peer_shared vma by invalid dest or size\n");
		goto unlock;
	}

	if (!(vma_dest->vm_flags & VM_WRITE)) {
		gmem_err("dest is not writable.\n");
		goto unlock;
	}
	mmap_read_unlock(mm);

	__hmemcpy(hnid, dest, src, size);

	return 0;

unlock:
	mmap_read_unlock(mm);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(hmemcpy);
