/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 *
 * Description: Functions definition of ipourma_main
 */

#ifndef _IPOURMA_MAIN_H
#define _IPOURMA_MAIN_H

#include "ipourma_types.h"

extern struct ubcore_client g_ipourma_ubcore_client;

void ipourma_register_netdev(struct work_struct *work);
struct ipourma_address_iter *ipourma_address_iter_init(struct ipourma_dev_priv *priv);
int ipourma_address_iter_next(struct ipourma_address_iter *iter);
void ipourma_address_iter_read(struct ipourma_address_iter *iter, union ubcore_eid *eid);

#endif
