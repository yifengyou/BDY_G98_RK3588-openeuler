/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 HiSilicon Technologies Co., Ltd. All rights reserved. */

#ifndef _UB_CDMA_CDMA_API_H_
#define _UB_CDMA_CDMA_API_H_

#include <linux/atomic.h>
#include <linux/kabi.h>
#include <uapi/ub/cdma/cdma_abi.h>

/**
 * struct dma_device - DMA device structure
 * @attr: CDMA device attribute info: EID, UPI etc
 * @ref_cnt: reference count for adding a context to device
 * @private_data: cdma context resources pointer
 */
struct dma_device {
	struct cdma_device_attr attr;
	atomic_t ref_cnt;
	void *private_data;
	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
};

enum dma_cr_opcode {
	DMA_CR_OPC_SEND = 0x00,
	DMA_CR_OPC_SEND_WITH_IMM,
	DMA_CR_OPC_SEND_WITH_INV,
	DMA_CR_OPC_WRITE_WITH_IMM,
};

/**
 * union dma_cr_flag - DMA completion record flag
 * @bs: flag bit value structure
 * @value: flag value
 */
union dma_cr_flag {
	struct {
		u8 s_r : 1; /* indicate CR stands for sending or receiving */
		u8 jetty : 1; /* indicate id in the CR stands for jetty or JFS */
		u8 suspend_done : 1; /* suspend done flag */
		u8 flush_err_done : 1; /* flush error done flag */
		u8 reserved : 4;
	} bs;
	u8 value;
};

/**
 * struct dma_cr - DMA completion record structure
 * @status: completion record status
 * @user_ctx: user private data information, optional
 * @opcode: DMA operation code
 * @flag: completion record flag
 * @completion_len: the number of bytes transferred
 * @local_id: local JFS ID
 * @remote_id: remote JFS ID, not in use for now
 * @tpn: transport number
 */
struct dma_cr {
	enum dma_cr_status status;
	u64 user_ctx;
	enum dma_cr_opcode opcode;
	union dma_cr_flag flag;
	u32 completion_len;
	u32 local_id;
	u32 remote_id;
	u32 tpn;
	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
};

/**
 * struct queue_cfg - DMA queue config structure
 * @queue_depth: queue depth
 * @priority: the priority of JFS, ranging from [0, 15]
 * @user_ctx: user private data information, optional
 * @dcna: remote device CNA
 * @rmt_eid: remote device EID
 */
struct queue_cfg {
	u32 queue_depth;
	u8 priority;
	u64 user_ctx;
	u32 dcna;
	struct dev_eid rmt_eid;
	u32 trans_mode;
	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
};

/**
 * struct dma_seg - DMA segment structure
 * @handle: segment resource handle
 * @sva: payload virtual address
 * @len: payload data length
 * @tid: payload token id
 * @token_value: not used for now
 * @token_value_valid: not used for now
 */
struct dma_seg {
	u64 handle;
	u64 sva;
	u64 len;
	u32 tid; /* data valid only in bit 0-19 */
	u32 token_value;
	bool token_value_valid;
	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
};

struct dma_seg_cfg {
	u64 sva;
	u64 len;
	u32 token_value;
	bool token_value_valid;
	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
};

/**
 * struct dma_context - DMA context structure
 * @dma_dev: DMA device pointer
 * @tid: token id for segment
 */
struct dma_context {
	struct dma_device *dma_dev;
	u32 tid; /* data valid only in bit 0-19 */
};

enum dma_status {
	DMA_STATUS_OK,
	DMA_STATUS_INVAL,
};

/**
 * struct dma_cas_data - DMA CAS data structure
 * @compare_data: compare data, length <= 8B: CMP value, length > 8B: data address
 * @swap_data: swap data, length <= 8B: swap value, length > 8B: data address
 */
struct dma_cas_data {
	u64 compare_data;
	u64 swap_data;
	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
};

/**
 * struct dma_notify_data - DMA write witch notify data structure
 * @notify_seg: notify segment pointer
 * @notify_data: notify data value
 */
struct dma_notify_data {
	struct dma_seg *notify_seg;
	u64 notify_data;
	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
};

/**
 * struct dma_client - DMA register client structure
 * @list_node: client list
 * @client_name: client name pointer
 * @add: add DMA resource function pointer
 * @remove: remove DMA resource function pointer
 * @stop: stop DMA operation function pointer
 */
struct dma_client {
	struct list_head list_node;
	char *client_name;
	int (*add)(u32 eid);
	void (*remove)(u32 eid);
	void (*stop)(u32 eid);
	KABI_RESERVE(1)
	KABI_RESERVE(2)
	KABI_RESERVE(3)
	KABI_RESERVE(4)
};

struct dma_device *dma_get_device_list(u32 *num_devices);

void dma_free_device_list(struct dma_device *dev_list, u32 num_devices);

struct dma_device *dma_get_device_by_eid(struct dev_eid *eid);

int dma_create_context(struct dma_device *dma_dev);

void dma_delete_context(struct dma_device *dma_dev, int handle);

int dma_alloc_queue(struct dma_device *dma_dev, int ctx_id,
		    struct queue_cfg *cfg);

void dma_free_queue(struct dma_device *dma_dev, int queue_id);

struct dma_seg *dma_register_seg(struct dma_device *dma_dev, int ctx_id,
				 struct dma_seg_cfg *cfg);

void dma_unregister_seg(struct dma_device *dma_dev, struct dma_seg *dma_seg);

struct dma_seg *dma_import_seg(struct dma_seg_cfg *cfg);

void dma_unimport_seg(struct dma_seg *dma_seg);

enum dma_status dma_write(struct dma_device *dma_dev, struct dma_seg *rmt_seg,
			  struct dma_seg *local_seg, int queue_id);

enum dma_status dma_write_with_notify(struct dma_device *dma_dev,
				      struct dma_seg *rmt_seg,
				      struct dma_seg *local_seg, int queue_id,
				      struct dma_notify_data *data);

enum dma_status dma_read(struct dma_device *dma_dev, struct dma_seg *rmt_seg,
			 struct dma_seg *local_seg, int queue_id);

enum dma_status dma_cas(struct dma_device *dma_dev, struct dma_seg *rmt_seg,
			struct dma_seg *local_seg, int queue_id,
			struct dma_cas_data *data);

enum dma_status dma_faa(struct dma_device *dma_dev, struct dma_seg *rmt_seg,
			struct dma_seg *local_seg, int queue_id, u64 add);

int dma_poll_queue(struct dma_device *dma_dev, int queue_id, u32 cr_cnt,
		   struct dma_cr *cr);

int dma_register_client(struct dma_client *client);

void dma_unregister_client(struct dma_client *client);

#endif /* _UB_CDMA_CDMA_API_H_ */
