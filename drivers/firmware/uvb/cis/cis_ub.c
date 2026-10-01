// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: UVB over UB message processing module, handles CIS UB init,
 *              func register/lookup and UB message.
 * Author: mengkanglai
 * Create: 2025-12-18
 */

#define pr_fmt(fmt) "[UVB_UB]: " fmt

#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/auxiliary_bus.h>
#include <ub/ubase/ubase_comm_dev.h>
#include <ub/ubase/ubase_comm_cmd.h>
#include "cis_ub.h"
#include "cis_info_process.h"

enum uvb_opcode {
	CIS_OPC_SEND = 0xFA00,
	CIS_OPC_RECV = 0xFA01,
};

void ub_msg_header_init(struct msg_pkt_header *header)
{
	header->ulh.cfg = UB_COMPACT_LINK_CFG;
	header->nth.nth_nlp = NTH_NLP_WITH_TPH;
	header->nth.mm = NTH_NLP_WITH_MGMT;
	header->ctph_nlp = CTPH_NLP_UPI_40BITS_UEID;
	header->tp_opcode = CTPH_OPCODE_NOT_CNP;
	header->btah.ta_opcode = TAH_OPCODE_MSG;
	header->msgetah.msg_code = UB_MSG_CODE;
	header->msgetah.sub_msg_code = UB_SUB_MSG_CODE;
	header->pload.type = UB_PAYLOAD_TYPE;
	header->pload.version = UB_PAYLOAD_VERSION;
}

void setup_resp_pkt(struct msg_pkt_header *resp, struct msg_pkt_header *rcv)
{
	struct compact_network_header *nth = &rcv->nth;

	ub_msg_header_init(resp);
	resp->upi = rcv->upi;
	resp->pload.message_id = ~rcv->pload.message_id;
	resp->pload.sender_id = rcv->pload.sender_id;
	resp->pload.receiver_id = rcv->pload.receiver_id;
	resp->nth.dcna = nth->scna;
	resp->deid = eid_gen(rcv->seid_h, rcv->seid_l);
	resp->nth.scna = nth->dcna;
	resp->seid_l = seid_low(rcv->deid);
	resp->seid_h = seid_high(rcv->deid);
	resp->msgetah.rsp = 0x1;
	resp->btah.ini_tassn = rcv->btah.ini_tassn;
	resp->btah.ini_rc_id = rcv->btah.ini_rc_id;
	resp->btah.ee = rcv->btah.ee;
}

static bool check_pload_valid(struct payload *pload, u32 len)
{
	if (!pload->sender_id || !pload->receiver_id) {
		pr_err("senderid or receiverid can't be null\n");
		return false;
	}

	if (UBIOS_GET_MESSAGE_FLAG(pload->message_id) != UBIOS_CALL_ID_FLAG) {
		pr_err("uvb over ub not a cis call\n");
		return false;
	}

	if (len > sizeof(struct msg_pkt_header) && !pload->data_total_size) {
		pr_err("ub pload have data but data size is null\n");
		return false;
	}

	if (len == sizeof(struct msg_pkt_header) && pload->data_total_size) {
		pr_err("ub pload no data but data size is not null\n");
		return false;
	}

	return true;
}

int uvb_handle_cmdq_event(void *dev, void *data, u32 len)
{
	int err = 0;
	msg_handler func;
	struct cis_message msg = {};
	struct ubase_cmd_buf in;
	struct msg_pkt_header *resp;
	struct msg_pkt_header *ub_header = (struct msg_pkt_header *)data;
	struct payload *pload = &ub_header->pload;
	u32 output_size = pload->output_buffer_size == UVB_OUTPUT_SIZE_NULL ? 0 :
		pload->output_buffer_size;
	msg.input = pload->data;
	msg.input_size = pload->data_total_size;

	if (ub_header->msgetah.plen - sizeof(struct payload) < pload->data_total_size) {
		pr_err("it's a framed message, not supported\n");
		return -EINVAL;
	}

	if (!check_pload_valid(pload, len)) {
		pr_err("check payload param failed\n");
		return -EINVAL;
	}

	pr_info("handle msg for callid=%08x, receverid=%08x\n",
			pload->message_id, pload->receiver_id);
	func = search_local_cis_func(pload->message_id, pload->receiver_id);
	if (!func) {
		pr_err("not found registered func\n");
		return -EOPNOTSUPP;
	}

	resp = kzalloc(sizeof(struct msg_pkt_header) + output_size, GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	if (!output_size) {
		msg.p_output_size = NULL;
		msg.output = NULL;
	} else {
		resp->pload.data_total_size = output_size;
		msg.p_output_size = &resp->pload.data_total_size;
		msg.output = resp->pload.data;
	}

	err = func(&msg);
	if (err)
		pr_err("execute cis func failed for %08x, err=%d\n", pload->message_id, err);

	setup_resp_pkt(resp, ub_header);
	resp->msgetah.plen = resp->pload.data_total_size + sizeof(struct payload);
	resp->pload.returned_status = (u32)err;

	ubase_fill_inout_buf(&in, CIS_OPC_SEND, false,
			sizeof(struct msg_pkt_header) + resp->pload.data_total_size, resp);
	err = ubase_cmd_send_in(dev, &in);
	if (err)
		pr_err("send ubase info err, err=%d\n", err);

	kfree(resp);

	return err;
}

struct ubase_crq_event_nb uvb_crq_events = {
	.opcode = CIS_OPC_RECV,
	.crq_handler = uvb_handle_cmdq_event,
};

int uvb_probe(struct auxiliary_device *aux_dev, const struct auxiliary_device_id *id)
{
	int ret = 0;

	uvb_crq_events.back = aux_dev;
	ret = ubase_register_crq_event(aux_dev, &uvb_crq_events);
	if (ret) {
		pr_err("register uvb crq events failed, err=%d\n", ret);
		return -1;
	}

	pr_info("register uvb crq events success\n");

	return 0;
}

void uvb_remove(struct auxiliary_device *aux_dev)
{
	ubase_unregister_crq_event(aux_dev, CIS_OPC_RECV);
}
