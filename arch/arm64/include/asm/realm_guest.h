/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025. Huawei Technologies Co., Ltd. All rights reserved.
 */
#ifndef __REALM_GUEST_H
#define __REALM_GUEST_H

void enable_swiotlb_for_realm_dev(struct device *dev, bool enable);

void realm_guest_init(void);

struct page *realm_alloc_swiotlb_shared_pages(gfp_t gfp, unsigned int order);

bool realm_free_swiotlb_shared_pages(void *addr, unsigned int order);

#ifdef CONFIG_HISI_CCADA_GUEST
int ccada_init_mem_region(struct pci_dev *pdev, int bar);
#endif

#endif /* __REALM_GUEST_H */
