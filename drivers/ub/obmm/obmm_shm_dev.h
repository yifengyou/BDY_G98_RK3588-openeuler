/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
 */

#ifndef OBMM_SHM_DEV_H
#define OBMM_SHM_DEV_H

#include "obmm_core.h"

int obmm_shm_dev_init(void);
void obmm_shm_dev_exit(void);
int obmm_shm_dev_add(struct obmm_region *reg);
void obmm_shm_dev_del(struct obmm_region *reg);
void wait_until_dev_released(struct obmm_region *reg);


#endif
