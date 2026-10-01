/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UNIC_VLAN_H__
#define __UNIC_VLAN_H__

#include "unic_dev.h"

int unic_init_vlan_config(struct unic_dev *unic_dev);
void unic_uninit_vlan_config(struct unic_dev *unic_dev);
int unic_set_vlan_table(struct unic_dev *unic_dev, __be16 proto, u16 vlan_id,
			bool is_add);
int unic_set_vlan_filter(struct unic_dev *unic_dev, bool enable);
void unic_sync_vlan_filter(struct unic_dev *unic_dev);

#endif /* __UNIC_VLAN_H__ */
