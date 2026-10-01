// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright(c) 2025 HiSilicon Technologies CO., All rights reserved.
 * Description: Creating user-mode page tables by user va information.
 */

#define pr_fmt(fmt) "[UMMU_CORE][MATT]: " fmt

#include <uapi/linux/ummu_core.h>
#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include "ummu_core_priv.h"

static struct mm_struct *get_tid_mm(u32 tid)
{
	guard(mutex)(&global_device_lock);
	if (!global_core_device)
		return NULL;

	return ummu_core_get_mm(global_core_device, tid);
}

static struct device *get_sva_separated_dev(u32 tid)
{
	guard(mutex)(&global_device_lock);
	if (!global_core_device)
		return NULL;

	return ummu_core_get_device(global_core_device, tid);
}

static bool check_tid_mm_matched(struct ummu_matt_domain *matt_domain)
{
	struct mm_struct *mm;

	if (matt_domain->l_tid == UMMU_INVALID_TID &&
	    matt_domain->r_tid == UMMU_INVALID_TID) {
		pr_debug("l_tid and r_tid are invalid.\n");
		return false;
	}

	if (matt_domain->l_tid != UMMU_INVALID_TID) {
		mm = get_tid_mm(matt_domain->l_tid);
		if (mm != matt_domain->mm) {
			pr_debug("l_tid = %u mm not matched.\n", matt_domain->l_tid);
			return false;
		}
	}

	if (matt_domain->r_tid != UMMU_INVALID_TID) {
		mm = get_tid_mm(matt_domain->r_tid);
		if (mm != matt_domain->mm) {
			pr_debug("r_tid = %u mm not matched.\n", matt_domain->r_tid);
			return false;
		}
	}

	return true;
}

static int sva_matt_inner_map(u32 tid, unsigned long addr, struct sg_table *sgt,
			      int prot, size_t *mapped)
{
	struct iommu_domain *domain;
	int iommu_prot = prot;
	struct device *dev;
	ssize_t size;
	int ret = 0;

	dev = get_sva_separated_dev(tid);
	if (!dev)
		return -ENODEV;

	iommu_prot |= dev_is_dma_coherent(dev) ? IOMMU_CACHE : 0;

	domain = iommu_get_domain_for_dev(dev);
	if (!domain) {
		ret = -ENOENT;
		goto out;
	}

	size = iommu_map_sgtable(domain, addr, sgt, iommu_prot);
	if (size < 0) {
		pr_err("tid = %u map matt failed\n", tid);
		ret = -EFAULT;
		goto out;
	}
	*mapped = size;

out:
	ummu_core_put_device(dev);
	return ret;
}

static void sva_matt_inner_unmap(u32 tid, unsigned long addr, size_t size)
{
	struct iommu_domain *domain;
	struct device *dev;
	size_t unmapped;

	dev = get_sva_separated_dev(tid);
	if (!dev)
		return;

	domain = iommu_get_domain_for_dev(dev);
	if (!domain)
		goto out;

	unmapped = iommu_unmap(domain, addr, size);
	if (unmapped != size)
		pr_err("tid = %u unmap matt failed\n", tid);
out:
	ummu_core_put_device(dev);
}

int ummu_sva_matt_map(struct ummu_matt_domain *matt_domain, unsigned long addr,
		      struct sg_table *sgt, int prot)
{
	ssize_t l_mapped, r_mapped;
	int ret;

	if (unlikely(!matt_domain || !sgt))
		return -EINVAL;

	if (!check_tid_mm_matched(matt_domain))
		return -EPERM;

	if (matt_domain->l_tid != UMMU_INVALID_TID) {
		ret = sva_matt_inner_map(matt_domain->l_tid,
					 addr, sgt, prot, &l_mapped);
		if (ret)
			return ret;
	}

	if (matt_domain->r_tid != UMMU_INVALID_TID) {
		ret = sva_matt_inner_map(matt_domain->r_tid,
					 addr, sgt, prot, &r_mapped);
		if (ret)
			goto err_out;
	}

	return 0;
err_out:
	if (matt_domain->l_tid != UMMU_INVALID_TID)
		sva_matt_inner_unmap(matt_domain->l_tid, addr, l_mapped);

	return ret;
}
EXPORT_SYMBOL_GPL(ummu_sva_matt_map);

int ummu_sva_matt_unmap(struct ummu_matt_domain *matt_domain,
			unsigned long addr, size_t size)
{
	if (unlikely(!matt_domain))
		return -EINVAL;

	if (!check_tid_mm_matched(matt_domain))
		return -EPERM;

	if (matt_domain->l_tid != UMMU_INVALID_TID)
		sva_matt_inner_unmap(matt_domain->l_tid, addr, size);

	if (matt_domain->r_tid != UMMU_INVALID_TID)
		sva_matt_inner_unmap(matt_domain->r_tid, addr, size);

	return 0;
}
EXPORT_SYMBOL_GPL(ummu_sva_matt_unmap);
