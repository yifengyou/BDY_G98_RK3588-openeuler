/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2026 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UNIC_BOND_H__
#define __UNIC_BOND_H__

#include "unic_comm_addr.h"

#define UNIC_BOND_IP_REQ_SIZE		4
#define UNIC_PATTERN3_MODE		1

enum unic_bond_ip_notify_cmd {
	UNIC_BOND_IP_NOTIFY_CMD_ADD = 0x00,
	UNIC_BOND_IP_NOTIFY_CMD_DEL = 0x01,
};

struct unic_bond_ip_notify_req {
	__le16	ip_cmd;
	__le16	ip_mask;
	__le32	ip_addr[UNIC_BOND_IP_REQ_SIZE];
};

enum unic_bond_port_change_cmd {
	UNIC_CTRLQ_BOND_DEL_PORT = 0x00,
	UNIC_CTRLQ_BOND_ADD_PORT = 0x01,
};

struct unic_ctrlq_bond_status_notify_req {
	u8	bonding_cmd;
	u8	port_id;
	u8	rsv[2];
};

static inline bool unic_bond_ip_sync_supported(struct unic_dev *unic_dev)
{
	struct auxiliary_device *adev = unic_dev->comdev.adev;
	struct ubase_caps *caps = ubase_get_dev_caps(adev);

	return caps->packet_pattern_mode == UNIC_PATTERN3_MODE;
}

void unic_sync_bond_ip_table(struct unic_dev *unic_dev);
void unic_uninit_bond_ip_table(struct unic_dev *unic_dev);
int unic_update_bond_ipaddr(struct unic_dev *unic_dev, struct sockaddr *sa,
			    u16 ip_mask, enum UNIC_COMM_ADDR_STATE state);
int unic_sync_bond_status(struct net_device *netdev);

#endif /* __UNIC_BOND_H__ */
