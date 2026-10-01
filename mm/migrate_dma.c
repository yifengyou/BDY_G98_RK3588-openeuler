// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support to use DMA channels for page migration.
 *
 * Copyright (C) 2025 Huawei Limited
 */

#define pr_fmt(fmt) "migrate_dma: " fmt

#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>

/* DMA channel track its transfers done */
struct dma_channel_work {
	struct dma_chan *chan;
	enum dma_status status;
	struct completion done;
};

static void folios_dma_copy_completion_callback(void *param,
			const struct dmaengine_result *result)
{
	struct dma_channel_work *chan_work = param;

	if (result) {
		enum dmaengine_tx_result dma_res = result->result;

		if (dma_res == DMA_TRANS_NOERROR)
			chan_work->status = DMA_COMPLETE;
		else
			chan_work->status = DMA_ERROR;
	}

	complete(&chan_work->done);
}

static int process_folio_dma_transfer(struct dma_channel_work *chan_work,
			struct folio *src, struct folio *dst)
{
	struct dma_chan *chan = chan_work->chan;
	struct device *dev = dmaengine_get_dma_device(chan);
	enum dma_ctrl_flags flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	struct dma_async_tx_descriptor *tx;
	dma_addr_t src_handle, dst_handle;
	size_t size = folio_size(src);
	int ret;

	src_handle = dma_map_page(dev, &src->page, 0, size, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, src_handle)) {
		dev_err(dev, "map dma src page error.\n");
		return -ENOMEM;
	}

	dst_handle = dma_map_page(dev, &dst->page, 0, size, DMA_FROM_DEVICE);
	if (dma_mapping_error(dev, dst_handle)) {
		dev_err(dev, "map dma dst page error.\n");
		ret = -ENOMEM;
		goto out_unmap;
	}

	tx = dmaengine_prep_dma_memcpy(chan, dst_handle, src_handle,
					size, flags);
	if (unlikely(!tx)) {
		dev_err(dev, "prep dma memcpy error.\n");
		ret = -EBUSY;
		goto out_unmap_all;
	}

	tx->callback_result = folios_dma_copy_completion_callback;
	tx->callback_param = chan_work;
	init_completion(&chan_work->done);
	chan_work->status = DMA_ERROR;

	if (dma_submit_error(dmaengine_submit(tx))) {
		dev_err(dev, "dma submit error.\n");
		ret = -EINVAL;
		goto out_unmap_all;
	}

	dma_async_issue_pending(chan);
	if (!wait_for_completion_timeout(&chan_work->done,
				msecs_to_jiffies(1000))) {
		ret = -ETIMEDOUT;
		goto out_unmap_all;
	}

	ret = (chan_work->status == DMA_COMPLETE) ? 0 : -EPROTO;

out_unmap_all:
	dma_unmap_page(dev, dst_handle, size, DMA_FROM_DEVICE);
out_unmap:
	dma_unmap_page(dev, src_handle, size, DMA_TO_DEVICE);

	return ret;
}

static bool folio_dma_chan_filter(struct dma_chan *chan, void *param)
{
	return !strcmp(dev_name(chan->device->dev), "ub_dma_device");
}

int folio_dma_copy(struct folio *dst, struct folio *src)
{
	struct dma_channel_work *chan_work;
	struct dma_slave_config dma_cfg;
	struct dma_chan *chan;
	dma_cap_mask_t mask;
	int ret = -ENODEV;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);
	chan = dma_request_channel(mask, folio_dma_chan_filter, NULL);
	if (!chan) {
		pr_err("failed to allocate dma channel.\n");
		return ret;
	}

	memset(&dma_cfg, 0, sizeof(dma_cfg));
	dma_cfg.direction = DMA_MEM_TO_MEM;
	ret = dmaengine_slave_config(chan, &dma_cfg);
	if (ret) {
		pr_err("failed to config dma channel.\n");
		goto out_release;
	}

	chan_work = kmalloc(sizeof(*chan_work), GFP_KERNEL);
	if (unlikely(!chan_work)) {
		pr_err("failed to allocate memory for chan work.\n");
		goto out_release;
	}

	chan_work->chan = chan;
	ret = process_folio_dma_transfer(chan_work, src, dst);
	if (unlikely(ret))
		pr_err("failed to process folio dma transfer.\n");

	kfree(chan_work);
out_release:
	dma_release_channel(chan);

	return ret;
}
EXPORT_SYMBOL(folio_dma_copy);
