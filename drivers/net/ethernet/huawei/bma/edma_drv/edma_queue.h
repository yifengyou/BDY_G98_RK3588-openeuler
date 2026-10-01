/* SPDX-License-Identifier: GPL-2.0 */
/* Huawei iBMA driver.
 * Copyright (c) 2025, Huawei Technologies Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef EDMA_QUEUE_H
#define EDMA_QUEUE_H
#include "edma_host.h"

s32 check_dma_queue_state(u32 state, u32 flag);
void set_dma_queue_sq_base_l(u32 val);
void set_dma_queue_sq_base_h(u32 val);
void set_dma_queue_cq_base_l(u32 val);
void set_dma_queue_cq_base_h(u32 val);
void reset_edma_host(struct edma_host_s *edma_host);
int transfer_edma_host(struct edma_host_s *host, struct bma_priv_data_s *priv,
		       struct bma_dma_transfer_s *transfer);
s32 transfer_dma_queue(struct bma_dma_transfer_s *dma_transfer);
#endif
