// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (c) Huawei Technologies Co., Ltd. 2020-2024
 * All rights reserved.
 *
 * Define the caqm system control parameters.
 */
#include <linux/if_caqm.h>
#include <linux/types.h>

#ifdef CONFIG_ETH_CAQM
int sysctl_caqm_cc_type __read_mostly;
EXPORT_SYMBOL(sysctl_caqm_cc_type);
int sysctl_caqm_debug_info __read_mostly = 10;
EXPORT_SYMBOL(sysctl_caqm_debug_info);
int sysctl_caqm_alpha_fx_8 __read_mostly = 1 * FIXED_POINT_8;
EXPORT_SYMBOL(sysctl_caqm_alpha_fx_8);
int sysctl_caqm_beta __read_mostly = 512;
EXPORT_SYMBOL(sysctl_caqm_beta);
unsigned int sysctl_caqm_min_cwnd __read_mostly = 1;
EXPORT_SYMBOL(sysctl_caqm_min_cwnd);
int sysctl_caqm_mtu_unit __read_mostly = 1024;
EXPORT_SYMBOL(sysctl_caqm_mtu_unit);
int sysctl_caqm_data_hint_unit __read_mostly = 8;
EXPORT_SYMBOL(sysctl_caqm_data_hint_unit);
unsigned int sysctl_caqm_ack_hint_unit __read_mostly = 64;
EXPORT_SYMBOL(sysctl_caqm_ack_hint_unit);
struct static_key_false sysctl_caqm_enable __read_mostly;
EXPORT_SYMBOL(sysctl_caqm_enable);
u8 sysctl_caqm_en_data;
EXPORT_SYMBOL(sysctl_caqm_en_data);
u64 sysctl_caqm_filter_nics __read_mostly;
EXPORT_SYMBOL(sysctl_caqm_filter_nics);
// tp->srtt_us is 1/8 us, so the default is 200us
u32 sysctl_caqm_rtt_standard __read_mostly = 200 * 8;
EXPORT_SYMBOL(sysctl_caqm_rtt_standard);
#endif
