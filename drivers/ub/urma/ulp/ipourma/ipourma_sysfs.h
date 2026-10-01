/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 *
 * Description: Functions definition of ipourma_sysfs
 */

#ifndef _IPOURMA_SYSFS_H
#define _IPOURMA_SYSFS_H

#include "ipourma_types.h"

#define RESET_CMD "reset all status"

void ipourma_register_sysfs(struct ipourma_dev_priv *pos);
void ipourma_unregister_sysfs(struct ipourma_dev_priv *pos);

#endif
