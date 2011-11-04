/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
#include <stdio.h>
#include "lib/errno.h"
#include "colibri/init.h"
#include "lib/memory.h"
#include "lib/bitstring.h"
#include "lib/misc.h"
#include "fop/fop.h"
#include "fop/fop_format_def.h"
#include "fop/fop_format.h"
#include "rpc/ut/test_u.h"
#include "rpc/ut/test.ff"
#include "rpc/rpccore.h"
#include "fop/fop_base.h"
#include "rpc/rpc_onwire.h"
#include "xcode/bufvec_xcode.h"
#include "lib/vec.h"
#include "rpc/session_internal.h"
#include "rpc/rpc_base.h"
#include "lib/ut.h"
#include "rpc/rpc_opcodes.h"

extern struct c2_fop_type_format c2_fop_onwire_test_tfmt;
extern struct c2_fop_type_format c2_fop_onwire_test_arr_tfmt;

static struct c2_rpc rpc_obj;

struct c2_fop_type_ops onwire_test_ops = {
	.fto_size_get = c2_xcode_fop_size_get,
};


size_t test_item_size_get(const struct c2_rpc_item *item)
{
	uint64_t	len = 0;
	struct c2_fop	*fop;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	if(fop != NULL)	{
		len = fop->f_type->ft_ops->fto_size_get(fop);
		len += ITEM_ONWIRE_HEADER_SIZE;
	}
		return (size_t)len;
}

const struct c2_rpc_item_type_ops c2_rpc_item_test_ops = {
	.rito_encode = c2_rpc_fop_default_encode,
	.rito_decode = c2_rpc_fop_default_decode,
	.rito_item_size = c2_rpc_item_fop_default_size
};

C2_FOP_TYPE_DECLARE(c2_fop_onwire_test, "onwire test", &onwire_test_ops,
		    C2_RPC_ONWIRE_UT_OPCODE, C2_RPC_ITEM_TYPE_REQUEST,
		    &c2_rpc_item_test_ops);

static struct c2_verno verno = {
	.vn_lsn = 1111,
	.vn_vc = 2222,
};

static struct c2_verno p_no = {
	.vn_lsn = 3333,
	.vn_vc = 4444
};

static struct c2_verno ls_no = {
	.vn_lsn = 55555,
	.vn_vc = 6666
};

void populate_item(struct c2_rpc_item *item)
{
	struct c2_rpc_slot_ref	slot_ref;

	item->ri_slot_refs[0].sr_sender_id = 0xdead;
	item->ri_slot_refs[0].sr_session_id = 0xbeef;
	item->ri_slot_refs[0].sr_uuid.su_uuid = 0xeaeaeaea;
	slot_ref.sr_xid  = 0x11111111;
	slot_ref.sr_slot_gen = 0x22222222;
	slot_ref.sr_slot_id = 0x666;
	memcpy(&slot_ref.sr_verno, &verno, sizeof(struct c2_verno));
	memcpy(&slot_ref.sr_last_persistent_verno, &p_no,
	       sizeof(struct c2_verno));
	memcpy(&slot_ref.sr_last_seen_verno, &ls_no, sizeof(struct c2_verno));
	memcpy(&item->ri_slot_refs[0], &slot_ref,
	       sizeof(struct c2_rpc_slot_ref));
}

void populate_rpc_obj(struct c2_rpc *rpc, struct c2_rpc_item *item)
{

	c2_list_add(&rpc->r_items, &item->ri_rpcobject_linkage);

}

static void rpc_encdec_test(void)
{
	struct c2_fop			*f1, *f2, *f3;
	struct c2_fop_onwire_test		*ccf1, *ccf2, *ccf3;
	int				rc;
	struct c2_rpc_item		*item1, *item2, *item3;
	struct c2_rpc			*obj, obj2;
	struct c2_net_buffer		*nb;
	struct c2_bufvec_cursor		cur;
	void				*cur_addr;

	/* Onwire tests */
	C2_ALLOC_PTR(item1);
	C2_ALLOC_PTR(item2);
	C2_ALLOC_PTR(item3);

	rc = c2_fop_type_format_parse(&c2_fop_onwire_test_arr_tfmt);;
	rc = c2_fop_type_build(&c2_fop_onwire_test_fopt);
	C2_UT_ASSERT(rc == 0);
	f1 = c2_fop_alloc(&c2_fop_onwire_test_fopt, NULL);
	C2_UT_ASSERT(f1 != NULL);
	f2 = c2_fop_alloc(&c2_fop_onwire_test_fopt, NULL);
	C2_UT_ASSERT(f2 != NULL);
	f3 = c2_fop_alloc(&c2_fop_onwire_test_fopt, NULL);
	C2_UT_ASSERT(f3 != NULL);

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

	item1 = &f1->f_item;
	item2 = &f2->f_item;
	item3 = &f3->f_item;
	populate_item(item1);
	populate_item(item2);
	populate_item(item3);

	obj = &rpc_obj;
	c2_list_init(&obj->r_items);

	populate_rpc_obj(obj, item1);
	populate_rpc_obj(obj, item2);
	populate_rpc_obj(obj, item3);

	C2_ALLOC_PTR(nb);
	c2_bufvec_alloc(&nb->nb_buffer, 13, 72);
	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
	cur_addr = c2_bufvec_cursor_addr(&cur);
	C2_UT_ASSERT(C2_IS_8ALIGNED(cur_addr));
	rc =  c2_rpc_encode(obj, nb);
	C2_UT_ASSERT(rc == 0);
	c2_list_init(&obj2.r_items);
	rc = c2_rpc_decode(&obj2, nb);
	C2_UT_ASSERT(rc == 0);
	c2_fop_type_fini(&c2_fop_onwire_test_fopt);
}

const struct c2_test_suite rpc_onwire_ut = {
        .ts_name = "onwire-ut",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "onwire enc/decode", rpc_encdec_test },
                { NULL, NULL }
        }
};

