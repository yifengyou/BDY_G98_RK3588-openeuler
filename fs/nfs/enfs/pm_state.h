/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#ifndef PM_STATE_H
#define PM_STATE_H

#include <linux/types.h>
#include <linux/sunrpc/xprt.h>

enum enfs_path_state {
	PM_STATE_INIT,
	PM_STATE_NORMAL,
	PM_STATE_UNSTABLE,
	PM_STATE_FAULT,
	PM_STATE_UNDEFINED	// xprt is not multipath xprt
};

void pm_set_path_state(struct rpc_xprt *xprt, enum enfs_path_state state);
enum enfs_path_state pm_get_path_state(struct rpc_xprt *xprt);

static inline bool enfs_is_path_connected(enum enfs_path_state state)
{
	if (state == PM_STATE_NORMAL || state == PM_STATE_UNSTABLE)
		return true;
	return false;
}

void pm_get_path_state_desc(struct rpc_xprt *xprt, char *buf, int len);
void pm_get_xprt_state_desc(struct rpc_xprt *xprt, char *buf, int len);

#endif // PM_STATE_H
