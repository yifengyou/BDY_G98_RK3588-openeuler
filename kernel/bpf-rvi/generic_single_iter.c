// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2025 Huawei Technologies Co., Ltd */

#include <linux/init.h>
#include <linux/filter.h>
#include <linux/btf_ids.h>
#include <linux/bpf.h>

static void *generic_single_seq_start(struct seq_file *seq, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

static void *generic_single_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void generic_single_seq_stop(struct seq_file *seq, void *v)
{
}

struct bpf_iter__generic_single {
	__bpf_md_ptr(struct bpf_iter_meta *, meta);
};

static int generic_single_seq_show(struct seq_file *seq, void *v)
{
	struct bpf_iter_meta meta;
	struct bpf_iter__generic_single ctx;
	struct bpf_prog *prog;

	meta.seq = seq;
	prog = bpf_iter_get_info(&meta, false);
	if (!prog)
		return 0;

	ctx.meta = &meta;
	return bpf_iter_run_prog(prog, &ctx);
}

static const struct seq_operations generic_single_seq_ops = {
	.start	= generic_single_seq_start,
	.next	= generic_single_seq_next,
	.stop	= generic_single_seq_stop,
	.show	= generic_single_seq_show,
};

/*
 * Users of "generic_single" iter type:
 *  - cpu_online
 *  - loadavg
 *  - uptime
 *  - swaps
 */
DEFINE_BPF_ITER_FUNC(generic_single, struct bpf_iter_meta *meta)

static const struct bpf_iter_seq_info generic_single_seq_info = {
	.seq_ops		= &generic_single_seq_ops,
	.init_seq_private	= NULL,
	.fini_seq_private	= NULL,
	.seq_priv_size		= 0,
};

static struct bpf_iter_reg generic_single_reg_info = {
	.target			= "generic_single",
	.seq_info		= &generic_single_seq_info,
};

static int __init generic_single_iter_init(void)
{
	return bpf_iter_reg_target(&generic_single_reg_info);
}
late_initcall(generic_single_iter_init);
