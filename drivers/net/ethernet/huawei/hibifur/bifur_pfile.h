/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */
#ifndef BIFUR_PFILE_H
#define BIFUR_PFILE_H

#define BIFUR_MAX_CMD_LEN   (1024 * 1024)

#define BIFUR_MOD_NAME "bifur"
#define BIFUR_PROC_DIR_MOD 0550

#define BUFUR_CHECK_KFREE(m) \
do {				 \
	kfree(m);		 \
	m = NULL;		\
} while (0)

#define BIFUR_UNREF_PARAM(x)  ((x))

struct bifur_msg {
	u32 drv_cmd;
	u32 in_buf_len;
	u32 out_buf_len;
	u32 out_data_len;
	void *in_buf;
	void *out_buf;
	u8 rsvd[24];
};

enum bifur_cmd_m {
	BIFUR_DRV_CMD_FUNC_ATTR_GET = 1,
	BIFUR_DRV_CMD_VF_ALLOC,
	BIFUR_DRV_CMD_MAC_GET,
	BIFUR_CMD_BUTT
};

struct bifur_vf_alloc_cmd_msg {
	unsigned int dbdf;
};

struct bifur_vf_alloc_cmd_rsp {
	u32 vf_dbdf;
};

struct bifur_func_attr_get_cmd_msg {
	unsigned int dbdf;
};

struct bifur_func_attr_get_cmd_rsp {
	u32 func_type;
};

struct bifur_mac_get_cmd_msg {
	unsigned int dbdf;
};

struct bifur_mac_get_cmd_rsp {
	u8 mac[6];
};

struct bifur_global_file_list_t {
	struct mutex lock;
	struct list_head list;
};

struct bifur_proc_file_t {
	struct list_head node;
	pid_t pid;
};

int bifur_global_dev_init(void);
void bifur_global_dev_uninit(void);

#endif // BIFUR_PFILE_H
