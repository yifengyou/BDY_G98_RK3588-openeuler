/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mfs

#if !defined(_TRACE_MFS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MFS_H

#include <linux/tracepoint.h>
#include <linux/fs.h>

TRACE_EVENT(mfs_lookup,
	TP_PROTO(struct inode *dir, struct dentry *dentry, unsigned int flag),
	TP_ARGS(dir, dentry, flag),
	TP_STRUCT__entry(
		__field(dev_t,		dev)
		__field(ino_t,		ino)
		__string(name,		dentry->d_name.name)
		__field(unsigned int,	flag)
	),
	TP_fast_assign(
		__entry->dev = dir->i_sb->s_dev;
		__entry->ino = dir->i_ino;
		__assign_str(name, dentry->d_name.name);
		__entry->flag = flag;
	),

	TP_printk("dev=%d ino=%lu name=%s flag=%x",
		MINOR(__entry->dev), __entry->ino, __get_str(name), __entry->flag)
);

DECLARE_EVENT_CLASS(mfs_file_normal,
	TP_PROTO(struct inode *inode, struct file *file),
	TP_ARGS(inode, file),
	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(mode_t,	mode)
	),
	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->mode = file->f_mode;
	),

	TP_printk("dev=%d ino=%lu mode=%o",
		MINOR(__entry->dev), __entry->ino, __entry->mode)
);

DEFINE_EVENT(mfs_file_normal, mfs_open,
	TP_PROTO(struct inode *inode, struct file *file),
	TP_ARGS(inode, file)
);

DEFINE_EVENT(mfs_file_normal, mfs_release,
	TP_PROTO(struct inode *inode, struct file *file),
	TP_ARGS(inode, file)
);

TRACE_EVENT(mfs_post_event_read,
	TP_PROTO(struct inode *inode, loff_t off, uint64_t len, int op),
	TP_ARGS(inode, off, len, op),
	TP_STRUCT__entry(
		__field(dev_t,	dev)
		__field(ino_t,	ino)
		__field(loff_t,	off)
		__field(uint64_t,	len)
		__field(int,	op)
	),
	TP_fast_assign(
		__entry->dev = inode->i_sb->s_dev;
		__entry->ino = inode->i_ino;
		__entry->off = off;
		__entry->len = len;
		__entry->op = op;
	),

	TP_printk("(miss) dev=%d ino=%lu off=%lld len=%llu op=%d",
		MINOR(__entry->dev), __entry->ino, __entry->off, __entry->len, __entry->op)
);

TRACE_EVENT(mfs_dev_read,
	TP_PROTO(struct file *file, int op, uint32_t msgid, uint32_t fd),
	TP_ARGS(file, op, msgid, fd),
	TP_STRUCT__entry(
		__field(dev_t,		dev)
		__field(ino_t,		ino)
		__field(int,		op)
		__field(uint32_t,	msgid)
		__field(uint32_t,	fd)
	),
	TP_fast_assign(
		__entry->dev = file->f_inode->i_sb->s_dev;
		__entry->ino = file->f_inode->i_ino;
		__entry->op = op;
		__entry->msgid = msgid;
		__entry->fd = fd;
	),

	TP_printk("dev=%d ino=%lu op=%d msgid=%u fd=%u",
		MINOR(__entry->dev), __entry->ino, __entry->op, __entry->msgid, __entry->fd)
);

#endif /* _TRACE_MFS_H */

#include <trace/define_trace.h>
