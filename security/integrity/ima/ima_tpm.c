// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Huawei Technologies Co., Ltd.
 *
 * Authors:
 * GONG Ruiqi <gongruiqi1@huawei.com>
 *
 * File: ima_rot_tpm.c
 *	TPM implementation of IMA RoT
 */

#include <linux/tpm.h>
#include <linux/module.h>

#include "ima.h"

static struct tpm_chip *ima_tpm_chip;

void ima_pcrread(u32 idx, struct tpm_digest *d)
{
	if (!ima_tpm_chip)
		return;

	if (tpm_pcr_read(ima_tpm_chip, idx, d) != 0)
		pr_err("Error Communicating to TPM chip\n");
}

static int ima_pcr_extend(struct tpm_digest *digests_arg, int pcr)
{
	int result = 0;

	if (!ima_tpm_chip)
		return result;

	result = tpm_pcr_extend(ima_tpm_chip, pcr, digests_arg);
	if (result != 0)
		pr_err("Error Communicating to TPM chip, result: %d\n", result);
	return result;
}

int ima_tpm_init(struct ima_rot *rot)
{
	ima_tpm_chip = tpm_default_chip();
	if (!ima_tpm_chip)
		return -ENODEV;

	rot->nr_allocated_banks = ima_tpm_chip->nr_allocated_banks;
	rot->allocated_banks = ima_tpm_chip->allocated_banks;

	return 0;
}

int ima_tpm_extend(struct tpm_digest *digests_arg, const void *args)
{
	const int pcr = *(const int *)args;

	return ima_pcr_extend(digests_arg, pcr);
}

int ima_tpm_calc_boot_aggregate(struct ima_digest_data *hash)
{
	return ima_calc_boot_aggregate(hash);
}
