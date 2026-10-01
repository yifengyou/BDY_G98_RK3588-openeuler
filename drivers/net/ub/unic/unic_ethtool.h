/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UNIC_ETHTOOL_H__
#define __UNIC_ETHTOOL_H__

#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <ub/ubase/ubase_comm_dev.h>

#define UNIC_TXRX_MIN_DEPTH	64

#define UNIC_RX_TX_PAUSE_ON	1
#define UNIC_RX_TX_PAUSE_OFF	0

#define UNIC_TX_PAUSE_EN	BIT(0)
#define UNIC_RX_PAUSE_EN	BIT(1)
#define UNIC_FC_PFC_EN		BIT(2)

struct unic_reset_type_map {
	enum ethtool_reset_flags reset_flags;
	enum ubase_reset_type reset_type;
};

struct unic_coalesce {
	u16		int_gl;
	u16		int_ql;
	u8		adapt_enable : 1;
	u8		resv : 7;
};

void unic_set_ethtool_ops(struct net_device *netdev);

#endif /* __UNIC_ETHTOOL_H__ */
