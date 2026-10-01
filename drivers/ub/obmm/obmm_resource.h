/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright(c) Huawei Technologies Co., Ltd. 2025 All rights reserved.
 * Description: OBMM Framework's implementations.
 */
#ifndef OBMM_RESOURCE_H
#define OBMM_RESOURCE_H

#include <linux/ioport.h>

struct ubmem_resource;

struct ubmem_resource *setup_ubmem_resource(phys_addr_t pa, resource_size_t size, bool preimport);
int release_ubmem_resource(struct ubmem_resource *ubmem_res);
int lock_save_memdev_descendents(struct ubmem_resource *ubmem_res);
void restore_unlock_memdev_descendents(struct ubmem_resource *ubmem_res);

struct resource *setup_memdev_resource(struct ubmem_resource *ubmem_res, phys_addr_t pa,
				       resource_size_t size, int mem_id);
int release_memdev_resource(struct ubmem_resource *ubmem_res, struct resource *memdev_res);

#endif
