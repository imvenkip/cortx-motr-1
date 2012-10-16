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

#define cmp_item(field) item1->field == item2->field
enum {
	NR = 255,
};
static bool item_compare(struct c2_rpc_item *item1, struct c2_rpc_item *item2)
{
	struct c2_fop      *fop;
	struct c2_fop_ping *ping_fop;

	fop = c2_rpc_item_to_fop(item2);
	ping_fop = c2_fop_data(fop);

	return  cmp_item(ri_flags) && cmp_item(ri_magic) &&
		cmp_item(ri_type->rit_magic) && cmp_item(ri_type->rit_flags) &&
		cmp_item(ri_type->rit_opcode) &&
		cmp_item(ri_slot_refs[0].sr_verno.vn_lsn) &&
		cmp_item(ri_slot_refs[0].sr_sender_id) &&
		cmp_item(ri_slot_refs[0].sr_session_id) &&
		cmp_item(ri_slot_refs[0].sr_verno.vn_vc) &&
		cmp_item(ri_slot_refs[0].sr_uuid.su_uuid) &&
		cmp_item(ri_slot_refs[0].sr_last_persistent_verno.vn_lsn) &&
		cmp_item(ri_slot_refs[0].sr_last_persistent_verno.vn_vc) &&
		cmp_item(ri_slot_refs[0].sr_last_seen_verno.vn_lsn) &&
		cmp_item(ri_slot_refs[0].sr_last_seen_verno.vn_vc) &&
		cmp_item(ri_slot_refs[0].sr_slot_id) &&
		cmp_item(ri_slot_refs[0].sr_xid) &&
		cmp_item(ri_slot_refs[0].sr_slot_gen) &&
		ping_fop->fp_arr.f_data[0] == 1000;

}
static bool packet_compare(struct c2_rpc_packet *p1, struct c2_rpc_packet *p2)
{
	struct c2_rpc_item *item1;
	struct c2_rpc_item *item2;
	bool		    rc = true;

	if (p1->rp_nr_items != p2->rp_nr_items || p1->rp_size != p2->rp_size ||
			p1->rp_status != p2->rp_status)
		return false;
	item1 = c2_tlist_head(&packet_item_tl, &p1->rp_items);
	item2 = c2_tlist_head(&packet_item_tl, &p2->rp_items);
	rc &= item_compare(item1, item2);
	return rc;

}
/* Constraints for formation have not been taken into consideration */
void test_packet_encode()
{
	struct c2_fop       *fop_for_encd;
	struct c2_fop       *fop_decoded;
	struct c2_fop_ping  *ping_fop;
	struct c2_rpc_item  *item;
	struct c2_rpc_packet p_for_encd;
	struct c2_bufvec     bufvec;
	struct c2_rpc_packet p_decoded;
	c2_bcount_t          bufvec_size;

	/* All initializations */
	c2_ping_fop_init();
	fop_for_encd = c2_fop_alloc(&c2_fop_ping_fopt, NULL);
	C2_UT_ASSERT(fop_for_encd != NULL);
	C2_ALLOC_ARR(ping_fop->fp_arr.f_data, 1);
	C2_UT_ASSERT(ping_fop->fp_arr.f_data != NULL)
	C2_UT_ASSERT(!c2_bufvec_alloc_aligned(&bufvec, NR,
				C2_SEG_SIZE, C2_SEG_SHIFT));

	ping_fop = c2_fop_data(fop_for_encd);
	ping_fop->fp_arr.f_count = 1;

	item = &fop_for_encd->f_item;
	item->ri_ops = &c2_fop_default_item_ops;

	ping_fop->fp_arr.f_data[0] = 1000;
	c2_rpc_packet_init(&p_for_encd);
	c2_rpc_packet_add_item(&p_for_encd, item);
	C2_UT_ASSERT(!c2_rpc_packet_encode(&p_for_encd, &bufvec));
	bufvec_size = c2_vec_count(&bufvec.ov_vec);

	c2_rpc_packet_init(&p_decoded);
	C2_UT_ASSERT(!c2_rpc_packet_decode(&p_decoded, &bufvec, 0,
				bufvec_size));
	C2_UT_ASSERT(packet_compare(&p_for_encd, &p_decoded));

	item = c2_tlist_head(&packet_item_tl, &p_decoded.rp_items);
	fop_decoded = c2_rpc_item_to_fop(item);
	c2_bufvec_free_aligned(&bufvec, C2_SEG_SHIFT);
	c2_rpc_packet_remove_all_items(&p_for_encd);
	c2_rpc_packet_fini(&p_for_encd);
	c2_rpc_packet_remove_all_items(&p_decoded);
	c2_rpc_item_fini(item);
	c2_rpc_packet_fini(&p_decoded);
	c2_free(ping_fop->fp_arr.f_data);
	ping_fop = c2_fop_data(fop_decoded);
	c2_free(ping_fop->fp_arr.f_data);
	c2_fop_free(fop_decoded);
}

c2_fop_free(fop_for_encd);
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
