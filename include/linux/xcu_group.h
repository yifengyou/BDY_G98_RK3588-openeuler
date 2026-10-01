/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __XSCHED_XCU_GROUP_H__
#define __XSCHED_XCU_GROUP_H__

#include <linux/idr.h>
#include <uapi/linux/xsched/xcu_vstream.h>

#ifndef CONFIG_XSCHED_NR_CUS
#define CONFIG_XSCHED_NR_CUS 128
#endif /* !CONFIG_XSCHED_NR_CUS */
#define XSCHED_NR_CUS CONFIG_XSCHED_NR_CUS

extern struct xcu_group *xcu_group_root;

enum xcu_type {
	XCU_TYPE_ROOT,
	XCU_TYPE_XPU,
};

enum xcu_sqe_op_type {
	SQE_SET_NOTIFY,
	SQE_IS_NOTIFY,
};

struct xcu_op_handler_params {
	int fd;
	struct xcu_group *group;
	void *payload;
	union {
		struct {
			void *param_1;
			void *param_2;
			void *param_3;
			void *param_4;
			void *param_5;
			void *param_6;
			void *param_7;
			void *param_8;
		};
	};
};

typedef int (*xcu_op_handler_fn_t)(struct xcu_op_handler_params *params);

struct xcu_operation {
	xcu_op_handler_fn_t run;
	xcu_op_handler_fn_t finish;
	xcu_op_handler_fn_t wait;
	xcu_op_handler_fn_t complete;
	xcu_op_handler_fn_t alloc;
	xcu_op_handler_fn_t logic_alloc;
	xcu_op_handler_fn_t logic_free;
	xcu_op_handler_fn_t sqe_op;
};

struct xcu_group {
	/* sq id. */
	uint32_t id;

	/* Type of XCU group. */
	enum xcu_type type;

	/* IDR for the next layer of XCU group tree. */
	struct idr next_layer;

	/* Pointer to the previous XCU group in the XCU group tree. */
	struct xcu_group *previous_layer;

	/* Pointer to operation fn pointers object describing
	 * this XCU group's callbacks.
	 */
	struct xcu_operation *opt;

	/* Pointer to the XCU related to this XCU group. */
	struct xsched_cu *xcu;
};

int xcu_group_attach(struct xcu_group *new_group,
		     struct xcu_group *previous_group);
void xcu_group_detach(struct xcu_group *group);
struct xcu_group *xcu_group_find(struct xcu_group *group, int id);
struct xcu_group *xcu_group_init(int id);
void xcu_group_free(struct xcu_group *group);

extern int xcu_run(struct xcu_op_handler_params *params);
extern int xcu_wait(struct xcu_op_handler_params *params);
extern int xcu_complete(struct xcu_op_handler_params *params);
extern int xcu_finish(struct xcu_op_handler_params *params);
extern int xcu_alloc(struct xcu_op_handler_params *params);
extern int xcu_logic_alloc(struct xcu_op_handler_params *params);
extern int xcu_logic_free(struct xcu_op_handler_params *params);
extern int xcu_sqe_op(struct xcu_op_handler_params *params);

int xsched_xcu_register(struct xcu_group *group, uint32_t phys_id);
int xsched_xcu_unregister(struct xcu_group *group, uint32_t phys_id);
#endif /* __XSCHED_XCU_GROUP_H__ */
