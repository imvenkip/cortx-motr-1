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
 * Original author: Nachiket Sahasrabuddhe<nachiket_sahasrabuddhe@xyratex.com>
 * Original creation date: 10/04/2012
 */

#include "colibri/init.h"
#include "lib/vec.h"
#include "lib/memory.h"
#include "lib/ut.h"
#include "fop/fop.h"
#include "rpc/packet.h"
#include "rpc/it/ping_fop.h"
#include "rpc/it/ping_fop_ff.h"
#include "rpc/ut/sample.h"

#define cmp_item(item1, item2, field) item1->field == item2->field

static inline uint32_t fop_opcode(struct c2_fop *fop)
{
return fop->f_type->ft_rec_type.rt_opcode;
}

static bool fop_data_compare(struct c2_fop *fop1,
			     struct c2_fop *fop2)
{
	C2_PRE(fop_opcode(fop1) == fop_opcode(fop2));

		struct c2_fop_ping *ping_data1;
		struct c2_fop_ping *ping_data2;
		struct c2_fop_ping_rep *ping_rep_data1;
		struct c2_fop_ping_rep *ping_rep_data2;
	switch (fop_opcode(fop1)) {

		/* Ping FOPs */
	case C2_RPC_PING_OPCODE:
		ping_data1 = c2_fop_data(fop1);
		ping_data2 = c2_fop_data(fop2);

		return *ping_data1->fp_arr.f_data == *ping_data2->fp_arr.f_data;
	case C2_RPC_PING_REPLY_OPCODE:
		ping_rep_data1 = c2_fop_data(fop1);
		ping_rep_data2 = c2_fop_data(fop2);

		return ping_rep_data1->fpr_rc == ping_rep_data2->fpr_rc;

	default:
		return false;

	}
	return false;
}

static bool item_compare(struct c2_rpc_item *item1, struct c2_rpc_item *item2)
{

	return  cmp_item(item1, item2, ri_flags) &&
		cmp_item(item1, item2, ri_magic) &&
		cmp_item(item1, item2, ri_type->rit_magic) &&
		cmp_item(item1, item2, ri_type->rit_flags) &&
		cmp_item(item1, item2, ri_type->rit_opcode) &&
		cmp_item(item1, item2, ri_slot_refs[0].sr_verno.vn_lsn) &&
		cmp_item(item1, item2, ri_slot_refs[0].sr_sender_id) &&
		cmp_item(item1, item2, ri_slot_refs[0].sr_session_id) &&
		cmp_item(item1, item2, ri_slot_refs[0].sr_verno.vn_vc) &&
		cmp_item(item1, item2, ri_slot_refs[0].sr_uuid.su_uuid) &&
		cmp_item(item1, item2,
			 ri_slot_refs[0].sr_last_persistent_verno.vn_lsn) &&
		cmp_item(item1, item2,
			 ri_slot_refs[0].sr_last_persistent_verno.vn_vc) &&
		cmp_item(item1, item2,
			 ri_slot_refs[0].sr_last_seen_verno.vn_lsn) &&
		cmp_item(item1, item2,
			 ri_slot_refs[0].sr_last_seen_verno.vn_vc) &&
		cmp_item(item1, item2,
			 ri_slot_refs[0].sr_slot_id) &&
		cmp_item(item1, item2,
			 ri_slot_refs[0].sr_xid) &&
		cmp_item(item1, item2, ri_slot_refs[0].sr_slot_gen);

}

static bool packet_compare(struct c2_rpc_packet *p1, struct c2_rpc_packet *p2)
{
	struct c2_rpc_item *item1;
	struct c2_rpc_item *item2;
	struct c2_fop	   *fop1;
	struct c2_fop	   *fop2;

	bool		    rc = true;

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
	struct c2_fop          *ping_fop;
	struct c2_fop          *decoded_ping_fop;
	struct c2_fop	       *ping_fop_rep;
	struct c2_fop	       *decoded_ping_fop_rep;
	struct c2_fop_ping     *ping_fop_data;
	struct c2_fop_ping_rep *ping_fop_rep_data;
	struct c2_rpc_item     *item;
	struct c2_rpc_packet    p_for_encd;
	struct c2_rpc_packet    p_decoded;
	struct c2_bufvec        bufvec;
	c2_bcount_t             bufvec_size;

	/* All initializations */
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

	C2_UT_ASSERT(!c2_rpc_packet_encode(&p_for_encd, &bufvec));
	bufvec_size = c2_vec_count(&bufvec.ov_vec);

	c2_rpc_packet_init(&p_decoded);
	C2_UT_ASSERT(!c2_rpc_packet_decode(&p_decoded, &bufvec, 0,
				bufvec_size));
	C2_UT_ASSERT(packet_compare(&p_for_encd, &p_decoded));


	/* Fini business */
	item = c2_tlist_head(&packet_item_tl, &p_decoded.rp_items);
	decoded_ping_fop = c2_rpc_item_to_fop(item);
	item = packet_item_tlist_next(&p_decoded.rp_items, item);
	decoded_ping_fop_rep = c2_rpc_item_to_fop(item);
	c2_bufvec_free_aligned(&bufvec, C2_SEG_SHIFT);
	c2_rpc_packet_remove_all_items(&p_for_encd);
	c2_rpc_packet_fini(&p_for_encd);
	c2_rpc_packet_remove_all_items(&p_decoded);
	c2_rpc_packet_fini(&p_decoded);
	c2_free(ping_fop_data->fp_arr.f_data);
	ping_fop_data = c2_fop_data(decoded_ping_fop);
	c2_free(ping_fop_data->fp_arr.f_data);
	c2_fop_free(decoded_ping_fop);
	c2_fop_free(decoded_ping_fop_rep);

	c2_fop_free(ping_fop);
	c2_fop_free(ping_fop_rep);
	c2_ping_fop_fini();
}

const struct c2_test_suite packet_ut = {
	.ts_name = "packet-ut",
	.ts_tests = {
		{ "packet-encode-decode-test", test_packet_encode},
		{ NULL, NULL}
	}
};
C2_EXPORTED(packet_ut);
