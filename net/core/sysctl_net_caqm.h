/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) Huawei Technologies Co., Ltd. 2024-2024
 * All rights reserved.
 *
 * Authors:	Chengjun Jia <jiachengjun2@huawei.com>
 */
#ifndef _NET_CORE_SYSCTL_NET_CAQM_H
#define _NET_CORE_SYSCTL_NET_CAQM_H

#ifdef CONFIG_ETH_CAQM
#include <linux/if_caqm.h>
#include <linux/sysctl.h>

#define INT16_MAX			  (32767)

// cc_type is 3bit, so the max value is 0b'111
static const unsigned int sysctl_caqm_cc_type_max = 7;
static const unsigned int sysctl_caqm_alpha_fx_8_max = INT16_MAX;
static const unsigned int sysctl_caqm_mtu_unit_min = 64;
static const unsigned int sysctl_caqm_mtu_unit_max = 9000;
static const unsigned int sysctl_caqm_data_hint_unit_max = 1024;
static const unsigned int sysctl_caqm_ack_hint_unit_max = 1024;

static int proc_caqm_enable(struct ctl_table *table, int write,
				   void *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;
	struct ctl_table tmp = {
		.data = &sysctl_caqm_en_data,
		.maxlen		= sizeof(u8),
		.mode = table->mode,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	};

	ret = proc_dou8vec_minmax(&tmp, write, buffer, lenp, ppos);

	if (write) {
		if (sysctl_caqm_en_data)
			static_branch_enable(&sysctl_caqm_enable);
		else
			static_branch_disable(&sysctl_caqm_enable);
	}

	return ret;
}
#endif
#endif /* _NET_CORE_SYSCTL_NET_CAQM_H */
