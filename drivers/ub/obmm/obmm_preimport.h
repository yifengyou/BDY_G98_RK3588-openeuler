/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2025. All rights reserved.
 */
#ifndef OBMM_PREIMPORT_H
#define OBMM_PREIMPORT_H

#include <ub/ubus/ub-mem-decoder.h>
#include <uapi/ub/obmm.h>
#include "obmm_core.h"

struct ub_mem_info;
struct resource;
struct ubmem_resource;

struct preimport_range {
	int numa_id;

	phys_addr_t start;
	phys_addr_t end;

	unsigned int scna;
	unsigned int dcna;
	u8 seid[16];
	u8 deid[16];
	unsigned int use_count;

	struct list_head node;
};

extern void *not_ready_ptr;

int check_preimport_cmd_common(const struct obmm_cmd_preimport *cmd_preimport);
int preimport_prepare_common(struct preimport_range *preimport_range, uint8_t base_dist);
int preimport_release_common(struct preimport_range *preimport_range, bool force);
int check_preimport_datapath_common(const struct preimport_range *preimport_range,
				    const struct obmm_datapath *datapath);

int preimport_prepare_prefilled(struct obmm_cmd_preimport *cmd_preimport);
int preimport_release_prefilled(phys_addr_t start, phys_addr_t end);
void preimport_init_prefilled(void);
void preimport_exit_prefilled(void);

/* belows are exposed to other components of OBMM */
bool is_numa_base_dist_valid(uint8_t base_dist);
int obmm_set_numa_distance(unsigned int cna, int nid_remote, uint8_t base_dist);

int obmm_preimport(struct obmm_cmd_preimport *cmd_preimport);
int obmm_unpreimport(struct obmm_cmd_preimport *cmd_preimport);
int module_preimport_init(void);
void module_preimport_exit(void);

int preimport_commit_prefilled(phys_addr_t start, phys_addr_t end,
			       const struct obmm_datapath *datapath, int *p_numa_id,
			       void **p_handle);
int preimport_uncommit_prefilled(void *handle, phys_addr_t start, phys_addr_t end);
struct ubmem_resource *preimport_get_resource_prefilled(void *handle);

#endif
