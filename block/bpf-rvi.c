#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/part_stat.h>
#include <linux/pid_namespace.h>
#include <linux/bpf.h>
#include <linux/namei.h>
#include <linux/fs_struct.h>

#include "blk-mq.h"
#include "blk-cgroup.h"

/*
 * Diskstats
 */

struct diskstats_seq_priv {
	struct class_dev_iter iter;	// must be the first,
					// to let us reuse disk_seqf_next()
	struct blkcg *task_blkcg;
};

/*
 * Basically the same with disk_seqf_start() but without allocating iter and
 * then overwriting seqf->private, which points to priv_data->target_private
 * in bpf_iter case (see prepare_seq_file()), and is needed to retrieve
 * struct bpf_iter_priv_data. Here we allocate iter via setting
 * .seq_priv_size and turning priv_data->target_private into iter.
 */
static void *bpf_disk_seqf_start(struct seq_file *seqf, loff_t *pos)
{
	loff_t skip = *pos;
	struct diskstats_seq_priv *priv = seqf->private;
	struct class_dev_iter *iter;
	struct device *dev;
	struct task_struct *reaper = get_current_level1_reaper();

	priv->task_blkcg = css_to_blkcg(task_css(reaper ?: current, io_cgrp_id));
	if (reaper)
		put_task_struct(reaper);

	iter = &priv->iter;
	class_dev_iter_init(iter, &block_class, NULL, &disk_type);
	do {
		dev = class_dev_iter_next(iter);
		if (!dev)
			return NULL;
	} while (skip--);

	return dev_to_disk(dev);
}

/*
 * Similar to the difference between {bpf_,}disk_seqf_start,
 * here we don't free iter.
 */
static void bpf_disk_seqf_stop(struct seq_file *seqf, void *v)
{
	struct diskstats_seq_priv *priv = seqf->private;
	struct class_dev_iter *iter = &priv->iter;

	/* stop is called even after start failed :-( */
	if (iter)
		class_dev_iter_exit(iter);
}

/* Same with disk_seqf_next() */
static void *bpf_disk_seqf_next(struct seq_file *seqf, void *v, loff_t *pos)
{
	struct device *dev;

	(*pos)++;
	dev = class_dev_iter_next(seqf->private);
	if (dev)
		return dev_to_disk(dev);

	return NULL;
}

struct bpf_iter__diskstats {
	__bpf_md_ptr(struct bpf_iter_meta *, meta);
	__bpf_md_ptr(struct block_device *, bd);
	__bpf_md_ptr(struct disk_stats *, native_stat);
	unsigned int inflight __aligned(8);
	__bpf_md_ptr(struct blkcg *, task_blkcg);
};

DEFINE_BPF_ITER_FUNC(diskstats, struct bpf_iter_meta *meta,
		     struct block_device *bd, struct disk_stats *native_stat,
		     uint inflight,
		     struct blkcg *task_blkcg)

static int native_diskstats_show(struct seq_file *seqf, struct block_device *hd,
				 struct disk_stats *stat, unsigned int inflight)
{
	seq_printf(seqf, "%4d %7d %pg "
		   "%lu %lu %lu %u "
		   "%lu %lu %lu %u "
		   "%u %u %u "
		   "%lu %lu %lu %u "
		   "%lu %u"
		   "\n",
		   MAJOR(hd->bd_dev), MINOR(hd->bd_dev), hd,
		   stat->ios[STAT_READ],
		   stat->merges[STAT_READ],
		   stat->sectors[STAT_READ],
		   (unsigned int)div_u64(stat->nsecs[STAT_READ],
						NSEC_PER_MSEC),
		   stat->ios[STAT_WRITE],
		   stat->merges[STAT_WRITE],
		   stat->sectors[STAT_WRITE],
		   (unsigned int)div_u64(stat->nsecs[STAT_WRITE],
						NSEC_PER_MSEC),
		   inflight,
		   jiffies_to_msecs(stat->io_ticks),
		   (unsigned int)div_u64(stat->nsecs[STAT_READ] +
					 stat->nsecs[STAT_WRITE] +
					 stat->nsecs[STAT_DISCARD] +
					 stat->nsecs[STAT_FLUSH],
						NSEC_PER_MSEC),
		   stat->ios[STAT_DISCARD],
		   stat->merges[STAT_DISCARD],
		   stat->sectors[STAT_DISCARD],
		   (unsigned int)div_u64(stat->nsecs[STAT_DISCARD],
					 NSEC_PER_MSEC),
		   stat->ios[STAT_FLUSH],
		   (unsigned int)div_u64(stat->nsecs[STAT_FLUSH],
					 NSEC_PER_MSEC)
		);
	return 0;
}

static int __diskstats_show(struct seq_file *seqf, struct block_device *hd,
			    struct disk_stats *stat, unsigned int inflight)
{
	struct bpf_iter__diskstats ctx;
	struct bpf_iter_meta meta;
	struct bpf_prog *prog;
	struct diskstats_seq_priv *priv = seqf->private;

	meta.seq = seqf;
	prog = bpf_iter_get_info(&meta, false);
	if (!prog)
		return native_diskstats_show(seqf, hd, stat, inflight);

	ctx.meta = &meta;
	ctx.bd = hd;
	ctx.native_stat = stat;
	ctx.inflight = inflight;
	ctx.task_blkcg = priv->task_blkcg;
	return bpf_iter_run_prog(prog, &ctx);
}

/* The only function that becomes extern from static. Don't bother to place it in header file. */
void part_stat_read_all(struct block_device *part, struct disk_stats *stat);

static int diskstats_show(struct seq_file *seqf, void *v)
{
	struct gendisk *gp = v;
	struct block_device *hd;
	unsigned int inflight;
	struct disk_stats stat;
	unsigned long idx;
	int ret = 0;

	rcu_read_lock();
	xa_for_each(&gp->part_tbl, idx, hd) {
		if (bdev_is_partition(hd) && !bdev_nr_sectors(hd))
			continue;
		if (queue_is_mq(gp->queue))
			inflight = blk_mq_in_flight(gp->queue, hd);
		else
			inflight = part_in_flight(hd);

		if (inflight) {
			part_stat_lock();
			update_io_ticks(hd, jiffies, true);
			part_stat_unlock();
		}
		part_stat_read_all(hd, &stat);
		ret = __diskstats_show(seqf, hd, &stat, inflight);
		if (ret)
			break;
	}
	rcu_read_unlock();

	return ret;
}

static const struct seq_operations bpf_diskstats_op = {
	.start	= bpf_disk_seqf_start,
	.next	= bpf_disk_seqf_next,
	.stop	= bpf_disk_seqf_stop,
	.show	= diskstats_show
};

static const struct bpf_iter_seq_info diskstats_seq_info = {
	.seq_ops		= &bpf_diskstats_op,
	.init_seq_private	= NULL,
	.fini_seq_private	= NULL,
	.seq_priv_size		= sizeof(struct diskstats_seq_priv),
};

static struct bpf_iter_reg diskstats_reg_info = {
	.target			= "diskstats",
	.ctx_arg_info_size	= 2,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__diskstats, bd),
		  PTR_TO_BTF_ID },
		{ offsetof(struct bpf_iter__diskstats, native_stat),
		  PTR_TO_BTF_ID },
	},
	.seq_info		= &diskstats_seq_info,
};

BTF_ID_LIST(btf_diststats_ids)
BTF_ID(struct, block_device)
BTF_ID(struct, disk_stats)

static int __init diskstats_iter_init(void)
{
	diskstats_reg_info.ctx_arg_info[0].btf_id = btf_diststats_ids[0];
	diskstats_reg_info.ctx_arg_info[1].btf_id = btf_diststats_ids[1];
	return bpf_iter_reg_target(&diskstats_reg_info);
}
late_initcall(diskstats_iter_init);

/*
 * Partitions
 */

struct traverse_ctx {
	struct dir_context ctx;
	struct dentry *parent_dentry;
	struct xarray *dev_list;
	unsigned int index;
};

/*
 * This is a dir iteration callback that only performs block device collection
 * and counting, so it always returns true to keep the iteration go.
 */
static bool filldir_callback(struct dir_context *ctx, const char *name,
			     int namelen, loff_t offset, u64 ino,
			     unsigned int d_type)
{
	struct traverse_ctx *tctx = container_of(ctx, struct traverse_ctx, ctx);
	struct dentry *child_dentry;
	struct inode *inode;
	struct bdev_handle *handle;
	void *rc;

	if (d_type != DT_BLK)
		return true;

	child_dentry = lookup_one_len(name, tctx->parent_dentry, namelen);
	if (IS_ERR(child_dentry)) {
		pr_warn("Lookup failed for %s: %ld\n", name, PTR_ERR(child_dentry));
		return true;
	}

	// double check if it's block dev
	inode = d_inode(child_dentry);
	if (!S_ISBLK(inode->i_mode))
		goto err_put;

	handle = bdev_open_by_dev(inode->i_rdev, BLK_OPEN_READ, NULL, NULL);
	if (IS_ERR(handle)) {
		pr_err("Failed to open block device %s (err=%ld)\n",
			name, PTR_ERR(handle));
		goto err_put;
	}

	rc = xa_store(tctx->dev_list, tctx->index++, handle->bdev, GFP_KERNEL);
	if (xa_is_err(rc))
		pr_warn("xa_store() on %d failed\n", tctx->index - 1);

	bdev_release(handle);
err_put:
	dput(child_dentry);
	return true;
}

static unsigned int get_targeted_dev(struct xarray *dev_list)
{
	struct task_struct *reaper;
	struct path root_path, dev_path;
	struct file *dir;
	int ret;
	struct traverse_ctx buf = {
		.ctx.actor = filldir_callback,
		.dev_list = dev_list,
		.index = 0,
	};

	xa_init(dev_list);
	reaper = get_current_level1_reaper();
	if (!reaper)
		return 0;

	/* Reference: get_task_root() */
	task_lock(reaper);
	if (!reaper->fs) {
		task_unlock(reaper);
		goto out_put_reaper;
	}
	get_fs_root(reaper->fs, &root_path);
	task_unlock(reaper);

	/*
	 * For vfs_path_lookup(), @name being "dev" or "/dev" makes no
	 * difference, since struct nameidata.root is preset.
	 */
	ret = vfs_path_lookup(root_path.dentry, root_path.mnt, "dev",
			      LOOKUP_FOLLOW|LOOKUP_DIRECTORY, &dev_path);
	if (ret)
		goto out_put_root;

	dir = dentry_open(&dev_path, O_RDONLY, current_cred());
	if (IS_ERR(dir))
		goto out_put_devpath;
	buf.parent_dentry = dev_path.dentry;

	iterate_dir(dir, &buf.ctx);

	filp_close(dir, NULL);
out_put_devpath:
	path_put(&dev_path);
out_put_root:
	path_put(&root_path);
out_put_reaper:
	put_task_struct(reaper);
	return buf.index;
}

struct partitions_seq_priv {
	struct class_dev_iter iter; // must be the first
	struct xarray dev_list;
	unsigned int dev_list_size;
};

static void *bpf_show_partitions_start(struct seq_file *seqf, loff_t *pos)
{
	struct partitions_seq_priv *priv = seqf->private;
	void *p;

	p = bpf_disk_seqf_start(seqf, pos);
	if (!IS_ERR_OR_NULL(p) && !*pos)
		seq_puts(seqf, "major minor  #blocks  name\n\n");

	priv->dev_list_size = get_targeted_dev(&priv->dev_list);

	return p;
}

static void bpf_show_partitions_stop(struct seq_file *seqf, void *v)
{
	struct block_device *entry;
	struct partitions_seq_priv *priv = seqf->private;
	unsigned long index;

	bpf_disk_seqf_stop(seqf, v);

	xa_for_each(&priv->dev_list, index, entry) {
		xa_erase(&priv->dev_list, index);
		kfree(entry);
	}
}

struct bpf_iter__partitions {
	__bpf_md_ptr(struct bpf_iter_meta *, meta);
	__bpf_md_ptr(struct block_device *, part);
};

DEFINE_BPF_ITER_FUNC(partitions, struct bpf_iter_meta *meta,
		     struct block_device *part)

static void native_show_partition(struct seq_file *seqf, struct block_device *part)
{
	if (!bdev_nr_sectors(part))
		return;
	seq_printf(seqf, "%4d  %7d %10llu %pg\n",
		   MAJOR(part->bd_dev), MINOR(part->bd_dev),
		   bdev_nr_sectors(part) >> 1, part);
}

static void __show_partition(struct seq_file *seqf, struct block_device *part)
{
	struct bpf_iter__partitions ctx;
	struct bpf_iter_meta meta;
	struct bpf_prog *prog;

	meta.seq = seqf;
	prog = bpf_iter_get_info(&meta, false);
	if (!prog)
		return native_show_partition(seqf, part);

	ctx.meta = &meta;
	ctx.part = part;
	bpf_iter_run_prog(prog, &ctx);
}

/* Inconvenient to operate Xarray in bpf progs. */
static int bpf_show_partitions(struct seq_file *seqf, void *v)
{
	struct partitions_seq_priv *priv = seqf->private;
	struct gendisk *sgp = v;
	struct block_device *part;
	unsigned long idx;

	if (!get_capacity(sgp) || (sgp->flags & GENHD_FL_HIDDEN))
		return 0;

	rcu_read_lock();
	xa_for_each(&sgp->part_tbl, idx, part) {
		for (int i = 0; i < priv->dev_list_size; ++i)
			if (part == xa_load(&priv->dev_list, i))
				__show_partition(seqf, part);
	}
	rcu_read_unlock();
	return 0;
}

static const struct seq_operations bpf_partitions_op = {
	.start	= bpf_show_partitions_start,
	.next	= bpf_disk_seqf_next,
	.stop	= bpf_show_partitions_stop,
	.show	= bpf_show_partitions
};

static const struct bpf_iter_seq_info partitions_seq_info = {
	.seq_ops		= &bpf_partitions_op,
	.init_seq_private	= NULL,
	.fini_seq_private	= NULL,
	.seq_priv_size		= sizeof(struct partitions_seq_priv),
};

static struct bpf_iter_reg partitions_reg_info = {
	.target			= "partitions",
	.ctx_arg_info_size	= 1,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__partitions, part),
		  PTR_TO_BTF_ID }, // part won't be NULL
	},
	.seq_info		= &partitions_seq_info,
};

BTF_ID_LIST(btf_partitions_ids)
BTF_ID(struct, block_device)

static int __init partitions_iter_init(void)
{
	partitions_reg_info.ctx_arg_info[0].btf_id = btf_partitions_ids[0];
	return bpf_iter_reg_target(&partitions_reg_info);
}
late_initcall(partitions_iter_init);
