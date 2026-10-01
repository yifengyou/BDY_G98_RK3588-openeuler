// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright (C) 2025. Huawei Technologies Co., Ltd */

#include "internal.h"

#include <linux/err.h>
#include <linux/fs_stack.h>
#include <linux/namei.h>

#include <trace/events/mfs.h>

static int mfs_inode_eq(struct inode *inode, void *lower_target)
{
	return mfs_lower_inode(inode) == (struct inode *)lower_target;
}

static int mfs_inode_set(struct inode *inode, void *lower_target)
{
	return 0;
}

static struct inode *_mfs_get_inode(struct super_block *sb,
					  struct path *lower_path,
					  struct path *cache_path)
{
	struct mfs_sb_info *sbi = MFS_SB(sb);
	struct inode *ret, *lower_inode, *cache_inode;

	lower_inode = d_inode(lower_path->dentry);
	cache_inode = d_inode(cache_path->dentry);

	/* lower file system cannot change */
	if (lower_inode->i_sb != sbi->lower.dentry->d_sb) {
		ret = ERR_PTR(-EXDEV);
		goto out;
	}

	/* check consistency: mode and size */
	if ((lower_inode->i_mode & S_IFMT) != (cache_inode->i_mode & S_IFMT)) {
		ret = ERR_PTR(-EUCLEAN);
		goto out;
	}
	if (S_ISREG(lower_inode->i_mode)
		&& lower_inode->i_size != cache_inode->i_size) {
		ret = ERR_PTR(-EUCLEAN);
		goto out;
	}

	/* allocate new inode for mfs */
	ret = mfs_iget(sb, lower_inode, cache_path);
out:
	return ret;
}

static int _lookup_create(struct path *lpath, struct path *parent_cpath,
			      const char *name, struct path *cpath)
{
	struct dentry *ldentry, *parent_cdentry, *dentry;
	struct inode *linode, *cdir;
	int ret = 0, _ret;

	ldentry = lpath->dentry;
	parent_cdentry = parent_cpath->dentry;
	linode = d_inode(ldentry);
	cdir = d_inode(parent_cpath->dentry);

	inode_lock_nested(cdir, I_MUTEX_PARENT);
retry:
	dentry = lookup_one_len(name, parent_cdentry, strlen(name));
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto out;
	}

	cpath->mnt = mntget(parent_cpath->mnt);
	cpath->dentry = dentry;
	if (d_is_positive(dentry))
		goto out;

	if (d_is_dir(ldentry)) {
		ret = vfs_mkdir(&nop_mnt_idmap, cdir, dentry, linode->i_mode);
		if (ret)
			goto new_err;
		/*
		 * In the event that the filesystem does not use the @dentry
		 * but leaves it negative or unhashes it.
		 */
		if (unlikely(d_unhashed(dentry))) {
			mntput(parent_cpath->mnt);
			dput(dentry);
			goto retry;
		}
		inode_unlock(cdir);
	} else {
		/* dir or file, symlink will be considerred the regular file */
		ret = vfs_create(&nop_mnt_idmap, cdir, dentry, linode->i_mode, true);
		if (ret)
			goto new_err;
		inode_unlock(cdir);
		ret = vfs_truncate(cpath, linode->i_size);
		if (ret)
			goto truncate_err;
	}
	return ret;

truncate_err:
	inode_lock_nested(cdir, I_MUTEX_PARENT);
	_ret = vfs_unlink(&nop_mnt_idmap, cdir, dentry, NULL);
	if (_ret)
		pr_err("cleanup failed for file:%s, err:%d\n", name, _ret);
new_err:
	mntput(parent_cpath->mnt);
	dput(dentry);
out:
	inode_unlock(cdir);
	return ret;
}

static struct dentry *mfs_lookup(struct inode *dir, struct dentry *dentry,
				    unsigned int flag)
{
	struct path parent_lpath, parent_cpath, lpath, cpath;
	struct dentry *ret, *parent;
	struct inode *inode;
	const char *name;
	int err;

	trace_mfs_lookup(dir, dentry, flag);
	parent = dget_parent(dentry);
	mfs_get_path(parent, &parent_lpath, &parent_cpath);
	err = mfs_alloc_dentry_info(dentry);
	if (err) {
		ret = ERR_PTR(err);
		goto out;
	}
	/* lookup from lower layer */
	name = dentry->d_name.name;
	err = vfs_path_lookup(parent_lpath.dentry,
			      parent_lpath.mnt,
			      name, 0, &lpath);
	if (err) {
		ret = ERR_PTR(err);
		mfs_free_dentry_info(dentry);
		goto out;
	}
	/* check from cache layer */
	err = vfs_path_lookup(parent_cpath.dentry,
			      parent_cpath.mnt,
			      name, 0, &cpath);
	if (err) {
		if (err != -ENOENT) {
cdentry_fail:
			ret = ERR_PTR(err);
			path_put(&lpath);
			mfs_free_dentry_info(dentry);
			goto out;
		}
		err = _lookup_create(&lpath, &parent_cpath, name, &cpath);
		if (err)
			goto cdentry_fail;
	}
	/* build the inode from lower layer */
	inode = _mfs_get_inode(dir->i_sb, &lpath, &cpath);
	if (IS_ERR(inode)) {
		path_put(&lpath);
		path_put(&cpath);
		mfs_free_dentry_info(dentry);
		ret = ERR_PTR(PTR_ERR(inode));
		goto out;
	}
	mfs_install_path(dentry, &lpath, &cpath);
	ret = d_splice_alias(inode, dentry);
	if (IS_ERR(ret)) {
		path_put(&lpath);
		path_put(&cpath);
		mfs_free_dentry_info(dentry);
	}
out:
	mfs_put_path(&parent_lpath, &parent_cpath);
	dput(parent);
	return ret;
}

static int mfs_getattr(struct mnt_idmap *idmap, const struct path *path,
		       struct kstat *stat, u32 request_mask,
		       unsigned int query_flags)
{
	struct mfs_inode *vi = MFS_I(d_inode(path->dentry));

	generic_fillattr(idmap, request_mask, vi->lower, stat);
	return 0;
}

static const char *mfs_get_link(struct dentry *dentry,
				struct inode *inode,
				struct delayed_call *done)
{
	struct mfs_sb_info *sbi = MFS_SB(inode->i_sb);
	struct path lpath, cpath;
	struct dentry *ldentry;
	const char *p;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	mfs_get_path(dentry, &lpath, &cpath);
	ldentry = lpath.dentry;
	p = vfs_get_link(ldentry, done);
	mfs_put_path(&lpath, &cpath);

	if (IS_ERR(p) || p[0] != '/')
		return p;
	if (strlen(p) <= strlen(sbi->mtree))
		return ERR_PTR(-EXDEV);
	if (strncmp(sbi->mtree, p, strlen(sbi->mtree)) != 0)
		return ERR_PTR(-EXDEV);
	p += strlen(sbi->mtree);
	if (p[0] != '/')
		return ERR_PTR(-EXDEV);
	p += 1;
	return p;
}

static const struct inode_operations mfs_dir_iops = {
	.lookup		= mfs_lookup,
	.getattr	= mfs_getattr,
};

static const struct inode_operations mfs_symlink_iops = {
	.getattr	= mfs_getattr,
	.get_link	= mfs_get_link,
};

static const struct inode_operations mfs_file_iops = {
	.getattr	= mfs_getattr,
};

struct inode *mfs_iget(struct super_block *sb, struct inode *lower_inode,
			 struct path *cache_path)
{
	struct inode *inode, *cache_inode = d_inode(cache_path->dentry);
	struct mfs_inode *vi;
	int err;

	if (!igrab(lower_inode))
		return ERR_PTR(-ESTALE);
	if (!igrab(cache_inode)) {
		err = -ESTALE;
		goto err_put_lower;
	}
	inode = iget5_locked(sb, lower_inode->i_ino,
			     mfs_inode_eq,
			     mfs_inode_set,
			     lower_inode);
	if (!inode) {
		err = -ENOMEM;
		goto err_put_cache;
	}
	/* found in cache */
	if (!(inode->i_state & I_NEW)) {
		iput(cache_inode);
		iput(lower_inode);
		return inode;
	}
	/* new inode */
	vi = MFS_I(inode);
	inode->i_ino = lower_inode->i_ino;
	switch (lower_inode->i_mode & S_IFMT) {
	case S_IFREG:
		inode->i_op = &mfs_file_iops;
		inode->i_fop = &mfs_file_fops;
		break;
	case S_IFDIR:
		inode->i_op = &mfs_dir_iops;
		inode->i_fop = &mfs_dir_fops;
		break;
	case S_IFLNK:
		inode->i_op = &mfs_symlink_iops;
		break;
	default:
		err = -EOPNOTSUPP;
		goto err_inode;
	}
	inode->i_mapping->a_ops = &mfs_aops;
	if (S_ISREG(cache_inode->i_mode)) {
		vi->vfs_inode.i_private = mfs_alloc_object(inode, cache_path);
		if (IS_ERR(vi->vfs_inode.i_private)) {
			err = PTR_ERR(vi->vfs_inode.i_private);
			vi->vfs_inode.i_private = NULL;
			goto err_inode;
		}
	}
	vi->lower = lower_inode;
	vi->cache = cache_inode;
	fsstack_copy_attr_all(inode, lower_inode);
	fsstack_copy_inode_size(inode, lower_inode);
	unlock_new_inode(inode);
	return inode;
err_inode:
	iget_failed(inode);
err_put_cache:
	iput(cache_inode);
err_put_lower:
	iput(lower_inode);
	return ERR_PTR(err);
}
