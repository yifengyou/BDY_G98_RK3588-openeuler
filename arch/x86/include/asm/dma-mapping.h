/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_DMA_MAPPING_H
#define _ASM_X86_DMA_MAPPING_H

extern const struct dma_map_ops *dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(void)
{
	return dma_ops;
}

#if IS_BUILTIN(CONFIG_INTEL_IOMMU) && IS_BUILTIN(CONFIG_X86_64)

bool is_zhaoxin_kh40000(void);
void kh40000_set_iommu_dma_ops(struct device *dev);
const struct dma_map_ops *kh40000_get_direct_dma_ops(void);

#endif

#endif
