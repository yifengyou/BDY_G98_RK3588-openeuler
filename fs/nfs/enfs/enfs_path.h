/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#ifndef ENFS_PATH_H
#define ENFS_PATH_H

int enfs_alloc_xprt_ctx(struct rpc_xprt *xprt);
void enfs_free_xprt_ctx(struct rpc_xprt *xprt);

#endif // ENFS_PATH_H
