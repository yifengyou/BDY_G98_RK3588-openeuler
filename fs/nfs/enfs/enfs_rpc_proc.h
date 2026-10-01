/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#ifndef _ENFS_RPC_PROC_H_
#define _ENFS_RPC_PROC_H_

#include "enfs_lookup_cache.h"

#define ENFS_RPC_PROG_NUM 733301
#define ENFS_RPC_PROG_VERSION 1
#define ENFSPROC_MAX 2
#define ENFSPROC_LOOKUPCACHE 0

int enfs_rpc_send(struct rpc_clnt *clnt, unsigned int opcode,
		  struct enfs_get_onfig_args *args, struct enfs_get_onfig_res *res);
void enfs_proc_reg(unsigned int opcode, const struct rpc_procinfo *rpc_proc);
#endif
