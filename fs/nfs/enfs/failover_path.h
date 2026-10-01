/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#ifndef FAILOVER_PATH_H
#define FAILOVER_PATH_H

#include <linux/sunrpc/sched.h>

void failover_handle(struct rpc_task *task);
bool failover_prepare_transmit(struct rpc_task *task);
void failover_reselect_transport(struct rpc_task *task, struct rpc_clnt *clnt);
bool failover_task_need_call_start_again(struct rpc_task *task);

#endif // FAILOVER_PATH_H
