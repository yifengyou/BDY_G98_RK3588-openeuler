// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#include <linux/sunrpc/metrics.h>
#include <linux/sunrpc/xprt.h>

#include "enfs.h"
#include "enfs_log.h"
#include "enfs_path.h"

// only create ctx in this function
// alloc iostat memory in create_clnt
int enfs_alloc_xprt_ctx(struct rpc_xprt *xprt)
{
	struct enfs_xprt_context *ctx;

	if (!xprt) {
		enfs_log_error("invalid xprt pointer.\n");
		return -EINVAL;
	}

	ctx = kzalloc(sizeof(struct enfs_xprt_context), GFP_KERNEL);
	if (!ctx) {
		enfs_log_error("add xprt test failed.\n");
		return -ENOMEM;
	}

	xprt_set_reserve_context(xprt, (void *)ctx);
	return 0;
}

// free multi_context and iostat memory
void enfs_free_xprt_ctx(struct rpc_xprt *xprt)
{
	struct enfs_xprt_context *ctx = xprt_get_reserve_context(xprt);

	if (ctx) {
		if (ctx->stats) {
			rpc_free_iostats(ctx->stats);
			ctx->stats = NULL;
		}
		kfree(ctx);
		xprt_set_reserve_context(xprt, NULL);
	}
}
