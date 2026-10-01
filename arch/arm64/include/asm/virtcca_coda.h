/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024. Huawei Technologies Co., Ltd. All rights reserved.
 */
#ifndef __VIRTCCA_CODA_H
#define __VIRTCCA_CODA_H

#include <asm/virtcca_io_hook.h>

#include "../../../drivers/iommu/arm/arm-smmu-v3/arm-smmu-v3.h"
#include "../../../drivers/iommu/arm/arm-smmu-v3/arm-s-smmu-v3.h"

#ifdef CONFIG_HISI_VIRTCCA_CODA

#define MAX_CC_DEV_NUM_ORDER    8
#define MASK_DEV_FUNCTION       0xfff8
#define MASK_DEV_BUS            0xff

#define DEV_BUS_NUM             0x8
#define DEV_FUNCTION_NUM        0x3

#define STE_ENTRY_SIZE          0x40

#define SMMU_DOMAIN_IS_SAME     0x2

enum cc_dev_type {
	CC_DEV_NONE_TYPE,	/* No assigned CC devices */
	CC_DEV_HOST_TYPE,	/* CC devices assigned to the host */
	CC_DEV_NVM_TYPE,	/* CC devices assigned to normal vm */
	CC_DEV_CVM_TYPE,	/* CC devices assigned to confidential vm */
};

int virtcca_attach_dev(struct iommu_domain *domain, struct iommu_group *group,
	bool iommu_secure);
void virtcca_detach_dev(struct iommu_domain *domain, struct iommu_group *group);

int virtcca_vdev_create(struct pci_dev *pci_dev);
int add_coda_pci_dev(struct pci_dev *pdev);

u64 virtcca_get_iommu_device_msi_addr(struct iommu_group *iommu_group);
int virtcca_iommu_group_set_dev_msi_addr(struct iommu_group *iommu_group, unsigned long *iova);
int virtcca_map_msi_address(struct kvm *kvm, struct arm_smmu_domain *smmu_domain, phys_addr_t pa,
	unsigned long map_size);

int virtcca_iommu_map(struct iommu_domain *domain, unsigned long iova,
	phys_addr_t paddr, size_t size, int prot);
size_t virtcca_iommu_unmap(struct iommu_domain *domain,
	unsigned long iova, size_t size);
int virtcca_map_pages(void *ops, unsigned long iova,
	phys_addr_t paddr, size_t pgsize, size_t pgcount,
	int iommu_prot, size_t *mapped);
size_t virtcca_unmap_pages(void *ops, unsigned long iova,
	size_t pgsize, size_t pgcount);

void virtcca_pci_read_msi_msg(struct pci_dev *dev, struct msi_msg *msg,
	void __iomem *base);
bool virtcca_pci_write_msg_msix(struct msi_desc *desc, struct msi_msg *msg);
bool virtcca_pci_write_msg_msi(struct msi_desc *desc, struct msi_msg *msg);
void virtcca_msix_prepare_msi_desc(struct pci_dev *dev,
	struct msi_desc *desc, void __iomem *addr);
bool virtcca_pci_msix_write_vector_ctrl(struct msi_desc *desc, u32 ctrl);
bool virtcca_pci_msix_mask(struct msi_desc *desc);
int virtcca_msix_mask_all_cc(struct pci_dev *dev, void __iomem *base, int tsize, u64 dev_num);

int virtcca_pci_generic_config_read(void __iomem *addr, unsigned char bus_num,
	unsigned int devfn, int size, u32 *val);
int virtcca_pci_generic_config_write(void __iomem *addr, unsigned char bus_num,
	unsigned int devfn, int size, u32 val);

bool is_virtcca_pci_io_rw(struct vfio_pci_core_device *vdev);
void virtcca_pci_io_write(struct vfio_pci_core_device *vdev, u64 val,
	u64 size, void __iomem *io);
u64 virtcca_pci_io_read(struct vfio_pci_core_device *vdev,
	u64 size, void __iomem *io);

bool virtcca_iommu_domain_get_kvm(struct iommu_domain *domain, struct kvm **kvm);
bool virtcca_check_is_cvm_or_not(void *iommu, struct kvm **kvm);
int virtcca_vfio_iommu_map(void *iommu, dma_addr_t iova,
	unsigned long pfn, long npage, int prot);
int cvm_vfio_add_kvm_to_smmu_domain(struct file *filp, void *kv);
struct kvm *virtcca_arm_smmu_get_kvm(struct arm_smmu_domain *domain);
void kvm_get_arm_smmu_domain(struct kvm *kvm, struct list_head *smmu_domain_group_list);
struct iommu_group *cvm_vfio_file_iommu_group(struct file *file);

struct iommu_group *virtcca_vfio_file_iommu_group(struct file *file);

bool is_cc_vmid(u32 vmid, u64 s_smmu_id);
bool is_cc_root_bd(u32 root_bd);

u64 get_g_cc_dev_msi_addr(u32 sid);

void set_g_cc_dev_msi_addr(u32 sid, u64 msi_addr);

u32 get_g_coda_dev_vm_type(u32 sid);

u32 get_g_coda_dev_vm_type(u32 sid);

void g_coda_dev_table_init(void);

u32 virtcca_tmi_dev_attach(struct arm_smmu_domain *arm_smmu_domain, struct kvm *kvm);

void virtcca_iommu_dma_get_msi_page(void *cookie, dma_addr_t *iova, phys_addr_t *phys);

int virtcca_msi_map(struct vfio_pci_core_device *vdev);

static inline u8 virtcca_readb(void __iomem *addr, struct pci_dev *pdev)
{
	return tmi_mmio_read(mmio_va_to_pa(addr), CVM_RW_8_BIT, pci_dev_id(pdev));
}

static inline u16 virtcca_readw(void __iomem *addr, struct pci_dev *pdev)
{
	return tmi_mmio_read(mmio_va_to_pa(addr), CVM_RW_16_BIT, pci_dev_id(pdev));
}

static inline u32 virtcca_readl(void __iomem *addr, struct pci_dev *pdev)
{
	return tmi_mmio_read(mmio_va_to_pa(addr), CVM_RW_32_BIT, pci_dev_id(pdev));
}

size_t virtcca_pci_get_rom_size(void *pdev, void __iomem *rom,
			       size_t size);
bool is_virtcca_cc_dev(u32 sid);
int virtcca_add_coda_pci_dev(struct pci_dev *pdev);
void virtcca_dev_destroy(u64 dev_num, u64 clean);
bool is_virtcca_pci_cc_dev(struct device *dev);
int virtcca_create_vdev(struct device *dev);

#endif /* CONFIG_HISI_VIRTCCA_CODA */
#endif /* __VIRTCCA_CODA_H */
