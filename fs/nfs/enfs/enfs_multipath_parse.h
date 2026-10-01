/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef _ENFS_MULTIPATH_PARSE_H_
#define _ENFS_MULTIPATH_PARSE_H_

#include "enfs.h"

struct multipath_mount_options {
	int version;
	struct nfs_ip_list *remote_ip_list;
	struct nfs_ip_list *local_ip_list;
	struct enfs_route_dns_info *pRemoteDnsInfo;
	u32 fill_local;
	u32 reserve[2];
};

int nfs_multipath_parse_options(enum nfsmultipathoptions type, char *str,
				void **enfs_option, struct net *net_ns);
int nfs_multipath_alloc_options(void **enfs_option);
void nfs_multipath_free_options(void **enfs_option);
void enfs_set_mount_data(void **enfs_option, const char *hostname);

#endif
