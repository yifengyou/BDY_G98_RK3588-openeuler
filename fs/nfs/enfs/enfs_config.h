/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#ifndef ENFS_CONFIG_H
#define ENFS_CONFIG_H

#include <linux/types.h>

#define ENFS_VERSION 4 // client version is 4, server version is 10002
#define ENFS_SERVER_VERSION_BASE 10001 // 24A server version is 10001

#define ENFS_PM_PING_TMIE_OUT 3

enum {
	ENFS_MULTIPATH_ENABLE = 0,
	ENFS_MULTIPATH_DISABLE = 1,
};

enum {
	ENFS_LOADBALANCE_RR,
	ENFS_LOADBALANCE_SHARDVIEW,
};

enum {
	ENFS_LOOKUPCACHE_DISABLE = 0,
	ENFS_LOOKUPCACHE_ENABLE = 1,
};

enum {
	ENFS_V0 = 0,
	ENFS_V1,
	ENFS_V2,
	ENFS_V3,
	ENFS_V4,
	ENFS_VERSION_BUTT
};

int32_t enfs_get_config_path_detect_interval(void);
int32_t enfs_get_config_path_detect_timeout(void);
int32_t enfs_get_config_multipath_timeout(void);
int32_t enfs_get_config_multipath_state(void);
int32_t enfs_get_config_loadbalance_mode(void);
int32_t enfs_get_config_dns_update_interval(void);
int32_t enfs_get_config_dns_auto_multipath_resolution(void);
int32_t enfs_get_config_shardview_update_interval(void);
int32_t enfs_get_config_lookupcache_interval(void);
int32_t enfs_get_config_lookupcache_state(void);
int32_t enfs_get_config_link_count_per_mount(void);
int32_t enfs_get_config_link_count_total(void);
int32_t enfs_get_native_link_io_status(void);
int32_t enfs_get_create_path_no_route(void);
bool enfs_check_config_wwn(uint64_t wwn);
bool enfs_whitelist_filte(char *ip_addr);
int32_t enfs_config_load(void);
int32_t enfs_config_timer_init(void);
void enfs_config_timer_exit(void);
int GetEnfsConfigIpFiltersCount(void);
#endif // ENFS_CONFIG_H
