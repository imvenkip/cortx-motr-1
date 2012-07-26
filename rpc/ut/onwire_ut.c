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
 * Original author: Subhash Arya <subhash_arya@xyratex.com>
 * Original creation date: 06/25/2011
 */
#include "lib/errno.h"
#include "colibri/init.h"
#include "lib/memory.h"
#include "lib/bitstring.h"
#include "lib/misc.h"
#include "lib/trace.h"
#include "fop/fop.h"
#include "fop/fop_format.h"
#include "rpc/rpc2.h"
#include "fop/fop_base.h"
#include "fop/fop_item_type.h"
#include "rpc/rpc_onwire.h"
#include "xcode/bufvec_xcode.h"
#include "lib/vec.h"
#include "rpc/session_internal.h"
#include "rpc/item.h"
#include "lib/ut.h"
#include "rpc/rpc_opcodes.h"
#include "rpc/ut/onwire_fops.h"

static struct c2_rpc  rpc_obj;
struct c2_rpc_item   *item1;
struct c2_rpc_item   *item2;
struct c2_rpc_item   *item3;

struct c2_fop_type_ops onwire_test_ops = {
	.fto_size_get = c2_xcode_fop_size_get,
};

C2_FOP_TYPE_DECLARE(c2_fop_onwire_test, "onwire test", &onwire_test_ops,
		    C2_RPC_ONWIRE_UT_OPCODE, C2_RPC_ITEM_TYPE_REQUEST);

static struct c2_verno verno = {
	.vn_lsn = 1111,
	.vn_vc  = 2222,
};

static struct c2_verno p_no = {
	.vn_lsn = 3333,
	.vn_vc  = 4444
};

static struct c2_verno ls_no = {
	.vn_lsn = 55555,
	.vn_vc  = 6666
};

void item_populate(struct c2_rpc_item *item)
{
	struct c2_rpc_slot_ref *slot_ref;

	C2_UT_ASSERT(item != NULL);

	item->ri_slot_refs[0].sr_sender_id    = 0xdead;
	item->ri_slot_refs[0].sr_session_id   = 0xbeef;
	item->ri_slot_refs[0].sr_uuid.su_uuid = 0xeaeaeaea;
	slot_ref                              = &item->ri_slot_refs[0];
	slot_ref->sr_xid                      = 0x11111111;
	slot_ref->sr_slot_gen                 = 0x22222222;
	slot_ref->sr_slot_id                  = 0x666;
	slot_ref->sr_verno                    = verno;
	slot_ref->sr_last_persistent_verno    = p_no;
	slot_ref->sr_last_seen_verno          = ls_no;
}

void rpc_obj_populate(struct c2_rpc *rpc, struct c2_rpc_item *item)
{
	C2_UT_ASSERT(rpc != NULL);
	C2_UT_ASSERT(item != NULL);

	c2_list_add(&rpc->r_items, &item->ri_rpcobject_linkage);
}

static void fop_data_free(struct c2_fop *fop)
{
	struct c2_fop_onwire_test *fdata;

	C2_UT_ASSERT(fop != NULL);

	fdata = c2_fop_data(fop);
	c2_free(fdata->t_arr.t_data);
}

static void rpc_obj_free(struct c2_rpc *rpc)
{
	struct c2_rpc_item *item;
	struct c2_rpc_item *next_item;
	struct c2_fop	   *fop;

	C2_UT_ASSERT(rpc != NULL);

	c2_list_for_each_entry_safe(&rpc->r_items, item, next_item,
				    struct c2_rpc_item, ri_rpcobject_linkage) {
		c2_list_del(&item->ri_rpcobject_linkage);
		fop = c2_rpc_item_to_fop(item);
		fop_data_free(fop);
		c2_fop_free(fop);
	}
}

static void test_rpc_encdec(void)
{
	int			   rc;
	struct c2_fop		  *f1;
	struct c2_fop		  *f2;
	struct c2_fop		  *f3;
	struct c2_fop_onwire_test *ccf1;
	struct c2_fop_onwire_test *ccf2;
	struct c2_fop_onwire_test *ccf3;
	struct c2_rpc		  *obj;
	struct c2_rpc		   obj2;
	struct c2_net_buffer	  *nb;
	struct c2_bufvec_cursor	   cur;
	void			  *cur_addr;
	size_t			   allocated;

	allocated = c2_allocated();
	/* Build and allocate test fops. */
	rc = c2_fop_type_build(&c2_fop_onwire_test_fopt);
	C2_UT_ASSERT(rc == 0);

	f1 = c2_fop_alloc(&c2_fop_onwire_test_fopt, NULL);
	C2_UT_ASSERT(f1 != NULL);
	f2 = c2_fop_alloc(&c2_fop_onwire_test_fopt, NULL);
	C2_UT_ASSERT(f2 != NULL);
	f3 = c2_fop_alloc(&c2_fop_onwire_test_fopt, NULL);
	C2_UT_ASSERT(f3 != NULL);

	/* Initialise fop data. */
	ccf1 = c2_fop_data(f1);
	C2_UT_ASSERT(ccf1 != NULL);
	ccf1->t_arr.t_count = 4;
	C2_ALLOC_ARR(ccf1->t_arr.t_data, 4);
	ccf1->t_arr.t_data[0] = 0xa;
	ccf1->t_arr.t_data[1] = 0xb;
	ccf1->t_arr.t_data[2] = 0xc;
	ccf1->t_arr.t_data[3] = 0xd;

	ccf2 = c2_fop_data(f2);
	C2_UT_ASSERT(ccf2 != NULL);
	ccf2->t_arr.t_count = 4;
	C2_ALLOC_ARR(ccf2->t_arr.t_data, 4);
	ccf2->t_arr.t_data[0] = 0xa;
	ccf2->t_arr.t_data[1] = 0xb;
	ccf2->t_arr.t_data[2] = 0xc;
	ccf2->t_arr.t_data[3] = 0xd;

	ccf3 = c2_fop_data(f3);
	C2_UT_ASSERT(ccf3 != NULL);
	ccf3->t_arr.t_count = 4;
	C2_ALLOC_ARR(ccf3->t_arr.t_data, 4);
	ccf3->t_arr.t_data[0] = 0xa;
	ccf3->t_arr.t_data[1] = 0xb;
	ccf3->t_arr.t_data[2] = 0xc;
	ccf3->t_arr.t_data[3] = 0xd;

	/* Initialise rpc items. */
	item1 = &f1->f_item;
	item2 = &f2->f_item;
	item3 = &f3->f_item;
	item_populate(item1);
	item_populate(item2);
	item_populate(item3);

	/* Add items to the rpc object. */
	obj = &rpc_obj;
	c2_list_init(&obj->r_items);

	rpc_obj_populate(obj, item1);
	rpc_obj_populate(obj, item2);
	rpc_obj_populate(obj, item3);

	/* Allocate and initialise network buffer. */
	C2_ALLOC_PTR(nb);
	c2_bufvec_alloc(&nb->nb_buffer, 13, 72);
	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));

	/* Serialize the rpc object onto the network buffer. */
	rc =  c2_rpc_encode(obj, nb);
	C2_UT_ASSERT(rc == 0);

	/* Deserialize the rpc object from the network buffer. */
	c2_list_init(&obj2.r_items);
	rc = c2_rpc_decode(&obj2, nb, nb->nb_length, nb->nb_offset);
	C2_UT_ASSERT(rc == 0);

	/* Free and fini the allocated objects. */
	c2_bufvec_free(&nb->nb_buffer);
	c2_free(nb);
	rpc_obj_free(obj);
	rpc_obj_free(&obj2);
	c2_fop_type_fini(&c2_fop_onwire_test_fopt);
	C2_UT_ASSERT(allocated == c2_allocated());
}

static int onwire_fop_init(void)
{
	c2_xc_onwire_fops_init();
	return 0;
}

static int onwire_fop_fini(void)
{
	c2_xc_onwire_fops_fini();
	return 0;
}

const struct c2_test_suite rpc_onwire_ut = {
        .ts_name = "onwire-ut",
        .ts_init = onwire_fop_init,
        .ts_fini = onwire_fop_fini,
        .ts_tests = {
                { "onwire enc/decode", test_rpc_encdec },
                { NULL, NULL }
        }
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
