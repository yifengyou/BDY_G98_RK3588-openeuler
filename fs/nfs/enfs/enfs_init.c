// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <linux/module.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs.h>
#include <linux/nfs4.h>
#include <linux/nfs3.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include "enfs.h"
#include "enfs_multipath_parse.h"
#include "enfs_multipath_client.h"
#include "enfs_remount.h"
#include "enfs_lookup_cache.h"
#include "enfs_rpc_init.h"
#include "enfs_log.h"
#include "enfs_multipath.h"
#include "mgmt_init.h"
#include "dns_internal.h"
#include "shard.h"
#include "enfs_config.h"

static struct enfs_adapter_ops enfs_adapter = {
	.name = "enfs",
	.owner = THIS_MODULE,
	.parse_mount_options = nfs_multipath_parse_options,
	.free_mount_options = nfs_multipath_free_options,
	.client_info_init = nfs_multipath_client_info_init,
	.client_info_free = nfs_multipath_client_info_free,
	.client_info_match = nfs_multipath_client_info_match,
	.nfs4_client_info_match = nfs4_multipath_client_info_match,
	.client_info_show = nfs_multipath_client_info_show,
	.remount_ip_list = enfs_remount,
	.set_mount_data = enfs_set_mount_data,
	.trigger_get_capability = enfs_trigger_get_capability,
};

struct enfs_init_entry {
	char *name;
	int (*init)(void);
	void (*final)(void);
};

static inline void init_helper_finalize(struct enfs_init_entry *job, int idx)
{
	struct enfs_init_entry *entry = NULL;

	while (idx > 0) {
		idx = idx - 1;
		entry = &job[idx];
		if (entry->final != NULL) {
			entry->final();
			enfs_log_error("final %s.\n", entry->name);
		}
	}
}

static inline int init_helper_init(struct enfs_init_entry *job, int size)
{
	int ret;
	int i;
	struct enfs_init_entry *entry = NULL;

	for (i = 0; i < size; i++) {
		entry = &job[i];
		ret = entry->init();
		if (ret) {
			enfs_log_error("init step(%d) init(%s) fail.\n", i,
				       entry->name);
			goto init_err;
		}
	}

	return 0;

init_err:
	init_helper_finalize(job, i);
	return -1;
}

static struct enfs_init_entry init_entry[] = {
	{ "multipath", enfs_multipath_init, enfs_multipath_exit },
	{ "shard", enfs_shard_init, enfs_shard_exit },
	{ "mgmt", mgmt_init, mgmt_fini },
	{ "dns", enfs_dns_init, enfs_dns_exit },
};

static int __init init_enfs(void)
{
	int ret;

	enfs_config_load();

	ret = enfs_adapter_register(&enfs_adapter);
	if (ret) {
		enfs_log_error("regist enfs_adapter fail. ret %d\n", ret);
		return -1;
	}

	ret = init_helper_init(init_entry, ARRAY_SIZE(init_entry));
	if (ret) {
		enfs_adapter_unregister(&enfs_adapter);
		return -1;
	}

	ret = enfs_rpc_init();
	if (ret) {
		enfs_adapter_unregister(&enfs_adapter);
		return -1;
	}

	return 0;
}

static void __exit exit_enfs(void)
{
	enfs_lookupcache_fini();
	init_helper_finalize(init_entry, ARRAY_SIZE(init_entry));
	enfs_adapter_unregister(&enfs_adapter);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huawei Tech. Co., Ltd.");
MODULE_DESCRIPTION("Nfs client multipath");
MODULE_VERSION("1.0");

module_init(init_enfs);
module_exit(exit_enfs);
