// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include "failover_time.h"
#include <linux/jiffies.h>
#include <linux/sunrpc/clnt.h>
#include "enfs_config.h"
#include "enfs_log.h"
#include "failover_com.h"
#include "pm_ping.h"

static unsigned long failover_get_mulitipath_timeout(struct rpc_clnt *clnt)
{
	unsigned long config_tmo = enfs_get_config_multipath_timeout() * HZ;
	unsigned long clnt_tmo = clnt->cl_timeout->to_initval;

	if (config_tmo == 0)
		return clnt_tmo;

	return config_tmo > clnt_tmo ? clnt_tmo : config_tmo;
}

void failover_adjust_task_timeout(struct rpc_task *task, void *condition)
{
	struct rpc_clnt *clnt = NULL;
	unsigned long tmo;
	int disable_mpath = enfs_get_config_multipath_state();

	if (disable_mpath != ENFS_MULTIPATH_ENABLE) {
		enfs_log_debug("Multipath is not enabled.\n");
		return;
	}

	clnt = task->tk_client;
	if (unlikely(clnt == NULL)) {
		enfs_log_error("task associate client is NULL.\n");
		return;
	}

	if (!failover_is_enfs_clnt(clnt)) {
		enfs_log_debug("The clnt is not a enfs-managed type.\n");
		return;
	}

	tmo = failover_get_mulitipath_timeout(clnt);
	if (tmo == 0) {
		enfs_log_debug("Multipath is not enabled.\n");
		return;
	}

	if (task->tk_timeout != 0) {
		task->tk_timeout = task->tk_timeout < tmo ? task->tk_timeout :
							    tmo;
	} else {
		task->tk_timeout = tmo;
	}
}

static unsigned long get_normal_io_req_timeout(struct rpc_rqst *req)
{
	unsigned long rq_majortimeo = req->rq_timeout;
	const struct rpc_timeout *to = req->rq_task->tk_client->cl_timeout;

	if (to->to_exponential)
		rq_majortimeo <<= to->to_retries;
	else
		rq_majortimeo += to->to_increment * to->to_retries;
	if (rq_majortimeo > to->to_maxval || rq_majortimeo == 0)
		rq_majortimeo = to->to_maxval;
	return rq_majortimeo;
}

void failover_init_task_req(struct rpc_task *task, struct rpc_rqst *req)
{
	struct rpc_clnt *clnt = NULL;
	unsigned long current_timeout;
	unsigned long timeout = 0;
	int disable_mpath = enfs_get_config_multipath_state();

	if (disable_mpath != ENFS_MULTIPATH_ENABLE) {
		enfs_log_debug("Multipath is not enabled.\n");
		return;
	}

	clnt = task->tk_client;
	if (unlikely(clnt == NULL)) {
		enfs_log_error("task associate client is NULL.\n");
		return;
	}

	if (!failover_is_enfs_clnt(clnt)) {
		enfs_log_debug("The clnt is not a enfs-managed type.\n");
		return;
	}

	if (!pm_ping_is_test_xprt_task(task)) {
		timeout = get_normal_io_req_timeout(req);
		req->rq_timeout = failover_get_mulitipath_timeout(clnt);
	} else {
		req->rq_timeout = enfs_get_config_path_detect_timeout() * HZ;
		timeout = (unsigned long)enfs_get_config_path_detect_timeout() *
			  HZ;
	}

	current_timeout = (ktime_ms_delta(ktime_get(), task->tk_start)) * HZ /
			  MSEC_PER_SEC;
	if (timeout > current_timeout)
		req->rq_majortimeo = (timeout - current_timeout) + jiffies;
	else
		req->rq_majortimeo = jiffies;

}
