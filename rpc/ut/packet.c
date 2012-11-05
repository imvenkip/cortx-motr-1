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
#include "lib/vec.h"
#include "lib/memory.h"		/* C2_ALLOC_ARR */
#include "lib/misc.h"
#include "lib/arith.h"          /* c2_align() */
#include "lib/ut.h"
#include "colibri/init.h"
#include "colibri/magic.h"
#include "fop/fop.h"
#include "addb/addbff/addb_ff.h"
#include "rpc/packet.h"
#include "rpc/rpc_opcodes.h"
#include "rpc/it/ping_fop.h"
#include "rpc/it/ping_fop_ff.h"
#include "rpc/it/ping_fop_ff.c"
#include "rpc/it/ping_fom.c"
#include "rpc/it/ping_fop.c"    /* c2_fop_ping_fopt */

#define cmp_field(obj1, obj2, field)((obj1)->field == (obj2)->field)

static struct c2_rpc_item *prepare_ping_fop_item(void);
static struct c2_rpc_item *prepare_ping_rep_fop_item(void);
static struct c2_rpc_item *prepare_addb_fop_item(void);
static void fill_ping_fop_data(struct c2_fop_ping_arr *fp_arr);
static void fill_addb_header(struct c2_addb_record_header *header);
static void fill_addb_data(struct c2_mem_buf *addb_data);
static void populate_item(struct c2_rpc_item *item);
static void packet_compare(struct c2_rpc_packet *p1, struct c2_rpc_packet *p2);
static void item_compare(struct c2_rpc_item *item1, struct c2_rpc_item *item2);
static void fop_data_compare(struct c2_fop *fop1, struct c2_fop *fop2);
static void cmp_ping_fop_data(struct c2_fop_ping_arr *fp_arr1,
			      struct c2_fop_ping_arr *fp_arr2);
static void cmp_addb_record_buf(struct c2_mem_buf *buf1,
				struct c2_mem_buf *buf2);
static void packet_fini(struct c2_rpc_packet *packet);

static int packet_encdec_ut_init(void)
{
	c2_ping_fop_init();
	c2_addb_fop_init();
	return 0;
}

static int packet_encdec_ut_fini(void)
{
	c2_ping_fop_fini();
	c2_addb_fop_fini();
	return 0;
}

static void test_packet_encode_decode(void)
{
	struct c2_rpc_item  *item;
	struct c2_rpc_packet packet;
	struct c2_rpc_packet decoded_packet;
	struct c2_bufvec     bufvec;
	c2_bcount_t          bufvec_size;
	int		     rc;

	c2_rpc_packet_init(&packet);

	item = prepare_ping_fop_item();
	c2_rpc_packet_add_item(&packet, item);
	item = prepare_ping_rep_fop_item();
	c2_rpc_packet_add_item(&packet, item);
	item = prepare_addb_fop_item();
	c2_rpc_packet_add_item(&packet, item);
	bufvec_size = c2_align(packet.rp_size, 8);
	/* c2_alloc_aligned() (lib/linux_kernel/memory.c), supports
	 * alignment of PAGE_SHIFT only
	 */
	rc = c2_bufvec_alloc_aligned(&bufvec, 1, bufvec_size, C2_SEG_SHIFT);
	C2_UT_ASSERT(rc == 0);
	rc = c2_rpc_packet_encode(&packet, &bufvec);
	C2_UT_ASSERT(rc == 0);
	c2_rpc_packet_init(&decoded_packet);
	rc = c2_rpc_packet_decode(&decoded_packet, &bufvec, 0, bufvec_size);
	C2_UT_ASSERT(rc == 0);
	packet_compare(&packet, &decoded_packet);
	packet_fini(&packet);
	packet_fini(&decoded_packet);
	c2_bufvec_free_aligned(&bufvec, C2_SEG_SHIFT);
}

static struct c2_rpc_item* prepare_ping_fop_item(void)
{
	struct c2_fop      *ping_fop;
	struct c2_fop_ping *ping_fop_data;
	struct c2_rpc_item *item;

	ping_fop = c2_fop_alloc(&c2_fop_ping_fopt, NULL);
	C2_UT_ASSERT(ping_fop != NULL);
	ping_fop_data = c2_fop_data(ping_fop);
	ping_fop_data->fp_arr.f_count = 1;
	C2_ALLOC_ARR(ping_fop_data->fp_arr.f_data,
		     ping_fop_data->fp_arr.f_count);
	C2_UT_ASSERT(ping_fop_data->fp_arr.f_data != NULL);
	fill_ping_fop_data(&ping_fop_data->fp_arr);
	item = &ping_fop->f_item;
	populate_item(item);
	return item;
}

static struct c2_rpc_item* prepare_ping_rep_fop_item(void)
{
	struct c2_fop	       *ping_fop_rep;
	struct c2_fop_ping_rep *ping_fop_rep_data;
	struct c2_rpc_item     *item;

	ping_fop_rep = c2_fop_alloc(&c2_fop_ping_rep_fopt, NULL);
	C2_UT_ASSERT(ping_fop_rep != NULL);
	ping_fop_rep_data = c2_fop_data(ping_fop_rep);
	ping_fop_rep_data->fpr_rc = 1001;
	item = &ping_fop_rep->f_item;
	populate_item(item);
	return item;
}

static struct c2_rpc_item* prepare_addb_fop_item(void)
{
	struct c2_fop         *addb_fop;
	struct c2_addb_record *addb_fop_data;
	struct c2_rpc_item    *item;

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
	populate_item(item);
	return item;
}

static void fill_ping_fop_data(struct c2_fop_ping_arr *fp_arr)
{
	int i;

	C2_UT_ASSERT(fp_arr->f_count != 0);
	C2_UT_ASSERT(fp_arr->f_data != NULL);

	for (i = 0; i < fp_arr->f_count; ++i) {
		fp_arr->f_data[i] = i % UINT64_MAX;
	}
}

static void fill_addb_header(struct c2_addb_record_header *header)
{
	*header = (struct c2_addb_record_header) {
		 .arh_magic1    = C2_ADDB_REC_HEADER_MAGIC1,
		 .arh_version   = ADDB_REC_HEADER_VERSION,
		 .arh_len       = 0,
		 .arh_event_id  = 1234,
		 .arh_timestamp = c2_time_now(),
		 .arh_magic2    = C2_ADDB_REC_HEADER_MAGIC2,
	};
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

static void populate_item(struct c2_rpc_item *item)
{
	item->ri_slot_refs[0].sr_ow = (struct c2_rpc_onwire_slot_ref) {
		.osr_uuid.su_uuid = 9876,
		.osr_sender_id = 101,
		.osr_session_id = 523,
		.osr_slot_id = 23,
		.osr_verno = {
			.vn_lsn = 7654,
			.vn_vc = 12345,
		},
		.osr_last_persistent_verno = {
			.vn_lsn = 4356,
			.vn_vc = 2345,
		},
		.osr_last_seen_verno = {
			.vn_lsn = 1456,
			.vn_vc = 7865,
		},
		.osr_xid = 212,
		.osr_slot_gen = 321,
	};
}

static void packet_compare(struct c2_rpc_packet *p1, struct c2_rpc_packet *p2)
{
	struct c2_rpc_item *item1;
	struct c2_rpc_item *item2;
	struct c2_fop      *fop1;
	struct c2_fop      *fop2;

	C2_UT_ASSERT(cmp_field(p1, p2, rp_size));
	C2_UT_ASSERT(memcmp(&p1->rp_ow, &p2->rp_ow, sizeof p1->rp_ow) == 0);

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

static void item_compare(struct c2_rpc_item *item1, struct c2_rpc_item *item2)
{

	struct c2_rpc_onwire_slot_ref *sr_ow1 = &item1->ri_slot_refs[0].sr_ow;
	struct c2_rpc_onwire_slot_ref *sr_ow2 = &item2->ri_slot_refs[0].sr_ow;

	C2_UT_ASSERT(cmp_field(item1, item2, ri_type->rit_opcode));
	C2_UT_ASSERT(memcmp(sr_ow1, sr_ow2, sizeof *sr_ow1) == 0);
}

static void fop_data_compare(struct c2_fop *fop1, struct c2_fop *fop2)
{
	struct c2_fop_ping     *ping_data1;
	struct c2_fop_ping     *ping_data2;
	struct c2_fop_ping_rep *ping_rep_data1;
	struct c2_fop_ping_rep *ping_rep_data2;
	struct c2_addb_record  *addb_data1;
	struct c2_addb_record  *addb_data2;

	C2_UT_ASSERT(c2_fop_opcode(fop1) == c2_fop_opcode(fop2));

	switch (c2_fop_opcode(fop1)) {

	case C2_RPC_PING_OPCODE:
		ping_data1 = c2_fop_data(fop1);
		ping_data2 = c2_fop_data(fop2);
		cmp_ping_fop_data(&ping_data1->fp_arr, &ping_data2->fp_arr);
		break;

	case C2_RPC_PING_REPLY_OPCODE:
		ping_rep_data1 = c2_fop_data(fop1);
		ping_rep_data2 = c2_fop_data(fop2);

		C2_UT_ASSERT(ping_rep_data1->fpr_rc == ping_rep_data2->fpr_rc);
		break;

	case C2_ADDB_RECORD_REQUEST_OPCODE:
		addb_data1 = c2_fop_data(fop1);
		addb_data2 = c2_fop_data(fop2);
		C2_UT_ASSERT(memcmp(&addb_data1->ar_header,
			     &addb_data2->ar_header,
			     sizeof addb_data2->ar_header) == 0);
		cmp_addb_record_buf(&addb_data1->ar_data, &addb_data2->ar_data);
	}
}

static void cmp_ping_fop_data(struct c2_fop_ping_arr *fp_arr1,
			      struct c2_fop_ping_arr *fp_arr2)
{
	C2_UT_ASSERT(cmp_field(fp_arr1, fp_arr2, f_count));
	C2_UT_ASSERT(fp_arr1->f_data != NULL);
	C2_UT_ASSERT(fp_arr2->f_data != NULL);
	C2_UT_ASSERT(c2_forall(i, fp_arr1->f_count,
		               fp_arr1->f_data[i] == fp_arr2->f_data[i]));
}

static void cmp_addb_record_buf(struct c2_mem_buf *buf1,
				struct c2_mem_buf *buf2)
{
	C2_UT_ASSERT(buf1->cmb_count == buf2->cmb_count);
	C2_UT_ASSERT(buf1->cmb_value != NULL);
	C2_UT_ASSERT(buf2->cmb_value != NULL);
	C2_UT_ASSERT(c2_forall(i, buf1->cmb_count,
		               buf1->cmb_value[i] == buf2->cmb_value[i]));
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
	c2_rpc_packet_fini(packet);
}

const struct c2_test_suite packet_encdec_ut = {
	.ts_name = "packet-encdec-ut",
	.ts_init = packet_encdec_ut_init,
	.ts_fini = packet_encdec_ut_fini,
	.ts_tests = {
		{ "packet-encode-decode-test", test_packet_encode_decode},
		{ NULL, NULL}
	}
};
C2_EXPORTED(packet_encdec_ut);
