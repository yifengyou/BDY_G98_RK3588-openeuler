// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Huawei Technologies Co., Ltd.
 *
 * Authors:
 * GONG Ruiqi <gongruiqi1@huawei.com>
 *
 * File: ima_rot.c
 *	IMA rot layer
 */

#include <linux/types.h>
#include <linux/list.h>
#include <linux/gfp_types.h>

#include "ima.h"
#include "ima_tpm.h"
#include "ima_virtcca.h"

static const char *name_rot_prefered;

/*
 * The list containing all possible RoT devices.
 *
 * The order of RoTs inside the list implies priority.
 * IOW, RoT device that owns higher priority should be placed at the front.
 */
static struct ima_rot ima_rots[] = {
#ifdef CONFIG_TCG_TPM
	{
		.name = "tpm",
		.init = ima_tpm_init,
		.extend = ima_tpm_extend,
		.calc_boot_aggregate = ima_tpm_calc_boot_aggregate,
	},
#endif
#ifdef CONFIG_HISI_VIRTCCA_GUEST
	{
		.name = "virtcca",
		.init = ima_virtcca_init,
		.extend = ima_virtcca_extend,
		.calc_boot_aggregate = ima_calc_virtcca_boot_aggregate,
	},
#endif
};

static int __init ima_rot_name(char *str)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ima_rots); i++) {
		if (!strcmp(str, ima_rots[i].name)) {
			name_rot_prefered = str;
			break;
		}
	}

	if (!name_rot_prefered)
		pr_info("%s is NOT implemented as an IMA RoT\n", str);

	return 1;
}
__setup("ima_rot=", ima_rot_name);

/*
 * Pick the most prioritized RoT that can be initialized successfully.
 */
struct ima_rot * __init ima_rot_init(void)
{
	int rc, i;

	for (i = 0; i < ARRAY_SIZE(ima_rots); i++) {
		if (name_rot_prefered && strcmp(name_rot_prefered, ima_rots[i].name))
			continue;

		pr_info("IMA RoT initializing %s\n", ima_rots[i].name);
		rc = ima_rots[i].init(&ima_rots[i]);
		if (!rc) {
			pr_info("%s initialized and taken as IMA RoT\n", ima_rots[i].name);
			return &ima_rots[i];
		}
	}

	return NULL;
}
