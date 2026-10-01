/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#ifndef FAILOVER_TIME_H
#define FAILOVER_TIME_H

#include <linux/sunrpc/sched.h>

void failover_adjust_task_timeout(struct rpc_task *task, void *condition);
void failover_init_task_req(struct rpc_task *task, struct rpc_rqst *req);

#endif // FAILOVER_TIME_H
