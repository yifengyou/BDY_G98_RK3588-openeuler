/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef _ENFS_REMOUNT_
#define _ENFS_REMOUNT_
#include <linux/string.h>
#include "enfs.h"

int enfs_remount(struct nfs_client *nfs_client, void *enfs_option);
int enfs_remount_iplist(struct nfs_client *nfs_client, void *enfs_option);

#endif
