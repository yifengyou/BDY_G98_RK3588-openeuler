/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 *
 * Description: Functions definition of ipourma_addr_res
 */

#ifndef _IPOURMA_ADDR_RES_H
#define _IPOURMA_ADDR_RES_H

#include "ipourma_types.h"

void ipourma_resolve_eids(struct net_device *dev, struct sk_buff *skb, u16 proto,
			  union ubcore_eid *src_eid, union ubcore_eid *dst_eid);
int ipourma_resolve_ipaddr(struct net_device *dev, u16 proto,
			   union ubcore_eid *eid, struct in6_addr *addr);

#endif
