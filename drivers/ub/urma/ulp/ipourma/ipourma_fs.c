// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Description: ipourma debugfs support
 */
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include "ipourma_main.h"
#include "ipourma_fs.h"

static struct dentry *ipourma_root;

/* address format: convert binary to readable string */
#define IPOURMA_IPV6_STR_LEN 5
#define IPOURMA_EID_BYTES_PER_PAIR 2
static void ipourma_format_eid(union ubcore_eid *eid, char *buf)
{
	int add_len = UBCORE_EID_SIZE >> 1;
	unsigned char byte1, byte2;
	int i, ret;

	for (i = 0; i < add_len; i++) {
		byte1 = eid->raw[IPOURMA_EID_BYTES_PER_PAIR * i];
		byte2 = eid->raw[IPOURMA_EID_BYTES_PER_PAIR * i + 1];
		ret = snprintf(buf + i * IPOURMA_IPV6_STR_LEN, IPOURMA_IPV6_STR_LEN + 1,
				"%02x%02x%c", byte1, byte2, (i < add_len - 1) ? ':' : '\0');
		if (ret < 0)
			return;
	}
}

/* function aimed to filter all-zero addresses */
static bool ipourma_is_zero_address(const union ubcore_eid *eid)
{
	int i;

	for (i = 0; i < UBCORE_EID_SIZE; i++) {
		if (eid->raw[i] != 0)
			return false;
	}
	return true;
}

static void *ipourma_address_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct ipourma_dev_priv *priv = seq->private;
	struct ipourma_address_iter *iter;
	struct ubcore_eid_table *eid_table;
	uint32_t eid_cnt = 0;

	eid_table = &priv->urma_dev->eid_table;
	spin_lock(&eid_table->lock);
	eid_cnt = eid_table->eid_cnt;
	spin_unlock(&eid_table->lock);

	if (*pos >= eid_cnt)
		return NULL;

	iter = ipourma_address_iter_init(priv);
	if (!iter) {
		pr_err("address_seq_start: iter init failed\n");
		return NULL;
	}
	iter->index = *pos;
	return iter;
}

static void *ipourma_address_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct ipourma_address_iter *iter = v;
	int ret;

	ret = ipourma_address_iter_next(iter);
	if (ret) {
		kfree(iter);
		return NULL;
	}
	*pos = iter->index;
	return iter;
}

static void ipourma_address_seq_stop(struct seq_file *seq, void *v)
{
	kfree(v);
}

static int ipourma_address_seq_show(struct seq_file *seq, void *v)
{
	struct ipourma_address_iter *iter = v;
	union ubcore_eid eid;
	char eid_str[sizeof "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff\n"];
	bool has_valid_eid;

	struct ubcore_eid_table *eid_table;
	uint32_t eid_cnt;

	if (!iter || !iter->priv || !iter->priv->urma_dev) {
		pr_err("%s: illegal ptr\n", __func__);
		return 0;
	}
	eid_table = &iter->priv->urma_dev->eid_table;
	spin_lock(&eid_table->lock);
	eid_cnt = eid_table->eid_cnt;
	has_valid_eid = (eid_cnt > 0 && eid_table->eid_entries);
	spin_unlock(&eid_table->lock);
	if (!has_valid_eid) {
		seq_printf(seq, "IPv6 Address %d: (no valid EIDs)\n", iter->index);
		return 0;
	}

	ipourma_address_iter_read(iter, &eid);
	if (ipourma_is_zero_address(&eid))
		return 0;

	ipourma_format_eid(&eid, eid_str);
	seq_printf(seq, "IPv6 Address %d: %s\n", iter->index, eid_str);
	return 0;
}

static const struct seq_operations ipourma_address_seq_ops = {
	.start = ipourma_address_seq_start,
	.next = ipourma_address_seq_next,
	.stop = ipourma_address_seq_stop,
	.show = ipourma_address_seq_show
};

static int ipourma_address_open(struct inode *inode, struct file *file)
{
	struct ipourma_dev_priv *priv_from_inode = inode->i_private;
	struct seq_file *seq;
	int ret;

	ret = seq_open(file, &ipourma_address_seq_ops);
	if (ret) {
		pr_err("ddress_open: seq_open failed (ret=%d)\n", ret);
		return ret;
	}
	seq = file->private_data;
	// manual value assignment
	seq->private = priv_from_inode;
	return 0;
}

static const struct file_operations ipourma_address_fops = {
	.owner = THIS_MODULE,
	.open = ipourma_address_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};

#define IPOURMA_DEBUGFILE_PERMISSION  0400
void ipourma_create_debug_files(struct ipourma_dev_priv *priv)
{
	char filename[IFNAMSIZ + sizeof("_address")];
	int ret;

	if (!priv) {
		pr_err("create_debug_files:priv is NULL\n");
		return;
	}
	if (!priv->dev) {
		pr_err("create_debug_files:priv->dev is NULL\n");
		return;
	}

	ret = snprintf(filename, sizeof(filename), "%s_address", priv->dev->name);
	if (ret < 0)
		return;

	priv->address_dentry = debugfs_create_file(filename, IPOURMA_DEBUGFILE_PERMISSION,
			ipourma_root, priv, &ipourma_address_fops);
	if (IS_ERR(priv->address_dentry))
		priv->address_dentry = NULL;
}

void ipourma_delete_debug_files(struct ipourma_dev_priv *priv)
{
	debugfs_remove(priv->address_dentry); // debugfs_remove(NULL) is safe
	priv->address_dentry = NULL;
}

int ipourma_register_debugfs(void)
{
	ipourma_root = debugfs_create_dir("ipourma", NULL);
	return ipourma_root ? 0 : -ENOMEM;
}

void ipourma_unregister_debugfs(void)
{
	debugfs_remove(ipourma_root);
	ipourma_root = NULL;
}

