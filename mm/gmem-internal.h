/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _GMEM_INTERNAL_H
#define _GMEM_INTERNAL_H

#include <linux/gmem.h>

#ifdef CONFIG_GMEM

/* h-NUMA topology */
struct hnode {
	unsigned int id;
	struct gm_dev *dev;

	struct task_struct *swapd_task;

	struct list_head freelist;
	struct list_head activelist;
	spinlock_t freelist_lock;
	spinlock_t activelist_lock;
	spinlock_t lock;
	atomic_t nr_free_pages;
	atomic_t nr_active_pages;

	unsigned long max_memsize;

	bool import_failed;
};

static inline void hnode_active_pages_inc(struct hnode *hnode)
{
	atomic_inc(&hnode->nr_active_pages);
}

static inline void hnode_active_pages_dec(struct hnode *hnode)
{
	atomic_dec(&hnode->nr_active_pages);
}

static inline void hnode_free_pages_inc(struct hnode *hnode)
{
	atomic_inc(&hnode->nr_free_pages);
}

static inline void hnode_free_pages_dec(struct hnode *hnode)
{
	atomic_dec(&hnode->nr_free_pages);
}

static inline int get_hnuma_id(struct gm_dev *gm_dev)
{
	return first_node(gm_dev->registered_hnodes);
}

/*
 * @GM_MAPPING_CPU: peer-shared page in gm_mapping is on host side
 * @GM_MAPPING_DEVICE：peer-shared page in gm_mapping is on device side
 * @GM_MAPPING_NOMAP：no peer-shared page in gm_mapping
 */
#define GM_MAPPING_CPU		0x10
#define GM_MAPPING_DEVICE	0x20
#define GM_MAPPING_NOMAP	0x40

#define GM_MAPPING_TYPE_MASK	(GM_MAPPING_CPU \
				|GM_MAPPING_DEVICE \
				| GM_MAPPING_NOMAP)


static inline void gm_mapping_flags_set(struct gm_mapping *gm_mapping,
				int flags)
{
	if (flags & GM_MAPPING_TYPE_MASK)
		gm_mapping->flag &= ~GM_MAPPING_TYPE_MASK;

	gm_mapping->flag |= flags;
}

static inline void gm_mapping_flags_clear(struct gm_mapping *gm_mapping,
				int flags)
{
	gm_mapping->flag &= ~flags;
}

static inline bool gm_mapping_cpu(struct gm_mapping *gm_mapping)
{
	return !!(gm_mapping->flag & GM_MAPPING_CPU);
}

static inline bool gm_mapping_device(struct gm_mapping *gm_mapping)
{
	return !!(gm_mapping->flag & GM_MAPPING_DEVICE);
}

static inline bool gm_mapping_nomap(struct gm_mapping *gm_mapping)
{
	return !!(gm_mapping->flag & GM_MAPPING_NOMAP);
}

void __init hnuma_init(void);
bool is_hnode(int nid);
unsigned int alloc_hnode_id(void);
void free_hnode_id(unsigned int nid);
struct hnode *get_hnode(unsigned int hnid);
struct gm_dev *get_gm_dev(unsigned int nid);
void hnode_init(struct hnode *hnode, unsigned int hnid, struct gm_dev *dev);
void hnode_deinit(unsigned int hnid, struct gm_dev *dev);

#define GM_PAGE_EVICTING	0x1
#define GM_PAGE_PINNED		0x2

static inline void gm_page_flags_set(struct gm_page *gm_page, int flags)
{
	gm_page->flag |= flags;
}

static inline void gm_page_flags_clear(struct gm_page *gm_page, int flags)
{
	gm_page->flag &= ~flags;
}

static inline bool gm_page_evicting(struct gm_page *gm_page)
{
	return !!(gm_page->flag & GM_PAGE_EVICTING);
}

static inline bool gm_page_pinned(struct gm_page *gm_page)
{
	return !!(gm_page->flag & GM_PAGE_PINNED);
}

void mark_gm_page_pinned(struct gm_page *gm_page);
void mark_gm_page_active(struct gm_page *gm_page);
void mark_gm_page_unpinned(struct gm_page *gm_page);

int __init gm_page_cachep_init(void);
void gm_page_cachep_destroy(void);

void hnode_freelist_add(struct hnode *hnode, struct gm_page *gm_page);
void hnode_activelist_add(struct hnode *hnode, struct gm_page *gm_page);
void hnode_activelist_del(struct hnode *hnode, struct gm_page *gm_page);
void hnode_activelist_del_and_add(struct hnode *hnode,
				struct gm_page *gm_page);

void gm_page_add_rmap(struct gm_page *gm_page,
				struct mm_struct *mm, unsigned long va);
void gm_page_remove_rmap(struct gm_page *gm_page);
void gm_free_page(struct gm_page *gm_page);
struct gm_page *gm_alloc_page(struct mm_struct *mm, struct hnode *hnode);

static inline void get_gm_page(struct gm_page *gm_page)
{
	atomic_inc(&gm_page->refcount);
}

static inline void put_gm_page(struct gm_page *gm_page)
{
	if (atomic_dec_and_test(&gm_page->refcount))
		gm_free_page(gm_page);
}

int hnode_init_sysfs(unsigned int hnid);
int __init gm_init_sysfs(void);
void gm_deinit_sysfs(void);


vm_fault_t do_peer_shared_anonymous_page(struct vm_fault *vmf);
unsigned long gm_vm_mmap_pgoff(struct file *file, unsigned long addr,
				unsigned long len, unsigned long prot,
				unsigned long flag, unsigned long pgoff);

vm_fault_t gm_host_fault_locked(struct vm_fault *vmf, unsigned int order);
unsigned long gmem_unmap_align(struct mm_struct *mm,
				unsigned long start, size_t len);
void gmem_unmap_region(struct mm_struct *mm, unsigned long start, size_t len);
bool gm_mmap_check_flags(unsigned long flags);
static inline bool check_peer_shared_vma_thp_disabled(struct vm_area_struct *vma)
{
	return vma_is_peer_shared(vma) && vma_thp_disabled(vma, vma->vm_flags);
}

static inline unsigned long gm_align_vma_range(unsigned long addr, bool align_down)
{
	return align_down ? ALIGN_DOWN(addr, HPAGE_SIZE) : ALIGN(addr, HPAGE_SIZE);
}

static inline unsigned long gm_round_up(unsigned long length)
{
	return round_up(length, HPAGE_SIZE);
}

unsigned long
gm_get_unmapped_area_aligned(struct file *file, unsigned long addr, unsigned long len,
				unsigned long pgoff, unsigned long flags);
void destroy_gm_as(struct mm_struct *mm);

#else
static inline
vm_fault_t do_peer_shared_anonymous_page(struct vm_fault *vmf) { return 0; }
static inline unsigned long gmem_unmap_align(struct mm_struct *mm,
				unsigned long start, size_t len) { return 0; }
static inline void gmem_unmap_region(struct mm_struct *mm,
				unsigned long start, size_t len) {}
static inline
unsigned long gm_vm_mmap_pgoff(struct file *file, unsigned long addr,
				unsigned long len, unsigned long prot,
				unsigned long flag, unsigned long pgoff)
{
	return 0;
}
static inline unsigned long gm_round_up(unsigned long length) { return length; }
static inline bool check_peer_shared_vma_thp_disabled(struct vm_area_struct *vma) { return false; }
static inline unsigned long gm_align_vma_range(unsigned long addr, bool dummy) { return addr; }
static inline bool gm_mmap_check_flags(unsigned long flags) { return true; }
static inline unsigned long
gm_get_unmapped_area_aligned(struct file *file, unsigned long addr, unsigned long len,
				unsigned long pgoff, unsigned long flags) { return 0; }
static inline void destroy_gm_as(struct mm_struct *mm) {}
#endif /* CONFIG_GMEM */

#endif /* _GMEM_INTERNAL_H */
