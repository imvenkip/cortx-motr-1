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
#include "lib/memory.h"		/* M0_ALLOC_ARR */
#include "lib/misc.h"
#include "lib/arith.h"          /* m0_align() */
#include "lib/ut.h"
#include "mero/init.h"
#include "mero/magic.h"
#include "fop/fop.h"
#include "addb/addbff/addb.h"
#include "addb/addbff/addb_xc.h"
#include "rpc/rpc.h"
#include "rpc/rpc_internal.h"
#include "rpc/rpc_opcodes.h"
#include "rpc/it/ping_fop.h"
#include "rpc/it/ping_fop_xc.h"
#include "rpc/it/ping_fop_xc.c"
#include "rpc/it/ping_fom.c"
#include "rpc/it/ping_fop.c"    /* m0_fop_ping_fopt */

#define cmp_field(obj1, obj2, field)((obj1)->field == (obj2)->field)

static struct m0_rpc_item *prepare_ping_fop_item(void);
static struct m0_rpc_item *prepare_ping_rep_fop_item(void);
static struct m0_rpc_item *prepare_addb_fop_item(void);
static void fill_ping_fop_data(struct m0_fop_ping_arr *fp_arr);
static void fill_addb_header(struct m0_addb_record_header *header);
static void fill_addb_data(struct m0_mem_buf *addb_data);
static void populate_item(struct m0_rpc_item *item);
static void packet_compare(struct m0_rpc_packet *p1, struct m0_rpc_packet *p2);
static void item_compare(struct m0_rpc_item *item1, struct m0_rpc_item *item2);
static void fop_data_compare(struct m0_fop *fop1, struct m0_fop *fop2);
static void cmp_ping_fop_data(struct m0_fop_ping_arr *fp_arr1,
			      struct m0_fop_ping_arr *fp_arr2);
static void cmp_addb_record_buf(struct m0_mem_buf *buf1,
				struct m0_mem_buf *buf2);
static void packet_fini(struct m0_rpc_packet *packet);

static int packet_encdec_ut_init(void)
{
	m0_ping_fop_init();
	m0_addb_fop_init();
	return 0;
}

static int packet_encdec_ut_fini(void)
{
	m0_ping_fop_fini();
	m0_addb_fop_fini();
	return 0;
}

static void test_packet_encode_decode(void)
{
	struct m0_rpc_item  *item;
	struct m0_rpc_packet packet;
	struct m0_rpc_packet decoded_packet;
	struct m0_bufvec     bufvec;
	m0_bcount_t          bufvec_size;
	int		     rc;

	m0_rpc_packet_init(&packet);

	item = prepare_ping_fop_item();
	m0_rpc_packet_add_item(&packet, item);
	m0_rpc_item_put(item);
	item = prepare_ping_rep_fop_item();
	m0_rpc_packet_add_item(&packet, item);
	m0_rpc_item_put(item);
	item = prepare_addb_fop_item();
	m0_rpc_packet_add_item(&packet, item);
	m0_rpc_item_put(item);
	bufvec_size = m0_align(packet.rp_size, 8);
	/* m0_alloc_aligned() (lib/linux_kernel/memory.c), supports
	 * alignment of PAGE_SHIFT only
	 */
	rc = m0_bufvec_alloc_aligned(&bufvec, 1, bufvec_size, M0_SEG_SHIFT);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rpc_packet_encode(&packet, &bufvec);
	M0_UT_ASSERT(rc == 0);
	m0_rpc_packet_init(&decoded_packet);
	rc = m0_rpc_packet_decode(&decoded_packet, &bufvec, 0, bufvec_size);
	M0_UT_ASSERT(rc == 0);
	packet_compare(&packet, &decoded_packet);
	packet_fini(&packet);
	packet_fini(&decoded_packet);
	m0_bufvec_free_aligned(&bufvec, M0_SEG_SHIFT);
}

static struct m0_rpc_item* prepare_ping_fop_item(void)
{
	struct m0_fop      *ping_fop;
	struct m0_fop_ping *ping_fop_data;
	struct m0_rpc_item *item;

	ping_fop = m0_fop_alloc(&m0_fop_ping_fopt, NULL);
	M0_UT_ASSERT(ping_fop != NULL);
	ping_fop_data = m0_fop_data(ping_fop);
	ping_fop_data->fp_arr.f_count = 1;
	M0_ALLOC_ARR(ping_fop_data->fp_arr.f_data,
		     ping_fop_data->fp_arr.f_count);
	M0_UT_ASSERT(ping_fop_data->fp_arr.f_data != NULL);
	fill_ping_fop_data(&ping_fop_data->fp_arr);
	item = &ping_fop->f_item;
	populate_item(item);
	return item;
}

static struct m0_rpc_item* prepare_ping_rep_fop_item(void)
{
	struct m0_fop	       *ping_fop_rep;
	struct m0_fop_ping_rep *ping_fop_rep_data;
	struct m0_rpc_item     *item;

	ping_fop_rep = m0_fop_alloc(&m0_fop_ping_rep_fopt, NULL);
	M0_UT_ASSERT(ping_fop_rep != NULL);
	ping_fop_rep_data = m0_fop_data(ping_fop_rep);
	ping_fop_rep_data->fpr_rc = 1001;
	item = &ping_fop_rep->f_item;
	populate_item(item);
	return item;
}

static struct m0_rpc_item* prepare_addb_fop_item(void)
{
	struct m0_fop         *addb_fop;
	struct m0_addb_record *addb_fop_data;
	struct m0_rpc_item    *item;

	addb_fop = m0_fop_alloc(&m0_addb_record_fopt, NULL);
	M0_UT_ASSERT(addb_fop != NULL);

	addb_fop_data = m0_fop_data(addb_fop);
	addb_fop_data->ar_data.cmb_count = 10;
	M0_ALLOC_ARR(addb_fop_data->ar_data.cmb_value,
		     addb_fop_data->ar_data.cmb_count);
	fill_addb_header(&addb_fop_data->ar_header);
	fill_addb_data(&addb_fop_data->ar_data);
	item = &addb_fop->f_item;
	populate_item(item);
	return item;
}

static void fill_ping_fop_data(struct m0_fop_ping_arr *fp_arr)
{
	int i;

	M0_UT_ASSERT(fp_arr->f_count != 0);
	M0_UT_ASSERT(fp_arr->f_data != NULL);

	for (i = 0; i < fp_arr->f_count; ++i) {
		fp_arr->f_data[i] = i % UINT64_MAX;
	}
}

static void fill_addb_header(struct m0_addb_record_header *header)
{
	*header = (struct m0_addb_record_header) {
		 .arh_magic1    = M0_ADDB_REC_HEADER_MAGIC1,
		 .arh_version   = ADDB_REC_HEADER_VERSION,
		 .arh_len       = 0,
		 .arh_event_id  = 1234,
		 .arh_timestamp = m0_time_now(),
		 .arh_magic2    = M0_ADDB_REC_HEADER_MAGIC2,
	};
}

static void fill_addb_data(struct m0_mem_buf *addb_data)
{
	int i;

	M0_UT_ASSERT(addb_data->cmb_count != 0);
	M0_UT_ASSERT(addb_data->cmb_value != NULL);

	for (i = 0; i < addb_data->cmb_count; ++i) {
		addb_data->cmb_value[i] = i % UINT8_MAX;
	}
}

static void populate_item(struct m0_rpc_item *item)
{
	item->ri_slot_refs[0].sr_ow = (struct m0_rpc_onwire_slot_ref) {
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

static void packet_compare(struct m0_rpc_packet *p1, struct m0_rpc_packet *p2)
{
	struct m0_rpc_item *item1;
	struct m0_rpc_item *item2;
	struct m0_fop      *fop1;
	struct m0_fop      *fop2;

	M0_UT_ASSERT(cmp_field(p1, p2, rp_size));
	M0_UT_ASSERT(memcmp(&p1->rp_ow, &p2->rp_ow, sizeof p1->rp_ow) == 0);

	for (item1 = m0_tlist_head(&packet_item_tl, &p1->rp_items),
	     item2 = m0_tlist_head(&packet_item_tl, &p2->rp_items);
	     item1 != NULL && item2 != NULL;
	     item1 = packet_item_tlist_next(&p1->rp_items, item1),
	     item2 = packet_item_tlist_next(&p2->rp_items, item2)) {
		item_compare(item1, item2);
		fop1 = m0_rpc_item_to_fop(item1);
		fop2 = m0_rpc_item_to_fop(item2);
		fop_data_compare(fop1, fop2);
	}
}

static void item_compare(struct m0_rpc_item *item1, struct m0_rpc_item *item2)
{

	struct m0_rpc_onwire_slot_ref *sr_ow1 = &item1->ri_slot_refs[0].sr_ow;
	struct m0_rpc_onwire_slot_ref *sr_ow2 = &item2->ri_slot_refs[0].sr_ow;

	M0_UT_ASSERT(cmp_field(item1, item2, ri_type->rit_opcode));
	M0_UT_ASSERT(memcmp(sr_ow1, sr_ow2, sizeof *sr_ow1) == 0);
}

static void fop_data_compare(struct m0_fop *fop1, struct m0_fop *fop2)
{
	struct m0_fop_ping     *ping_data1;
	struct m0_fop_ping     *ping_data2;
	struct m0_fop_ping_rep *ping_rep_data1;
	struct m0_fop_ping_rep *ping_rep_data2;
	struct m0_addb_record  *addb_data1;
	struct m0_addb_record  *addb_data2;

	M0_UT_ASSERT(m0_fop_opcode(fop1) == m0_fop_opcode(fop2));

	switch (m0_fop_opcode(fop1)) {

	case M0_RPC_PING_OPCODE:
		ping_data1 = m0_fop_data(fop1);
		ping_data2 = m0_fop_data(fop2);
		cmp_ping_fop_data(&ping_data1->fp_arr, &ping_data2->fp_arr);
		break;

	case M0_RPC_PING_REPLY_OPCODE:
		ping_rep_data1 = m0_fop_data(fop1);
		ping_rep_data2 = m0_fop_data(fop2);

		M0_UT_ASSERT(ping_rep_data1->fpr_rc == ping_rep_data2->fpr_rc);
		break;

	case M0_ADDB_RECORD_REQUEST_OPCODE:
		addb_data1 = m0_fop_data(fop1);
		addb_data2 = m0_fop_data(fop2);
		M0_UT_ASSERT(memcmp(&addb_data1->ar_header,
			     &addb_data2->ar_header,
			     sizeof addb_data2->ar_header) == 0);
		cmp_addb_record_buf(&addb_data1->ar_data, &addb_data2->ar_data);
	}
}

static void cmp_ping_fop_data(struct m0_fop_ping_arr *fp_arr1,
			      struct m0_fop_ping_arr *fp_arr2)
{
	M0_UT_ASSERT(cmp_field(fp_arr1, fp_arr2, f_count));
	M0_UT_ASSERT(fp_arr1->f_data != NULL);
	M0_UT_ASSERT(fp_arr2->f_data != NULL);
	M0_UT_ASSERT(m0_forall(i, fp_arr1->f_count,
		               fp_arr1->f_data[i] == fp_arr2->f_data[i]));
}

static void cmp_addb_record_buf(struct m0_mem_buf *buf1,
				struct m0_mem_buf *buf2)
{
	M0_UT_ASSERT(buf1->cmb_count == buf2->cmb_count);
	M0_UT_ASSERT(buf1->cmb_value != NULL);
	M0_UT_ASSERT(buf2->cmb_value != NULL);
	M0_UT_ASSERT(m0_forall(i, buf1->cmb_count,
		               buf1->cmb_value[i] == buf2->cmb_value[i]));
}

static void packet_fini(struct m0_rpc_packet *packet)
{
	M0_UT_ASSERT(packet != NULL);

	/* items will be freed as soon as they're removed
	   from packet, because this is last reference on them */
	m0_rpc_packet_remove_all_items(packet);
	m0_rpc_packet_fini(packet);
}

const struct m0_test_suite packet_encdec_ut = {
	.ts_name = "packet-encdec-ut",
	.ts_init = packet_encdec_ut_init,
	.ts_fini = packet_encdec_ut_fini,
	.ts_tests = {
		{ "packet-encode-decode-test", test_packet_encode_decode},
		{ NULL, NULL}
	}
};
M0_EXPORTED(packet_encdec_ut);
