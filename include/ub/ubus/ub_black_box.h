/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef __UB_BLACK_BOX_H__
#define __UB_BLACK_BOX_H__

#include <ub/ubus/ubus.h>

typedef int (*ub_fault_record)(u32 ctl_no, u32 device_id, u32 entity_idx,
			       u32 event_id, void *data);

void ub_fault_log(struct ub_entity *uent, u32 event_id, void *data);
int ub_fault_register(ub_fault_record record);
void ub_fault_unregister(void);
int ub_fault_vdm(u32 ctl_no, struct ub_vdm_pld *vdm_pld);

#endif /* __UB_BLACK_BOX_H__ */
