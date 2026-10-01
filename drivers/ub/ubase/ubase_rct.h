/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2026 HiSilicon Technologies Co., Ltd. All rights reserved.
 *
 */

#ifndef __UBASE_RCT_H__
#define __UBASE_RCT_H__

#include "ubase_dev.h"

#define UBASE_RC_TYPE 2U
#define UBASE_RCE_SIZE 64U
#define UBASE_RC_READY_STATE 1U
#define UBASE_RC_AVAIL_SGMT_OST 512U

#define UBASE_RCE_TOKEN_ID_L_MASK GENMASK(11, 0)
#define UBASE_RCE_TOKEN_ID_H_OFFSET 12U
#define UBASE_RCE_ADDR_L_OFFSET 12U
#define UBASE_RCE_ADDR_L_MASK GENMASK(19, 0)
#define UBASE_RCE_ADDR_H_OFFSET 32U

struct ubase_rc_ctx {
	/* DW0 */
	u32 rsv0 : 5;
	u32 type : 3;
	u32 rce_shift : 4;
	u32 rsv1 : 4;
	u32 state : 3;
	u32 rsv2 : 1;
	u32 rce_token_id_l : 12;
	/* DW1 */
	u32 rce_token_id_h : 8;
	u32 rsv3 : 4;
	u32 rce_base_addr_l : 20;
	/* DW2 */
	u32 rce_base_addr_h;
	/* DW3~DW31 */
	u32 rsv4[28];
	u32 avail_sgmt_ost : 10;
	u32 rsv5 : 22;
	/* DW32~DW63 */
	u32 rsv6[32];
};

int ubase_rc_init(struct ubase_dev *udev);
void ubase_rc_uninit(struct ubase_dev *udev);
int ubase_create_rc_queue_ctx(struct ubase_dev *udev, u32 rc_queue_idx);
int ubase_destroy_rc_queue_ctx(struct ubase_dev *udev, u32 rc_queue_idx);

#endif /* __UBASE_RCT_H__ */
