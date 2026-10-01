// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Description: ipourma utils
 */
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/printk.h>
#include "ipourma_utils.h"

#define IPOURMA_SKB_HEX_DUMP_ROW_SIZE 16
#define IPOURMA_SKB_HEX_DUMP_MAX_LEN 128


// print first 64 bytes of skb head

void pr_skb_head_plus_linear(const struct sk_buff *skb, const char *prefix)
{
	pr_debug("[%s] skb head:0x%llx, linear:0x%llx, total len=%d, ",
		prefix ? prefix : "IPoURMA", (u64)(skb->head), (u64)(skb->data), skb->len);
	pr_debug("headroom=%d, linear len=%d, nr_frags=%d\n",
		skb_headroom(skb), skb_headlen(skb), skb_shinfo(skb)->nr_frags);
	if ((skb_headroom(skb) + skb_headlen(skb)) > 0) {
		pr_debug("skb starts from head:\n");
		print_hex_dump_debug("", DUMP_PREFIX_OFFSET, IPOURMA_SKB_HEX_DUMP_ROW_SIZE,
					1, skb->head,
					min_t(size_t, (skb_headroom(skb) + skb_headlen(skb)),
						IPOURMA_SKB_HEX_DUMP_MAX_LEN),
					false);
	}
}

