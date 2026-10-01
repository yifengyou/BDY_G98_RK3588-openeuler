/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
 */
#ifndef OBMM_EXPORT_REGION_H
#define OBMM_EXPORT_REGION_H

#include "obmm_core.h"

int flush_export_region(struct obmm_export_region *e_reg, unsigned long offset,
			unsigned long length, unsigned long cache_ops);
int kernel_pgtable_set_export_invalid(struct obmm_export_region *e_reg, unsigned long offset,
				      unsigned long length, bool set_nc);
int map_export_region(struct vm_area_struct *vma, struct obmm_export_region *e_reg,
		      enum obmm_mmap_granu mmap_granu);

int get_pa_detail_export_region(const struct obmm_export_region *e_reg, unsigned long pa,
				struct obmm_ext_addr *ext_addr);

int get_offset_detail_export_region(const struct obmm_export_region *e_reg, unsigned long offset,
				    struct obmm_ext_addr *ext_addr);

#endif
