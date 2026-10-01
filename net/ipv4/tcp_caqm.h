/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) Huawei Technologies Co., Ltd. 2020-2024
 * All rights reserved.
 *
 * Define the caqm header file
 */
#ifndef _TCP_CAQM_H
#define _TCP_CAQM_H
#ifdef CONFIG_ETH_CAQM

#include <linux/if_caqm.h>
#include <linux/types.h>
#include <linux/math64.h>
#include <linux/tcp.h>

/* CAQM parameter */
#define CAQM_ALPHA_SHIFT (3U)
#define CAQM_MTU_SIZE (sysctl_caqm_mtu_unit)
#define CAQM_UNIT_VALUE (sysctl_caqm_data_hint_unit)
#define CAQM_ACK_UNIT_VALUE (sysctl_caqm_ack_hint_unit)

// parameter alpha for generating hint value, hint = (caqm_para_alpha >> 3) * MTU / cw * MTU
#define caqm_para_alpha (sysctl_caqm_alpha_fx_8)
// parameter beta for updating caqm_cwd, cwd -= beta when 'C=1' is recved
#define caqm_para_beta (sysctl_caqm_beta)
// parameter for the minimum caqm_cwd
#define caqm_para_min_cwnd (sysctl_caqm_min_cwnd)

#define MAX_HINT_VAL (0xFF)

/* caqm_flags in tcp_sock alias ecn_flags(already used 1,2,4,8) */
#define TCP_CAQM_SRV (16)
#define TCP_CAQM_CLI (32)
#define TCP_CAQM_OK	(TCP_CAQM_SRV | TCP_CAQM_CLI)

#define TCP_EXFLAGS_CLI_CAQM (1)
#define TCP_EXFLAGS_SRV_CAQM (2)

/* CAQM Alg State */
enum CaqmState {
	CAQM_STATE_START = 1,	// Slow_Start
	CAQM_STATE_CONG,		// Cong_Avoid
};

/* CAQM Alg context */
struct caqm_ca {
	u16 caqm_ca_enable;
	u16 sender_state;
	s64 cw_to_back;		// delta cwnd to feedback
	s64 left_hint_sum;	// left hint, Unit is 1/2^12
	int totalCwndAdjust;
	u32 loss_cwnd;
};

static inline void init_caqm_ca(struct caqm_ca *caqm_ca)
{
	caqm_ca->caqm_ca_enable = 0;
	caqm_ca->sender_state = CAQM_STATE_START;
	caqm_ca->cw_to_back = 0;
	caqm_ca->loss_cwnd = 0;
	caqm_ca->left_hint_sum = 0;
	caqm_ca->totalCwndAdjust = 0;
	// tp->ecn_flags can not be initialized here,
	//  require the original tcp code to set it as 0.
}

static inline bool tcp_caqm_is_cwnd_limited(const struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	const struct caqm_ca *caqm_ca = inet_csk_ca(sk);

	if (tp->is_cwnd_limited)
		return true;

	if (caqm_ca->sender_state == CAQM_STATE_START)
		return tp->snd_cwnd < 2 * tp->max_packets_out;

	return false;
}

/* Step 1：get the hint value from caqm  */
static inline u8 get_data_to_set_hint(struct sock *sk, const u32 tcp_cwnd)
{
	struct caqm_ca *caqm_ca = inet_csk_ca(sk);
	struct tcp_sock *tp = tcp_sk(sk);

	// Attention need MTU_SIZE%sysctl_caqm_data_hint_unit == 0
	u8 hint = CAQM_MTU_SIZE / sysctl_caqm_data_hint_unit;

	if (caqm_ca->sender_state == CAQM_STATE_CONG) {
		u32 rtt = sysctl_caqm_rtt_standard;

		if (tp->srtt_us > sysctl_caqm_rtt_standard / 4 &&
		    tp->srtt_us < sysctl_caqm_rtt_standard * 4) {
			rtt = tp->srtt_us;
		}
		u32 temp = (caqm_para_alpha * rtt * CAQM_MTU_SIZE) /
		    (tcp_cwnd * sysctl_caqm_data_hint_unit * sysctl_caqm_rtt_standard);
		temp = temp >> CAQM_ALPHA_SHIFT;

		if (temp >= MAX_HINT_VAL) {
			hint = MAX_HINT_VAL;
			return hint;
		}
		hint = temp;
		u64 tmp1 = (((u64)rtt * CAQM_MTU_SIZE) << FIXED_POINT_20);
		u32 tmp2 = tcp_cwnd * sysctl_caqm_data_hint_unit * sysctl_caqm_rtt_standard;
		s64 hint_last;

		tmp1 *= caqm_para_alpha;
		tmp1 = tmp1 >> CAQM_ALPHA_SHIFT;
		do_div(tmp1, tmp2);
		hint_last = (s64)tmp1 - ((s64)hint << FIXED_POINT_20);
		caqm_ca->left_hint_sum += hint_last;
		if (caqm_ca->left_hint_sum >= FIXED_POINT_20_UNIT) {
			caqm_ca->left_hint_sum -= FIXED_POINT_20_UNIT;
			hint += 1U;
		}
	}
	return hint;
}

/* fill the data packet's caqm_hdr_info */
static inline void build_data_hdr(u8 hint, struct caqm_hdr_info *pkt)
{
	*((u16 *)pkt) = 0;
	pkt->cc_type = sysctl_caqm_cc_type;
	pkt->caqm_en = 1;
	pkt->c_bit = 0;
	pkt->i_bit = 1;
	pkt->hint = hint;
}

static inline void set_data_caqm_hdr(struct sk_buff *skb, struct sock *sk, const u32 tcp_cwnd)
{
	u8 data_hint = get_data_to_set_hint(sk, tcp_cwnd);
	struct skb_caqm_info *cinfo = get_skb_caqm_info(skb);

	build_data_hdr(data_hint, (struct caqm_hdr_info *)&cinfo->send_hdr);
}

/* Step 3: fill ACK and feedback */
/* Hint upper bound for one ACK */
#define MAX_WIN_DELTA_VAL (MAX_HINT_VAL * (int)sysctl_caqm_ack_hint_unit)
#define MIN_WIN_DELTA_VAL (0 - MAX_WIN_DELTA_VAL)
static inline int get_ack_back_hint(const struct caqm_ca *caqm_ca)
{
	if (caqm_ca->cw_to_back >= MAX_WIN_DELTA_VAL)
		return MAX_HINT_VAL;
	else if (caqm_ca->cw_to_back <= MIN_WIN_DELTA_VAL)
		return 0 - MAX_HINT_VAL;
	u32 cw_to_back_sign = caqm_ca->cw_to_back < 0 ? 1 : 0;
	u64 tmp_val = cw_to_back_sign ? (0 - caqm_ca->cw_to_back) : (caqm_ca->cw_to_back);

	do_div(tmp_val, sysctl_caqm_ack_hint_unit);
	return cw_to_back_sign ? (0 - (s64)tmp_val) : tmp_val;
}

/* get the ack to back: fill it */
static inline void build_ack_hdr(int hint, struct caqm_hdr_info *pkt)
{
	*((u16 *)pkt) = 0;
	pkt->caqm_en = 0;
	pkt->cc_type = sysctl_caqm_cc_type;
	if (hint >= 0) {
		pkt->c_bit = 0;
		pkt->i_bit = 1;
		pkt->hint = hint;
	} else {// hint < 0, need down speed
		pkt->c_bit = 1;
		pkt->i_bit = 0;
		pkt->hint = 0 - hint;
	}
}

/* set the ack and update cw_to_back */
static inline void set_ack_caqm_hdr(struct caqm_ca *caqm_ca, struct caqm_hdr_info *ack_hdr_info)
{
	int ack_hint = get_ack_back_hint(caqm_ca);

	build_ack_hdr(ack_hint, ack_hdr_info);
	caqm_ca->cw_to_back -= (s64)ack_hint * sysctl_caqm_ack_hint_unit;
}

/* Step 4: recv ACK, update cwnd */
static inline void update_caqm_state(struct caqm_ca *caqm_ca, struct sk_buff *skb)
{
	struct skb_caqm_info *cinfo = get_skb_caqm_info(skb);
	// if (cinfo->caqm_en || (cinfo->c_bit && cinfo->i_bit)) {
	if (cinfo->pkt_type == CAQM_PKT_ACK && cinfo->recv_hint <= 0 &&
	    caqm_ca->sender_state == CAQM_STATE_START)
		caqm_ca->sender_state = CAQM_STATE_CONG;
}

// For tcp_input.c
static inline bool tcp_ca_needs_caqm(const struct sock *sk)
{
	const struct inet_connection_sock *icsk = inet_csk(sk);

	return icsk->icsk_ca_ops->flags & TCP_CONG_NEEDS_CAQM;
}
#endif

static inline void try_to_recv_pkt_w_caqm(struct sock *sk, struct sk_buff *skb)
{
#ifdef CONFIG_ETH_CAQM
	struct skb_caqm_info *cinfo = get_skb_caqm_info(skb);
	struct tcp_sock *tp = tcp_sk(sk);
	struct caqm_ca *caqm_ca = inet_csk_ca(sk);
	const struct tcphdr *th = tcp_hdr(skb);

	if (!static_branch_unlikely(&sysctl_caqm_enable) || !tcp_ca_needs_caqm(sk))
		return;

	// Deal with the syn packet
	if (th->syn) {
		if (!th->ack && !th->rst && sk->sk_state == TCP_LISTEN) {
			tp->ecn_flags |= TCP_CAQM_SRV;
			if (th->res1 & TCP_EXFLAGS_CLI_CAQM)
				tp->ecn_flags |= TCP_CAQM_CLI;
		} else if (sk->sk_state == TCP_SYN_SENT) {
			if (th->res1 & TCP_EXFLAGS_SRV_CAQM)
				tp->ecn_flags |= TCP_CAQM_SRV;
		}
		return;
	}

	if (cinfo->recv_en == 0)
		return;

	// pkt has caqm hdr
	caqm_ca->caqm_ca_enable = 1;
	// ACKor payload?
	if (cinfo->pkt_type == CAQM_PKT_DATA) {
		caqm_ca->cw_to_back += cinfo->recv_hint; // record data cwnd to caqm_ca
	} else if (cinfo->pkt_type == CAQM_PKT_ACK) {
		if (sk->sk_state == TCP_ESTABLISHED)
			update_caqm_state(caqm_ca, skb);
		caqm_ca->totalCwndAdjust += cinfo->recv_hint;
		int pkt_num = caqm_ca->totalCwndAdjust / sysctl_caqm_mtu_unit;

		caqm_ca->totalCwndAdjust -= pkt_num * sysctl_caqm_mtu_unit;
		if (tcp_caqm_is_cwnd_limited(sk) || pkt_num < 0)
			tp->snd_cwnd = max_t(int, (int)tp->snd_cwnd + pkt_num, caqm_para_min_cwnd);
	}
#endif
}

// For tcp_output.c
// 1. update skb's caqm metadata;
// 2. return the 4b tcp header reserved field to add;
static inline u8 try_to_update_skb_for_caqm(struct sock *sk, struct sk_buff *skb)
{
#ifdef CONFIG_ETH_CAQM
	struct tcp_sock *tp = tcp_sk(sk);
	struct skb_caqm_info *cinfo = get_skb_caqm_info(skb);
	bool skb_has_syn = ((TCP_SKB_CB(skb)->tcp_flags & TCPHDR_SYN) == TCPHDR_SYN);
	bool skb_has_ack = ((TCP_SKB_CB(skb)->tcp_flags & TCPHDR_ACK) == TCPHDR_ACK);
	bool tp_ecn_has_caqm = ((tp->ecn_flags & TCP_CAQM_OK) == TCP_CAQM_OK);
	struct caqm_ca *caqm_ca = inet_csk_ca(sk);
	u8 tcp_hdr_rsrvd_4b = 0;

	if (!static_branch_unlikely(&sysctl_caqm_enable))
		return 0;
	if (!tcp_ca_needs_caqm(sk))
		return 0;

	if (tp_ecn_has_caqm && !skb_has_syn && cinfo->send_en == 0) {
		/* Pure ACK or Payload? */
		if (skb->len == 0)
			set_ack_caqm_hdr(caqm_ca, (struct caqm_hdr_info *)&cinfo->send_hdr);
		else
			set_data_caqm_hdr(skb, sk, tp->snd_cwnd);

		set_skb_caqm_info_send_en(skb, 1);
	} else if (skb_has_syn && skb_has_ack) {
		/* Packet CAQM state for a SYN-ACK */
		tcp_hdr_rsrvd_4b |= TCP_EXFLAGS_SRV_CAQM;
	} else if (skb_has_syn) {
		/* Packet CAQM state for a SYN.  */
		tp->ecn_flags |= TCP_CAQM_CLI; //init caqm flags
		tcp_hdr_rsrvd_4b |= TCP_EXFLAGS_CLI_CAQM;
	}
	return tcp_hdr_rsrvd_4b;
#endif
	return 0;
}

static inline void tcp_caqm_make_synack(const struct sock *sk, struct tcphdr *th)
{
#ifdef CONFIG_ETH_CAQM
	if (static_branch_unlikely(&sysctl_caqm_enable) && tcp_ca_needs_caqm(sk))
		th->res1 |= TCP_EXFLAGS_SRV_CAQM;
#endif
}

// For tcp_minisocks.c
static inline void tcp_copy_ecn_flags(struct sock *parent, struct sock *child)
{
#ifdef CONFIG_ETH_CAQM
	if (static_branch_unlikely(&sysctl_caqm_enable)) {
		struct tcp_sock *parent_tp = tcp_sk(parent);
		struct tcp_sock *child_tp = tcp_sk(child);

		if (parent_tp->ecn_flags != child_tp->ecn_flags)
			child_tp->ecn_flags = parent_tp->ecn_flags;
	}
#endif
}

#ifdef CONFIG_ETH_CAQM
static inline int caqm_leave_room_size(const struct sock *sk)
{
	if (!static_branch_unlikely(&sysctl_caqm_enable) || !tcp_ca_needs_caqm(sk))
		return 0;
	return CAQM_HLEN;
}
#endif

#endif
