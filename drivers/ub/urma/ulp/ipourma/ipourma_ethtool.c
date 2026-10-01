// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Description: ipourma ethtool support
 */

#include <linux/kernel.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/version.h>

#include "ub/urma/ubcore_uapi.h"
#include "ipourma_ethtool.h"

#define IPOURMA_DRIVER_VERSION	"0.1.0"
#define IPOURMA_FW_VERSION	"0.1.0"

struct ipourma_stats {
	char stat_string[ETH_GSTRING_LEN];
	int stat_offset;
};

#define IPOURMA_NETDEV_STAT(m) { \
		.stat_string = #m, \
		.stat_offset = offsetof(struct rtnl_link_stats64, m) }

static const struct ipourma_stats ipourma_gstrings_stats[] = {
	IPOURMA_NETDEV_STAT(rx_packets),
	IPOURMA_NETDEV_STAT(tx_packets),
	IPOURMA_NETDEV_STAT(rx_bytes),
	IPOURMA_NETDEV_STAT(tx_bytes),
	IPOURMA_NETDEV_STAT(tx_errors),
	IPOURMA_NETDEV_STAT(rx_dropped),
	IPOURMA_NETDEV_STAT(tx_dropped),
};

#define IPOURMA_GLOBAL_STATS_LEN	ARRAY_SIZE(ipourma_gstrings_stats)

static int ub_speed_enum_to_int(enum ubcore_speed speed)
{
	switch (speed) {
	case UBCORE_SP_10M:
		return SPEED_10;
	case UBCORE_SP_100M:
		return SPEED_100;
	case UBCORE_SP_1G:
		return SPEED_1000;
	case UBCORE_SP_2_5G:
		return SPEED_2500;
	case UBCORE_SP_5G:
		return SPEED_5000;
	case UBCORE_SP_10G:
		return SPEED_10000;
	case UBCORE_SP_14G:
		return SPEED_14000;
	case UBCORE_SP_25G:
		return SPEED_25000;
	case UBCORE_SP_40G:
		return SPEED_40000;
	case UBCORE_SP_50G:
		return SPEED_50000;
	case UBCORE_SP_100G:
		return SPEED_100000;
	case UBCORE_SP_200G:
		return SPEED_200000;
	case UBCORE_SP_400G:
		return SPEED_400000;
	case UBCORE_SP_800G:
		return SPEED_800000;
	}

	return SPEED_UNKNOWN;
}

static int ipourma_get_link_ksettings(struct net_device *netdev,
					struct ethtool_link_ksettings *cmd)
{
	struct ipourma_dev_priv *priv;
	struct ubcore_device *dev;
	struct ubcore_device_status status;
	int ret, speed;

	if (IS_ERR_OR_NULL(netdev) || IS_ERR_OR_NULL(cmd))
		return -EINVAL;
	priv = netdev_priv(netdev);
	dev = priv->urma_dev;

	if (!netif_carrier_ok(netdev)) {
		cmd->base.speed = SPEED_UNKNOWN;
		cmd->base.duplex = DUPLEX_UNKNOWN;
		return 0;
	}

	if (IS_ERR_OR_NULL(priv->urma_dev->ops) ||
		IS_ERR_OR_NULL(priv->urma_dev->ops->query_device_status))
		return -EINVAL;

	ret = priv->urma_dev->ops->query_device_status(dev, &status);
	if (ret)
		return ret;

	/* For IP over URMA, each port corresponds to a ubcore_device */
	speed = ub_speed_enum_to_int(status.port_status[0].active_speed);
	if (speed < 0)
		return -EINVAL;

	cmd->base.speed		 = (uint32_t)speed;
	cmd->base.duplex	 = DUPLEX_FULL;
	cmd->base.phy_address	 = 0xFF;
	cmd->base.autoneg	 = AUTONEG_DISABLE;
	cmd->base.port		 = PORT_FIBRE;

	return 0;
}

static void ipourma_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *drvinfo)
{
	struct ipourma_dev_priv *priv;

	if (IS_ERR_OR_NULL(netdev) || IS_ERR_OR_NULL(drvinfo))
		return;

	priv = netdev_priv(netdev);

	strscpy(drvinfo->version, IPOURMA_DRIVER_VERSION,
		sizeof(drvinfo->version));
	strscpy(drvinfo->fw_version, IPOURMA_FW_VERSION,
		sizeof(drvinfo->fw_version));

	if (priv->urma_dev->dev.parent) {
		strscpy(drvinfo->bus_info, dev_name(priv->urma_dev->dev.parent),
			sizeof(drvinfo->bus_info));
	}

	strscpy(drvinfo->driver, "ub_ipourma", sizeof(drvinfo->driver));
}

static void ipourma_get_strings(struct net_device __always_unused *dev,
			      u32 stringset, u8 *data)
{
	u8 *p = data;
	int i;

	if (IS_ERR_OR_NULL(dev) || IS_ERR_OR_NULL(data))
		return;
	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < IPOURMA_GLOBAL_STATS_LEN; i++) {
			memcpy(p, ipourma_gstrings_stats[i].stat_string,
				ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	default:
		break;
	}
}

static void ipourma_get_ethtool_stats(struct net_device *dev,
				    struct ethtool_stats __always_unused *stats,
				    u64 *data)
{
	int i;
	struct net_device_stats *net_stats;
	u8 *p;

	if (IS_ERR_OR_NULL(dev) || IS_ERR_OR_NULL(data))
		return;
	net_stats = &dev->stats;
	p = (u8 *)net_stats;
	for (i = 0; i < IPOURMA_GLOBAL_STATS_LEN; i++)
		data[i] = *(u64 *)(p + ipourma_gstrings_stats[i].stat_offset);
}

static int ipourma_get_sset_count(struct net_device __always_unused *dev,
				 int sset)
{
	if (IS_ERR_OR_NULL(dev))
		return -EINVAL;
	switch (sset) {
	case ETH_SS_STATS:
		return IPOURMA_GLOBAL_STATS_LEN;
	default:
		break;
	}
	return -EOPNOTSUPP;
}

static const struct ethtool_ops ipourma_ethtool_ops = {
	.get_link_ksettings	= ipourma_get_link_ksettings,
	.get_drvinfo		= ipourma_get_drvinfo,
	.get_strings		= ipourma_get_strings,
	.get_ethtool_stats	= ipourma_get_ethtool_stats,
	.get_sset_count		= ipourma_get_sset_count,
	.get_link		= ethtool_op_get_link,
};

void ipourma_set_ethtool_ops(struct net_device *dev)
{
	dev->ethtool_ops = &ipourma_ethtool_ops;
}
