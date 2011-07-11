#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
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
#include "rpc/rpc_bufvec.c"
#include "lib/vec.h"

extern struct c2_fop_type_format c2_fop_test_tfmt;

static struct c2_rpc rpc_obj;

int test_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	printf("Called test_handler\n");
	return 0;
}

int test_bufvec_enc(struct c2_fop *fop, struct c2_bufvec_cursor *cur)
{
	struct c2_fop_test  *f;
	int 		    rc;

	C2_PRE(fop != NULL);
	C2_PRE(cur != NULL);

	f = c2_fop_data(fop);
	rc = c2_bufvec_uint64(cur, &f->t_time, BUFVEC_ENCODE);
	if (rc != 0)
		return -EFAULT;

	rc = c2_bufvec_uint32(cur, &f->t_timeout, BUFVEC_ENCODE);
	if (rc != 0)
		return -EFAULT;
	return rc;
}

int test_bufvec_dec(struct c2_fop *fop, struct c2_bufvec_cursor *cur)
{
	struct c2_fop_test  *f;
	int 		    rc;

	C2_PRE(fop != NULL);
	C2_PRE(cur != NULL);

	f = c2_fop_data(fop);
	rc = c2_bufvec_uint64(cur, &f->t_time, BUFVEC_DECODE);
	if (rc != 0)
		return -EFAULT;

	rc = c2_bufvec_uint32(cur, &f->t_timeout, BUFVEC_DECODE);
	if (rc != 0)
		return -EFAULT;
	return rc;
}

uint64_t test_fop_size_get(struct c2_fop *fop)
{
	uint64_t 	size;

	C2_PRE(fop != NULL);
	size = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;
	return size;
}
struct c2_fop_type_ops test_ops = {
	.fto_execute = test_handler,
	.fto_getsize = test_fop_size_get,
	.fto_bufvec_encode = test_bufvec_enc,
	.fto_bufvec_decode = test_bufvec_dec
};

C2_FOP_TYPE_DECLARE(c2_fop_test, "test", 60,
			&test_ops);

size_t test_item_size_get(struct c2_rpc_item *item)
{
	size_t		len = 0;
	struct c2_fop	*fop;

	C2_PRE(item != NULL);

	fop = c2_rpc_item_to_fop(item);
	if(fop != NULL){
		len = (size_t)fop->f_type->ft_ops->fto_getsize;
		len += sizeof item->ri_type->rit_opcode;
		len += sizeof(struct c2_rpc_item_header);
		}
		return len;
}

const struct c2_rpc_item_type_ops c2_rpc_item_test_ops = {
	.rito_encode = c2_rpc_fop_default_encode,
	.rito_decode = c2_rpc_fop_default_decode,
	.rio_item_size = test_item_size_get
};

/*static struct c2_rpc_item_type c2_rpc_item_type_test = {
	.rit_ops = &c2_rpc_item_test_ops,
	};
*/
static struct c2_fop_rpc_item_type c2_fop_rpc_item_type_test = {
	.fri_i_type = {
		.rit_ops = &c2_rpc_item_test_ops,
	},
};

static struct c2_rpc_item_type *c2_rpc_item_type_test =
	      &c2_fop_rpc_item_type_test.fri_i_type;

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
	struct c2_rpc			*obj, obj2;
	struct c2_net_buffer		*nb;
	struct c2_bufvec		vec;
	struct c2_bufvec_cursor		cur;
	struct c2_fop_rpc_item_type	*fri;
	uint32_t			enc_val, dec_val;
	uint64_t			enc64, dec64;
	unsigned char			c,d;

	enc_val = 0xABCDEF;
	enc64 = 0x12345678;
	c = 0xa;
	c2_bufvec_alloc(&vec, 16, 2);
	c2_bufvec_cursor_init(&cur, &vec);

	/* Encode bufvec tests */
	rc = c2_bufvec_uint32(&cur, &enc_val, BUFVEC_ENCODE);
	C2_ASSERT(rc == 0);
	rc = c2_bufvec_uint64(&cur, &enc64, BUFVEC_ENCODE);
	C2_ASSERT(rc == 0);
	rc = c2_bufvec_uchar(&cur, &c, BUFVEC_ENCODE);
	C2_ASSERT(rc == 0);
	enc_val = 0x123456;
	rc = c2_bufvec_uint32(&cur, &enc_val, BUFVEC_ENCODE);
	C2_ASSERT(rc == 0);

	/*Decode bufvec tests */
	c2_bufvec_cursor_init(&cur, &vec);
	rc = c2_bufvec_uint32(&cur, &dec_val, BUFVEC_DECODE);
	C2_ASSERT(rc == 0);
	printf("\nBUF DECODED val: %x\n", dec_val);
	rc = c2_bufvec_uint64(&cur, &dec64, BUFVEC_DECODE);
	C2_ASSERT(rc == 0);
	printf("\nBUF DECODED val: %lx\n", dec64);
	rc = c2_bufvec_uchar(&cur, &d, BUFVEC_DECODE);
	C2_ASSERT(rc == 0);
	printf("\nBUF DECODED val: %x\n", d);
	rc = c2_bufvec_uint32(&cur, &dec_val, BUFVEC_DECODE);
	C2_ASSERT(rc == 0);
	printf("\nBUF DECODED val: %x\n", dec_val);

	/* Onwire tests */
	C2_ALLOC_PTR(item1);
	C2_ALLOC_PTR(item2);
	C2_ALLOC_PTR(item3);

	c2_init();
	rc = c2_fop_type_build(&c2_fop_test_fopt);
	C2_ASSERT(rc == 0);
	/*
	   Associate an fop type with its item type. This should ideally be
	   done in a seperate function, but currently there's no such interface
	*/
	c2_rpc_item_type_add(c2_rpc_item_type_test);
	fri = &c2_fop_rpc_item_type_test;
	c2_fop_test_fopt.ft_ri_type = fri;
	fri->fri_f_type = &c2_fop_test_fopt;
	c2_rpc_item_type_test->rit_opcode = c2_fop_test_fopt.ft_code;

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
	item1->ri_type = c2_rpc_item_type_test;
	item2 = &f2->f_item;
	item2->ri_type = c2_rpc_item_type_test;
	item3 = &f3->f_item;
	item3->ri_type = c2_rpc_item_type_test;
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
	C2_ASSERT(rc == 0);
	c2_list_init(&obj2.r_items);
	rc = c2_rpc_decode(&obj2, nb);
	C2_ASSERT(rc == 0);
	c2_fop_type_fini(&c2_fop_test_fopt);
	c2_fini();
	return 0;
}

