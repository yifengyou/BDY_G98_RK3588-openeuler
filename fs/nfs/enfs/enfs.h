/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#ifndef _ENFS_H_
#define _ENFS_H_
#include <linux/atomic.h>
#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs3.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include "linux/sunrpc/sunrpc_enfs_adapter.h"
#include "../enfs_adapter.h"

#define IP_ADDRESS_LEN_MAX 64
#define MAX_IP_PAIR_PER_MOUNT 8
#define MAX_IP_INDEX (MAX_IP_PAIR_PER_MOUNT)
#define MAX_SUPPORTED_LOCAL_IP_COUNT 8
#define MAX_SUPPORTED_REMOTE_IP_COUNT 1024
#define DEFAULT_SUPPORTED_REMOTE_IP_COUNT 32
#define MIN_SUPPORTED_REMOTE_IP_COUNT 2

#define MAX_DNS_NAME_LEN 512
#define MAX_DNS_SUPPORTED 2
#define ENFS_NOT_SUPPORT 524

#define ENFS_MAX_LINK_COUNT 16384
#define DEFAULT_ENFS_MAX_LINK_COUNT 512
#define MIN_ENFS_MAX_LINK_COUNT 512

#define ENFS_MAX_MOUNT_COUNT 256
#define EXTEND_MAX_DNS_NAME_LEN 256

#define ENFS_UNSTABLE_STATE_TIMEOUT	(30 * 60) /* seconds */
#define ENFS_RECONNECT_TIME_CNT	3

struct nfs_ip_list {
	int count;
	struct sockaddr_storage address[MAX_SUPPORTED_REMOTE_IP_COUNT];
	size_t addrlen[MAX_SUPPORTED_REMOTE_IP_COUNT];
};

struct enfs_dns_info_single {
	char dnsname[MAX_DNS_NAME_LEN]; // valid only if dnsExist is true
};

struct enfs_route_dns_info {
	int dnsNameCount; /* Count of DNS name in the list */
	struct enfs_dns_info_single routeRemoteDnsList[MAX_DNS_SUPPORTED];
};

struct enfs_reconnect_time {
	s64 time[ENFS_RECONNECT_TIME_CNT + 1];
	s8 head;
	s8 tail;
	s8 reserve[2];
	unsigned int xprt_cookie;
};

struct rpc_iostats;
struct enfs_xprt_context {
	int version;
	struct sockaddr_storage srcaddr;
	struct rpc_iostats *stats;
	bool main;
	atomic_t path_state;
	atomic_t path_check_state;
	atomic_long_t queuelen;
	uint64_t lsid;
	uint64_t wwn;
	uint32_t cpuId;
	u32 protocol; // TCP or UDP or RDMA
	int64_t lastTime;
	struct enfs_reconnect_time reconnect_time;
	u32 reserve[30];
};

static inline bool enfs_is_main_xprt(struct rpc_xprt *xprt)
{
	struct enfs_xprt_context *ctx = xprt_get_reserve_context(xprt);

	if (!ctx)
		return false;
	return ctx->main;
}

static inline bool enfs_timeout_ms(ktime_t *start, int ms)
{
	ktime_t stop = ktime_get();

	if (ktime_to_ms(ktime_sub(stop, *start)) > ms)
		return true;
	return false;
}

static inline void enfs_msleep(long ms)
{
	long sleep_time;
	long schedule_timeo;

	sleep_time = (long)((ms * HZ) / 1000);

	set_current_state(TASK_INTERRUPTIBLE);
	if (sleep_time <= 0)
		schedule_timeo = schedule_timeout(1);
	else
		schedule_timeo = schedule_timeout(sleep_time);

	while (schedule_timeo > 0) {
		schedule_timeo = schedule_timeout(schedule_timeo);
		if (schedule_timeo > sleep_time)
			return;
	}
}

bool enfs_insert_ip_list(struct nfs_ip_list *ip_list, int max,
			 struct sockaddr_storage *addr);
bool enfs_ip_list_contain(struct nfs_ip_list *ip_list,
			  struct sockaddr_storage *addr);

bool enfs_link_count_add(int num);
int enfs_link_count_num(void);
void enfs_clnt_get_linkcap(struct rpc_clnt *clnt);
void enfs_clnt_release_linkcap(struct rpc_clnt *clnt);
bool enfs_mount_count_add(int num);
int enfs_mount_count(void);

struct rpcclnt_release_item {
	struct list_head node;
	struct rpc_clnt *clnt;
};

struct clnt_release_item {
	struct list_head node;
	struct nfs_client *client;
	struct rpc_clnt *clnt;
};

void enfs_destroy_clnt_list(struct list_head *head);
void enfs_destroy_rpcclnt_list(struct list_head *head);

#endif
