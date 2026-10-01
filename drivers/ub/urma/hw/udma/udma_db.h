/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright(c) 2025 HiSilicon Technologies CO., Ltd. All rights reserved. */

#ifndef __UDMA_DB_H__
#define __UDMA_DB_H__

#include "udma_ctx.h"
#include "udma_dev.h"

int udma_pin_sw_db(struct udma_context *ctx, struct udma_sw_db *db);
void udma_unpin_sw_db(struct udma_context *ctx, struct udma_sw_db *db, bool dirty);
int udma_alloc_sw_db(struct udma_dev *dev, struct udma_sw_db *db,
		     enum udma_db_type type);
void udma_free_sw_db(struct udma_dev *dev, struct udma_sw_db *db);
struct udma_page_priv *udma_get_sw_db(struct udma_context *ctx, uint64_t db_addr);
void udma_put_sw_db(struct udma_context *ctx, uint64_t db_addr);

#endif /* __UDMA_DB_H__ */
