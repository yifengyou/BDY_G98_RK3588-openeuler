/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_XSCHED_TYPES_H
#define _UAPI_LINUX_XSCHED_TYPES_H

#include <linux/types.h>

struct xsched_attr {
	/* Scheduling class type, from enum xcu_sched_class */
	__u32 xsched_class;

	/* RT scheduling priority, from enum xse_prio */
	__u32 xsched_priority;
};

enum xcu_sched_class {
	XSCHED_TYPE_RT = 0,
	XSCHED_TYPE_CFS = 1,
	XSCHED_TYPE_NUM,
	XSCHED_TYPE_DFLT = XSCHED_TYPE_RT
};

enum xse_prio {
	XSE_PRIO_HIGH = 0,
	XSE_PRIO_LOW = 4,
	NR_XSE_PRIO,
	XSE_PRIO_DFLT = XSE_PRIO_LOW
};

#endif /* _UAPI_LINUX_XSCHED_TYPES_H */
