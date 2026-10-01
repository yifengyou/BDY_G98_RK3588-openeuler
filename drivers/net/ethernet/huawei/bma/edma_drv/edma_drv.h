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

#ifndef EDMA_DRV_H
#define EDMA_DRV_H

#define DMA_STATISTICS_LEN  16
#define DMA_CH_TAG_SIZE	 64

#define HISILICON_VENDOR_ID	 0x19e5
#define DMA_PCIE_DEVICE_ID	  0xa122

#define MAX_DMA_CHS		 4  /* The current version supports a maximum of 2x2 channels. */
#define DMA_CHS_EACH_PORT   2

#define MAX_SQ_DEPTH	0xFFFF
#define MAX_CQ_DEPTH	0xFFFF

#define DMA_DONE_MASK	   0x1
#define DMA_DONE_UNMASK	 0x0
#define DMA_ERR_MASK		0x7FFFE
#define DMA_ERR_UNMASK	  0x0

#define BD_SO 0
#define BD_RO 1

#define SIZE_4M  0x400000
#define SIZE_16K 0x4000
#define SIZE_64K 0x10000
#define SIZE_OF_U64 0x8
#define SPD_SIZE_MAX 32

/* Use integer arithmetic for approximate computation instead of floating-point. */
#define US_PER_SECOND_DIV_1KB (1000000 / 1024)

#define DMA_PHY_STORE_OFFSET (SIZE_64K - SIZE_OF_U64)
#define DMA_RMT_PHY_STORE_OFFSET (DMA_PHY_STORE_OFFSET - SIZE_OF_U64)
#define BIT_0_TO_31_MASK 0xFFFFFFFF

#define DMA_TMOUT (2 * HZ) /* 2 seconds */

enum {
	EP0 = 0,
	EP1 = 1
};

enum {
	DRC_LOCAL  = 0,
	DRC_REMOTE = 1
};

enum {
	DIR_B2H = 0,
	DIR_H2B = 1,
};

enum {
	DMA_INIT		= 0x0,
	DMA_RESET	   = 0x1,
	DMA_PAUSE	   = 0x2,
	DMA_NOTIFY	  = 0x3,
	LINKDOWN		= 0x4,
	LINKUP		  = 0x5,
	FLR			 = 0x6
};

enum {
	PF0 = 0,
	PF1 = 1,
	PF2 = 2,
	PF4 = 4,
	PF7 = 7,
	PF10 = 10
};

enum {
	RESERVED		= 0x0,  /* reserved */
	SMALL_PACKET	= 0x1,  /* SmallPacket Descriptor */
	DMA_READ		= 0x2,  /* Read Descriptor */
	DMA_WRITE	   = 0x3,  /* Write Descriptor */
	DMA_LOOP		= 0x4,  /* Loop Descriptor */
	DMA_MIX		 = 0x10, /* not available, User-defined for test */
	DMA_WD_BARRIER  = 0x11, /* not available, User-defined for test */
	DMA_RD_BARRIER  = 0x12, /* not available, User-defined for test */
	DMA_LP_BARRIER  = 0x13  /* not available, User-defined for test */
};

enum {
	IDLE_STATE	  = 0x0,  /* dma channel in idle status */
	RUN_STATE	   = 0x1,  /* dma channel in run status */
	CPL_STATE	   = 0x2,  /* dma channel in cpld status */
	PAUSE_STATE	 = 0x3,  /* dma channel in pause status */
	HALT_STATE	  = 0x4,  /* dma channel in halt status */
	ABORT_STATE	 = 0x5,  /* dma channel in abort status */
	WAIT_STATE	  = 0x6   /* dma channel in wait status */
};

/* CQE status */
enum {
	DMA_DONE		  = 0x0,	/* sqe done succ */
	OPCODE_ERR		= 0x1,	/* sqe opcode invalid */
	LEN_ERR		   = 0x2,	/* sqe length invalid, only ocurs in smallpackt */
	DROP_EN		   = 0x4,	/* sqe drop happen */
	WR_RMT_ERR		= 0x8,	/* write data to host fail */
	RD_RMT_ERR		= 0x10,   /* read data from host fail */
	RD_AXI_ERR		= 0x20,   /* read data/sqe from local fail */
	WR_AXI_ERR		= 0x40,   /* write data/cqe to local fail */
	POISON_CPL_ERR	= 0x80,   /* poison data */
	SUB_SQ_ERR		= 0x100,  /* read sqe with CPL TLP */
	DMA_CH_RESET	  = 0x200,  /* dma channel should reset */
	LINK_DOWN_ERR	 = 0x400,  /* linkdown happen */
	RECOVERY		  = 0x800   /* error status to be reset */
};

enum {
	SDI_DMA_ADDR_SIZE_16K = 0,
	SDI_DMA_ADDR_SIZE_32K = 1,
	SDI_DMA_ADDR_SIZE_64K = 2,
	SDI_DMA_ADDR_SIZE_128K = 3
};

union U_DMA_QUEUE_SQ_DEPTH {
	struct {
		unsigned int	dma_queue_sq_depth	: 16; /* [15..0] */
		unsigned int	reserved_0			: 16; /* [31..16] */
	} bits;

	unsigned int	u32;
};

union U_DMA_QUEUE_CQ_DEPTH {
	struct {
		unsigned int	dma_queue_cq_depth	: 16; /* [15..0] */
		unsigned int	reserved_0			: 16; /* [31..16] */
	} bits;

	unsigned int	u32;
};

union U_DMA_QUEUE_CQ_HEAD_PTR {
	struct {
		unsigned int	dma_queue_cq_head_ptr : 16; /* [15..0] */
		unsigned int	reserved_0			: 16; /* [31..16] */
	} bits;

	unsigned int	u32;
};

union U_DMA_QUEUE_CQ_TAIL_PTR {
	struct {
		unsigned int	dma_queue_cq_tail_ptr : 16; /* [15..0]  */
		unsigned int	dma_queue_sqhd		: 16; /* [31..16]  */
	} bits;

	unsigned int	u32;
};

union U_DMA_QUEUE_SQ_TAIL_PTR {
	struct {
		unsigned int	dma_queue_sq_tail_ptr : 16; /* [15..0] */
		unsigned int	reserved_0			: 16; /* [31..16] */
	} bits;

	unsigned int	u32;
};

union U_DMA_QUEUE_CTRL0 {
	struct {
		unsigned int	dma_queue_en			: 1; /* [0] */
		unsigned int	dma_queue_icg_en		: 1; /* [1] */
		unsigned int	reserved				: 1; /* [2] */
		unsigned int	dma_rst_without_cq_ack_enable : 1; /* [3] */
		unsigned int	dma_queue_pause		 : 1; /* [4] */
		unsigned int	reserved_1			  : 3; /* [7..5] */
		unsigned int	dma_queue_arb_weight	: 8; /* [15..8] */
		unsigned int	reserved_2			  : 3; /* [18...16] */
		unsigned int	dma_queue_cq_mrg_en	 : 1; /* [19]  */
		unsigned int	dma_queue_cq_mrg_time   : 2; /* [21..20] */
		unsigned int	dma_queue_local_err_done_int_en	 : 1; /* [22] */
		unsigned int	dma_queue_remote_err_done_int_en	: 1; /* [23] */
		unsigned int	reserved_3				: 1; /* [24] */
		unsigned int	dma_queue_cq_full_disable		   : 1; /* [25] */
		unsigned int	dma_queue_cq_drct_sel			   : 1; /* [26] */
		unsigned int	dma_queue_sq_drct_sel			   : 1; /* [27] */
		unsigned int	dma_queue_sq_pa_lkp_err_abort_en	: 1; /* [28] */
		unsigned int	dma_queue_sq_proc_err_abort_en	  : 1; /* [29] */
		unsigned int	dma_queue_sq_drop_err_abort_en	  : 1; /* [30] */
		unsigned int	dma_queue_sq_cfg_err_abort_en	   : 1; /* [31] */
	} bits;

	unsigned int	u32;
};

union U_DMA_QUEUE_CTRL1 {
	struct {
		unsigned int	dma_queue_reset	   : 1; /* [0] */
		unsigned int	dma_queue_abort_exit  : 1; /* [1] */
		unsigned int	dma_va_enable		 : 1; /* [2] */
		unsigned int	reserved_0			: 1; /* [3] */
		unsigned int	dma_queue_port_num	: 4; /* [7..4] */
		unsigned int	dma_queue_remote_msi_x_mask : 1; /* [8] */
		unsigned int	dma_va_enable_sq			: 1; /* [9] */
		unsigned int	dma_va_enable_cq			: 1; /* [10] */
		unsigned int	dma_queue_local_pfx_er	  : 1; /* [11] */
		unsigned int	dma_queue_local_pfx_pmr	 : 1; /* [12] */
		unsigned int	reserved_1				  : 3; /* [15...13] */
		unsigned int	dma_queue_qos_en			: 1; /* [16] */
		unsigned int	dma_queue_qos			   : 4; /* [20...17] */
		unsigned int	dma_queue_mpam_id		   : 11; /* [31..21] */
	} bits;

	unsigned int	u32;
};

union U_DMA_QUEUE_FSM_STS {
	struct {
		unsigned int	dma_queue_sts		 : 4; /* [3..0] */
		unsigned int	dma_queue_not_work	: 1; /* [4] */
		unsigned int	dma_queue_wait_spd_data_sts : 1; /* [5] */
		unsigned int	reserved_0			: 1; /* [6] */
		unsigned int	reserved_1			: 1; /* [7] */
		unsigned int	dma_queue_sub_fsm_sts : 3; /* [10..8] */
		unsigned int	reserved_2			: 21; /* [31..11] */
	} bits;

	unsigned int	u32;
};

union U_DMA_QUEUE_INT_STS {
	struct {
		unsigned int	dma_queue_done_int_sts  : 1; /* [0] */
		unsigned int	dma_queue_err00_int_sts : 1; /* [1] */
		unsigned int	dma_queue_err01_int_sts : 1; /* [2] */
		unsigned int	dma_queue_err02_int_sts : 1; /* [3] */
		unsigned int	dma_queue_err03_int_sts : 1; /* [4] */
		unsigned int	reserved				: 1; /* [5] */
		unsigned int	dma_queue_err05_int_sts : 1; /* [6] */
		unsigned int	dma_queue_err06_int_sts : 1; /* [7] */
		unsigned int	dma_queue_err07_int_sts : 1; /* [8] */
		unsigned int	dma_queue_err08_int_sts : 1; /* [9] */
		unsigned int	dma_queue_err09_int_sts : 1; /* [10] */
		unsigned int	dma_queue_err10_int_sts : 1; /* [11] */
		unsigned int	dma_queue_err11_int_sts : 1; /* [12] */
		unsigned int	dma_queue_err12_int_sts : 1; /* [13] */
		unsigned int	dma_queue_err13_int_sts : 1; /* [14] */
		unsigned int	dma_queue_err14_int_sts : 1; /* [15] */
		unsigned int	dma_queue_err15_int_sts : 1; /* [16] */
		unsigned int	dma_queue_err16_int_sts : 1; /* [17] */
		unsigned int	dma_queue_err17_int_sts : 1; /* [18] */
		unsigned int	reserved_0			 : 13; /* [31..19] */
	} bits;

	unsigned int	u32;
};

union U_DMA_QUEUE_INT_MSK {
	struct {
		unsigned int	dma_queue_done_int_msk  : 1; /* [0] */
		unsigned int	dma_queue_err00_int_msk : 1; /* [1] */
		unsigned int	dma_queue_err01_int_msk : 1; /* [2] */
		unsigned int	dma_queue_err02_int_msk : 1; /* [3] */
		unsigned int	dma_queue_err03_int_msk : 1; /* [4] */
		unsigned int	reserved				: 1; /* [5] */
		unsigned int	dma_queue_err05_int_msk : 1; /* [6] */
		unsigned int	dma_queue_err06_int_msk : 1; /* [7] */
		unsigned int	dma_queue_err07_int_msk : 1; /* [8] */
		unsigned int	dma_queue_err08_int_msk : 1; /* [9] */
		unsigned int	dma_queue_err09_int_msk : 1; /* [10] */
		unsigned int	dma_queue_err10_int_msk : 1; /* [11] */
		unsigned int	dma_queue_err11_int_msk : 1; /* [12] */
		unsigned int	dma_queue_err12_int_msk : 1; /* [13] */
		unsigned int	dma_queue_err13_int_msk : 1; /* [14] */
		unsigned int	dma_queue_err14_int_msk : 1; /* [15] */
		unsigned int	dma_queue_err15_int_msk : 1; /* [16] */
		unsigned int	dma_queue_err16_int_msk : 1; /* [17] */
		unsigned int	dma_queue_err17_int_msk : 1; /* [18] */
		unsigned int	reserved_0			: 13 ; /* [31..19] */
	} bits;

	unsigned int	u32;
};

struct dma_ch_sq_s {
	u32 opcode : 4; /* [0~3] opcode */
	u32 drop : 1; /* [4] drop */
	u32 nw : 1; /* [5] nw */
	u32 wd_barrier : 1; /* [6] write done barrier */
	u32 rd_barrier : 1; /* [7] read done barrier */
	u32 ldie : 1; /* [8] LDIE */
	u32 rdie : 1; /* [9] rDIE */
	u32 loop_barrier : 1; /* [10] */
	u32 spd_barrier : 1; /* [11] */
	u32 attr : 3; /* [12~14] attr */
	u32 cq_disable : 1; /* [15] reserved */
	u32 addrt : 2; /* [16~17] at */
	u32 p3p4 : 2; /* [18~19] P3 P4 */
	u32 pf : 3; /* [20~22] pf */
	u32 vfen : 1; /* [23] vfen */
	u32 vf : 8; /* [24~31] vf */
	u32 pasid : 20; /* [0~19] pasid */
	u32 er : 1; /* [20] er */
	u32 pmr : 1; /* [21] pmr */
	u32 prfen : 1; /* [22] prfen */
	u32 reserved5 : 1; /* [23] reserved */
	u32 msi : 8; /* [24~31] MSI/MSI-X vector */
	u32 flow_id : 8; /* [0~7] Flow ID */
	u32 reserved6 : 8; /* [8~15] reserved */
	u32 TH : 1; /* [16] TH */
	u32 PH : 2; /* [17~18] PH */
	u32 reserved7 : 13; /* [19~31] reserved: some multiplex fields */
	u32 length;
	u32 src_addr_l;
	u32 src_addr_h;
	u32 dst_addr_l;
	u32 dst_addr_h;
};

struct dma_ch_cq_s {
	u32 reserved1;
	u32 reserved2;
	u32 sqhd : 16;
	u32 reserved3 : 16;
	u32 reserved4 : 16; /* [0~15] reserved */
	u32 vld : 1; /* [16] vld */
	u32 status : 15; /* [17~31] status */
};

#endif /* EDMA_DRV_H */
