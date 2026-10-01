/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2025. All rights reserved.
 *
 * Description: Functions definition of ipourma_fs
 */

#ifndef _IPOURMA_FS_H
#define _IPOURMA_FS_H

#include "ipourma_types.h"

int ipourma_register_debugfs(void);
void ipourma_unregister_debugfs(void);
void ipourma_create_debug_files(struct ipourma_dev_priv *priv);
void ipourma_delete_debug_files(struct ipourma_dev_priv *priv);

#endif
