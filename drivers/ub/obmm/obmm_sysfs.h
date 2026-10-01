/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
 */

#ifndef OBMM_SYSFS_H
#define OBMM_SYSFS_H

#include "obmm_core.h"

const struct attribute_group **obmm_region_get_attr_groups(const struct obmm_region *);

#endif
