// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include "mgmt_init.h"
#include <linux/printk.h>
#include "enfs_errcode.h"
#include "enfs_config.h"

int32_t mgmt_init(void)
{
	return enfs_config_timer_init();
}

void mgmt_fini(void)
{
	enfs_config_timer_exit();

}
