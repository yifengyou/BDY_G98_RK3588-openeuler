// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * Description: ubcore dmac file
 * Author: Li Wenhao
 * Create: 2024-05-22
 * Note:
 * History: 2024-05-22: Create file
 */

#include <net/arp.h>
#include <net/neighbour.h>
#include <net/route.h>
#include <net/netevent.h>
#include <net/ip6_route.h>

#include <linux/version.h>
#include <net/ipv6_stubs.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>

#include "ubcore_log.h"
#include "ubcore_dmac.h"

struct ubcore_dev_addr {
	uint8_t src_mac_addr[UBCORE_MAC_BYTES];
	uint8_t dst_mac_addr[UBCORE_MAC_BYTES];
	unsigned short dev_type;
	struct net *net;
	int bound_dev_if;
	int hoplimit;
};

static int ubcore_dst_fetch_ha(const struct dst_entry *dst_ety,
	struct ubcore_dev_addr *dev_addr, const void *dst_addr)
{
	struct neighbour *neigh;
	int result = 0;

	neigh = dst_neigh_lookup(dst_ety, dst_addr);
	if (!neigh)
		return -ENODATA;

	if (!(neigh->nud_state & NUD_VALID)) {
		neigh_event_send(neigh, NULL);
		result = -ENODATA;
	} else {
		neigh_ha_snapshot(dev_addr->dst_mac_addr, neigh, dst_ety->dev);
	}
	neigh_release(neigh);

	return result;
}

static int ubcore_fetch_ha(const struct dst_entry *dst, struct ubcore_dev_addr *dev_addr,
	const struct ubcore_net_addr *dip)
{
	const void *daddr = (dip->type == UBCORE_NET_ADDR_TYPE_IPV4) ?
		(const void *)&dip->net_addr.in4.addr :
		(const void *)&dip->net_addr;

	might_sleep();
	return ubcore_dst_fetch_ha(dst, dev_addr, daddr);
}

static int ubcore_addr_resolve_neigh(const struct dst_entry *dst, const struct ubcore_net_addr *dip,
	struct ubcore_dev_addr *addr, unsigned int ndev_flags)
{
	int ret = 0;

	if (ndev_flags & IFF_LOOPBACK) {
		memcpy(addr->dst_mac_addr, addr->src_mac_addr, UBCORE_MAC_BYTES);
	} else {
		if ((ndev_flags & IFF_NOARP)) {
			ubcore_log_err("the device do ARP internally\n");
			return -1;
		}
		ret = ubcore_fetch_ha(dst, addr, dip);
	}
	return ret;
}

static void set_addr_netns_by_mue_rcu(struct ubcore_dev_addr *addr, struct net_device *ndev)
{
	addr->net = dev_net(ndev);
	addr->bound_dev_if = ndev->ifindex;
}

static int ubcore_addr6_resolve(struct ubcore_net_addr *sip, const struct ubcore_net_addr *dip,
	struct ubcore_dev_addr *dev_addr, struct dst_entry **pdst)
{
	struct in6_addr sin6_addr;
	struct in6_addr din6_addr;
	struct dst_entry *dst;
	struct flowi6 fl6;
	int ret;

	memcpy(&sin6_addr, &sip->net_addr, sizeof(struct in6_addr));
	memcpy(&din6_addr, &dip->net_addr, sizeof(struct in6_addr));
	memset(&fl6, 0, sizeof(fl6));
	fl6.daddr = din6_addr;
	fl6.saddr = sin6_addr;
	fl6.flowi6_oif = dev_addr->bound_dev_if;
	ret = ip6_dst_lookup(dev_addr->net, NULL, &dst, &fl6);
	if (ret != 0) {
		ubcore_log_err("Failed to perform route lookup on flow\n");
		return ret;
	}
	dst = xfrm_lookup_route(dev_addr->net, dst, flowi6_to_flowi(&fl6), NULL, 0);
	if (IS_ERR(dst)) {
		ubcore_log_err("Failed to find dst\n");
		return PTR_ERR(dst);
	}
	if (ipv6_addr_any(&sin6_addr))
		sin6_addr = fl6.saddr;
	dev_addr->hoplimit = ip6_dst_hoplimit(dst);

	*pdst = dst;
	return 0;
}

static int ubcore_addr4_resolve(struct ubcore_net_addr *sip, const struct ubcore_net_addr *dip,
	struct ubcore_dev_addr *dev_addr, struct rtable **prt)
{
	__be32 src_ip = sip->net_addr.in4.addr;
	__be32 dst_ip = dip->net_addr.in4.addr;
	struct rtable *rt;
	struct flowi4 fl4;
	int ret;

	memset(&fl4, 0, sizeof(fl4));
	fl4.daddr = dst_ip;
	fl4.saddr = src_ip;
	rt = ip_route_output_key(dev_addr->net, &fl4);
	ret = PTR_ERR_OR_ZERO(rt);
	if (ret)
		return ret;

	sip->net_addr.in4.addr = fl4.saddr;
	dev_addr->hoplimit = ip4_dst_hoplimit(&rt->dst);
	*prt = rt;
	return 0;
}

int ubcore_get_dmac_by_ip(struct ubcore_device *dev,
	const struct ubcore_net_addr *net_addr, uint8_t *mac)
{
	struct ubcore_dev_addr dev_addr = {0};
	struct ubcore_net_addr sip = {0};
	struct net_device *ndev = NULL;
	struct dst_entry *dst = NULL;
	unsigned int ndev_flags = 0;
	struct rtable *rt = NULL;
	int ret;

	rcu_read_lock();
	if (dev->netdev) {
		set_addr_netns_by_mue_rcu(&dev_addr, dev->netdev);
	} else {
		rcu_read_unlock();
		ubcore_log_err("Failed to get dmac, no net_dev available!\n");
		return -ENODEV;
	}

	if (net_addr->type == UBCORE_NET_ADDR_TYPE_IPV4) {
		ret = ubcore_addr4_resolve(&sip, net_addr, &dev_addr, &rt);
		dst = &rt->dst;
	} else
		ret = ubcore_addr6_resolve(&sip, net_addr, &dev_addr, &dst);
	if (ret) {
		rcu_read_unlock();
		ubcore_log_err("Failed to find dst, ret is %d\n", ret);
		return ret;
	}

	ndev = READ_ONCE(dst->dev);
	rcu_read_unlock();
	if (ndev == NULL) {
		ubcore_log_err("ndev is NULL!\n");
		ret = -EINVAL;
		goto free_rt;
	}
	ndev_flags = ndev->flags;
	ret = ubcore_addr_resolve_neigh(dst, net_addr, &dev_addr, ndev_flags);
	memcpy(mac, dev_addr.dst_mac_addr, UBCORE_MAC_BYTES);

free_rt:
	if (net_addr->type == UBCORE_NET_ADDR_TYPE_IPV4)
		ip_rt_put(rt);
	else
		dst_release(dst);
	return ret;
}
