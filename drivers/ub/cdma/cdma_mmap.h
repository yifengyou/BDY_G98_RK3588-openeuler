/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef __CDMA_MMAP_H__
#define __CDMA_MMAP_H__

#include <linux/sched/mm.h>
#include "cdma_types.h"

void cdma_unmap_vma_pages(struct cdma_file *cfile);
const struct vm_operations_struct *cdma_get_umap_ops(void);
void cdma_umap_priv_init(struct cdma_umap_priv *priv, struct vm_area_struct *vma);

#endif /* __CDMA_MMAP_H__ */
