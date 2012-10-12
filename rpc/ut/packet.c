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
#include "lib/cookie.h"
#include "lib/ut.h"
#include "fop/fop.h"
#include "rpc/packet.h"
#include "rpc/it/ping_fop.h"

#define cmp_item(field) item1->field == item2->field
enum {
	NR = 255,
};
# if 1
static bool item_compare(struct c2_rpc_item *item1, struct c2_rpc_item *item2)
{
	bool rc;
	rc = cmp_item(ri_flags) && cmp_item(ri_magic) &&
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
		cmp_item(ri_slot_refs[0].sr_slot_gen);
	return rc;
}
#endif
#if 1
static bool packet_compare(struct c2_rpc_packet *p1, struct c2_rpc_packet *p2)
{
	struct c2_rpc_item *item1;
#if 1
	struct c2_rpc_item *item2;
	struct c2_rpc_item *temp_item1;
	struct c2_rpc_item *temp_item2;

#endif
	bool		    rc = true;
	if (p1->rp_nr_items != p2->rp_nr_items || p1->rp_size != p2->rp_size ||
			p1->rp_status != p2->rp_status)
		return false;
#if 1
	for (item1 = c2_tlist_head(&packet_item_tl, &p1->rp_items),
	     item2 = c2_tlist_head(&packet_item_tl, &p2->rp_items);
	     item1 != NULL && item2 != NULL &&
	     ((temp_item1 = c2_tlist_next(&packet_item_tl, &p1->rp_items, item1),
	       true)
	      && ((temp_item2 = c2_tlist_next(&packet_item_tl, &p2->rp_items,
				              item2)),
		   true)) && rc; item1 = temp_item1, item2 = temp_item2) {

		rc &= item_compare(item1, item2);
	}
#endif
	return rc;

}
#endif
/* Constraints for formation have not been taken into consideration */
void test_packet_encode()
{
# if 1
	struct c2_fop       *fop;
	struct c2_rpc_item  *item;
	struct c2_rpc_packet p_for_encd;
	struct c2_bufvec     bufvec;
	struct c2_rpc_packet p_decoded;
	c2_bcount_t          bufvec_size;

	/* fop and item gets initialized */
	c2_ping_fop_init();
	fop = c2_fop_alloc(&c2_fop_ping_fopt, NULL);
#if 1
	C2_UT_ASSERT(fop != NULL);
	item = &fop->f_item;
	item->ri_ops = &c2_fop_default_item_ops;
	item->ri_session = NULL;
	item->ri_prio = C2_RPC_ITEM_PRIO_MID;
	item->ri_deadline = 0;
	item->ri_rpc_time = c2_time_now();
#endif

	c2_rpc_packet_init(&p_for_encd);
	c2_rpc_packet_add_item(&p_for_encd, item);
	C2_UT_ASSERT(!c2_bufvec_alloc_aligned(&bufvec, NR,
				                 C2_SEG_SIZE, C2_SEG_SHIFT));
	C2_UT_ASSERT(!c2_rpc_packet_encode(&p_for_encd, &bufvec));
	bufvec_size = c2_vec_count(&bufvec.ov_vec);

#if 1
	c2_rpc_packet_init(&p_decoded);
	C2_UT_ASSERT(!c2_rpc_packet_decode(&p_decoded, &bufvec, 0,
				           bufvec_size));
#endif
	C2_UT_ASSERT(packet_compare(&p_for_encd, &p_decoded));
	c2_bufvec_free_aligned(&bufvec, C2_SEG_SHIFT);
	c2_rpc_packet_remove_all_items(&p_for_encd);
	c2_rpc_packet_fini(&p_for_encd);
	c2_fop_free(fop);
	c2_ping_fop_fini();
	item = c2_tlist_head(&packet_item_tl, &p_decoded.rp_items);
//	item_list = c2_tlist_next(&packet_item_tl, &p_decoded.rp_items, item);
	c2_rpc_packet_remove_all_items(&p_decoded);
	c2_rpc_packet_fini(&p_decoded);
//	c2_rpc_item_fini(item);
#endif
}

const struct c2_test_suite packet_ut = {
	.ts_name = "packet-ut",
	.ts_tests = {
		{ "packet-encode-decode-test", test_packet_encode},
		{ NULL, NULL}
	}
};
C2_EXPORTED(packet_ut);
