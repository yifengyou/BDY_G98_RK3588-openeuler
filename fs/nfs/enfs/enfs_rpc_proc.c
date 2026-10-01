// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/clnt.h>
#include "enfs_rpc_proc.h"
#include "enfs_lookup_cache.h"
#include "enfs_log.h"
#include "enfs.h"

struct rpc_procinfo enfs_procedures[ENFSPROC_MAX] = { { 0 } };
static ktime_t start_enfs_rpc_send = { 0 };

static bool start_enfs_rpc_send_init;

int enfs_rpc_send(struct rpc_clnt *clnt, unsigned int opcode,
		  struct enfs_get_onfig_args *args, struct enfs_get_onfig_res *res)
{
	int status;
	int32_t interval_ms = 5 * 60 * 1000;

	struct rpc_message msg = {
		.rpc_proc = &enfs_procedures[opcode],
		.rpc_argp = args,
		.rpc_resp = res,
	};

	status = rpc_call_sync(clnt, &msg,
			       RPC_TASK_SOFT | RPC_TASK_TIMEOUT |
				       RPC_TASK_SOFTCONN | RPC_TASK_ENFS);
	/* every 5 minutes prints the error log */
	if (status) {
		if (!start_enfs_rpc_send_init) {
			start_enfs_rpc_send_init = true;
			start_enfs_rpc_send = ktime_get();
		} else if (enfs_timeout_ms(&start_enfs_rpc_send, interval_ms)) {
			enfs_log_error("NFS reply failed, status:%d\n",
				       status);
			start_enfs_rpc_send = ktime_get();
		}
	}
	return status;
}

void enfs_proc_reg(unsigned int opcode, const struct rpc_procinfo *rpc_proc)
{
	enfs_procedures[opcode] = *rpc_proc;
}
