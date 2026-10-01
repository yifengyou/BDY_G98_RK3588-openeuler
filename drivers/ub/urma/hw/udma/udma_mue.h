/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_MUE_H__
#define __UDMA_MUE_H__

#include <linux/device.h>
#include "udma_dev.h"

int udma_register_ue_msg_req_event(struct auxiliary_device *adev);
int udma_register_ue_msg_rsp_event(struct auxiliary_device *adev);
void udma_unregister_ue_msg_req_event(struct auxiliary_device *adev);
void udma_unregister_ue_msg_rsp_event(struct auxiliary_device *adev);

#endif /* __UDMA_MUE_H__ */
