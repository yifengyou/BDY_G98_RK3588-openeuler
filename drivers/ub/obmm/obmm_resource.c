// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright(c) Huawei Technologies Co., Ltd. 2025 All rights reserved.
 * Description: OBMM Framework's implementations.
 *
 * OBMM utilizes the iomem resource tree infrastructure to expose the physical address range of each
 * OBMM memory device to other kernel components. External accessors should never modify the
 * resource tree structure (with or without resource lock) and should take the resource lock while
 * traversing the resource tree edges. "walk_iomem_res_desc" declared in <linux/ioport.h> serves as
 * a valid accessing candidate.
 *
 * Resource Tree Structure:
 *
 * OBMM introduces two layers in the iomem resource tree:
 *
 *   1. The UBMEM resource: The UBMEM resource models a range of UB memory physical address range.
 *      The range of memory maps or may map remote memory. It is always a direct child of the iomem
 *      resource root node.
 *
 *   2. The OBMM memory device: The OBMM memory device resource models a range of UB memory physical
 *      address range which is associated with an OBMM memory device. It is always a leaf of the
 *      iomem resource tree.
 *
 * If the imported memory is manged with remote NUMA, there might an extra interior layers between
 * the two metioned above. In our context we refer to it as NUMA resource.
 *
 * Below is an example:
 *
 *   (iomem_resource)
 *      PREIMPORT_UBMEM
 *        System RAM (Remote)
 *          MEMID_1
 *          MEMID_2
 *      DIRECT_IMPORT_UBMEM
 *        System RAM (Remote)
 *          MEMID_3
 *      DIRECT_IMPORT_UBMEM
 *        MEMID_4
 *
 * Things become complicated when we are handling the removal of a memory device which shares the
 * preimport UBMEM resource with memory devices which outlives itself. Current NUMA remote
 * implementation would remove the "System RAM (Remote)" resource first and re-insert the resource
 * afterwards. The living memory devices would not be preserved. Therefore it is necessary to save
 * all the memory device descendents before shutting down the part of the preimport memory.
 *
 * Concurrency Notes:
 *
 * As metioned in the beginning, for external accessors, everything under ubmem_resource in the
 * iomem_resource tree might be read with kernel resource_lock but should never be modified (even
 * with the lock). The only exception would be memory hotplug / NUMA remote setup process which is
 * triggered by OBMM. With this presumption it is safe for OBMM itself to traverse the resource tree
 * without kernel resource lock. On contrast, all modifications to the subtree takes the kernel
 * resource lock to avoid racing with external readers. Lastly, there is a mutex per UBMEM resource
 * which synchronizes internal accesses to the subtree.
 */

#define pr_fmt(fmt) "OBMM: resource:" fmt

#include <linux/slab.h>
#include <linux/mutex.h>

#include "obmm_resource.h"

#define MEMID_IORES_PREFIX "MEMID_"

struct ubmem_resource {
	struct resource res;
	bool preimport;

	/* serialize the children save-restore process (only necessary for preimport range) */
	struct mutex mutex;
	struct resource *memdev_res_shelter;
};

struct ubmem_resource *setup_ubmem_resource(phys_addr_t pa, resource_size_t size, bool preimport)
{
	int ret;
	struct ubmem_resource *ubmem_res;

	ubmem_res = kzalloc(sizeof(struct ubmem_resource), GFP_KERNEL);
	if (!ubmem_res)
		return ERR_PTR(-ENOMEM);

	ubmem_res->res.start = pa;
	ubmem_res->res.end = pa + size - 1;
	ubmem_res->res.name = preimport ? "PREIMPORT_UBMEM" : "DIRECT_IMPORT_UBMEM";
	ubmem_res->res.flags = IORESOURCE_MEM;

	ubmem_res->preimport = preimport;
	mutex_init(&ubmem_res->mutex);

	ret = insert_resource(&iomem_resource, &ubmem_res->res);
	if (ret) {
		kfree(ubmem_res);
		return ERR_PTR(ret);
	}
	return ubmem_res;
}

int release_ubmem_resource(struct ubmem_resource *ubmem_res)
{
	int ret;

	ret = remove_resource(&ubmem_res->res);
	if (ret)
		return ret;
	mutex_destroy(&ubmem_res->mutex);
	kfree(ubmem_res);
	return 0;
}

/*
 * Move memdev_res saved in the sheltered list back under the refreshed NUMA resource. This function
 * should be called only when the NUMA resource is present.
 */
static void restore_sheltered_memdev_locked(struct ubmem_resource *ubmem_res)
{
	struct resource *numa_res, *memdev_res;

	numa_res = ubmem_res->res.child;

	memdev_res = ubmem_res->memdev_res_shelter;
	while (memdev_res) {
		ubmem_res->memdev_res_shelter = memdev_res->sibling;

		memdev_res->sibling = NULL;
		WARN_ON(request_resource(numa_res, memdev_res));

		memdev_res = ubmem_res->memdev_res_shelter;
	}
}

/*
 * Take memory device resource under the NUMA resource to be reset and chain them in the sheltered
 * list
 */
int lock_save_memdev_descendents(struct ubmem_resource *ubmem_res)
{
	int ret;
	struct resource *numa_res, *memdev_res, *next, **shelter_tail;

	if (!ubmem_res->preimport)
		return 0;

	mutex_lock(&ubmem_res->mutex);

	numa_res = ubmem_res->res.child;
	if (!numa_res)
		return 0;
	WARN_ON(numa_res->sibling != NULL);

	memdev_res = numa_res->child;
	shelter_tail = &ubmem_res->memdev_res_shelter;
	while (memdev_res) {
		next = memdev_res->sibling;

		ret = release_resource(memdev_res);
		if (ret) {
			pr_err("failed to remove memdev resource %s: unexpected racing happened.\n",
			       memdev_res->name ? memdev_res->name : "(null)");
			goto out_restore;
		}
		memdev_res->child = memdev_res->parent = memdev_res->sibling = NULL;
		*shelter_tail = memdev_res;

		shelter_tail = &memdev_res->sibling;
		memdev_res = next;
	}
	return 0;

out_restore:
	restore_sheltered_memdev_locked(ubmem_res);
	mutex_unlock(&ubmem_res->mutex);
	return ret;
}

void restore_unlock_memdev_descendents(struct ubmem_resource *ubmem_res)
{
	if (!ubmem_res->preimport)
		return;

	restore_sheltered_memdev_locked(ubmem_res);
	mutex_unlock(&ubmem_res->mutex);
}

struct resource *setup_memdev_resource(struct ubmem_resource *ubmem_res, phys_addr_t pa,
				       resource_size_t size, int mem_id)
{
	int ret;
	struct resource *memdev_res, *parent;

	memdev_res = kzalloc(sizeof(struct resource), GFP_KERNEL);
	if (!memdev_res)
		return ERR_PTR(-ENOMEM);

	memdev_res->start = pa;
	memdev_res->end = pa + size - 1;
	memdev_res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
	memdev_res->name = kasprintf(GFP_KERNEL, MEMID_IORES_PREFIX "%d", mem_id);
	if (!memdev_res->name) {
		ret = -ENOMEM;
		goto err_free_res;
	}

	/* Be a descendent of the UBMEM resource */
	parent = &ubmem_res->res;
	mutex_lock(&ubmem_res->mutex);

	/* if NUMA resource is present, make itself a child of the NUMA resource */
	if (parent->child)
		parent = parent->child;

	ret = request_resource(parent, memdev_res);
	if (ret) {
		pr_err("failed to request resource under parent %s, ret=%pe.\n", parent->name,
		       ERR_PTR(ret));
		goto err_unlock;
	}

	mutex_unlock(&ubmem_res->mutex);
	return memdev_res;

err_unlock:
	mutex_unlock(&ubmem_res->mutex);
	kfree(memdev_res->name);
err_free_res:
	kfree(memdev_res);
	return ERR_PTR(ret);
}

int release_memdev_resource(struct ubmem_resource *ubmem_res, struct resource *memdev_res)
{
	int ret;

	mutex_lock(&ubmem_res->mutex);
	ret = release_resource(memdev_res);
	mutex_unlock(&ubmem_res->mutex);

	if (ret)
		return ret;
	kfree(memdev_res->name);
	kfree(memdev_res);
	return 0;
}
