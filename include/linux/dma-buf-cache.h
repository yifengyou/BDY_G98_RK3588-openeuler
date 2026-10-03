/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DMA_BUF_CACHE_H
#define _LINUX_DMA_BUF_CACHE_H

#include <linux/dma-buf.h>

static inline struct dma_buf_attachment *
dma_buf_cache_attach(struct dma_buf *dmabuf, struct device *dev)
{
	return dma_buf_attach(dmabuf, dev);
}

static inline void dma_buf_cache_detach(struct dma_buf *dmabuf,
					struct dma_buf_attachment *attach)
{
	dma_buf_detach(dmabuf, attach);
}

static inline struct sg_table *
dma_buf_cache_map_attachment(struct dma_buf_attachment *attach,
			     enum dma_data_direction direction)
{
	return dma_buf_map_attachment(attach, direction);
}

static inline void
dma_buf_cache_unmap_attachment(struct dma_buf_attachment *attach,
			       struct sg_table *sg_table,
			       enum dma_data_direction direction)
{
	dma_buf_unmap_attachment(attach, sg_table, direction);
}

#endif /* _LINUX_DMA_BUF_CACHE_H */
