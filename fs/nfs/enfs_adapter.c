// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <linux/types.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs3.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/sunrpc/sched.h>
#include <linux/nfs_iostat.h>
#include <linux/nfs_mount.h>
#include "enfs_adapter.h"
#include "iostat.h"
#include "enfs/enfs_log.h"

static struct enfs_adapter_ops __rcu *enfs_adapter;

static DEFINE_MUTEX(enfs_module_mutex);

int enfs_adapter_register(struct enfs_adapter_ops *ops)
{
	struct enfs_adapter_ops *old;

	old = cmpxchg((struct enfs_adapter_ops **)&enfs_adapter, NULL, ops);
	if (old == NULL || old == ops)
		return 0;
	enfs_log_error("regist enfs_adapter ops %p fail. old %p\n", ops, old);
	return -EPERM;
}
EXPORT_SYMBOL_GPL(enfs_adapter_register);

int enfs_adapter_unregister(struct enfs_adapter_ops *ops)
{
	struct enfs_adapter_ops *old;

	old = cmpxchg((struct enfs_adapter_ops **)&enfs_adapter, ops, NULL);
	if (old == ops || old == NULL)
		return 0;
	enfs_log_error("unregist enfs_adapter ops %p fail. old %p\n", ops, old);
	return -EPERM;
}
EXPORT_SYMBOL_GPL(enfs_adapter_unregister);

struct enfs_adapter_ops *nfs_multipath_router_get(void)
{
	struct enfs_adapter_ops *ops;

	rcu_read_lock();
	ops = rcu_dereference(enfs_adapter);
	if (ops == NULL) {
		rcu_read_unlock();
		return NULL;
	}
	if (!try_module_get(ops->owner))
		ops = NULL;
	rcu_read_unlock();
	return ops;
}

void nfs_multipath_router_put(struct enfs_adapter_ops *ops)
{
	if (ops)
		module_put(ops->owner);
}

bool is_valid_option(enum nfsmultipathoptions option)
{
	if (option < REMOTEADDR || option >= INVALID_OPTION) {
		pr_warn("ENFS: invalid option %d\n",
			option);
		return false;
	}

	return true;
}

int enfs_parse_mount_options(enum nfsmultipathoptions option, char *str,
			     struct nfs_fs_context *mnt, struct fs_context *fc)
{
	int rc;
	struct enfs_adapter_ops *ops;

	// whether insert enfs.ko or not
	ops = nfs_multipath_router_get();
	if (ops == NULL) {
		enfs_log_debug("prepare loading eNFS module\n");
		mutex_lock(&enfs_module_mutex);
		rc = request_module("enfs");
		mutex_unlock(&enfs_module_mutex);

		if (rc) {
			enfs_log_debug("failed loading eNFS module\n");
			return -EOPNOTSUPP;
		}

		ops = nfs_multipath_router_get();
	}

	if ((ops == NULL) || (ops->parse_mount_options == NULL) ||
	    !is_valid_option(option)) {
		nfs_multipath_router_put(ops);
		enfs_log_debug("parsing nfs mount option enfs not load\n");
		return -EOPNOTSUPP;
	}
	// nfs_multipath_parse_options
	enfs_log_debug("parsing nfs mount option '%s' type: %d\n", str, option);
	rc = ops->parse_mount_options(option, str, &mnt->enfs_option,
				      fc->net_ns);
	nfs_multipath_router_put(ops);
	return rc;
}

void enfs_free_mount_options(struct nfs_fs_context *data)
{
	struct enfs_adapter_ops *ops;

	if (data->enfs_option == NULL)
		return;

	ops = nfs_multipath_router_get();
	if ((ops == NULL) || (ops->free_mount_options == NULL)) {
		nfs_multipath_router_put(ops);
		return;
	}
	ops->free_mount_options((void *)&data->enfs_option);
	nfs_multipath_router_put(ops);
}

int nfs_create_multi_path_client(struct nfs_client *client,
				 const struct nfs_client_initdata *cl_init)
{
	int ret = 0;
	struct enfs_adapter_ops *ops;

	if (cl_init->enfs_option == NULL)
		return 0;

	ops = nfs_multipath_router_get();
	if (ops != NULL && ops->client_info_init != NULL)
		ret = ops->client_info_init((void *)&client->cl_multipath_data,
					    cl_init);
	nfs_multipath_router_put(ops);

	return ret;
}
EXPORT_SYMBOL_GPL(nfs_create_multi_path_client);

void nfs_free_multi_path_client(struct nfs_client *clp)
{
	struct enfs_adapter_ops *ops;

	if (clp->cl_multipath_data == NULL)
		return;

	ops = nfs_multipath_router_get();
	if (ops != NULL && ops->client_info_free != NULL)
		ops->client_info_free(clp->cl_multipath_data);
	nfs_multipath_router_put(ops);
}

int nfs_multipath_client_match(void *src, void *dst)
{
	int ret = true;
	struct enfs_adapter_ops *ops;

	if (src == NULL && dst == NULL)
		return true;

	if ((src == NULL && dst) || (src && dst == NULL))
		return false;

	ops = nfs_multipath_router_get();
	if (ops != NULL && ops->client_info_match != NULL)
		ret = ops->client_info_match(src, dst);
	nfs_multipath_router_put(ops);

	return ret;
}

int nfs4_multipath_client_match(void *src, void *dst)
{
	int ret = true;
	struct enfs_adapter_ops *ops;

	if (src == NULL && dst == NULL)
		return true;

	if (src == NULL || dst == NULL)
		return false;

	ops = nfs_multipath_router_get();
	if (ops != NULL && ops->nfs4_client_info_match != NULL)
		ret = ops->nfs4_client_info_match(src, dst);
	nfs_multipath_router_put(ops);

	return ret;
}
EXPORT_SYMBOL_GPL(nfs4_multipath_client_match);

void nfs_multipath_show_client_info(struct seq_file *mount_option,
				    struct nfs_server *server)
{
	struct enfs_adapter_ops *ops;

	if (mount_option == NULL || server == NULL || server->client == NULL ||
	    server->nfs_client->cl_multipath_data == NULL)
		return;

	ops = nfs_multipath_router_get();
	if (ops != NULL && ops->client_info_show != NULL)
		ops->client_info_show(mount_option, server);
	nfs_multipath_router_put(ops);
}

int nfs_remount_iplist(struct nfs_client *nfs_client, void *enfs_option)
{
	int ret = 0;
	struct enfs_adapter_ops *ops;

	if (nfs_client == NULL || nfs_client->cl_rpcclient == NULL)
		return 0;

	ops = nfs_multipath_router_get();
	if (ops != NULL && ops->remount_ip_list != NULL)
		ret = ops->remount_ip_list(nfs_client, enfs_option);
	nfs_multipath_router_put(ops);
	return ret;
}
EXPORT_SYMBOL_GPL(nfs_remount_iplist);

bool nfs_has_created_multipath(struct nfs_client *nfs_client)
{
	if (nfs_client == NULL || nfs_client->cl_multipath_data == NULL)
		return false;
	else
		return true;
}
EXPORT_SYMBOL_GPL(nfs_has_created_multipath);


void nfs_multipath_set_mount_data(void **opt, const char *hostname)
{
	struct enfs_adapter_ops *ops = nfs_multipath_router_get();

	if (ops != NULL && ops->set_mount_data != NULL)
		ops->set_mount_data(opt, hostname);
	nfs_multipath_router_put(ops);
}
EXPORT_SYMBOL_GPL(nfs_multipath_set_mount_data);

bool enfs_check_have_lookup_cache_flag(struct nfs_server *server, int flag)
{
	/* rule:
	 * 1. first check user lookupcache flag match or not
	 * 2. then if user lookupcache option is positive/none, will ignore server
	 * lookupcache flag. if user lookupcache option is all, will check server
	 * ookupcache flag.

	 * we don't use enfs ops to check, because during upgrade ops will be null, it will
	 * cause result will change when upgrade.
	 */
	if (server->flags & flag)
		return true;

	if (server->flags &
	    (NFS_MOUNT_LOOKUP_CACHE_NONE | NFS_MOUNT_LOOKUP_CACHE_NONEG))
		return false;

	return ((server->enfs_flags & flag) ? true : false);
}
EXPORT_SYMBOL_GPL(enfs_check_have_lookup_cache_flag);

void enfs_trigger_get_server_capability(struct nfs_server *server)
{
	struct enfs_adapter_ops *ops;

	ops = nfs_multipath_router_get();
	if (ops != NULL && ops->trigger_get_capability != NULL)
		ops->trigger_get_capability(server);
	nfs_multipath_router_put(ops);

}
EXPORT_SYMBOL_GPL(enfs_trigger_get_server_capability);
