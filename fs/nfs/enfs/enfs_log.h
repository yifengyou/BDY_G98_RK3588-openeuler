/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#ifndef ENFS_LOG_H
#define ENFS_LOG_H

#include <linux/nfs_fs.h>

static inline bool is_enfs_debug(void)
{
	ifdebug(ENFS)
		return true;
	return false;
}

#define enfs_log_info(fmt, ...) \
	pr_info("enfs:[%s]" pr_fmt(fmt), __func__, ##__VA_ARGS__)
#define enfs_log_error(fmt, ...) \
	pr_err("enfs:[%s]" pr_fmt(fmt), __func__, ##__VA_ARGS__)
#define enfs_log_debug(fmt, ...)						\
do {										\
	dfprintk(ENFS, "enfs:[%s] " pr_fmt(fmt), __func__, ##__VA_ARGS__);	\
} while (0)									\

#endif // ENFS_ERRCODE_H
