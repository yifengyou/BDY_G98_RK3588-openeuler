/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 *
 * Description: Functions definition of ipourma_utils
 */

#ifndef _IPOURMA_UTILS_H
#define _IPOURMA_UTILS_H

#include "ipourma_types.h"

void pr_skb_head_plus_linear(const struct sk_buff *skb, const char *prefix);

#endif
