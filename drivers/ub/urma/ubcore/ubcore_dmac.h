/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2026. All rights reserved.
 *
 * Description: ubcore dmac header file
 * Author: Li Wenhao
 * Create: 2024-05-22
 * Note:
 * History: 2024-05-22: Create file
 */

#ifndef UBCORE_DMAC_H
#define UBCORE_DMAC_H
#include "ubcore_priv.h"

int ubcore_get_dmac_by_ip(struct ubcore_device *dev,
	const struct ubcore_net_addr *net_addr, uint8_t *mac);
#endif // UBCORE_DMAC_H
