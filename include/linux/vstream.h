/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VSTREAM_H
#define _LINUX_VSTREAM_H

#include <uapi/linux/xsched/xcu_vstream.h>
#include <linux/ktime.h>

#define MAX_VSTREAM_SIZE 2048

/* Vstream metadata describes each incoming kick
 * that gets stored into a list of pending kicks
 * inside a vstream to keep track of what is left
 * to be processed by a driver.
 */
typedef struct vstream_metadata {
	uint32_t exec_time;
	/* A value of SQ tail that has been passed with the
	 * kick that is described by this exact metadata object.
	 */
	uint32_t sq_tail;
	uint32_t sqe_num;
	uint32_t sq_id;
	uint8_t sqe[XCU_SQE_SIZE_MAX];

	/* Report buffer for fake read. */
	int8_t cqe[XCU_CQE_BUF_SIZE];
	uint32_t cqe_num;
	int32_t timeout;

	/* A node for metadata list */
	struct list_head node;

	struct vstream_info *parent;

	/* Time of list insertion */
	ktime_t add_time;
} vstream_metadata_t;

typedef int vstream_manage_t(struct vstream_args *arg);

typedef struct vstream_info {
	uint32_t id;
	uint32_t sq_id;
	uint32_t vcq_id;
	uint32_t logic_vcq_id;
	uint32_t dev_id;
	uint32_t channel_id;
	uint32_t fd;
	uint32_t task_type;
	int tgid;
	int sqcq_type;

	void *drv_ctx;

	int inode_fd;

	/* Pointer to corresponding context. */
	struct xsched_context *ctx;

	/* List node in context's vstream list. */
	struct list_head ctx_node;

	/* Pointer to an CU object on which this
	 * vstream is currently being processed.
	 * NULL if vstream is not being processed.
	 */
	struct xsched_cu *xcu;

	/* List node in an CU list of vstreams that
	 * are currently being processed by this specific CU.
	 */
	struct list_head xcu_node;

	/* Private vstream data. */
	void *data;

	spinlock_t stream_lock;

	uint32_t kicks_count;

	/* List of metadata a.k.a. all recorded unprocesed
	 * kicks for this exact vstream.
	 */
	struct list_head metadata_list;
} vstream_info_t;

int vstream_alloc(struct vstream_args *arg);
int vstream_free(struct vstream_args *arg);
int vstream_kick(struct vstream_args *arg);

#endif /* _LINUX_VSTREAM_H */
