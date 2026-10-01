// SPDX-License-Identifier: GPL-2.0-or-later
/* Volume handling.
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/fs.h>
#include <linux/slab.h>
#include "internal.h"
#include <trace/events/fscache.h>

void cachefiles_get_volume(struct cachefiles_volume *volume)
{
	refcount_inc(&volume->ref);
}

void cachefiles_put_volume(struct cachefiles_volume *volume)
{
	if (refcount_dec_and_test(&volume->ref)) {
		mutex_destroy(&volume->lock);
		kfree(volume);
	}
}

/*
 * Allocate and set up a volume representation.  We make sure all the fanout
 * directories are created and pinned.
 */
void cachefiles_acquire_volume(struct fscache_volume *vcookie)
{
	struct cachefiles_volume *volume;
	struct cachefiles_cache *cache = vcookie->cache->cache_priv;
	const struct cred *saved_cred;
	struct dentry *vdentry, *fan;
	size_t len;
	char *name;
	bool is_new = false;
	int ret, n_accesses, i;

	_enter("");

	volume = kzalloc(sizeof(struct cachefiles_volume), GFP_KERNEL);
	if (!volume)
		return;
	volume->vcookie = vcookie;
	volume->cache = cache;
	INIT_LIST_HEAD(&volume->cache_link);
	mutex_init(&volume->lock);

	cachefiles_begin_secure(cache, &saved_cred);

	len = vcookie->key[0];
	name = kmalloc(len + 3, GFP_NOFS);
	if (!name)
		goto error_vol;
	name[0] = 'I';
	memcpy(name + 1, vcookie->key + 1, len);
	name[len + 1] = 0;

retry:
	vdentry = cachefiles_get_directory(cache, cache->store, name, &is_new);
	if (IS_ERR(vdentry))
		goto error_name;
	volume->dentry = vdentry;

	if (is_new) {
		if (!cachefiles_set_volume_xattr(volume))
			goto error_dir;
	} else {
		ret = cachefiles_check_volume_xattr(volume);
		if (ret < 0) {
			if (ret != -ESTALE)
				goto error_dir;
			inode_lock_nested(d_inode(cache->store), I_MUTEX_PARENT);
			cachefiles_bury_object(cache, NULL, cache->store, vdentry,
					       FSCACHE_VOLUME_IS_WEIRD);
			cachefiles_put_directory(volume->dentry);
			cond_resched();
			goto retry;
		}
	}
	
	for (i = 0; i < 256; i++) {
		sprintf(name, "@%02x", i);
		fan = cachefiles_get_directory(cache, vdentry, name, NULL);
		if (IS_ERR(fan))
			goto error_fan;
		volume->fanout[i] = fan;
	}

	cachefiles_end_secure(cache, saved_cred);

	/*
	 * The purpose of introducing volume->ref is twofold:
	 * 1) To allow cachefiles_object to pin cachefiles_volume.
	 * 2) To handle the concurrency between cachefiles_free_volume() and
	 * cachefiles_withdraw_volume() introduced by enabling sync_unhash
	 * volume, preventing the former from releasing cachefiles_volume and
	 * causing a use-after-free in the latter.
	 */
	refcount_set(&volume->ref, 1);
	/* Prevent writing vcookie->cache_priv before writing volume->ref. */
	smp_store_release(&vcookie->cache_priv, volume);
	n_accesses = atomic_inc_return(&vcookie->n_accesses); /* Stop wakeups on dec-to-0 */
	trace_fscache_access_volume(vcookie->debug_id, 0,
				    refcount_read(&vcookie->ref),
				    n_accesses, fscache_access_cache_pin);

	spin_lock(&cache->object_list_lock);
	list_add(&volume->cache_link, &volume->cache->volumes);
	spin_unlock(&cache->object_list_lock);

	kfree(name);
	return;

error_fan:
	for (i = 0; i < 256; i++)
		cachefiles_put_directory(volume->fanout[i]);
error_dir:
	cachefiles_put_directory(volume->dentry);
error_name:
	kfree(name);
error_vol:
	mutex_destroy(&volume->lock);
	kfree(volume);
	cachefiles_end_secure(cache, saved_cred);
}

/*
 * Release a volume representation.
 */
static void __cachefiles_free_volume(struct cachefiles_volume *volume)
{
	int i;

	_enter("");

	mutex_lock(&volume->lock);
	if (volume->dir_has_put) {
		mutex_unlock(&volume->lock);
		return;
	}

	volume->dir_has_put = true;

	for (i = 0; i < 256; i++)
		cachefiles_put_directory(volume->fanout[i]);
	cachefiles_put_directory(volume->dentry);
	mutex_unlock(&volume->lock);
}

void cachefiles_free_volume(struct fscache_volume *vcookie)
{
	struct cachefiles_volume *volume = vcookie->cache_priv;

	/*
	 * Prevents access to the cachefiles_cache that has been freed caused
	 * by the concurrency between cachefiles_free_volume() and
	 * cachefiles_daemon_release(), the later may kree(cache).
	 */
	mutex_lock(&volume->lock);
	if (!volume->dir_has_put) {
		spin_lock(&volume->cache->object_list_lock);
		list_del_init(&volume->cache_link);
		spin_unlock(&volume->cache->object_list_lock);
	}
	mutex_unlock(&volume->lock);

	vcookie->cache_priv = NULL;
	__cachefiles_free_volume(volume);
	cachefiles_put_volume(volume);
}

void cachefiles_withdraw_volume(struct cachefiles_volume *volume)
{
	cachefiles_set_volume_xattr(volume);
	__cachefiles_free_volume(volume);
}
