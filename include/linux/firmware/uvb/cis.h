/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: cis header
 * Create: 2025-04-18
 */

#ifndef CIS_H
#define CIS_H
#include <linux/types.h>

// Call ID
#define UBIOS_CALL_ID_FLAG                      0x3
#define UBIOS_CALL_ID_PANIC_CALL                0xc00b2010
#define UBIOS_CALL_ID_GET_DEVICE_INFO           0xc00b0b26

// User ID format
#define UBIOS_USER_ID_NO                        (0x00 << 24)
#define UBIOS_USER_ID_BIOS                      (0x01 << 24)
#define UBIOS_USER_ID_BMC                       (0x0B << 24)
#define UBIOS_USER_ID_UB_DEVICE                 (0x10 << 24)
#define UBIOS_USER_ID_INTERGRATED_UB_DEVICE     (0x11 << 24)
#define UBIOS_USER_ID_RICH_OS                   (0x20 << 24)
#define UBIOS_USER_ID_TRUST_OS                  (0x30 << 24)
#define UBIOS_USER_ID_PCIE_DEVICE               (0x40 << 24)
#define UBIOS_USER_ID_INTERGRATED_PCIE_DEVICE   (0x41 << 24)
#define UBIOS_USER_ID_ALL                       (0xFF << 24)
#define UBIOS_USER_TYPE_MASK                    UBIOS_USER_ID_ALL
#define UBIOS_USER_INDEX_MASK                   ((u32)(~UBIOS_USER_TYPE_MASK))

#define UBIOS_MY_USER_ID                        UBIOS_USER_ID_INTERGRATED_UB_DEVICE
#define UBIOS_GET_MESSAGE_FLAG(user_id)         ((u32)((user_id) >> 30))

struct cis_message {
	void *input;
	u32 input_size;
	void *output;
	u32 *p_output_size;
};

// cis call
int cis_call_by_uvb(u32 call_id, u32 sender_id,
					u32 receiver_id, struct cis_message *msg, bool is_sync);
int uvb_polling_sync(void *data);

// cis register
typedef int (*msg_handler)(struct cis_message *msg);
int register_local_cis_func(u32 call_id, u32 receiver_id, msg_handler func);
int unregister_local_cis_func(u32 call_id, u32 receiver_id);

#endif
