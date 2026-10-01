/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024. Huawei Technologies Co., Ltd. All rights reserved.
 */
#ifndef __LINUX_IMA_VIRTCCA_H
#define __LINUX_IMA_VIRTCCA_H

#include "ima.h"

int ima_virtcca_init(struct ima_rot *rot);
int ima_calc_virtcca_boot_aggregate(struct ima_digest_data *hash);
int ima_virtcca_extend(struct tpm_digest *digests_arg, const void *args);
#endif
