// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: ODF get fdt info
 * Author: mengkanglai
 * Create: 2025-04-18
 */
#include <asm/unaligned.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <linux/byteorder/generic.h>
#include <linux/printk.h>

int odf_get_fdt_ubiostbl(u64 *phys_addr, char *tbl)
{
	int node, len;
	const void *prop;

	node = fdt_path_offset(initial_boot_params, "/chosen");
	if (node < 0) {
		pr_err("failed to get device tree chosen node\n");
		return -EINVAL;
	}
	prop = fdt_getprop(initial_boot_params, node, tbl, &len);
	if (!prop) {
		pr_err("failed to get property\n");
		return -EINVAL;
	}
	*phys_addr = (len == 4) ? (u64)be32_to_cpup((const u32 *)prop) :
			get_unaligned_be64(prop);

	return 0;
}
EXPORT_SYMBOL(odf_get_fdt_ubiostbl);
