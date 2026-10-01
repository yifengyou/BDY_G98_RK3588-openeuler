// SPDX-License-Identifier: GPL-2.0
/* Huawei iBMA driver.
 * Copyright (c) 2025, Huawei Technologies Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/errno.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "bma_pci.h"
#include "edma_host.h"
#include "edma_queue.h"

static u32 pcie_dma_read(u32 offset)
{
	u32 reg_val;

	reg_val = readl(get_bma_dev()->bma_pci_dev->bma_base_addr + offset);
	BMA_LOG(DLOG_DEBUG, "readl, offset 0x%x val 0x%x\n", offset, reg_val);
	return reg_val;
}

static void pcie_dma_write(u32 offset, u32 reg_val)
{
	u32 read_val;

	(void)writel(reg_val, get_bma_dev()->bma_pci_dev->bma_base_addr + offset);
	read_val = readl(get_bma_dev()->bma_pci_dev->bma_base_addr + offset);
	if (read_val != reg_val) {
		BMA_LOG(DLOG_DEBUG,
			"writel fail, read_value: 0x%x, set_value: 0x%x, offset: 0x%x\n",
			read_val, reg_val, offset);
		return;
	}
	BMA_LOG(DLOG_DEBUG, "writel, offset 0x%x val 0x%x\n", offset, reg_val);
}

static void set_dma_queue_int_msk(u32 val)
{
	(void)pcie_dma_write(PCIE_DMA_QUEUE_INT_MSK_0_REG, val);
}

static void set_dma_queue_err_int_msk(u32 val)
{
	union U_DMA_QUEUE_INT_MSK reg_val;

	// The least significant bit (bit 0) of this register is reserved and must be cleared,
	// while the remaining bits should retain their original values.
	reg_val.u32 = val & 0xFFFFFFFE;
	(void)pcie_dma_write(PCIE_DMA_QUEUE_ERR_INT_MSK_0_REG, reg_val.u32);
}

static void set_dma_queue_int_sts(u32 val)
{
	union U_DMA_QUEUE_INT_STS reg_val;

	reg_val.u32 = val;
	(void)pcie_dma_write(PCIE_DMA_QUEUE_INT_STS_0_REG, reg_val.u32);
}

static void get_dma_queue_int_sts(u32 *val)
{
	union U_DMA_QUEUE_INT_STS reg_val;

	reg_val.u32 = pcie_dma_read(PCIE_DMA_QUEUE_INT_STS_0_REG);
	*val = reg_val.u32;
}

static void get_dma_queue_fsm_sts(u32 *val)
{
	union U_DMA_QUEUE_FSM_STS reg_val;

	reg_val.u32 = pcie_dma_read(PCIE_DMA_QUEUE_FSM_STS_0_REG);
	*val = reg_val.bits.dma_queue_sts;
}

static void pause_dma_queue(u32 val)
{
	union U_DMA_QUEUE_CTRL0 reg_val;

	reg_val.u32 = pcie_dma_read(PCIE_DMA_QUEUE_CTRL0_0_REG);
	reg_val.bits.dma_queue_pause = val;
	(void)pcie_dma_write(PCIE_DMA_QUEUE_CTRL0_0_REG, reg_val.u32);
}

static void enable_dma_queue(u32 val)
{
	union U_DMA_QUEUE_CTRL0 reg_val;

	reg_val.u32 = pcie_dma_read(PCIE_DMA_QUEUE_CTRL0_0_REG);
	reg_val.bits.dma_queue_en = val;
	(void)pcie_dma_write(PCIE_DMA_QUEUE_CTRL0_0_REG, reg_val.u32);
}

static void reset_dma_queue(u32 val)
{
	union U_DMA_QUEUE_CTRL1 reg_val;

	reg_val.u32 = pcie_dma_read(PCIE_DMA_QUEUE_CTRL1_0_REG);
	reg_val.bits.dma_queue_reset = val;
	(void)pcie_dma_write(PCIE_DMA_QUEUE_CTRL1_0_REG, reg_val.u32);
}

static void set_dma_queue_sq_tail(u32 val)
{
	union U_DMA_QUEUE_SQ_TAIL_PTR reg_val;

	reg_val.bits.dma_queue_sq_tail_ptr = val;
	(void)pcie_dma_write(PCIE_DMA_QUEUE_SQ_TAIL_PTR_0_REG, reg_val.u32);
}

static void set_dma_queue_cq_head(u32 val)
{
	union U_DMA_QUEUE_CQ_HEAD_PTR reg_val;

	reg_val.bits.dma_queue_cq_head_ptr = val;
	(void)pcie_dma_write(PCIE_DMA_QUEUE_CQ_HEAD_PTR_0_REG, reg_val.u32);
}

void set_dma_queue_sq_base_l(u32 val)
{
	(void)pcie_dma_write(PCIE_DMA_QUEUE_SQ_BASE_L_0_REG, val);
}

void set_dma_queue_sq_base_h(u32 val)
{
	(void)pcie_dma_write(PCIE_DMA_QUEUE_SQ_BASE_H_0_REG, val);
}

void set_dma_queue_cq_base_l(u32 val)
{
	(void)pcie_dma_write(PCIE_DMA_QUEUE_CQ_BASE_L_0_REG, val);
}

void set_dma_queue_cq_base_h(u32 val)
{
	(void)pcie_dma_write(PCIE_DMA_QUEUE_CQ_BASE_H_0_REG, val);
}

static void set_dma_queue_sq_depth(u32 val)
{
	union U_DMA_QUEUE_SQ_DEPTH reg_val;

	reg_val.u32 = pcie_dma_read(PCIE_DMA_QUEUE_SQ_DEPTH_0_REG);
	reg_val.bits.dma_queue_sq_depth = val;
	(void)pcie_dma_write(PCIE_DMA_QUEUE_SQ_DEPTH_0_REG, reg_val.u32);
}

static void set_dma_queue_cq_depth(u32 val)
{
	union U_DMA_QUEUE_CQ_DEPTH reg_val;

	reg_val.u32 = pcie_dma_read(PCIE_DMA_QUEUE_CQ_DEPTH_0_REG);
	reg_val.bits.dma_queue_cq_depth = val;
	(void)pcie_dma_write(PCIE_DMA_QUEUE_CQ_DEPTH_0_REG, reg_val.u32);
}

static void set_dma_queue_arb_weight(u32 val)
{
	union U_DMA_QUEUE_CTRL0 reg_val;

	reg_val.u32 = pcie_dma_read(PCIE_DMA_QUEUE_CTRL0_0_REG);
	reg_val.bits.dma_queue_arb_weight = val;
	(void)pcie_dma_write(PCIE_DMA_QUEUE_CTRL0_0_REG, reg_val.u32);
}

static void set_dma_queue_drct_sel(u32 val)
{
	union U_DMA_QUEUE_CTRL0 reg_val;

	reg_val.u32 = pcie_dma_read(PCIE_DMA_QUEUE_CTRL0_0_REG);
	reg_val.bits.dma_queue_cq_drct_sel = val;
	reg_val.bits.dma_queue_sq_drct_sel = val;
	(void)pcie_dma_write(PCIE_DMA_QUEUE_CTRL0_0_REG, reg_val.u32);
}

static void get_dma_queue_sq_tail(u32 *val)
{
	union U_DMA_QUEUE_SQ_TAIL_PTR reg_val;

	reg_val.u32 = pcie_dma_read(PCIE_DMA_QUEUE_SQ_TAIL_PTR_0_REG);
	*val = reg_val.bits.dma_queue_sq_tail_ptr;
}

static void get_dma_queue_cq_tail(u32 *val)
{
	union U_DMA_QUEUE_CQ_TAIL_PTR reg_val;

	reg_val.u32 = pcie_dma_read(PCIE_DMA_QUEUE_CQ_TAIL_PTR_0_REG);
	*val = reg_val.bits.dma_queue_cq_tail_ptr;
}

static void get_dma_queue_sq_head(u32 *val)
{
	u32 reg_val;

	reg_val = pcie_dma_read(PCIE_DMA_QUEUE_SQ_STS_0_REG);
	/* dma_queue_sq_head_ptr bit[15:0] */
	*val = reg_val & 0xFFFF;
}

static void set_dma_queue_err_abort(u32 val)
{
	union U_DMA_QUEUE_CTRL0 reg_val;

	reg_val.u32 = pcie_dma_read(PCIE_DMA_QUEUE_CTRL0_0_REG);
	reg_val.bits.dma_queue_sq_pa_lkp_err_abort_en = val;
	reg_val.bits.dma_queue_sq_proc_err_abort_en = val;
	reg_val.bits.dma_queue_sq_drop_err_abort_en = val;
	reg_val.bits.dma_queue_sq_cfg_err_abort_en = val;
	(void)pcie_dma_write(PCIE_DMA_QUEUE_CTRL0_0_REG, reg_val.u32);
}

static void set_dma_queue_flr_disable(u32 val)
{
	(void)pcie_dma_write(PCIE_DMA_FLR_DISABLE_REG, val);
}

static void clear_dma_queue_int_chk(u32 mask)
{
	u32 int_sts;

	(void)get_dma_queue_int_sts(&int_sts);
	if (int_sts & mask)
		(void)set_dma_queue_int_sts(mask);
}

s32 check_dma_queue_state(u32 state, u32 flag)
{
	u32 dma_state = 0;
	unsigned long timeout;

	BMA_LOG(DLOG_DEBUG, "state:%u, flag:%u\n", state, flag);

	timeout = jiffies + TIMER_INTERVAL_CHECK;

	while (1) {
		get_dma_queue_fsm_sts(&dma_state);
		BMA_LOG(DLOG_DEBUG, "DMA stats[%u]\n", dma_state);
		// Flag is 0 and state does not equal to target value
		// OR Flag is 1 and state is equal to target value
		if ((!flag && dma_state != state) || (flag && dma_state == state))
			break;

		if (time_after(jiffies, timeout)) {
			BMA_LOG(DLOG_DEBUG, "Wait stats[%u] fail\n", state);
			return -ETIMEDOUT;
		}
		udelay(1);
	}
	return 0;
}

static s32 reset_dma(void)
{
	u32 dma_state = 0;

	/* get dma channel fsm */
	check_dma_queue_state(WAIT_STATE, FALSE);
	get_dma_queue_fsm_sts(&dma_state);
	BMA_LOG(DLOG_DEBUG, "dma_state:%u\n", dma_state);
	switch (dma_state) {
	/* idle status, dma channel need no reset */
	case IDLE_STATE:
		return 0;
	case RUN_STATE:
		pause_dma_queue(ENABLE);
		fallthrough;
	case ABORT_STATE:
	case CPL_STATE:
		enable_dma_queue(DISABLE);
		if (check_dma_queue_state(RUN_STATE, FALSE))
			return -ETIMEDOUT;
		fallthrough;
	case PAUSE_STATE:
	case HALT_STATE:
		set_dma_queue_sq_tail(0);
		set_dma_queue_cq_head(0);
		reset_dma_queue(ENABLE);
		pause_dma_queue(DISABLE);
		if (check_dma_queue_state(IDLE_STATE, TRUE))
			return -ETIMEDOUT;
		fallthrough;
	default:
		return -EINVAL;
	}

	return 0;
}

static void init_dma(void)
{
	/* set dma channel sq tail */
	set_dma_queue_sq_tail(0);
	/* set dma channel cq head */
	set_dma_queue_cq_head(0);
	/* set dma queue drct sel */
	set_dma_queue_drct_sel(DRC_LOCAL);
	/* set dma channel sq depth */
	set_dma_queue_sq_depth(SQ_DEPTH - 1);
	/* set dma channel cq depth */
	set_dma_queue_cq_depth(CQ_DEPTH - 1);
	/* dma not process FLR , only cpu process FLR */
	set_dma_queue_flr_disable(0x1);
	/* set dma queue arb weight */
	set_dma_queue_arb_weight(0x1F);
	/* clear dma queue int status */
	set_dma_queue_int_sts(0x1FFF);
	/* set dma queue int mask */
	set_dma_queue_err_int_msk(0x0);
	set_dma_queue_int_msk(0x0);
	/* set dma queue abort err en */
	set_dma_queue_err_abort(ENABLE);
	/* enable dma channel en */
	enable_dma_queue(ENABLE);
}

s32 wait_done_dma_queue(unsigned long timeout)
{
	struct dma_ch_cq_s *p_cur_last_cq;
	struct dma_ch_cq_s *p_dma_cq;
	unsigned long end;
	u32 sq_tail;
	u32 sq_valid;
	u32 cq_tail;
	u32 cq_valid;

	p_dma_cq = (struct dma_ch_cq_s *)((&get_bma_dev()->edma_host)->edma_cq_addr);
	end = jiffies + timeout;

	while (time_before(jiffies, end)) {
		(void)get_dma_queue_sq_tail(&sq_tail);
		(void)get_dma_queue_cq_tail(&cq_tail);

		cq_valid = (cq_tail + CQ_DEPTH - 1) % (CQ_DEPTH);
		p_cur_last_cq = &p_dma_cq[cq_valid];
		sq_valid = (sq_tail + SQ_DEPTH - 1) % (SQ_DEPTH);
		BMA_LOG(DLOG_DEBUG,
			"sq_tail %d, cq_tail %d, cq_valid %d, sq_valid %d, p_cur_last_cq->sqhd %d\n",
			sq_tail, cq_tail, cq_valid, sq_valid, p_cur_last_cq->sqhd);
		if (p_cur_last_cq->sqhd == sq_valid) {
			set_dma_queue_cq_head(cq_valid);
			return 0;
		}
	}

	return -ETIMEDOUT;
}

static s32 submit_dma_queue_sq(u32 dir, struct bspveth_dmal pdmalbase_v, u32 pf)
{
	u32 sq_tail;
	u32 sq_head;
	u32 sq_availble;
	struct dma_ch_sq_s sq_submit;
	struct dma_ch_sq_s *p_dma_sq;

	p_dma_sq = (struct dma_ch_sq_s *)((&get_bma_dev()->edma_host)->edma_sq_addr);
	(void)get_dma_queue_sq_tail(&sq_tail);
	(void)get_dma_queue_sq_head(&sq_head);
	sq_availble = SQ_DEPTH - 1 - (((sq_tail - sq_head) + SQ_DEPTH) % SQ_DEPTH);
	if (sq_availble < 1) {
		BMA_LOG(DLOG_DEBUG, "cannot process %u descriptors, try again later\n", 1);
		return -1;
	}

	BMA_LOG(DLOG_DEBUG, "submit dma queue sq, sq_tail get %d, sq_head %d, sq_availble %d\n",
		sq_tail, sq_head, sq_availble);

	(void)memset(&sq_submit, 0, sizeof(sq_submit));
	if (dir == DIR_H2B)
		sq_submit.opcode = DMA_READ;
	else
		sq_submit.opcode = DMA_WRITE;

	BMA_LOG(DLOG_DEBUG, "PF: %u\n", pf);
	sq_submit.ldie = ENABLE;
	sq_submit.rdie = ENABLE;
	sq_submit.attr &= (~0x2); /* SO(Strong Ordering) */
	sq_submit.pf = pf & 0x7;  /* 0x7 */
	sq_submit.p3p4 = (pf >> 3) & 0x3; /* 0x3 */
	sq_submit.length = pdmalbase_v.len;
	sq_submit.src_addr_l = pdmalbase_v.slow;
	sq_submit.src_addr_h = pdmalbase_v.shi;
	sq_submit.dst_addr_l = pdmalbase_v.dlow;
	sq_submit.dst_addr_h = pdmalbase_v.dhi;

	BMA_LOG(DLOG_DEBUG, "submit dma queue sq, dir %d, op %d, length %d\n", dir,
		sq_submit.opcode, sq_submit.length);

	memcpy(p_dma_sq + sq_tail, &sq_submit, sizeof(sq_submit));
	sq_tail = (sq_tail + 1) % SQ_DEPTH;

	BMA_LOG(DLOG_DEBUG, "submit dma queue sq, sq_tail change %d,\n", sq_tail);
	wmb(); /* memory barriers. */

	/* readl last u32 of sq buffer descriptor to confirm the sq buffer descriptor
	 * write to local sq
	 */
	(void)readl((void __iomem *)(p_dma_sq + sq_tail + sizeof(struct dma_ch_sq_s)
				     - sizeof(u32)));

	(void)set_dma_queue_sq_tail(sq_tail);

	return 0;
}

s32 transfer_dma_queue(struct bma_dma_transfer_s *dma_transfer)
{
	struct bspveth_dmal *pdmalbase_v;
	u32 dmal_cnt;
	s32 ret;
	int i;

	if (!dma_transfer) {
		BMA_LOG(DLOG_DEBUG, "dma_transfer is NULL.\n");
		return -EFAULT;
	}

	BMA_LOG(DLOG_DEBUG, "transfer dma queue.\n");

	/* clear local done int */
	clear_dma_queue_int_chk(DMA_DONE_MASK);

	pdmalbase_v = dma_transfer->pdmalbase_v;
	dmal_cnt = dma_transfer->dmal_cnt;
	for (i = 0; i < dmal_cnt; i++)
		submit_dma_queue_sq(dma_transfer->dir, pdmalbase_v[i],
				    get_bma_dev()->bma_pci_dev->pdev->devfn);

	(void)set_dma_queue_int_msk(DMA_DONE_UNMASK);
	(void)set_dma_queue_err_int_msk(DMA_ERR_UNMASK);
	(void)enable_dma_queue(ENABLE);

	ret = wait_done_dma_queue(DMA_TMOUT);
	if (ret)
		BMA_LOG(DLOG_DEBUG, "EP DMA: dma wait timeout");

	return ret;
}

void reset_edma_host(struct edma_host_s *edma_host)
{
	unsigned long flags = 0;
	int count = 0;

	if (!edma_host)
		return;

	spin_lock_irqsave(&edma_host->reg_lock, flags);

	while (count++ < MAX_RESET_DMA_TIMES) {
		if (reset_dma() == 0) {
			BMA_LOG(DLOG_DEBUG, "reset dma successfully\n");
			init_dma();
			break;
		}

		mdelay(DELAY_BETWEEN_RESET_DMA);
	}

	spin_unlock_irqrestore(&edma_host->reg_lock, flags);
	BMA_LOG(DLOG_DEBUG, "reset dma count=%d\n", count);
}
