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

#ifndef EDMA_REG_H
#define EDMA_REG_H

#define PORT_EP 0
#define PORT_RP 1

#define ENABLE 1
#define DISABLE 0

#define TRUE 1
#define FALSE 0

/* core0:x2/x1 core1:x1 */
#define PCIE_CORE_NUM 2
#define PCIE_REG_OFFSET 0x100000U
#define PCIE_REG_SIZE   0x100000

#define GEN1 0x1
#define GEN2 0x2
#define GEN3 0x3
#define GEN4 0x4

#define PCIE_ADDR_H_SHIFT_32 32
#define PCIE_ADDR_L_32_MASK  0xFFFFFFFF

#define AP_DMA_BIT            BIT(5)
#define AP_MASK_ALL           0x3FF
#define AP_DMA_CHAN_REG_SIZE  0x100

/********************************************************************************************/
/*                  PCIE reg base                                                           */
/********************************************************************************************/
#define PCIE_BASE_ADDR                       0x1E100000U
#define AP_DMA_REG                           0x10000U
#define AP_IOB_TX_REG_BASE                   0x0U
#define AP_IOB_RX_REG_BASE                   0x4000U
#define AP_GLOBAL_REG_BASE                   0x8000U

/********************************************************************************************/
/*                   PCIE AP DMA REG                                                   */
/********************************************************************************************/
#define PCIE_DMA_EP_INT_MSK_REG             0x24   /* DMA_EP_INT_MSK */
#define PCIE_DMA_EP_INT_REG                 0x28   /* DMA_EP_INT */
#define PCIE_DMA_EP_INT_STS_REG             0x2C   /* DMA_EP_INT_STS */
#define PCIE_DMA_FLR_DISABLE_REG            0xA00  /* DMA_FLR_DISABLE */
#define PCIE_DMA_QUEUE_SQ_BASE_L_0_REG      0x2000 /* DMA Queue SQ Base Address Low Register */
#define PCIE_DMA_QUEUE_SQ_BASE_H_0_REG      0x2004 /* DMA Queue SQ Base Address High Register */
#define PCIE_DMA_QUEUE_SQ_DEPTH_0_REG       0x2008 /* DMA Queue SQ Depth */
#define PCIE_DMA_QUEUE_SQ_TAIL_PTR_0_REG    0x200C /* DMA Queue SQ Tail Pointer Register */
#define PCIE_DMA_QUEUE_CQ_BASE_L_0_REG      0x2010 /* DMA Queue CQ Base Address Low Register */
#define PCIE_DMA_QUEUE_CQ_BASE_H_0_REG      0x2014 /* DMA Queue CQ Base Address High Register */
#define PCIE_DMA_QUEUE_CQ_DEPTH_0_REG       0x2018 /* DMA Queue CQ Depth */
#define PCIE_DMA_QUEUE_CQ_HEAD_PTR_0_REG    0x201C /* DMA Queue CQ Head Pointer Register */
#define PCIE_DMA_QUEUE_CTRL0_0_REG          0x2020 /* DMA Queue control Register 0 */
#define PCIE_DMA_QUEUE_CTRL1_0_REG          0x2024 /* DMA Queue control Register 1 */
#define PCIE_DMA_QUEUE_FSM_STS_0_REG        0x2030 /* DMA Queue FSM Status Register */
#define PCIE_DMA_QUEUE_SQ_STS_0_REG         0x2034 /* DMA Queue SQ and CQ status Register */
#define PCIE_DMA_QUEUE_CQ_TAIL_PTR_0_REG    0x203C /* DMA Queue CQ Tail Pointer Register */
#define PCIE_DMA_QUEUE_INT_STS_0_REG        0x2040 /* DMA Queue Interrupt Status */
#define PCIE_DMA_QUEUE_INT_MSK_0_REG        0x2044 /* DMA Queue Interrupt Mask Register */
#define PCIE_DMA_QUEUE_ERR_INT_STS_0_REG    0x2048 /* DMA Queue Err Interrupt Status */
#define PCIE_DMA_QUEUE_ERR_INT_MSK_0_REG    0x204C /* DMA Queue Err Interrupt Mask Register */
#define PCIE_DMA_QUEUE_INT_RO_0_REG         0x206C /* DMA Queue Interrupt RO Register */

/********************************************************************************************/
/*                   PCIE AP_GLOBAL_REG                                                     */
/********************************************************************************************/
#define PCIE_CE_ENA                  0x0008
#define PCIE_UNF_ENA                 0x0010
#define PCIE_UF_ENA                  0x0018

#define PCIE_MSI_MASK                0x00F4
#define PORT_INTX_ASSERT_MASK        0x01B0
#define PORT_INTX_DEASSERT_MASK      0x01B4

#define PCIE_AP_NI_ENA               0x0100
#define PCIE_AP_CE_ENA               0x0104
#define PCIE_AP_UNF_ENA              0x0108
#define PCIE_AP_UF_ENA               0x010c
#define PCIE_AP_NI_MASK              0x0110
#define PCIE_AP_CE_MASK              0x0114
#define PCIE_AP_UNF_MASK             0x0118
#define PCIE_AP_UF_MASK              0x011C
#define PCIE_AP_NI_STATUS            0x0120
#define PCIE_AP_CE_STATUS            0x0124
#define PCIE_AP_UNF_STATUS           0x0128
#define PCIE_AP_UF_STATUS            0x012C
#define PCIE_CORE_NI_ENA             0x0160
#define PCIE_CORE_CE_ENA             0x0164
#define PCIE_CORE_UNF_ENA            0x0168
#define PCIE_CORE_UF_ENA             0x016c

#define AP_PORT_EN_REG               0x0800
#define AP_APB_SYN_RST               0x0810
#define AP_AXI_SYN_RST               0x0814
#define AP_IDLE                      0x0C08

/********************************************************************************************/
/*                   PCIE AP_IOB_RX_COM_REG Reg                                             */
/********************************************************************************************/
#define IOB_RX_AML_SNOOP                    0x1AAC
#define IOB_RX_MSI_INT_CTRL                 0x1040

#define IOB_RX_MSI_INT_ADDR_HIGH       0x1044
#define IOB_RX_MSI_INT_ADDR_LOW        0x1048

#define IOB_RX_PAB_SMMU_BYPASS_CTRL    0x2004

#define IOB_RX_DMA_REG_REMAP_0 0x0E30
#define IOB_RX_DMA_REG_REMAP_1 0x0E34

#endif /* EDMA_REG_H */
