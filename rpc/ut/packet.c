/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Nachiket Sahasrabuddhe <nachiket_sahasrabuddhe@xyratex.com>
 * Original creation date: 10/04/2012
 */

#include <limits.h>		 /* UCHAR_MAX */
#include "colibri/init.h"
#include "colibri/magic.h"
#include "lib/vec.h"
#include "lib/memory.h"		/* C2_ALLOC_ARR */
#include "lib/misc.h"
#include "lib/ut.h"
#include "fop/fop.h"
#include "rpc/packet.h"
#include "rpc/it/ping_fop.h"
#include "rpc/it/ping_fop_ff.h"
#include "addb/addbff/addb_ff.h"
#include "rpc/rpc_opcodes.h"

#define cmp_obj(obj1, obj2, field) obj1->field == obj2->field

enum {
	NR = 255
};

static void fop_fini_and_free(struct c2_fop *fop)
{
	struct c2_fop_ping    *ping_fop_data;
	struct c2_addb_record *addb_fop_data;

	C2_UT_ASSERT(fop != NULL);
	C2_UT_ASSERT(c2_fop_data(fop) != NULL);

	switch (c2_fop_opcode(fop)) {
	case C2_RPC_PING_OPCODE:
		ping_fop_data = c2_fop_data(fop);
		if (ping_fop_data->fp_arr.f_data != NULL)
			c2_free(ping_fop_data->fp_arr.f_data);
		break;

	case C2_ADDB_RECORD_REQUEST_OPCODE:
		addb_fop_data = c2_fop_data(fop);
		if (addb_fop_data->ar_data.cmb_count != 0)
			c2_free(addb_fop_data->ar_data.cmb_value);
		break;
	}
	c2_fop_free(fop);
}

static void packet_fini(struct c2_rpc_packet *packet)
{
	struct c2_rpc_item *item;
	struct c2_fop      *fop;

	C2_UT_ASSERT(packet != NULL);

	for_each_item_in_packet(item, packet) {
		fop = c2_rpc_item_to_fop(item);
	        c2_rpc_packet_remove_item(packet, item);
		fop_fini_and_free(fop);
	} end_for_each_item_in_packet;
}

static bool cmp_addb_record_header(struct c2_addb_record_header *header1,
				   struct c2_addb_record_header *header2)
{
	return  cmp_obj(header1, header2, arh_magic1)     &&
		cmp_obj(header1, header2, arh_version)    &&
		cmp_obj(header1, header2, arh_len)        &&
		cmp_obj(header1, header2, arh_event_id)   &&
		cmp_obj(header1, header2, arh_timestamp)  &&
		cmp_obj(header1, header2, arh_magic2);
}

static bool cmp_addb_record_buf(struct c2_mem_buf *buf1,
		                struct c2_mem_buf *buf2)
{
	C2_UT_ASSERT(buf1->cmb_count == buf2->cmb_count);
	C2_UT_ASSERT(buf1->cmb_value != NULL);
	C2_UT_ASSERT(buf2->cmb_value != NULL);

	return c2_forall(i, buf1->cmb_count,
			 buf1->cmb_value[i] == buf2->cmb_value[i]);
}

static void fill_addb_header(struct c2_addb_record_header *header)
{
	header->arh_magic1    = C2_ADDB_REC_HEADER_MAGIC1;
	header->arh_version   = ADDB_REC_HEADER_VERSION;
	header->arh_len       = 0;
	header->arh_event_id  = 1234;
	header->arh_timestamp = c2_time_now();
	header->arh_magic2    = C2_ADDB_REC_HEADER_MAGIC2;
}

static void fill_addb_data(struct c2_mem_buf *addb_data)
{
	int i;

	C2_UT_ASSERT(addb_data->cmb_count != 0);
	C2_UT_ASSERT(addb_data->cmb_value != NULL);

	for (i = 0; i < addb_data->cmb_count; ++i) {
		addb_data->cmb_value[i] = i % UCHAR_MAX;
	}
}

static bool fop_data_compare(struct c2_fop *fop1, struct c2_fop *fop2)
{
	/* Ping fop objects */
	struct c2_fop_ping     *ping_data1;
	struct c2_fop_ping     *ping_data2;
	struct c2_fop_ping_rep *ping_rep_data1;
	struct c2_fop_ping_rep *ping_rep_data2;

	/* ADDB fop objects */
	struct c2_addb_record  *addb_data1;
	struct c2_addb_record  *addb_data2;

	C2_UT_ASSERT(c2_fop_opcode(fop1) == c2_fop_opcode(fop2));

	switch (c2_fop_opcode(fop1)) {

	case C2_RPC_PING_OPCODE:
		ping_data1 = c2_fop_data(fop1);
		ping_data2 = c2_fop_data(fop2);

		return *ping_data1->fp_arr.f_data ==
		       *ping_data2->fp_arr.f_data;

	case C2_RPC_PING_REPLY_OPCODE:
		ping_rep_data1 = c2_fop_data(fop1);
		ping_rep_data2 = c2_fop_data(fop2);

		return ping_rep_data1->fpr_rc == ping_rep_data2->fpr_rc;

	case C2_ADDB_RECORD_REQUEST_OPCODE:
		addb_data1 = c2_fop_data(fop1);
		addb_data2 = c2_fop_data(fop2);
		return cmp_addb_record_header(&addb_data1->ar_header,
				              &addb_data2->ar_header) &&
			cmp_addb_record_buf(&addb_data1->ar_data,
					    &addb_data2->ar_data);
	}
	return false;
}

static bool item_compare(struct c2_rpc_item *item1, struct c2_rpc_item *item2)
{

	return   cmp_obj(item1, item2, ri_flags)                          &&
		 cmp_obj(item1, item2, ri_magic)                          &&
		 cmp_obj(item1, item2, ri_type->rit_magic)                &&
		 cmp_obj(item1, item2, ri_type->rit_flags)                &&
		 cmp_obj(item1, item2, ri_type->rit_opcode)               &&
		 cmp_obj(item1, item2, ri_slot_refs[0].sr_verno.vn_lsn)   &&
		 cmp_obj(item1, item2, ri_slot_refs[0].sr_sender_id)      &&
		 cmp_obj(item1, item2, ri_slot_refs[0].sr_session_id)     &&
		 cmp_obj(item1, item2, ri_slot_refs[0].sr_verno.vn_vc)    &&
		 cmp_obj(item1, item2, ri_slot_refs[0].sr_uuid.su_uuid)   &&
		 cmp_obj(item1, item2,
			 ri_slot_refs[0].sr_last_persistent_verno.vn_lsn) &&
		 cmp_obj(item1, item2,
			 ri_slot_refs[0].sr_last_persistent_verno.vn_vc)  &&
		 cmp_obj(item1, item2,
			 ri_slot_refs[0].sr_last_seen_verno.vn_lsn)       &&
		 cmp_obj(item1, item2,
			 ri_slot_refs[0].sr_last_seen_verno.vn_vc)        &&
		 cmp_obj(item1, item2,
			 ri_slot_refs[0].sr_slot_id)                      &&
		 cmp_obj(item1, item2,
			 ri_slot_refs[0].sr_xid)                          &&
		 cmp_obj(item1, item2, ri_slot_refs[0].sr_slot_gen);
}

static bool packet_compare(struct c2_rpc_packet *p1, struct c2_rpc_packet *p2)
{
	struct c2_rpc_item *item1;
	struct c2_rpc_item *item2;
	struct c2_fop      *fop1;
	struct c2_fop      *fop2;
	bool                rc = true;

	if (p1->rp_nr_items != p2->rp_nr_items || p1->rp_size != p2->rp_size ||
	    p1->rp_status != p2->rp_status)
		return false;
	for (item1 = c2_tlist_head(&packet_item_tl, &p1->rp_items),
	     item2 = c2_tlist_head(&packet_item_tl, &p2->rp_items);
	     item1 != NULL && item2 != NULL;
	     item1 = packet_item_tlist_next(&p1->rp_items, item1),
	     item2 = packet_item_tlist_next(&p2->rp_items, item2)) {
		rc &= item_compare(item1, item2);
		fop1 = c2_rpc_item_to_fop(item1);
		fop2 = c2_rpc_item_to_fop(item2);
		rc &= fop_data_compare(fop1, fop2);
	}
	return rc;
}

void test_packet_encode()
{
	/* Ping FOP and Ping reply FOP */
	struct c2_fop          *ping_fop;
	struct c2_fop          *ping_fop_rep;
	struct c2_fop_ping     *ping_fop_data;
	struct c2_fop_ping_rep *ping_fop_rep_data;

	/* ADDB record request */
	struct c2_fop	       *addb_fop;
	struct c2_addb_record  *addb_fop_data;

	/* RPC objects */
	struct c2_rpc_item     *item;
	struct c2_rpc_packet    p_for_encd;
	struct c2_rpc_packet    p_decoded;
	struct c2_bufvec        bufvec;
	c2_bcount_t             bufvec_size;

	/* Ping FOP and Ping-reply FOP */
	c2_ping_fop_init();
	ping_fop = c2_fop_alloc(&c2_fop_ping_fopt, NULL);
	C2_UT_ASSERT(ping_fop != NULL);

	ping_fop_data = c2_fop_data(ping_fop);
	C2_ALLOC_ARR(ping_fop_data->fp_arr.f_data, 1);
	C2_UT_ASSERT(ping_fop_data->fp_arr.f_data != NULL);

	ping_fop_rep = c2_fop_alloc(&c2_fop_ping_rep_fopt, NULL);
	C2_UT_ASSERT(ping_fop_rep != NULL);

	ping_fop_rep_data = c2_fop_data(ping_fop_rep);
	ping_fop_rep_data->fpr_rc = 1001;

	C2_UT_ASSERT(!c2_bufvec_alloc_aligned(&bufvec, NR,
				              C2_SEG_SIZE, C2_SEG_SHIFT));

	ping_fop_data->fp_arr.f_count = 1;

	item = &ping_fop->f_item;
	item->ri_ops = &c2_fop_default_item_ops;

	ping_fop_data->fp_arr.f_data[0] = 1000;
	c2_rpc_packet_init(&p_for_encd);
	c2_rpc_packet_add_item(&p_for_encd, item);

	item = &ping_fop_rep->f_item;
	item->ri_ops = &c2_fop_default_item_ops;
	c2_rpc_packet_add_item(&p_for_encd, item);

	/* ADDB FOP */
	c2_addb_fop_init();
	addb_fop = c2_fop_alloc(&c2_addb_record_fopt, NULL);
	C2_UT_ASSERT(addb_fop != NULL);

	addb_fop_data = c2_fop_data(addb_fop);
	addb_fop_data->ar_data.cmb_count = 10;
	C2_ALLOC_ARR(addb_fop_data->ar_data.cmb_value,
		     addb_fop_data->ar_data.cmb_count);
	fill_addb_header(&addb_fop_data->ar_header);
	fill_addb_data(&addb_fop_data->ar_data);

	item = &addb_fop->f_item;
	item->ri_ops = &c2_fop_default_item_ops;
	c2_rpc_packet_add_item(&p_for_encd, item);

	/* Encode, Decode, Compare */
	C2_UT_ASSERT(!c2_rpc_packet_encode(&p_for_encd, &bufvec));
	bufvec_size = c2_vec_count(&bufvec.ov_vec);

	c2_rpc_packet_init(&p_decoded);
	C2_UT_ASSERT(!c2_rpc_packet_decode(&p_decoded, &bufvec, 0,
					   bufvec_size));
	C2_UT_ASSERT(packet_compare(&p_for_encd, &p_decoded));


	/* Fini business */
	packet_fini(&p_for_encd);
	packet_fini(&p_decoded);
	c2_rpc_packet_fini(&p_for_encd);
	c2_rpc_packet_fini(&p_decoded);

	c2_bufvec_free_aligned(&bufvec, C2_SEG_SHIFT);

	c2_ping_fop_fini();
	c2_addb_fop_fini();
}

const struct c2_test_suite packet_ut = {
	.ts_name = "packet-ut",
	.ts_tests = {
		{ "packet-encode-decode-test", test_packet_encode},
		{ NULL, NULL}
	}
};
C2_EXPORTED(packet_ut);
