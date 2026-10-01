// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024. Huawei Technologies Co., Ltd. All rights reserved.
 */
#include <asm/virtcca_cvm_smc.h>
#include <asm/virtcca_cvm_guest.h>
#include "ima.h"

#define DEFAULT_CCA_REM_IDX 4

static enum hash_algo virtcca_algo;

static int ima_virtcca_init_algo(void)
{
	unsigned long result;
	struct virtcca_cvm_config cfg = { 0 };

	result = tsi_get_cvm_config(&cfg);
	if (result != TSI_SUCCESS) {
		pr_info("Error reading cvm config\n");
		return -EFAULT;
	}

	/* 0: SHA256, 1: SHA512 */
	virtcca_algo = cfg.algorithm ? HASH_ALGO_SHA512 : HASH_ALGO_SHA256;

	return 0;
}

int ima_virtcca_init(struct ima_rot *rot)
{
	int rc;

	if (!is_virtcca_cvm_world() || tsi_get_version() == SMCCC_RET_NOT_SUPPORTED)
		return -ENODEV;

	rc = ima_virtcca_init_algo();
	if (rc)
		return rc;

	rot->allocated_banks = kcalloc(1, sizeof(*rot->allocated_banks), GFP_KERNEL);
	if (!rot->allocated_banks)
		return -ENOMEM;

	rot->nr_allocated_banks = 1;
	rot->allocated_banks[0].alg_id = (virtcca_algo == HASH_ALGO_SHA512) ?
					 TPM_ALG_SHA512 : TPM_ALG_SHA256;
	rot->allocated_banks[0].digest_size = hash_digest_size[virtcca_algo];
	rot->allocated_banks[0].crypto_id = virtcca_algo;

	return 0;
}

int ima_calc_virtcca_boot_aggregate(struct ima_digest_data *hash)
{
	unsigned long result;
	struct virtcca_cvm_measurement cm = { 0 };

	hash->algo = virtcca_algo;
	hash->length = hash_digest_size[virtcca_algo];

	/* Read the measurement result of RIM as the boot aggregate */
	cm.index = RIM_MEASUREMENT_SLOT;

	result = tsi_measurement_read(&cm);
	if (result != TSI_SUCCESS) {
		pr_err("Error reading cvm measurement 0 for boot aggregate\n");
		return -EFAULT;
	}

	memcpy(hash->digest, cm.value, hash->length);

	return 0;
}

int ima_virtcca_extend(struct tpm_digest *digests_arg, const void *args)
{
	const int pcr = *(const int *)args;
	struct virtcca_cvm_measurement_extend cme;

	if (pcr > DEFAULT_CCA_REM_IDX || pcr == 0) {
		pr_err("IMA: Extending measurement to CCA REM register index invalid, is 0 or exceeds max limit 4\n");
		return -EINVAL;
	}

	if (pcr < DEFAULT_CCA_REM_IDX)
		pr_info("IMA: Extending measurement to CCA REM register 1~3 may conflict with uefi measure\n");

	cme.index = pcr;
	cme.size = hash_digest_size[virtcca_algo];

	/*
	 * virtcca has only one slot, so the algorithm of digests_arg[0] is always
	 * virtcca_algo according to the init process of ima_init_crypto() and
	 * ima_init_digets()
	 */
	memcpy(cme.value, digests_arg[0].digest, cme.size);

	return tsi_measurement_extend(&cme) == TSI_SUCCESS ? 0 : -EFAULT;
}
