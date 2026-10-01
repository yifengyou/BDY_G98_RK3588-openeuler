/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) Huawei Technologies Co., Ltd. 2024-2024
 * All rights reserved.
 *
 * caqm in the flow dissector, it is to support the rps for a caqm pkt
 * Authors:	Chengjun Jia <jiachengjun2@huawei.com>
 */
#ifndef _NET_CORE_FLOW_DISSECTOR_CAQM_H
#define _NET_CORE_FLOW_DISSECTOR_CAQM_H

#ifdef CONFIG_ETH_CAQM
#include <linux/if_caqm.h>
#include <net/flow_dissector.h>
static inline int rps_try_skip_caqm_hdr(const struct sk_buff *skb, const void *data,
				   __be16 *proto_ptr, int *nhoff_ptr, const int hlen)
{
	const struct caqm_hdr *caqm = NULL;
	struct caqm_hdr _caqm;

	if (!static_branch_unlikely(&sysctl_caqm_enable))
		return FLOW_DISSECT_RET_OUT_BAD;

	caqm = __skb_header_pointer(skb, *nhoff_ptr, sizeof(_caqm),
					    data, hlen, &_caqm);
	if (!caqm)
		return FLOW_DISSECT_RET_OUT_BAD;

	*proto_ptr = caqm->h_caqm_encapsulated_proto;
	*nhoff_ptr += sizeof(*caqm);
	return FLOW_DISSECT_RET_PROTO_AGAIN;
}

#endif
#endif
