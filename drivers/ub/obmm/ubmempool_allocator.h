/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * Description：OBMM Framework's implementations.
 */
#ifndef UBMEMPOOL_ALLOCATOR_H
#define UBMEMPOOL_ALLOCATOR_H

#include "obmm_core.h"

void free_memory_contiguous(struct mem_description_pool *desc);

int allocate_memory_contiguous(uint64_t size[], int length, struct mem_description_pool *desc,
			       bool zero, bool allow_slow);

size_t ubmempool_contract(int nid, bool is_hugepage);

int ubmempool_allocator_init(void);
void ubmempool_allocator_exit(void);

#endif
