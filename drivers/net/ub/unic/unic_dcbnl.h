/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UNIC_DCBNL_H__
#define __UNIC_DCBNL_H__

#include <linux/netdevice.h>
#include <ub/ubase/ubase_comm_stats.h>

#include "unic_stats.h"
#include "unic_ethtool.h"

static inline u64 unic_get_pfc_tx_pkts(struct ubase_eth_mac_stats *eth_stats,
				       u32 pri)
{
	u16 offset = UNIC_ETH_MAC_STATS_FIELD_OFFSET(tx_pri0_pfc_pkts) +
		     pri * sizeof(eth_stats->tx_pri0_pfc_pkts);

	return UNIC_STATS_READ(eth_stats, offset);
}

static inline u64 unic_get_pfc_rx_pkts(struct ubase_eth_mac_stats *eth_stats,
				       u32 pri)
{
	u16 offset = UNIC_ETH_MAC_STATS_FIELD_OFFSET(rx_pri0_pfc_pkts) +
		     pri * sizeof(eth_stats->rx_pri0_pfc_pkts);

	return UNIC_STATS_READ(eth_stats, offset);
}

#ifdef CONFIG_UB_UNIC_DCB
void unic_set_dcbnl_ops(struct net_device *netdev);
#else
static inline void unic_set_dcbnl_ops(struct net_device *netdev) {}
#endif

#endif /* __UNIC_DCBNL_H__ */
