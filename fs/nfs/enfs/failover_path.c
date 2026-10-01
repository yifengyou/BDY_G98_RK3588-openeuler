// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include "failover_path.h"
#include <linux/nfs.h>
#include <linux/nfs3.h>
#include <linux/nfs4.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/xprt.h>
#include "enfs_config.h"
#include "enfs_log.h"
#include "enfs.h"
#include "failover_com.h"
#include "pm_state.h"
#include "pm_ping.h"

enum enfs_failover_polic {
	FAILOVER_NOACTION = 1,
	FAILOVER_RETRY,
	FAILOVER_RETRY_DELAY,
	FAILOVER_RETURN_TIMEOUT,
};

static void failover_retry_path(struct rpc_task *task)
{
	int ret;

	ret = rpc_restart_call(task);

	if (ret == 1) {
		xprt_release(task);
		rpc_init_task_retry_counters(task);
		rpc_task_release_transport(task);
		task->tk_xprt = rpc_task_get_next_xprt(task->tk_client);
	}
}

static void failover_retry_path_delay(struct rpc_task *task, int32_t delay)
{
	failover_retry_path(task);
	rpc_delay(task, delay);
}

static void failover_exit_return_timeout(struct rpc_task *task)
{
	unsigned int execute = ktime_ms_delta(ktime_get(), task->tk_start);
	unsigned int config_timeout =
		enfs_get_config_path_detect_timeout() * MSEC_PER_SEC;

	if (execute > config_timeout)
		rpc_exit(task, -ETIMEDOUT);
}

static void failover_retry_path_by_policy(struct rpc_task *task,
					  enum enfs_failover_polic policy)
{
	if (policy == FAILOVER_RETRY)
		failover_retry_path(task);
	else if (policy == FAILOVER_RETRY_DELAY)
		failover_retry_path_delay(task, 3 * HZ);	// delay 3s
	else if (policy == FAILOVER_RETURN_TIMEOUT)
		failover_exit_return_timeout(task);

}

static enum enfs_failover_polic failover_get_nfs3_retry_policy(struct rpc_task *task)
{
	enum enfs_failover_polic policy = FAILOVER_NOACTION;
	const struct rpc_procinfo *procinfo = task->tk_msg.rpc_proc;
	u32 proc;

	if (unlikely(procinfo == NULL)) {
		enfs_log_error("the task contains no valid proc.\n");
		return FAILOVER_NOACTION;
	}

	proc = procinfo->p_proc;
	switch (proc) {
	case NFS3PROC_CREATE:
	case NFS3PROC_MKDIR:
	case NFS3PROC_REMOVE:
	case NFS3PROC_RMDIR:
	case NFS3PROC_SYMLINK:
	case NFS3PROC_LINK:
	case NFS3PROC_SETATTR:
	case NFS3PROC_WRITE:
		policy = FAILOVER_RETRY_DELAY;
		break;
	default:
		policy = FAILOVER_RETRY;
	}
	return policy;
}

static enum enfs_failover_polic failover_get_nfs4_retry_policy(struct rpc_task *task)
{
	enum enfs_failover_polic policy = FAILOVER_NOACTION;
	const struct rpc_procinfo *procinfo = task->tk_msg.rpc_proc;
	u32 proc_idx;

	if (unlikely(procinfo == NULL)) {
		enfs_log_error("the task contains no valid proc.\n");
		return FAILOVER_NOACTION;
	}

	proc_idx = procinfo->p_statidx;
	switch (proc_idx) {
	case NFSPROC4_CLNT_CREATE:
	case NFSPROC4_CLNT_REMOVE:
	case NFSPROC4_CLNT_LINK:
	case NFSPROC4_CLNT_SYMLINK:
	case NFSPROC4_CLNT_SETATTR:
	case NFSPROC4_CLNT_WRITE:
	case NFSPROC4_CLNT_RENAME:
	case NFSPROC4_CLNT_SETACL:
		policy = FAILOVER_RETRY_DELAY;
		break;
	default:
		policy = FAILOVER_RETRY;
	}
	return policy;
}

static enum enfs_failover_polic failover_get_retry_policy(struct rpc_task *task)
{
	struct rpc_clnt *clnt = task->tk_client;
	u32 version = clnt->cl_vers;
	enum enfs_failover_polic policy = FAILOVER_NOACTION;

	if (pm_ping_is_test_xprt_task(task))
		return FAILOVER_RETURN_TIMEOUT;
	// 1. if the task meant to send to certain xprt, take no action
	if (task->tk_flags & RPC_TASK_FIXED)
		return FAILOVER_NOACTION;
	// 2. get policy by different version of nfs protocal
	if (version == 3)	// nfs v3
		policy = failover_get_nfs3_retry_policy(task);
	else if (version == 4)	// nfs v4
		policy = failover_get_nfs4_retry_policy(task);
	else
		return FAILOVER_NOACTION;

	// 3. if the task is not send to target, retry immediately
	if (!RPC_WAS_SENT(task))
		policy = FAILOVER_RETRY;

	return policy;
}

static int failover_check_task(struct rpc_task *task)
{
	struct rpc_clnt *clnt = NULL;
	int disable_mpath = enfs_get_config_multipath_state();

	if (disable_mpath != ENFS_MULTIPATH_ENABLE) {
		enfs_log_debug("Multipath is not enabled.\n");
		return -EINVAL;
	}

	if (unlikely((task == NULL) || (task->tk_client == NULL))) {
		enfs_log_error("The task is not valid.\n");
		return -EINVAL;
	}

	clnt = task->tk_client;

	if (clnt->cl_prog != NFS_PROGRAM) {
		enfs_log_debug("The clnt is not prog{%u} type.\n",
			       clnt->cl_prog);
		return -EINVAL;
	}

	if (!failover_is_enfs_clnt(clnt)) {
		enfs_log_debug("The clnt is not a enfs-managed type.\n");
		return -EINVAL;
	}
	return 0;
}

void failover_handle(struct rpc_task *task)
{
	enum enfs_failover_polic policy;
	int ret;

	ret = failover_check_task(task);
	if (ret != 0)
		return;

	pm_set_path_state(task->tk_xprt, PM_STATE_FAULT);

	policy = failover_get_retry_policy(task);

	failover_retry_path_by_policy(task, policy);

}

static bool failover_is_task_use_fixed_path(struct rpc_task *task)
{
	if (task->tk_flags & RPC_TASK_FIXED)
		return true;

	if (pm_ping_is_test_xprt_task(task))
		return true;

	return false;
}

bool failover_task_need_call_start_again(struct rpc_task *task)
{
	int ret;

	ret = failover_check_task(task);
	if (ret != 0)
		return false;

	if (failover_is_task_use_fixed_path(task))
		return false;

	return true;
}

bool failover_prepare_transmit(struct rpc_task *task)
{
	if (failover_is_task_use_fixed_path(task))
		return true;

	if (pm_get_path_state(task->tk_xprt) == PM_STATE_FAULT) {
		task->tk_status = -ETIMEDOUT;
		return false;
	}

	return true;
}

/**
 * Reselect a normal link.
 */
static void reselect_xprt(struct rpc_task *task)
{
	rpc_task_release_transport(task);
	task->tk_xprt = rpc_task_get_next_xprt(task->tk_client);
}

void failover_reselect_transport(struct rpc_task *task, struct rpc_clnt *clnt)
{
	struct rpc_xprt **cursor = &clnt->cl_xpi.xpi_cursor;
	struct rpc_xprt *old;
	struct rpc_xprt *parent_cursor;
	struct rpc_clnt *parent_clnt = clnt;

	if (task->tk_xprt && !failover_prepare_transmit(task)) {
		reselect_xprt(task);
		return;
	}

	do {
		if (parent_clnt == parent_clnt->cl_parent)
			break;
		parent_clnt = parent_clnt->cl_parent;
	} while (parent_clnt);

	if (task->tk_xprt && clnt->cl_vers == 4 && parent_clnt &&
	    parent_clnt->cl_enfs) {
		old = smp_load_acquire(cursor); // multi thread to access
		parent_cursor = xprt_iter_get_xprt(&parent_clnt->cl_xpi);
		if (parent_cursor != old)
			smp_store_release(cursor, parent_cursor);
		// Xprt_get in xprt_iter_get_xprt, if return !NULL
		if (parent_cursor)
			xprt_put(parent_cursor);

		if (task->tk_xprt != clnt->cl_xpi.xpi_cursor &&
		    !(task->tk_flags & RPC_TASK_FIXED) &&
		    !(pm_ping_is_test_xprt_task(task))) {
			xprt_release(task);
			rpc_init_task_retry_counters(task);
			rpc_task_release_transport(task);
			task->tk_xprt = rpc_task_get_next_xprt(clnt);
		}
	}
}
