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
#include "rpc/it/ping_fop.c"
#include "rpc/it/ping_fop_ff.c"
#include "rpc/it/ping_fom.c"

#define cmp_field(obj1, obj2, field)((obj1)->field == (obj2)->field)

enum {
	NR = 255
};

static int packet_ut_init(void)
{
	c2_ping_fop_init();
	c2_addb_fop_init();
	return 0;
}

static int packet_ut_fini(void)
{
	c2_ping_fop_fini();
	c2_addb_fop_fini();
	return 0;
}

static void packet_fini(struct c2_rpc_packet *packet)
{
	struct c2_rpc_item *item;
	struct c2_fop      *fop;

	C2_UT_ASSERT(packet != NULL);

	for_each_item_in_packet(item, packet) {
		fop = c2_rpc_item_to_fop(item);
		c2_rpc_packet_remove_item(packet, item);
		c2_fop_free(fop);
	} end_for_each_item_in_packet;
}


static void cmp_addb_record_header(struct c2_addb_record_header *header1,
				   struct c2_addb_record_header *header2)
{
	C2_UT_ASSERT(cmp_field(header1, header2, arh_magic1));
	C2_UT_ASSERT(cmp_field(header1, header2, arh_version));
	C2_UT_ASSERT(cmp_field(header1, header2, arh_len));
	C2_UT_ASSERT(cmp_field(header1, header2, arh_event_id));
	C2_UT_ASSERT(cmp_field(header1, header2, arh_timestamp));
	C2_UT_ASSERT(cmp_field(header1, header2, arh_magic2));
}

static void cmp_addb_record_buf(struct c2_mem_buf *buf1,
				struct c2_mem_buf *buf2)
{
	bool rc;

	C2_UT_ASSERT(buf1->cmb_count == buf2->cmb_count);
	C2_UT_ASSERT(buf1->cmb_value != NULL);
	C2_UT_ASSERT(buf2->cmb_value != NULL);

	rc = c2_forall(i, buf1->cmb_count,
		       buf1->cmb_value[i] == buf2->cmb_value[i]);
	C2_UT_ASSERT(rc);
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
		addb_data->cmb_value[i] = i % UINT8_MAX;
	}
}

static void fop_data_compare(struct c2_fop *fop1, struct c2_fop *fop2)
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

		C2_UT_ASSERT(*ping_data1->fp_arr.f_data ==
			     *ping_data2->fp_arr.f_data);
		break;

	case C2_RPC_PING_REPLY_OPCODE:
		ping_rep_data1 = c2_fop_data(fop1);
		ping_rep_data2 = c2_fop_data(fop2);

		C2_UT_ASSERT(ping_rep_data1->fpr_rc == ping_rep_data2->fpr_rc);
		break;

	case C2_ADDB_RECORD_REQUEST_OPCODE:
		addb_data1 = c2_fop_data(fop1);
		addb_data2 = c2_fop_data(fop2);
		cmp_addb_record_header(&addb_data1->ar_header,
				       &addb_data2->ar_header);
		cmp_addb_record_buf(&addb_data1->ar_data, &addb_data2->ar_data);
	}
}

static void item_compare(struct c2_rpc_item *item1, struct c2_rpc_item *item2)
{

	struct c2_rpc_onwire_slot_ref *sr_ow1 = &item1->ri_slot_refs[0].sr_ow;
	struct c2_rpc_onwire_slot_ref *sr_ow2 = &item2->ri_slot_refs[0].sr_ow;

	C2_UT_ASSERT(cmp_field(item1, item2, ri_type->rit_opcode));
	C2_UT_ASSERT(cmp_field(sr_ow1, sr_ow2, osr_verno.vn_lsn));
	C2_UT_ASSERT(cmp_field(sr_ow1, sr_ow2, osr_sender_id));
	C2_UT_ASSERT(cmp_field(sr_ow1, sr_ow2, osr_session_id));
	C2_UT_ASSERT(cmp_field(sr_ow1, sr_ow2, osr_verno.vn_vc));
	C2_UT_ASSERT(cmp_field(sr_ow1, sr_ow2, osr_uuid.su_uuid));
	C2_UT_ASSERT(cmp_field(sr_ow1, sr_ow2,
			       osr_last_persistent_verno.vn_lsn));
	C2_UT_ASSERT(cmp_field(sr_ow1, sr_ow2,
			       osr_last_persistent_verno.vn_vc));
	C2_UT_ASSERT(cmp_field(sr_ow1, sr_ow2, osr_last_seen_verno.vn_lsn));
	C2_UT_ASSERT(cmp_field(sr_ow1, sr_ow2, osr_last_seen_verno.vn_vc));
	C2_UT_ASSERT(cmp_field(sr_ow1, sr_ow2, osr_slot_id));
	C2_UT_ASSERT(cmp_field(sr_ow1, sr_ow2, osr_xid));
	C2_UT_ASSERT(cmp_field(sr_ow1, sr_ow2, osr_slot_gen));
}

static void packet_compare(struct c2_rpc_packet *p1, struct c2_rpc_packet *p2)
{
	struct c2_rpc_item *item1;
	struct c2_rpc_item *item2;
	struct c2_fop      *fop1;
	struct c2_fop      *fop2;

	C2_UT_ASSERT(p1->rp_size == p2->rp_size);
	C2_UT_ASSERT(p1->rp_status == p2->rp_status);

	for (item1 = c2_tlist_head(&packet_item_tl, &p1->rp_items),
	     item2 = c2_tlist_head(&packet_item_tl, &p2->rp_items);
	     item1 != NULL && item2 != NULL;
	     item1 = packet_item_tlist_next(&p1->rp_items, item1),
	     item2 = packet_item_tlist_next(&p2->rp_items, item2)) {
		item_compare(item1, item2);
		fop1 = c2_rpc_item_to_fop(item1);
		fop2 = c2_rpc_item_to_fop(item2);
		fop_data_compare(fop1, fop2);
	}
}
static void populate_item(struct c2_rpc_item *item)
{
	item->ri_ops = &c2_fop_default_item_ops;
	item->ri_slot_refs[0].sr_ow.osr_uuid.su_uuid = 9876;
	item->ri_slot_refs[0].sr_ow.osr_sender_id = 101;
	item->ri_slot_refs[0].sr_ow.osr_session_id = 523;
	item->ri_slot_refs[0].sr_ow.osr_slot_id = 23;
	item->ri_slot_refs[0].sr_ow.osr_verno.vn_lsn = 7654;
	item->ri_slot_refs[0].sr_ow.osr_verno.vn_vc = 12345;
	item->ri_slot_refs[0].sr_ow.osr_last_persistent_verno.vn_lsn = 4356;
	item->ri_slot_refs[0].sr_ow.osr_last_persistent_verno.vn_vc = 2345;
	item->ri_slot_refs[0].sr_ow.osr_last_seen_verno.vn_lsn = 1456;
	item->ri_slot_refs[0].sr_ow.osr_last_seen_verno.vn_vc = 7865;
	item->ri_slot_refs[0].sr_ow.osr_xid = 212;
	item->ri_slot_refs[0].sr_ow.osr_slot_gen = 321;
}

static void prepare_ping_fop_item(struct c2_rpc_item **item,
				  struct c2_fop **ping_fop)
{
	struct c2_fop_ping *ping_fop_data;

	*ping_fop = c2_fop_alloc(&c2_fop_ping_fopt, NULL);
	C2_UT_ASSERT(*ping_fop != NULL);
	ping_fop_data = c2_fop_data(*ping_fop);
	C2_ALLOC_ARR(ping_fop_data->fp_arr.f_data, 1);
	C2_UT_ASSERT(ping_fop_data->fp_arr.f_data != NULL);
	ping_fop_data->fp_arr.f_count = 2;
	ping_fop_data->fp_arr.f_data[0] = 1000;
	item[0] = &ping_fop[0]->f_item;
	populate_item(item[0]);
}

static void prepare_ping_rep_fop_item(struct c2_rpc_item **item,
				      struct c2_fop **ping_fop_rep)
{
	struct c2_fop_ping_rep *ping_fop_rep_data;

	*ping_fop_rep = c2_fop_alloc(&c2_fop_ping_rep_fopt, NULL);
	C2_UT_ASSERT(*ping_fop_rep != NULL);
	ping_fop_rep_data = c2_fop_data(*ping_fop_rep);
	ping_fop_rep_data->fpr_rc = 1001;
	item[0] = &ping_fop_rep[0]->f_item;
	item[0]->ri_ops = &c2_fop_default_item_ops;
	populate_item(item[0]);
}

static void prepare_addb_fop_item(struct c2_rpc_item **item,
				  struct c2_fop **addb_fop)
{
	struct c2_addb_record *addb_fop_data;

	*addb_fop = c2_fop_alloc(&c2_addb_record_fopt, NULL);
	C2_UT_ASSERT(*addb_fop != NULL);

	addb_fop_data = c2_fop_data(*addb_fop);
	addb_fop_data->ar_data.cmb_count = 10;
	C2_ALLOC_ARR(addb_fop_data->ar_data.cmb_value,
		     addb_fop_data->ar_data.cmb_count);
	fill_addb_header(&addb_fop_data->ar_header);
	fill_addb_data(&addb_fop_data->ar_data);
	item[0] = &addb_fop[0]->f_item;
	item[0]->ri_ops = &c2_fop_default_item_ops;
	populate_item(item[0]);
}

void test_packet_encode(void)
{
	/* Ping FOP and Ping reply FOP */
	struct c2_fop         *ping_fop;
	struct c2_fop         *ping_fop_rep;
	/* ADDB record request */
	struct c2_fop	      *addb_fop;
	struct c2_addb_record *addb_fop_data;
	/* RPC objects */
	struct c2_rpc_item    *item;
	struct c2_rpc_packet   p_for_encd;
	struct c2_rpc_packet   p_decoded;
	struct c2_bufvec       bufvec;
	c2_bcount_t            bufvec_size;
	int		       rc;

	rc = c2_bufvec_alloc_aligned(&bufvec, NR, C2_SEG_SIZE, C2_SEG_SHIFT);
	C2_UT_ASSERT(rc == 0);
	c2_rpc_packet_init(&p_for_encd);

	/* Ping FOP and Ping-reply FOP */
	prepare_ping_fop_item(&item, &ping_fop);
	c2_rpc_packet_add_item(&p_for_encd, item);

	prepare_ping_rep_fop_item(&item, &ping_fop_rep);
	c2_rpc_packet_add_item(&p_for_encd, item);
	/* ADDB FOP */
	prepare_addb_fop_item(&item, &addb_fop);
	addb_fop_data = c2_fop_data(addb_fop);
	c2_rpc_packet_add_item(&p_for_encd, item);
	/* Encode, Decode, Compare */
	rc = c2_rpc_packet_encode(&p_for_encd, &bufvec);
	C2_UT_ASSERT(rc == 0);
	bufvec_size = c2_vec_count(&bufvec.ov_vec);

	c2_rpc_packet_init(&p_decoded);
	rc = c2_rpc_packet_decode(&p_decoded, &bufvec, 0, bufvec_size);
	C2_UT_ASSERT(rc == 0);
	packet_compare(&p_for_encd, &p_decoded);
	/* Fini business */
	packet_fini(&p_for_encd);
	packet_fini(&p_decoded);
	c2_rpc_packet_fini(&p_for_encd);
	c2_rpc_packet_fini(&p_decoded);
	c2_bufvec_free_aligned(&bufvec, C2_SEG_SHIFT);
}

const struct c2_test_suite packet_ut = {
	.ts_name = "packet-ut",
	.ts_init = packet_ut_init,
	.ts_fini = packet_ut_fini,
	.ts_tests = {
		{ "packet-encode-decode-test", test_packet_encode},
		{ NULL, NULL}
	}
};
C2_EXPORTED(packet_ut);
