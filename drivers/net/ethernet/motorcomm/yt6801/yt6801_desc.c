// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2022 - 2024 Motorcomm Electronic Technology Co.,Ltd. */

#include "yt6801_type.h"
#include "yt6801_desc.h"

void fxgmac_desc_data_unmap(struct fxgmac_pdata *priv,
			    struct fxgmac_desc_data *desc_data)
{
	if (desc_data->skb_dma) {
		if (desc_data->mapped_as_page) {
			dma_unmap_page(priv->dev, desc_data->skb_dma,
				       desc_data->skb_dma_len, DMA_TO_DEVICE);
		} else {
			dma_unmap_single(priv->dev, desc_data->skb_dma,
					 desc_data->skb_dma_len, DMA_TO_DEVICE);
		}
		desc_data->skb_dma = 0;
		desc_data->skb_dma_len = 0;
	}

	if (desc_data->skb) {
		dev_kfree_skb_any(desc_data->skb);
		desc_data->skb = NULL;
	}

	if (desc_data->rx.hdr.pa.pages)
		put_page(desc_data->rx.hdr.pa.pages);

	if (desc_data->rx.hdr.pa_unmap.pages) {
		dma_unmap_page(priv->dev, desc_data->rx.hdr.pa_unmap.pages_dma,
			       desc_data->rx.hdr.pa_unmap.pages_len,
			       DMA_FROM_DEVICE);
		put_page(desc_data->rx.hdr.pa_unmap.pages);
	}

	if (desc_data->rx.buf.pa.pages)
		put_page(desc_data->rx.buf.pa.pages);

	if (desc_data->rx.buf.pa_unmap.pages) {
		dma_unmap_page(priv->dev, desc_data->rx.buf.pa_unmap.pages_dma,
			       desc_data->rx.buf.pa_unmap.pages_len,
			       DMA_FROM_DEVICE);
		put_page(desc_data->rx.buf.pa_unmap.pages);
	}
	memset(&desc_data->tx, 0, sizeof(desc_data->tx));
	memset(&desc_data->rx, 0, sizeof(desc_data->rx));

	desc_data->mapped_as_page = 0;
}

static int fxgmac_ring_init(struct fxgmac_pdata *priv, struct fxgmac_ring *ring,
			    unsigned int dma_desc_count)
{
	/* Descriptors */
	ring->dma_desc_count = dma_desc_count;
	ring->dma_desc_head =
		dma_alloc_coherent(priv->dev, (sizeof(struct fxgmac_dma_desc) *
				   dma_desc_count),
				   &ring->dma_desc_head_addr, GFP_KERNEL);
	if (!ring->dma_desc_head)
		return -ENOMEM;

	/* Array of descriptor data */
	ring->desc_data_head = kcalloc(dma_desc_count,
				       sizeof(struct fxgmac_desc_data),
				       GFP_KERNEL);
	if (!ring->desc_data_head)
		return -ENOMEM;

	return 0;
}

static void fxgmac_ring_free(struct fxgmac_pdata *priv,
			     struct fxgmac_ring *ring)
{
	if (!ring)
		return;

	if (ring->desc_data_head) {
		for (u32 i = 0; i < ring->dma_desc_count; i++)
			fxgmac_desc_data_unmap(priv,
					       FXGMAC_GET_DESC_DATA(ring, i));

		kfree(ring->desc_data_head);
		ring->desc_data_head = NULL;
	}

	if (ring->rx_hdr_pa.pages) {
		dma_unmap_page(priv->dev, ring->rx_hdr_pa.pages_dma,
			       ring->rx_hdr_pa.pages_len, DMA_FROM_DEVICE);
		put_page(ring->rx_hdr_pa.pages);

		ring->rx_hdr_pa.pages = NULL;
		ring->rx_hdr_pa.pages_len = 0;
		ring->rx_hdr_pa.pages_offset = 0;
		ring->rx_hdr_pa.pages_dma = 0;
	}

	if (ring->rx_buf_pa.pages) {
		dma_unmap_page(priv->dev, ring->rx_buf_pa.pages_dma,
			       ring->rx_buf_pa.pages_len, DMA_FROM_DEVICE);
		put_page(ring->rx_buf_pa.pages);

		ring->rx_buf_pa.pages = NULL;
		ring->rx_buf_pa.pages_len = 0;
		ring->rx_buf_pa.pages_offset = 0;
		ring->rx_buf_pa.pages_dma = 0;
	}
	if (ring->dma_desc_head) {
		dma_free_coherent(priv->dev, (sizeof(struct fxgmac_dma_desc) *
				  ring->dma_desc_count), ring->dma_desc_head,
				  ring->dma_desc_head_addr);
		ring->dma_desc_head = NULL;
	}
}

static void fxgmac_rings_free(struct fxgmac_pdata *priv)
{
	struct fxgmac_channel *channel = priv->channel_head;

	fxgmac_ring_free(priv, channel->tx_ring);

	for (u32 i = 0; i < priv->channel_count; i++, channel++)
		fxgmac_ring_free(priv, channel->rx_ring);
}

static int fxgmac_rings_alloc(struct fxgmac_pdata *priv)
{
	struct fxgmac_channel *channel = priv->channel_head;
	int ret;

	ret = fxgmac_ring_init(priv, channel->tx_ring, priv->tx_desc_count);
	if (ret < 0) {
		dev_err(priv->dev, "Initializing Tx ring failed");
		goto err_init_ring;
	}

	for (u32 i = 0; i < priv->channel_count; i++, channel++) {
		ret = fxgmac_ring_init(priv, channel->rx_ring,
				       priv->rx_desc_count);
		if (ret < 0) {
			dev_err(priv->dev, "Initializing Rx ring failed\n");
			goto err_init_ring;
		}
	}
	return 0;

err_init_ring:
	fxgmac_rings_free(priv);
	return ret;
}

static void fxgmac_channels_free(struct fxgmac_pdata *priv)
{
	struct fxgmac_channel *channel = priv->channel_head;

	kfree(channel->tx_ring);
	channel->tx_ring = NULL;

	kfree(channel->rx_ring);
	channel->rx_ring = NULL;

	kfree(channel);
	priv->channel_head = NULL;
}

void fxgmac_channels_rings_free(struct fxgmac_pdata *priv)
{
	fxgmac_rings_free(priv);
	fxgmac_channels_free(priv);
}

static void fxgmac_set_msix_tx_irq(struct fxgmac_pdata *priv,
				   struct fxgmac_channel *channel)
{
	priv->channel_irq[FXGMAC_MAX_DMA_RX_CHANNELS] =
		priv->msix_entries[FXGMAC_MAX_DMA_RX_CHANNELS].vector;
	channel->dma_irq_tx = priv->channel_irq[FXGMAC_MAX_DMA_RX_CHANNELS];
}

static int fxgmac_channels_alloc(struct fxgmac_pdata *priv)
{
	struct fxgmac_channel *channel_head, *channel;
	struct fxgmac_ring *tx_ring, *rx_ring;
	int ret = -ENOMEM;

	channel_head = kcalloc(priv->channel_count,
			       sizeof(struct fxgmac_channel), GFP_KERNEL);

	if (!channel_head)
		return ret;

	tx_ring = kcalloc(FXGMAC_TX_1_RING, sizeof(struct fxgmac_ring),
			  GFP_KERNEL);
	if (!tx_ring)
		goto err_tx_ring;

	rx_ring = kcalloc(priv->rx_ring_count, sizeof(struct fxgmac_ring),
			  GFP_KERNEL);
	if (!rx_ring)
		goto err_rx_ring;

	channel = channel_head;
	for (u32 i = 0; i < priv->channel_count; i++, channel++) {
		snprintf(channel->name, sizeof(channel->name), "channel-%u", i);
		channel->priv = priv;
		channel->queue_index = i;
		channel->dma_regs = (priv)->hw_addr + DMA_CH_BASE +
				    (DMA_CH_INC * i);

		if (priv->per_channel_irq) {
			priv->channel_irq[i] = priv->msix_entries[i].vector;

			if (IS_ENABLED(CONFIG_PCI_MSI) &&  i < FXGMAC_TX_1_RING)
				fxgmac_set_msix_tx_irq(priv, channel);

			/* Get the per DMA rx interrupt */
			ret = priv->channel_irq[i];
			if (ret < 0) {
				dev_err(priv->dev, "channel irq[%u] failed\n",
					i + 1);
				goto err_irq;
			}

			channel->dma_irq_rx = ret;
		}

		if (i < FXGMAC_TX_1_RING)
			channel->tx_ring = tx_ring++;

		if (i < priv->rx_ring_count)
			channel->rx_ring = rx_ring++;
	}

	priv->channel_head = channel_head;
	return 0;

err_irq:
	kfree(rx_ring);

err_rx_ring:
	kfree(tx_ring);

err_tx_ring:
	kfree(channel_head);

	dev_err(priv->dev, "%s failed:%d\n", __func__, ret);
	return ret;
}

int fxgmac_channels_rings_alloc(struct fxgmac_pdata *priv)
{
	int ret;

	ret = fxgmac_channels_alloc(priv);
	if (ret < 0)
		goto err_alloc;

	ret = fxgmac_rings_alloc(priv);
	if (ret < 0)
		goto err_alloc;

	return 0;

err_alloc:
	fxgmac_channels_rings_free(priv);
	return ret;
}

static void fxgmac_set_buffer_data(struct fxgmac_buffer_data *bd,
				   struct fxgmac_page_alloc *pa,
				   unsigned int len)
{
	get_page(pa->pages);
	bd->pa = *pa;

	bd->dma_base = pa->pages_dma;
	bd->dma_off = pa->pages_offset;
	bd->dma_len = len;

	pa->pages_offset += len;
	if ((pa->pages_offset + len) > pa->pages_len) {
		/* This data descriptor is responsible for unmapping page(s) */
		bd->pa_unmap = *pa;

		/* Get a new allocation next time */
		pa->pages = NULL;
		pa->pages_len = 0;
		pa->pages_offset = 0;
		pa->pages_dma = 0;
	}
}

static int fxgmac_alloc_pages(struct fxgmac_pdata *priv,
			      struct fxgmac_page_alloc *pa, gfp_t gfp,
			      int order)
{
	struct page *pages = NULL;
	dma_addr_t pages_dma;

	/* Try to obtain pages, decreasing order if necessary */
	gfp |= __GFP_COMP | __GFP_NOWARN;
	while (order >= 0) {
		pages = alloc_pages(gfp, order);
		if (pages)
			break;

		order--;
	}

	if (!pages)
		return -ENOMEM;

	/* Map the pages */
	pages_dma = dma_map_page(priv->dev, pages, 0, PAGE_SIZE << order,
				 DMA_FROM_DEVICE);
	if (dma_mapping_error(priv->dev, pages_dma)) {
		put_page(pages);
		return -ENOMEM;
	}

	pa->pages = pages;
	pa->pages_len = PAGE_SIZE << order;
	pa->pages_offset = 0;
	pa->pages_dma = pages_dma;

	return 0;
}

#define FXGMAC_SKB_ALLOC_SIZE 512

int fxgmac_rx_buffe_map(struct fxgmac_pdata *priv, struct fxgmac_ring *ring,
			struct fxgmac_desc_data *desc_data)
{
	int ret;

	if (!ring->rx_hdr_pa.pages) {
		ret = fxgmac_alloc_pages(priv, &ring->rx_hdr_pa, GFP_ATOMIC, 0);
		if (ret)
			return ret;
	}
	/* Set up the header page info */
	fxgmac_set_buffer_data(&desc_data->rx.hdr, &ring->rx_hdr_pa,
			       priv->rx_buf_size);

	return 0;
}

void fxgmac_desc_tx_reset(struct fxgmac_desc_data *desc_data)
{
	struct fxgmac_dma_desc *dma_desc = desc_data->dma_desc;

	/* Reset the Tx descriptor
	 * Set buffer 1 (lo) address to zero
	 * Set buffer 1 (hi) address to zero
	 * Reset all other control bits (IC, TTSE, B2L & B1L)
	 * Reset all other control bits (OWN, CTXT, FD, LD, CPC, CIC, etc)
	 */
	dma_desc->desc0 = 0;
	dma_desc->desc1 = 0;
	dma_desc->desc2 = 0;
	dma_desc->desc3 = 0;

	/* Make sure ownership is written to the descriptor */
	dma_wmb();
}

void fxgmac_desc_rx_reset(struct fxgmac_desc_data *desc_data)
{
	struct fxgmac_dma_desc *dma_desc = desc_data->dma_desc;
	dma_addr_t hdr_dma;

	/* Reset the Rx descriptor
	 * Set buffer 1 (lo) address to header dma address (lo)
	 * Set buffer 1 (hi) address to header dma address (hi)
	 * set control bits OWN and INTE
	 */
	hdr_dma = desc_data->rx.hdr.dma_base + desc_data->rx.hdr.dma_off;
	dma_desc->desc0 = cpu_to_le32(lower_32_bits(hdr_dma));
	dma_desc->desc1 = cpu_to_le32(upper_32_bits(hdr_dma));
	dma_desc->desc2 = 0;
	dma_desc->desc3 = 0;
	fxgmac_desc_wr_bits(&dma_desc->desc3, RX_DESC3_INTE, 1);
	fxgmac_desc_wr_bits(&dma_desc->desc3, RX_DESC3_BUF2V, 0);
	fxgmac_desc_wr_bits(&dma_desc->desc3, RX_DESC3_BUF1V, 1);

	/* Since the Rx DMA engine is likely running, make sure everything
	 * is written to the descriptor(s) before setting the OWN bit
	 * for the descriptor
	 */
	dma_wmb();

	fxgmac_desc_wr_bits(&dma_desc->desc3, RX_DESC3_OWN, 1);

	/* Make sure ownership is written to the descriptor */
	dma_wmb();
}

int fxgmac_tx_skb_map(struct fxgmac_channel *channel, struct sk_buff *skb)
{
	struct fxgmac_pdata *priv = channel->priv;
	struct fxgmac_ring *ring = channel->tx_ring;
	unsigned int start_index, cur_index;
	struct fxgmac_desc_data *desc_data;
	unsigned int offset, datalen, len;
	struct fxgmac_pkt_info *pkt_info;
	unsigned int tso, vlan;
	dma_addr_t skb_dma;
	skb_frag_t *frag;

	offset = 0;
	start_index = ring->cur;
	cur_index = ring->cur;
	pkt_info = &ring->pkt_info;
	pkt_info->desc_count = 0;
	pkt_info->length = 0;

	tso = field_get(ATTR_TX_TSO_ENABLE, pkt_info->attr);
	vlan = field_get(ATTR_TX_VLAN_CTAG, pkt_info->attr);

	/* Save space for a context descriptor if needed */
	if ((tso && pkt_info->mss != ring->tx.cur_mss) ||
	    (vlan && pkt_info->vlan_ctag != ring->tx.cur_vlan_ctag))
		cur_index = FXGMAC_GET_ENTRY(cur_index, ring->dma_desc_count);

	desc_data = FXGMAC_GET_DESC_DATA(ring, cur_index);

	if (tso) {
		/* Map the TSO header */
		skb_dma = dma_map_single(priv->dev, skb->data,
					 pkt_info->header_len, DMA_TO_DEVICE);
		if (dma_mapping_error(priv->dev, skb_dma)) {
			dev_err(priv->dev, "dma map single failed\n");
			goto err_out;
		}
		desc_data->skb_dma = skb_dma;
		desc_data->skb_dma_len = pkt_info->header_len;

		offset = pkt_info->header_len;
		pkt_info->length += pkt_info->header_len;

		cur_index = FXGMAC_GET_ENTRY(cur_index, ring->dma_desc_count);
		desc_data = FXGMAC_GET_DESC_DATA(ring, cur_index);
	}

	/* Map the (remainder of the) packet */
	for (datalen = skb_headlen(skb) - offset; datalen;) {
		len = min_t(unsigned int, datalen, FXGMAC_TX_MAX_BUF_SIZE);
		skb_dma = dma_map_single(priv->dev, skb->data + offset, len,
					 DMA_TO_DEVICE);
		if (dma_mapping_error(priv->dev, skb_dma)) {
			dev_err(priv->dev, "dma map single failed\n");
			goto err_out;
		}
		desc_data->skb_dma = skb_dma;
		desc_data->skb_dma_len = len;

		datalen -= len;
		offset += len;
		pkt_info->length += len;

		cur_index = FXGMAC_GET_ENTRY(cur_index, ring->dma_desc_count);
		desc_data = FXGMAC_GET_DESC_DATA(ring, cur_index);
	}

	for (u32 i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		frag = &skb_shinfo(skb)->frags[i];
		offset = 0;

		for (datalen = skb_frag_size(frag); datalen;) {
			len = min_t(unsigned int, datalen,
				    FXGMAC_TX_MAX_BUF_SIZE);
			skb_dma = skb_frag_dma_map(priv->dev, frag, offset, len,
						   DMA_TO_DEVICE);
			if (dma_mapping_error(priv->dev, skb_dma)) {
				dev_err(priv->dev, "skb frag dma map failed\n");
				goto err_out;
			}
			desc_data->skb_dma = skb_dma;
			desc_data->skb_dma_len = len;
			desc_data->mapped_as_page = 1;

			datalen -= len;
			offset += len;
			pkt_info->length += len;

			cur_index = FXGMAC_GET_ENTRY(cur_index,
						     ring->dma_desc_count);
			desc_data = FXGMAC_GET_DESC_DATA(ring, cur_index);
		}
	}

	/* Save the skb address in the last entry. We always have some data
	 * that has been mapped so desc_data is always advanced past the last
	 * piece of mapped data - use the entry pointed to by cur_index - 1.
	 */
	desc_data = FXGMAC_GET_DESC_DATA(ring, (cur_index - 1) &
					 (ring->dma_desc_count - 1));
	desc_data->skb = skb;

	/* Save the number of descriptor entries used */
	if (start_index <= cur_index)
		pkt_info->desc_count = cur_index - start_index;
	else
		pkt_info->desc_count =
			ring->dma_desc_count - start_index + cur_index;

	return pkt_info->desc_count;

err_out:
	while (start_index < cur_index) {
		desc_data = FXGMAC_GET_DESC_DATA(ring, start_index);
		start_index =
			FXGMAC_GET_ENTRY(start_index, ring->dma_desc_count);
		fxgmac_desc_data_unmap(priv, desc_data);
	}

	return 0;
}

void fxgmac_dump_rx_desc(struct fxgmac_pdata *priv, struct fxgmac_ring *ring,
			 unsigned int idx)
{
	struct fxgmac_desc_data *desc_data;
	struct fxgmac_dma_desc *dma_desc;

	desc_data = FXGMAC_GET_DESC_DATA(ring, idx);
	dma_desc = desc_data->dma_desc;
	dev_dbg(priv->dev, "RX: dma_desc=%p, dma_desc_addr=%pad, RX_DESC[%d RX BY DEVICE] = %08x:%08x:%08x:%08x\n\n",
		dma_desc, &desc_data->dma_desc_addr, idx,
		le32_to_cpu(dma_desc->desc0), le32_to_cpu(dma_desc->desc1),
		le32_to_cpu(dma_desc->desc2), le32_to_cpu(dma_desc->desc3));
}

void fxgmac_dump_tx_desc(struct fxgmac_pdata *priv, struct fxgmac_ring *ring,
			 unsigned int idx, unsigned int count,
			 unsigned int flag)
{
	struct fxgmac_desc_data *desc_data;

	while (count--) {
		desc_data = FXGMAC_GET_DESC_DATA(ring, idx);
		dev_dbg(priv->dev, "TX: dma_desc=%p, dma_desc_addr=%pad, TX_DESC[%d %s] = %08x:%08x:%08x:%08x\n",
			desc_data->dma_desc, &desc_data->dma_desc_addr, idx,
			(flag == 1) ? "QUEUED FOR TX" : "TX BY DEVICE",
			le32_to_cpu(desc_data->dma_desc->desc0),
			le32_to_cpu(desc_data->dma_desc->desc1),
			le32_to_cpu(desc_data->dma_desc->desc2),
			le32_to_cpu(desc_data->dma_desc->desc3));

		idx++;
	}
}

int fxgmac_is_tx_complete(struct fxgmac_dma_desc *dma_desc)
{
	return !fxgmac_desc_rd_bits(dma_desc->desc3, TX_DESC3_OWN);
}

int fxgmac_is_last_desc(struct fxgmac_dma_desc *dma_desc)
{
	/* Rx and Tx share LD bit, so check TDES3.LD bit */
	return fxgmac_desc_rd_bits(dma_desc->desc3, TX_DESC3_LD);
}
