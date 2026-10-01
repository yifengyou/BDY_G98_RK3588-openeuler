/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024. Huawei Technologies Co., Ltd. All rights reserved.
 */
#ifndef __VIRTCCA_CVM_HOST_H
#define __VIRTCCA_CVM_HOST_H
#include <asm/cca_type.h>

#ifdef CONFIG_HISI_VIRTCCA_HOST

#define UEFI_LOADER_START 0x0
#define UEFI_SIZE 0x8000000

bool is_virtcca_cvm_enable(void);
void set_cca_cvm_type(int type);

#else

static inline bool is_virtcca_cvm_enable(void)
{
	return false;
}

static inline void set_cca_cvm_type(int type) {}

#endif /* CONFIG_HISI_VIRTCCA_HOST */
#endif /* __VIRTCCA_CVM_HOST_H */
