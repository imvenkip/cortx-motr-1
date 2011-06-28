#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <rpc/xdr.h>
#include "colibri/init.h"
#include "lib/memory.h"
#include "lib/bitstring.h"
#include "lib/misc.h"
#include "db/db.h"
#include "cob/cob.h"
#include "fop/fop.h"
#include "fop/fop_format_def.h"
#include "fop/fop_format.h"
#include "net/usunrpc/usunrpc.h"
#include "rpc/ut/test_u.h"
#include "rpc/ut/test.ff"
#include "rpc/rpccore.h"
#include "fop/fop_base.h"
#include "rpc/rpc_onwire.h"
#include "rpc/session_int.h"

extern struct c2_fop_type_format c2_fop_test_tfmt;

static struct c2_rpc rpc_obj;

int test_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	printf("Called test_handler\n");
	return 0;
}

struct c2_fop_type_ops test_ops = {
	.fto_execute = test_handler
};

C2_FOP_TYPE_DECLARE(c2_fop_test, "test", 60,
			&test_ops);

static struct c2_verno verno = {
	.vn_lsn = (uint64_t)0xabab,
	.vn_vc = (uint64_t)0xbcbc
};

static struct c2_verno p_no = {
	.vn_lsn = (uint64_t)0xcdcd,
	.vn_vc = (uint64_t)0xdede
};

static struct c2_verno ls_no = {
	.vn_lsn = (uint64_t)0xefef,
	.vn_vc = (uint64_t)0xffff
};

void populate_item(struct c2_rpc_item *item)
{
	struct c2_rpc_slot_ref	slot_ref;

	item->ri_sender_id = 0xdead;
	item->ri_session_id = 0xbeef;
	item->ri_uuid.su_uuid = 0xeaeaeaea;
	slot_ref.sr_xid  = 0x11111111;
	slot_ref.sr_slot_gen = 0x22222222;
	slot_ref.sr_slot_id = 0x666;
	memcpy(&slot_ref.sr_verno, &verno, sizeof(struct c2_verno));
	memcpy(&slot_ref.sr_last_persistent_verno, &p_no, sizeof(struct c2_verno));
	memcpy(&slot_ref.sr_last_seen_verno, &ls_no, sizeof(struct c2_verno));
	memcpy(&item->ri_slot_refs[0], &slot_ref, sizeof(struct c2_rpc_slot_ref));
}

void populate_rpc_obj(struct c2_rpc *rpc, struct c2_rpc_item *item)
{

	c2_list_add(&rpc->r_items, &item->ri_rpcobject_linkage);

}

int main()
{
	struct c2_fop			*f1, *f2, *f3;
	struct c2_fop_test		*ccf1, *ccf2, *ccf3;
	int				rc;
	struct c2_rpc_item		*item1, *item2, *item3;
	struct c2_rpc			*obj, *obj2;
	struct c2_net_buffer		*nb;

	C2_ALLOC_PTR(item1);
	C2_ALLOC_PTR(item2);
	C2_ALLOC_PTR(item3);

	c2_init();
	rc = c2_fop_type_build(&c2_fop_test_fopt);
	C2_ASSERT(rc == 0);
	f1 = c2_fop_alloc(&c2_fop_test_fopt, NULL);
	C2_ASSERT(f1 != NULL);
	f2 = c2_fop_alloc(&c2_fop_test_fopt, NULL);
	C2_ASSERT(f2 != NULL);
	f3 = c2_fop_alloc(&c2_fop_test_fopt, NULL);
	C2_ASSERT(f3 != NULL);

	ccf1 = c2_fop_data(f1);
	C2_ASSERT(ccf1 != NULL);
	ccf1->t_time = 0x123456;
	ccf1->t_timeout = 0xABCDEF;

	ccf2 = c2_fop_data(f2);
	C2_ASSERT(ccf2 != NULL);
	ccf2->t_time = 0xdefabc;
	ccf2->t_timeout = 0xdef123;

	ccf3 = c2_fop_data(f3);
	C2_ASSERT(ccf3 != NULL);
	ccf3->t_time = 0xdef456;
	ccf3->t_timeout = 0xdef789;

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
	c2_bufvec_alloc(&nb->nb_buffer, 10, 64);

	rc =  c2_rpc_encode ( obj, nb );
	rc = c2_rpc_decode(obj2, nb);
	c2_fop_type_fini(&c2_fop_test_fopt);
	c2_fini();
	return 0;
}

