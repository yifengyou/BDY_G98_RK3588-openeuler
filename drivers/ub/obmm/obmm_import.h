/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
 */
#ifndef OBMM_IMPORT_H
#define OBMM_IMPORT_H

#include "obmm_core.h"

int obmm_import(struct obmm_cmd_import *cmd_import);
int obmm_unimport(const struct obmm_cmd_unimport *cmd_unimport);

int flush_import_region(struct obmm_import_region *i_reg, unsigned long offset,
			unsigned long length, unsigned long cache_ops);
int map_import_region(struct vm_area_struct *vma, struct obmm_import_region *i_reg,
		      enum obmm_mmap_granu mmap_granu);

int get_pa_detail_import(const struct obmm_import_region *i_reg, unsigned long pa,
			 struct obmm_ext_addr *ext_addr);

int get_offset_detail_import(const struct obmm_import_region *i_reg, unsigned long offset,
			     struct obmm_ext_addr *ext_addr);

#endif
