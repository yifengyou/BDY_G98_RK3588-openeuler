/* SPDX-License-Identifier: GPL-2.0 */
/* Huawei iBMA driver.
 * Copyright (c) 2017, Huawei Technologies Co., Ltd.
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

#ifndef _BMA_KER_INTF_H_
#define _BMA_KER_INTF_H_

typedef long		__kernel_time_t;
#define BAD_FUNC_ADDR(x) ((0xFFFFFFFF == (x)) || (0 == (x)))

enum {
	/* 0 -127 msg */
	TYPE_LOGIC_PARTITION = 0,
	TYPE_UPGRADE = 1,
	TYPE_CDEV = 2,
	TYPE_VETH = 0x40,
	TYPE_MAX = 128,

	TYPE_KBOX = 129,
	TYPE_EDMA_DRIVER = 130,
	TYPE_UNKNOWN = 0xff,
};

enum dma_direction_e {
	BMC_TO_HOST = 0,
	HOST_TO_BMC = 1,
};

enum dma_type_e {
	DMA_NOT_LIST = 0,
	DMA_LIST = 1,
};

enum intr_mod {
	INTR_DISABLE = 0,
	INTR_ENABLE = 1,
};

enum addr_type {
	TYPE_EDMA_ADDR = 0,
	TYPE_VETH_ADDR = 1,
};

enum pci_type_e {
	PCI_TYPE_UNKNOWN,
	PCI_TYPE_171x,
	PCI_TYPE_1712
};

struct bma_dma_addr_s {
	dma_addr_t dma_addr;
	u32 dma_data_len;
};

struct dma_transfer_s {
	struct bma_dma_addr_s host_addr;
	struct bma_dma_addr_s bmc_addr;
};

struct dmalist_transfer_s {
	dma_addr_t dma_addr;
};

union transfer_u {
	struct dma_transfer_s nolist;
	struct dmalist_transfer_s list;
};

struct bspveth_dmal {
	u32 chl;
	u32 len;
	u32 slow;
	u32 shi;
	u32 dlow;
	u32 dhi;
};

struct bma_dma_transfer_s {
	enum dma_type_e type;
	enum dma_direction_e dir;
	union transfer_u transfer;
	struct bspveth_dmal *pdmalbase_v;
	u32 dmal_cnt;
};

struct bma_map_addr_s {
	enum pci_type_e pci_type;
	u32 host_number;
	enum addr_type addr_type;
	u32 addr;
};

int bma_intf_register_int_notifier(struct notifier_block *nb);
void bma_intf_unregister_int_notifier(struct notifier_block *nb);
int bma_intf_register_type(u32 type, u32 sub_type, enum intr_mod support_int,
			   void **handle);
int bma_intf_unregister_type(void **handle);
int bma_intf_check_dma_status(enum dma_direction_e dir);
int bma_intf_start_dma(void *handle, struct bma_dma_transfer_s *dma_transfer);
int bma_intf_int_to_bmc(void *handle);
void bma_intf_set_open_status(void *handle, int s);
int bma_intf_is_link_ok(void);
void bma_intf_reset_dma(enum dma_direction_e dir);
void bma_intf_clear_dma_int(enum dma_direction_e dir);

int bma_cdev_recv_msg(void *handle, char __user *data, size_t count);
int bma_cdev_add_msg(void *handle, const char __user *msg, size_t msg_len);

unsigned int bma_cdev_check_recv(void *handle);
void *bma_cdev_get_wait_queue(void *handle);
int bma_intf_check_edma_supported(void);

enum pci_type_e get_pci_type(void);
void set_pci_type(enum pci_type_e type);

int bma_intf_get_host_number(unsigned int *host_number);
int bma_intf_get_map_address(enum addr_type type, phys_addr_t *addr);

#define HOST_NUMBER_0 0
#define HOST_NUMBER_1 1

#define EDMA_1711_HOST0_ADDR 0x84810000
#define VETH_1711_HOST0_ADDR 0x84820000
#define EDMA_1712_HOST0_ADDR 0x85400000
#define VETH_1712_HOST0_ADDR 0x85410000
#define EDMA_1712_HOST1_ADDR 0x87400000
#define VETH_1712_HOST1_ADDR 0x87410000

#endif
