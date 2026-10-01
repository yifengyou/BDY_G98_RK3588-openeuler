/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025. Huawei Technologies Co., Ltd. All rights reserved.
 */
#ifndef __VIRTCCA_IO_HOOK_H
#define __VIRTCCA_IO_HOOK_H

#include <asm/virtcca_cvm_host.h>
#include <linux/iommu.h>
#include <linux/vfio_pci_core.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/io-64-nonatomic-hi-lo.h>

#ifdef CONFIG_HISI_VIRTCCA_CODA
#define CVM_RW_8_BIT	0x8
#define CVM_RW_16_BIT	0x10
#define CVM_RW_32_BIT	0x20
#define CVM_RW_64_BIT	0x40

void virtcca_pci_mmio_write(struct pci_dev *pdev, u64 val,
	u64 size, void __iomem *io);
u64 virtcca_pci_mmio_read(struct pci_dev *pdev,
	u64 size, void __iomem *io);
/* Has the root bus device number switched to secure */
bool is_cc_dev(u32 sid);

static inline void iowrite32be_hook(u32 val, void __iomem *addr, struct pci_dev *pdev)
{
	if (is_virtcca_cvm_enable() && is_cc_dev(pci_dev_id(pdev))) {
		virtcca_pci_mmio_write(pdev, cpu_to_be32(val), CVM_RW_32_BIT, addr);
		return;
	}
	iowrite32be(val, addr);
}

static inline u32 ioread32be_hook(void __iomem *addr, struct pci_dev *pdev)
{
	if (is_virtcca_cvm_enable() && is_cc_dev(pci_dev_id(pdev)))
		return cpu_to_be32(virtcca_pci_mmio_read(pdev, CVM_RW_32_BIT, addr));

	return ioread32be(addr);
}

static inline u16 ioread16be_hook(void __iomem *addr, struct pci_dev *pdev)
{
	if (is_virtcca_cvm_enable() && is_cc_dev(pci_dev_id(pdev)))
		return cpu_to_be16(virtcca_pci_mmio_read(pdev, CVM_RW_16_BIT, addr));

	return ioread16be(addr);
}

static inline u8 ioread8_hook(void __iomem *addr, struct pci_dev *pdev)
{
	if (is_virtcca_cvm_enable() && is_cc_dev(pci_dev_id(pdev)))
		return virtcca_pci_mmio_read(pdev, CVM_RW_8_BIT, addr);

	return ioread8(addr);
}

static inline void __raw_writel_hook(u32 val, void __iomem *addr, struct pci_dev *pdev)
{
	if (is_virtcca_cvm_enable() && is_cc_dev(pci_dev_id(pdev))) {
		virtcca_pci_mmio_write(pdev, val, CVM_RW_32_BIT, addr);
		return;
	}
	__raw_writel(val, addr);
}

static inline void __raw_writeq_hook(u64 val, void __iomem *addr, struct pci_dev *pdev)
{
	if (is_virtcca_cvm_enable() && is_cc_dev(pci_dev_id(pdev))) {
		virtcca_pci_mmio_write(pdev, val, CVM_RW_64_BIT, addr);
		return;
	}
	__raw_writeq(val, addr);
}

static inline void writel_hook(u32 val, void __iomem *addr, struct pci_dev *pdev)
{
	if (is_virtcca_cvm_enable() && is_cc_dev(pci_dev_id(pdev))) {
		virtcca_pci_mmio_write(pdev, val, CVM_RW_32_BIT, addr);
		return;
	}
	writel(val, addr);
}

static inline u32 readl_hook(void __iomem *addr, struct pci_dev *pdev)
{
	if (is_virtcca_cvm_enable() && is_cc_dev(pci_dev_id(pdev)))
		return virtcca_pci_mmio_read(pdev, CVM_RW_32_BIT, addr);

	return readl(addr);
}

static inline void writeq_hook(u64 val, void __iomem *addr, struct pci_dev *pdev)
{
	if (is_virtcca_cvm_enable() && is_cc_dev(pci_dev_id(pdev))) {
		virtcca_pci_mmio_write(pdev, val, CVM_RW_64_BIT, addr);
		return;
	}
	writeq(val, addr);
}

static inline void lo_hi_writeq_hook(__u64 val, void __iomem *addr, struct pci_dev *pdev)
{
	if (is_virtcca_cvm_enable() && is_cc_dev(pci_dev_id(pdev))) {
		virtcca_pci_mmio_write(pdev, (u32)val, CVM_RW_32_BIT, addr);
		virtcca_pci_mmio_write(pdev, (u32)(val >> 32), CVM_RW_32_BIT, addr + 4);
		return;
	}
	lo_hi_writeq(val, addr);
}

static inline void hi_lo_writeq_hook(__u64 val, void __iomem *addr, struct pci_dev *pdev)
{
	if (is_virtcca_cvm_enable() && is_cc_dev(pci_dev_id(pdev))) {
		virtcca_pci_mmio_write(pdev, (u32)(val >> 32), CVM_RW_32_BIT, addr + 4);
		virtcca_pci_mmio_write(pdev, (u32)val, CVM_RW_32_BIT, addr);
		return;
	}
	hi_lo_writeq(val, addr);
}

static inline u64 lo_hi_readq_hook(void __iomem *addr, struct pci_dev *pdev)
{
	if (is_virtcca_cvm_enable() && is_cc_dev(pci_dev_id(pdev)))
		return virtcca_pci_mmio_read(pdev, CVM_RW_64_BIT, addr);

	return lo_hi_readq(addr);
}

static inline void __iowrite64_copy_hook(void __iomem *to, const void *from,
	size_t count, struct pci_dev *pdev)
{
	if (is_virtcca_cvm_enable() && is_cc_dev(pci_dev_id(pdev))) {
		u64 __iomem *dst = to;
		const u64 *src = from;
		const u64 *end = src + count;

		while (src < end)
			virtcca_pci_mmio_write(pdev, *src++, CVM_RW_64_BIT, dst++);
		return;
	}
	__iowrite64_copy(to, from, count);
}

#else /* CONFIG_HISI_VIRTCCA_CODA */
#define iowrite32be_hook(v, a, p) iowrite32be(v, a)
#define ioread32be_hook(a, p) ioread32be(a)
#define ioread16be_hook(a, p) ioread16be(a)
#define ioread8_hook(a, p) ioread8(a)
#define __raw_writel_hook(v, a, p) __raw_writel(v, a)
#define __raw_writeq_hook(v, a, p) __raw_writeq(v, a)
#define writel_hook(v, a, p) writel(v, a)
#define readl_hook(a, p) readl(a)
#define lo_hi_writeq_hook(v, a, p) lo_hi_writeq(v, a)
#define hi_lo_writeq_hook(v, a, p) hi_lo_writeq(v, a)
#define lo_hi_readq_hook(a, p) lo_hi_readq(a)
#endif /* CONFIG_HISI_VIRTCCA_CODA */
#endif /* __VIRTCCA_IO_HOOK_H */
