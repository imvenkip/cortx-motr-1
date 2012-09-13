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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 28-Dec-2011
 */

#ifndef __KERNEL__
#include <stdio.h>                          /* printf */
#endif

#include "lib/memory.h"
#include "lib/vec.h"                        /* c2_bufvec */
#include "lib/misc.h"                       /* C2_SET0 */
#include "lib/ut.h"

#include "xcode/xcode.h"

struct foo {
	uint64_t f_x;
	uint64_t f_y;
};

typedef uint32_t tdef;

struct un {
	uint32_t u_tag;
	union {
		uint64_t u_x;
		char     u_y;
	} u;
};

struct top {
	struct foo t_foo;
	uint32_t   t_flag;
	struct v {
		uint32_t  v_nr;
		char     *v_data;
	} t_v;
	tdef              t_def;
	struct un         t_un;
	union {
		uint32_t *o_32;
		uint64_t *o_64;
	} t_opaq;
};

enum { CHILDREN_MAX = 16 };

struct static_xt {
	struct c2_xcode_type  xt;
	struct c2_xcode_field field[CHILDREN_MAX];
};

static struct static_xt xut_un = {
	.xt = {
		.xct_aggr   = C2_XA_UNION,
		.xct_name   = "un",
		.xct_sizeof = sizeof (struct un),
		.xct_nr     = 3
	}
};

static struct static_xt xut_tdef = {
	.xt = {
		.xct_aggr   = C2_XA_TYPEDEF,
		.xct_name   = "tdef",
		.xct_sizeof = sizeof (tdef),
		.xct_nr     = 1
	}
};

static struct static_xt xut_v = {
	.xt = {
		.xct_aggr   = C2_XA_SEQUENCE,
		.xct_name   = "v",
		.xct_sizeof = sizeof (struct v),
		.xct_nr     = 2
	}
};

static struct static_xt xut_foo = {
	.xt = {
		.xct_aggr   = C2_XA_RECORD,
		.xct_name   = "foo",
		.xct_sizeof = sizeof (struct foo),
		.xct_nr     = 2
	}
};

static struct static_xt xut_top = {
	.xt = {
		.xct_aggr   = C2_XA_RECORD,
		.xct_name   = "top",
		.xct_sizeof = sizeof (struct top),
		.xct_nr     = 6
	}
};

static char data[] = "Hello, world!\n";

static struct top T = {
	.t_foo  = {
		.f_x = 7,
		.f_y = 8
	},
	.t_flag = 0xF,
	.t_v    = {
		.v_nr   = sizeof data,
		.v_data = data
	},
	.t_un   = {
		.u_tag = 4
	},
	.t_opaq = {
		.o_32 = &T.t_v.v_nr
	}
};

static char                 ebuf[100];
static c2_bcount_t          count = ARRAY_SIZE(ebuf);
static void                *vec = ebuf;
static struct c2_bufvec     bvec  = C2_BUFVEC_INIT_BUF(&vec, &count);
static struct c2_xcode_ctx  ctx;

static struct tdata {
	struct _foo {
		uint64_t f_x;
		uint64_t f_y;
	} __attribute__((packed)) t_foo;
	uint32_t __attribute__((packed)) t_flag;
	struct _v {
		uint32_t v_nr;
		char     v_data[sizeof data];
	} __attribute__((packed)) t_v;
	uint32_t __attribute__((packed)) t_def;
	struct t_un {
		uint32_t u_tag;
		char     u_y;
	} __attribute__((packed)) t_un;
	uint32_t __attribute__((packed)) t_opaq;
} __attribute__((packed)) TD;

C2_BASSERT(sizeof TD < sizeof ebuf);

static int failure;

static int opaq_type(const struct c2_xcode_obj *par,
		     const struct c2_xcode_type **out)
{
	struct top *t = par->xo_ptr;

	C2_UT_ASSERT(par->xo_type == &xut_top.xt);

	if (!failure) {
		*out = t->t_flag == 0xf ? &C2_XT_U32 : &C2_XT_U64;
		return 0;
	} else
		return -ENOENT;
}

static int xcode_init(void)
{
	xut_v.xt.xct_child[0] = (struct c2_xcode_field){
		.xf_name   = "v_nr",
		.xf_type   = &C2_XT_U32,
		.xf_offset = offsetof(struct v, v_nr)
	};
	xut_v.xt.xct_child[1] = (struct c2_xcode_field){
		.xf_name   = "v_data",
		.xf_type   = &C2_XT_U8,
		.xf_offset = offsetof(struct v, v_data)
	};

	xut_foo.xt.xct_child[0] = (struct c2_xcode_field){
		.xf_name   = "f_x",
		.xf_type   = &C2_XT_U64,
		.xf_offset = offsetof(struct foo, f_x)
	};
	xut_foo.xt.xct_child[1] = (struct c2_xcode_field){
		.xf_name   = "f_y",
		.xf_type   = &C2_XT_U64,
		.xf_offset = offsetof(struct foo, f_y)
	};

	xut_top.xt.xct_child[0] = (struct c2_xcode_field){
		.xf_name   = "t_foo",
		.xf_type   = &xut_foo.xt,
		.xf_offset = offsetof(struct top, t_foo)
	};
	xut_top.xt.xct_child[1] = (struct c2_xcode_field){
		.xf_name   = "t_flag",
		.xf_type   = &C2_XT_U32,
		.xf_offset = offsetof(struct top, t_flag)
	};
	xut_top.xt.xct_child[2] = (struct c2_xcode_field){
		.xf_name   = "t_v",
		.xf_type   = &xut_v.xt,
		.xf_offset = offsetof(struct top, t_v)
	};
	xut_top.xt.xct_child[3] = (struct c2_xcode_field){
		.xf_name   = "t_def",
		.xf_type   = &xut_tdef.xt,
		.xf_offset = offsetof(struct top, t_def)
	};
	xut_top.xt.xct_child[4] = (struct c2_xcode_field){
		.xf_name   = "t_un",
		.xf_type   = &xut_un.xt,
		.xf_offset = offsetof(struct top, t_un)
	};
	xut_top.xt.xct_child[5] = (struct c2_xcode_field){
		.xf_name   = "t_opaq",
		.xf_type   = &C2_XT_OPAQUE,
		.xf_opaque = opaq_type,
		.xf_offset = offsetof(struct top, t_opaq)
	};

	xut_tdef.xt.xct_child[0] = (struct c2_xcode_field){
		.xf_name   = "def",
		.xf_type   = &C2_XT_U32,
		.xf_offset = 0
	};

	xut_un.xt.xct_child[0] = (struct c2_xcode_field){
		.xf_name   = "u_tag",
		.xf_type   = &C2_XT_U32,
		.xf_offset = offsetof(struct un, u_tag)
	};
	xut_un.xt.xct_child[1] = (struct c2_xcode_field){
		.xf_name   = "u_x",
		.xf_type   = &C2_XT_U64,
		.xf_tag    = 1,
		.xf_offset = offsetof(struct un, u.u_x)
	};
	xut_un.xt.xct_child[2] = (struct c2_xcode_field){
		.xf_name   = "u_y",
		.xf_type   = &C2_XT_U8,
		.xf_tag    = 4,
		.xf_offset = offsetof(struct un, u.u_y)
	};

	TD.t_foo.f_x  =  T.t_foo.f_x;
	TD.t_foo.f_y  =  T.t_foo.f_y;
	TD.t_flag     =  T.t_flag;
	TD.t_v.v_nr   =  T.t_v.v_nr;
	C2_ASSERT(T.t_v.v_nr == ARRAY_SIZE(TD.t_v.v_data));
	memcpy(TD.t_v.v_data, T.t_v.v_data, T.t_v.v_nr);
	TD.t_def      =  T.t_def;
	TD.t_un.u_tag =  T.t_un.u_tag;
	TD.t_un.u_y   =  T.t_un.u.u_y;
	TD.t_opaq     = *T.t_opaq.o_32;

	return 0;
}

#ifndef __KERNEL__
__attribute__((unused)) static void it_print(const struct c2_xcode_cursor *it)
{
	int i;
	const struct c2_xcode_cursor_frame *f;

	for (i = 0, f = &it->xcu_stack[0]; i < it->xcu_depth; ++i, ++f) {
		printf(".%s[%lu]",
		       f->s_obj.xo_type->xct_child[f->s_fieldno].xf_name,
		       f->s_elno);
	}
	printf(":%s ", c2_xcode_aggr_name[f->s_obj.xo_type->xct_aggr]);
	if (f->s_obj.xo_type->xct_aggr == C2_XA_ATOM) {
		switch (f->s_obj.xo_type->xct_atype) {
		case C2_XAT_VOID:
			printf("void");
			break;
		case C2_XAT_U8:
			printf("%c", *(char *)f->s_obj.xo_ptr);
			break;
		case C2_XAT_U32:
			printf("%x", *(uint32_t *)f->s_obj.xo_ptr);
			break;
		case C2_XAT_U64:
			printf("%x", (unsigned)*(uint64_t *)f->s_obj.xo_ptr);
			break;
		default:
			C2_IMPOSSIBLE("atom");
		}
	}
	printf("\n");
}
#endif

static void chk(struct c2_xcode_cursor *it, int depth,
		const struct c2_xcode_type *xt,
		void *addr, int fieldno, int elno,
		enum c2_xcode_cursor_flag flag)
{
	int                           rc;
	struct c2_xcode_obj          *obj;
	struct c2_xcode_cursor_frame *f;

	rc = c2_xcode_next(it);
	C2_UT_ASSERT(rc > 0);

	C2_UT_ASSERT(it->xcu_depth == depth);
	C2_UT_ASSERT(IS_IN_ARRAY(depth, it->xcu_stack));

	f   = c2_xcode_cursor_top(it);
	obj = &f->s_obj;

	C2_UT_ASSERT(obj->xo_type == xt);
	C2_UT_ASSERT(obj->xo_ptr  == addr);
	C2_UT_ASSERT(f->s_fieldno == fieldno);
	C2_UT_ASSERT(f->s_elno    == elno);
	C2_UT_ASSERT(f->s_flag    == flag);
}

static void xcode_cursor_test(void)
{
	int                    i;
	struct c2_xcode_cursor it;

	C2_SET0(&it);

	it.xcu_stack[0].s_obj.xo_type = &xut_top.xt;
	it.xcu_stack[0].s_obj.xo_ptr  = &T;

	chk(&it, 0, &xut_top.xt, &T, 0, 0, C2_XCODE_CURSOR_PRE);
	chk(&it, 1, &xut_foo.xt, &T.t_foo, 0, 0, C2_XCODE_CURSOR_PRE);
	chk(&it, 2, &C2_XT_U64, &T.t_foo.f_x, 0, 0, C2_XCODE_CURSOR_PRE);
	chk(&it, 2, &C2_XT_U64, &T.t_foo.f_x, 0, 0, C2_XCODE_CURSOR_POST);
	chk(&it, 1, &xut_foo.xt, &T.t_foo, 0, 0, C2_XCODE_CURSOR_IN);
	chk(&it, 2, &C2_XT_U64, &T.t_foo.f_y, 0, 0, C2_XCODE_CURSOR_PRE);
	chk(&it, 2, &C2_XT_U64, &T.t_foo.f_y, 0, 0, C2_XCODE_CURSOR_POST);
	chk(&it, 1, &xut_foo.xt, &T.t_foo, 1, 0, C2_XCODE_CURSOR_IN);
	chk(&it, 1, &xut_foo.xt, &T.t_foo, 2, 0, C2_XCODE_CURSOR_POST);
	chk(&it, 0, &xut_top.xt, &T, 0, 0, C2_XCODE_CURSOR_IN);
	chk(&it, 1, &C2_XT_U32, &T.t_flag, 0, 0, C2_XCODE_CURSOR_PRE);
	chk(&it, 1, &C2_XT_U32, &T.t_flag, 0, 0, C2_XCODE_CURSOR_POST);
	chk(&it, 0, &xut_top.xt, &T, 1, 0, C2_XCODE_CURSOR_IN);
	chk(&it, 1, &xut_v.xt, &T.t_v, 0, 0, C2_XCODE_CURSOR_PRE);
	chk(&it, 2, &C2_XT_U32, &T.t_v.v_nr, 0, 0, C2_XCODE_CURSOR_PRE);
	chk(&it, 2, &C2_XT_U32, &T.t_v.v_nr, 0, 0, C2_XCODE_CURSOR_POST);
	chk(&it, 1, &xut_v.xt, &T.t_v, 0, 0, C2_XCODE_CURSOR_IN);
	for (i = 0; i < ARRAY_SIZE(data); ++i) {
		chk(&it, 2, &C2_XT_U8,
		    &T.t_v.v_data[i], 0, 0, C2_XCODE_CURSOR_PRE);
		chk(&it, 2, &C2_XT_U8,
		    &T.t_v.v_data[i], 0, 0, C2_XCODE_CURSOR_POST);
		C2_UT_ASSERT(*(char *)it.xcu_stack[2].s_obj.xo_ptr == data[i]);
		chk(&it, 1, &xut_v.xt, &T.t_v, 1, i, C2_XCODE_CURSOR_IN);
	}
	chk(&it, 1, &xut_v.xt, &T.t_v, 2, 0, C2_XCODE_CURSOR_POST);
	chk(&it, 0, &xut_top.xt, &T, 2, 0, C2_XCODE_CURSOR_IN);
	chk(&it, 1, &xut_tdef.xt, &T.t_def, 0, 0, C2_XCODE_CURSOR_PRE);
	chk(&it, 2, &C2_XT_U32, &T.t_def, 0, 0, C2_XCODE_CURSOR_PRE);
	chk(&it, 2, &C2_XT_U32, &T.t_def, 0, 0, C2_XCODE_CURSOR_POST);
	chk(&it, 1, &xut_tdef.xt, &T.t_def, 0, 0, C2_XCODE_CURSOR_IN);
	chk(&it, 1, &xut_tdef.xt, &T.t_def, 1, 0, C2_XCODE_CURSOR_POST);
	chk(&it, 0, &xut_top.xt, &T, 3, 0, C2_XCODE_CURSOR_IN);
	chk(&it, 1, &xut_un.xt, &T.t_un, 0, 0, C2_XCODE_CURSOR_PRE);
	chk(&it, 2, &C2_XT_U32, &T.t_un.u_tag, 0, 0, C2_XCODE_CURSOR_PRE);
	chk(&it, 2, &C2_XT_U32, &T.t_un.u_tag, 0, 0, C2_XCODE_CURSOR_POST);
	chk(&it, 1, &xut_un.xt, &T.t_un, 0, 0, C2_XCODE_CURSOR_IN);
	chk(&it, 2, &C2_XT_U8, &T.t_un.u.u_y, 0, 0, C2_XCODE_CURSOR_PRE);
	chk(&it, 2, &C2_XT_U8, &T.t_un.u.u_y, 0, 0, C2_XCODE_CURSOR_POST);
	chk(&it, 1, &xut_un.xt, &T.t_un, 2, 0, C2_XCODE_CURSOR_IN);
	chk(&it, 1, &xut_un.xt, &T.t_un, 3, 0, C2_XCODE_CURSOR_POST);
	chk(&it, 0, &xut_top.xt, &T, 4, 0, C2_XCODE_CURSOR_IN);
	chk(&it, 1, &C2_XT_U32, T.t_opaq.o_32, 0, 0, C2_XCODE_CURSOR_PRE);
	chk(&it, 1, &C2_XT_U32, T.t_opaq.o_32, 0, 0, C2_XCODE_CURSOR_POST);
	chk(&it, 0, &xut_top.xt, &T, 5, 0, C2_XCODE_CURSOR_IN);
	chk(&it, 0, &xut_top.xt, &T, 6, 0, C2_XCODE_CURSOR_POST);

	C2_UT_ASSERT(c2_xcode_next(&it) == 0);
}

static void xcode_length_test(void)
{
	int result;

	c2_xcode_ctx_init(&ctx, &(struct c2_xcode_obj){ &xut_top.xt, &T });
	result = c2_xcode_length(&ctx);
	C2_UT_ASSERT(result == sizeof TD);
}

static void xcode_encode_test(void)
{
	int result;

	c2_xcode_ctx_init(&ctx, &(struct c2_xcode_obj){ &xut_top.xt, &T });
	c2_bufvec_cursor_init(&ctx.xcx_buf, &bvec);
	result = c2_xcode_encode(&ctx);
	C2_UT_ASSERT(result == 0);

	C2_UT_ASSERT(memcmp(&TD, ebuf, sizeof TD) == 0);
}

static void xcode_opaque_test(void)
{
	int result;

	failure = 1;
	c2_xcode_ctx_init(&ctx, &(struct c2_xcode_obj){ &xut_top.xt, &T });
	result = c2_xcode_length(&ctx);
	C2_UT_ASSERT(result == -ENOENT);
	failure = 0;
}

static void decode(struct c2_xcode_obj *obj)
{
	int result;

	c2_xcode_ctx_init(&ctx, &(struct c2_xcode_obj){ &xut_top.xt, NULL });
	ctx.xcx_alloc = c2_xcode_alloc;
	c2_bufvec_cursor_init(&ctx.xcx_buf, &bvec);

	result = c2_xcode_decode(&ctx);
	C2_UT_ASSERT(result == 0);

	*obj = ctx.xcx_it.xcu_stack[0].s_obj;
}

static void xcode_decode_test(void)
{
	struct c2_xcode_obj decoded;
	struct top *TT;

	decode(&decoded);
	TT = decoded.xo_ptr;
	C2_UT_ASSERT( TT != NULL);
	C2_UT_ASSERT( TT->t_foo.f_x    ==  T.t_foo.f_x);
	C2_UT_ASSERT( TT->t_foo.f_y    ==  T.t_foo.f_y);
	C2_UT_ASSERT( TT->t_flag       ==  T.t_flag);
	C2_UT_ASSERT( TT->t_v.v_nr     ==  T.t_v.v_nr);
	C2_UT_ASSERT(memcmp(TT->t_v.v_data, T.t_v.v_data, T.t_v.v_nr) == 0);
	C2_UT_ASSERT( TT->t_def        ==  T.t_def);
	C2_UT_ASSERT( TT->t_un.u_tag   ==  T.t_un.u_tag);
	C2_UT_ASSERT( TT->t_un.u.u_y   ==  T.t_un.u.u_y);
	C2_UT_ASSERT(*TT->t_opaq.o_32  == *T.t_opaq.o_32);

	c2_xcode_free(&decoded);
}

enum {
	FSIZE = sizeof(uint64_t) + sizeof(uint64_t)
};

static char             foo_buf[FSIZE];
static void            *foo_addr  = foo_buf;
static c2_bcount_t      foo_count = ARRAY_SIZE(foo_buf);
static struct c2_bufvec foo_bvec  = C2_BUFVEC_INIT_BUF(&foo_addr, &foo_count);

static int foo_length(struct c2_xcode_ctx *ctx, const void *obj)
{
	return ARRAY_SIZE(foo_buf);
}

static void foo_xor(char *buf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(foo_buf); ++i)
		buf[i] ^= 42;
}

static int foo_encode(struct c2_xcode_ctx *ctx, const void *obj)
{
	struct c2_bufvec_cursor cur;

	c2_bufvec_cursor_init(&cur, &foo_bvec);
	memcpy(foo_buf, obj, sizeof(struct foo));
	foo_xor(foo_buf);
	return c2_bufvec_cursor_copy(&ctx->xcx_buf, &cur, FSIZE) != FSIZE ?
		-EPROTO : 0;
}

static int foo_decode(struct c2_xcode_ctx *ctx, void *obj)
{
	struct c2_bufvec_cursor cur;

	c2_bufvec_cursor_init(&cur, &foo_bvec);
	if (c2_bufvec_cursor_copy(&cur, &ctx->xcx_buf, FSIZE) == FSIZE) {
		foo_xor(foo_buf);
		memcpy(obj, foo_buf, sizeof(struct foo));
		return 0;
	} else
		return -EPROTO;
}

static const struct c2_xcode_type_ops foo_ops = {
	.xto_length = foo_length,
	.xto_encode = foo_encode,
	.xto_decode = foo_decode
};

static void xcode_nonstandard_test(void)
{
	int result;

	xut_foo.xt.xct_ops = &foo_ops;

	c2_xcode_ctx_init(&ctx, &(struct c2_xcode_obj){ &xut_top.xt, &T });
	result = c2_xcode_length(&ctx);
	C2_UT_ASSERT(result == sizeof TD);

	c2_xcode_ctx_init(&ctx, &(struct c2_xcode_obj){ &xut_top.xt, &T });
	c2_bufvec_cursor_init(&ctx.xcx_buf, &bvec);
	result = c2_xcode_encode(&ctx);
	C2_UT_ASSERT(result == 0);

	foo_xor(ebuf);
	C2_UT_ASSERT(memcmp(&TD, ebuf, sizeof TD) == 0);
	foo_xor(ebuf);
	xcode_decode_test();
	xut_foo.xt.xct_ops = NULL;
}

static void xcode_cmp_test(void)
{
	struct c2_xcode_obj obj0;
	struct c2_xcode_obj obj1;
	struct top *t0;
	struct top *t1;
	int    cmp;

	xcode_encode_test();

	decode(&obj0);
	decode(&obj1);

	t0 = obj0.xo_ptr;
	t1 = obj1.xo_ptr;

	cmp = c2_xcode_cmp(&obj0, &obj0);
	C2_UT_ASSERT(cmp == 0);

	cmp = c2_xcode_cmp(&obj0, &obj1);
	C2_UT_ASSERT(cmp == 0);

	cmp = c2_xcode_cmp(&obj1, &obj0);
	C2_UT_ASSERT(cmp == 0);

	t1->t_foo.f_x--;
	cmp = c2_xcode_cmp(&obj0, &obj1);
	C2_UT_ASSERT(cmp > 0);
	cmp = c2_xcode_cmp(&obj1, &obj0);
	C2_UT_ASSERT(cmp < 0);

	t1->t_foo.f_x++;
	cmp = c2_xcode_cmp(&obj0, &obj1);
	C2_UT_ASSERT(cmp == 0);

	t1->t_v.v_data[0] = 'J';
	cmp = c2_xcode_cmp(&obj0, &obj1);
	C2_UT_ASSERT(cmp < 0);
	t1->t_v.v_data[0] = t0->t_v.v_data[0];

	t1->t_v.v_nr++;
	cmp = c2_xcode_cmp(&obj0, &obj1);
	C2_UT_ASSERT(cmp < 0);
	cmp = c2_xcode_cmp(&obj1, &obj0);
	C2_UT_ASSERT(cmp > 0);
	t1->t_v.v_nr--;

	c2_xcode_free(&obj0);
	c2_xcode_free(&obj1);
}

#define OBJ(xt, ptr) (&(struct c2_xcode_obj){ .xo_type = (xt), .xo_ptr = (ptr) })

static void xcode_read_test(void)
{
	int        result;
	struct foo F;
	struct un  U;
	struct v   V;
	struct top T;

	C2_SET0(&F);
	result = c2_xcode_read(OBJ(&xut_foo.xt, &F), "(10, 0xff)");
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(F.f_x == 10);
	C2_UT_ASSERT(F.f_y == 0xff);

	C2_SET0(&F);
	result = c2_xcode_read(OBJ(&xut_foo.xt, &F), " ( 10 , 0xff ) ");
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(F.f_x == 10);
	C2_UT_ASSERT(F.f_y == 0xff);

	C2_SET0(&F);
	result = c2_xcode_read(OBJ(&xut_foo.xt, &F), "(10,010)");
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(F.f_x == 10);
	C2_UT_ASSERT(F.f_y == 8);

	C2_SET0(&F);
	result = c2_xcode_read(OBJ(&xut_foo.xt, &F), " ( 10 , 0xff ) rest");
	C2_UT_ASSERT(result == -EINVAL);

	C2_SET0(&F);
	result = c2_xcode_read(OBJ(&xut_foo.xt, &F), "(10,)");
	C2_UT_ASSERT(result == -EPROTO);

	C2_SET0(&F);
	result = c2_xcode_read(OBJ(&xut_foo.xt, &F), "(10 12)");
	C2_UT_ASSERT(result == -EPROTO);

	C2_SET0(&F);
	result = c2_xcode_read(OBJ(&xut_foo.xt, &F), "()");
	C2_UT_ASSERT(result == -EPROTO);

	C2_SET0(&F);
	result = c2_xcode_read(OBJ(&xut_foo.xt, &F), "");
	C2_UT_ASSERT(result == -EPROTO);

	C2_SET0(&U);
	result = c2_xcode_read(OBJ(&xut_un.xt, &U), "{1| 42}");
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(U.u_tag == 1);
	C2_UT_ASSERT(U.u.u_x == 42);

	C2_SET0(&U);
	result = c2_xcode_read(OBJ(&xut_un.xt, &U), "{4| 8}");
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(U.u_tag == 4);
	C2_UT_ASSERT(U.u.u_y == 8);

	C2_SET0(&U);
	result = c2_xcode_read(OBJ(&xut_un.xt, &U), "{3| 0}");
	C2_UT_ASSERT(result == -EPROTO);

	C2_SET0(&U);
	result = c2_xcode_read(OBJ(&xut_un.xt, &U), "{3}");
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(U.u_tag == 3);

	C2_SET0(&V);
	result = c2_xcode_read(OBJ(&xut_v.xt, &V), "[0]");
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(V.v_nr == 0);

	C2_SET0(&V);
	result = c2_xcode_read(OBJ(&xut_v.xt, &V), "[1: 42]");
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(V.v_nr == 1);
	C2_UT_ASSERT(V.v_data[0] == 42);

	result = c2_xcode_read(OBJ(&xut_v.xt, &V), "[3: 42, 43, 44]");
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(V.v_nr == 3);
	C2_UT_ASSERT(V.v_data[0] == 42);
	C2_UT_ASSERT(V.v_data[1] == 43);
	C2_UT_ASSERT(V.v_data[2] == 44);

	C2_SET0(&V);
	result = c2_xcode_read(OBJ(&xut_v.xt, &V), "\"a\"");
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(V.v_nr == 1);
	C2_UT_ASSERT(strncmp(V.v_data, "a", 1) == 0);

	C2_SET0(&V);
	result = c2_xcode_read(OBJ(&xut_v.xt, &V), "\"abcdef\"");
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(V.v_nr == 6);
	C2_UT_ASSERT(strncmp(V.v_data, "abcdef", 6) == 0);

	C2_SET0(&V);
	result = c2_xcode_read(OBJ(&xut_v.xt, &V), "\"\"");
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(V.v_nr == 0);

	C2_SET0(&V);
	result = c2_xcode_read(OBJ(&xut_v.xt, &V), "\"");
	C2_UT_ASSERT(result == -EPROTO);

	C2_SET0(&T);
	result = c2_xcode_read(OBJ(&xut_top.xt, &T), ""
"((1, 2),"
" 8,"
" [4: 1, 2, 3, 4],"
" 4,"
" {1| 42},"
" 7)");
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(memcmp(&T, &(struct top){
		.t_foo  = { 1, 2 },
		.t_flag = 8,
		.t_v    = { .v_nr = 4, .v_data = T.t_v.v_data },
		.t_def  = 4,
		.t_un   = { .u_tag = 1, .u = { .u_x = 42 }},
		.t_opaq = { .o_32 = T.t_opaq.o_32 }}, sizeof T) == 0);

}

const struct c2_test_suite xcode_ut = {
        .ts_name = "xcode-ut",
        .ts_init = xcode_init,
        .ts_fini = NULL,
        .ts_tests = {
                { "xcode-cursor", xcode_cursor_test },
                { "xcode-length", xcode_length_test },
                { "xcode-encode", xcode_encode_test },
                { "xcode-opaque", xcode_opaque_test },
                { "xcode-decode", xcode_decode_test },
                { "xcode-nonstandard", xcode_nonstandard_test },
                { "xcode-cmp",    xcode_cmp_test },
		{ "xcode-read",   xcode_read_test },
                { NULL, NULL }
        }
};
C2_EXPORTED(xcode_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */


