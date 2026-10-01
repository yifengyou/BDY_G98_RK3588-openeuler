/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 * Description：OBMM Framework's implementations.
 */

#ifndef OBMM_EXPORT_C_H
#define OBMM_EXPORT_C_H
int obmm_export_common(struct obmm_export_region *e_reg);

int export_flags_to_region_flags(unsigned long *region_flags, unsigned long user_flags);

int alloc_export_memory_pid(struct obmm_export_region *e_reg);
void free_export_memory_pid(struct obmm_export_region *e_reg);
int alloc_export_memory_pool(struct obmm_export_region *e_reg);
int obmm_unexport_common(struct obmm_export_region *e_reg);
int obmm_export_from_pool(struct obmm_cmd_export *cmd_export);
int obmm_export_pid(struct obmm_cmd_export_pid *export_pid);
int obmm_unexport(const struct obmm_cmd_unexport *cmd_unexport);

int set_export_vendor(struct obmm_export_region *e_reg, const void __user *vendor_info,
		      unsigned int vendor_len);
void free_export_region(struct obmm_export_region *e_reg);
#endif
