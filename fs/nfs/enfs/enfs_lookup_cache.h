/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef _ENFS_LOOKUP_CACHE_H_
#define _ENFS_LOOKUP_CACHE_H_

#include <linux/types.h>
#include <linux/nfs.h>
#include <linux/nfs_fs.h>
#include <linux/sunrpc/clnt.h>

#define MAX_EXPID_LEN 32
#define MAX_EXPSTR_LEN (MAX_EXPID_LEN * 2 + 1)
#define ENFS_LOOKUP_CACHE_LEVEL 0

#define ENFS_fhandle_sz (sizeof(struct nfs_fh))
#define ENFS_lookupcacheargs_sz (sizeof(struct enfs_get_onfig_args))
#define ENFS_lookupcacheres_sz (sizeof(struct enfs_get_onfig_res))

enum {
	ENFS_LOOKUPCACHE_ALL = 0,
	ENFS_LOOKUPCACHE_NONEG,
	ENFS_LOOKUPCACHE_NONE
};

struct lookupcache_work {
	struct nfs_fh fh;
	void *server; /* struct nfs_server pointer, don't access the mem, maybe already freed */
	struct rpc_clnt *cl_rpcclient;
	struct work_struct work_lookup;
};

struct enfs_get_onfig_args {
	unsigned int version;
	unsigned int mask;
	unsigned int reserve;
	struct nfs_fh fh;
	unsigned int vers;
};

struct enfs_get_onfig_res {
	unsigned int version;
	unsigned int mask;
	unsigned int lookupCache;
	unsigned int reserve;
	unsigned int status;
};

struct nfs_enfs_s {
	union {
		struct enfs_get_onfig_args args;
		struct enfs_get_onfig_res res;
	} enfs_u;
};

void enfs_xdr_enc_lookupcacheargs(struct rpc_rqst *rqstp,
				  struct xdr_stream *xdr, const void *data);
int enfs_xdr_dec_lookupcacheres(struct rpc_rqst *req, struct xdr_stream *xdr,
				void *data);

#define PROC(proc, argtype, restype, timer)                    \
	.p_proc = ENFSPROC_##proc,                             \
	.p_encode = (kxdreproc_t)enfs_xdr_enc_##argtype##args, \
	.p_decode = (kxdrdproc_t)enfs_xdr_dec_##restype##res,  \
	.p_arglen = ENFS_##argtype##args_sz,                   \
	.p_replen = ENFS_##restype##res_sz, .p_timer = timer,  \
	.p_statidx = ENFSPROC_##proc, .p_name = #proc,

int enfs_lookupcache_init(void);
void enfs_lookupcache_fini(void);
void enfs_trigger_get_capability(struct nfs_server *nfs_server);

#endif
