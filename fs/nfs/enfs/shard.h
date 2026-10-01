/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#ifndef _ENFS_SHARD_H_
#define _ENFS_SHARD_H_

#include  <linux/types.h>
#include "exten_call.h"

void shard_set_transport(struct rpc_task *task, struct rpc_clnt *clnt);
int enfs_debug_match_cmd(char *str, size_t len);
int enfs_shard_init(void);
void enfs_shard_exit(void);

int enfs_delete_clnt_shard_cache(struct rpc_clnt *clnt);
void enfs_query_xprt_shard(struct rpc_clnt *clnt, struct rpc_xprt *xprt);
#endif // _ENFS_SHARD_H_
