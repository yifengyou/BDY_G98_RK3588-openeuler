/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 - Huawei Ltd.
 */

#ifndef __ASM_ACTLR_H
#define __ASM_ACTLR_H

#define ACTLR_ELx_XCALL_SHIFT		20
#define ACTLR_ELx_XCALL			(UL(1) << ACTLR_ELx_XCALL_SHIFT)

#define ACTLR_ELx_XINT_SHIFT		(21)
#define ACTLR_ELx_XINT			(UL(1) << ACTLR_ELx_XINT_SHIFT)

#endif /* __ASM_ACTLR_H */
