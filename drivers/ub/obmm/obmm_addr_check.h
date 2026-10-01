/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
 */
#ifndef OBMM_ADDR_DUP_CHECK_H
#define OBMM_ADDR_DUP_CHECK_H

#include <linux/types.h>

enum obmm_addr_user {
	OBMM_ADDR_USER_DIRECT_IMPORT,
	OBMM_ADDR_USER_PREIMPORT,
};
struct obmm_addr_info {
	enum obmm_addr_user user;
	void *data;
};

struct obmm_pa_range {
	phys_addr_t start;
	phys_addr_t end;
	struct obmm_addr_info info;
};

int occupy_pa_range(const struct obmm_pa_range *pa_range);
int free_pa_range(const struct obmm_pa_range *pa_range);

/* @addr is the search key and @info stores output value */
int query_pa_range(phys_addr_t addr, struct obmm_addr_info *info);
/* @addr is the search key and @info stores the overwrite value */
int update_pa_range(phys_addr_t addr, const struct obmm_addr_info *info);

void module_addr_check_init(void);
void module_addr_check_exit(void);

#endif
