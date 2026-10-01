/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef ENFS_ROUNDROBIN_H
#define ENFS_ROUNDROBIN_H

int enfs_lb_set_policy(struct rpc_clnt *clnt, void *data);
int enfs_lb_init(void);
void enfs_lb_exit(void);
const struct rpc_xprt_iter_ops *enfs_xprt_rr_ops(void);
const struct rpc_xprt_iter_ops *enfs_xprt_singular_ops(void);
bool enfs_is_rr_route(struct rpc_clnt *cln);
bool enfs_is_singularr_route(struct rpc_clnt *cln);
#endif
