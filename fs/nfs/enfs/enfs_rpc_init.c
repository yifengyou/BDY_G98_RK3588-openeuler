// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include "enfs_lookup_cache.h"

int enfs_rpc_init(void)
{
	int ret = 0;

	ret = enfs_lookupcache_init();

	return ret;
}
