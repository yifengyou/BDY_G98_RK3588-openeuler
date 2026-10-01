/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2022 - 2024 Motorcomm Electronic Technology Co.,Ltd. */

#ifndef YT6801_DESC_H
#define YT6801_DESC_H

#define FXGMAC_TX_DESC_CNT		256
#define FXGMAC_TX_DESC_MIN_FREE		(FXGMAC_TX_DESC_CNT >> 3)
#define FXGMAC_TX_DESC_MAX_PROC		(FXGMAC_TX_DESC_CNT >> 1)
#define FXGMAC_RX_DESC_CNT		1024
#define FXGMAC_RX_DESC_MAX_DIRTY	(FXGMAC_RX_DESC_CNT >> 3)

#define FXGMAC_GET_DESC_DATA(ring, idx)	((ring)->desc_data_head + (idx))
#define FXGMAC_GET_ENTRY(x, size)	(((x) + 1) & ((size) - 1))

void fxgmac_desc_tx_reset(struct fxgmac_desc_data *desc_data);
void fxgmac_desc_rx_reset(struct fxgmac_desc_data *desc_data);
void fxgmac_desc_data_unmap(struct fxgmac_pdata *priv,
			    struct fxgmac_desc_data *desc_data);

int fxgmac_channels_rings_alloc(struct fxgmac_pdata *priv);
void fxgmac_channels_rings_free(struct fxgmac_pdata *priv);
int fxgmac_tx_skb_map(struct fxgmac_channel *channel, struct sk_buff *skb);
int fxgmac_rx_buffe_map(struct fxgmac_pdata *priv, struct fxgmac_ring *ring,
			struct fxgmac_desc_data *desc_data);
void fxgmac_dump_tx_desc(struct fxgmac_pdata *priv, struct fxgmac_ring *ring,
			 unsigned int idx, unsigned int count,
			 unsigned int flag);
void fxgmac_dump_rx_desc(struct fxgmac_pdata *priv, struct fxgmac_ring *ring,
			 unsigned int idx);

int fxgmac_is_tx_complete(struct fxgmac_dma_desc *dma_desc);
int fxgmac_is_last_desc(struct fxgmac_dma_desc *dma_desc);

#endif /* YT6801_DESC_H */
