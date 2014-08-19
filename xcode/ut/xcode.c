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
#include <ctype.h>                          /* isspace */
#endif

#include "lib/memory.h"
#include "lib/vec.h"                        /* m0_bufvec */
#include "lib/misc.h"                       /* M0_SET0 */
#include "lib/errno.h"                      /* ENOENT */
#include "ut/ut.h"

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
	struct m0_xcode_type  xt;
	struct m0_xcode_field field[CHILDREN_MAX];
};

static struct static_xt xut_un = {
	.xt = {
		.xct_aggr   = M0_XA_UNION,
		.xct_name   = "un",
		.xct_sizeof = sizeof (struct un),
		.xct_nr     = 3
	}
};

static struct static_xt xut_tdef = {
	.xt = {
		.xct_aggr   = M0_XA_TYPEDEF,
		.xct_name   = "tdef",
		.xct_sizeof = sizeof (tdef),
		.xct_nr     = 1
	}
};

static struct static_xt xut_v = {
	.xt = {
		.xct_aggr   = M0_XA_SEQUENCE,
		.xct_name   = "v",
		.xct_sizeof = sizeof (struct v),
		.xct_nr     = 2
	}
};

static struct static_xt xut_foo = {
	.xt = {
		.xct_aggr   = M0_XA_RECORD,
		.xct_name   = "foo",
		.xct_sizeof = sizeof (struct foo),
		.xct_nr     = 2
	}
};

static struct static_xt xut_top = {
	.xt = {
		.xct_aggr   = M0_XA_RECORD,
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
static m0_bcount_t          count = ARRAY_SIZE(ebuf);
static void                *vec = ebuf;
static struct m0_bufvec     bvec  = M0_BUFVEC_INIT_BUF(&vec, &count);
static struct m0_xcode_ctx  ctx;

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

M0_BASSERT(sizeof TD < sizeof ebuf);

static int failure;

static int opaq_type(const struct m0_xcode_obj *par,
		     const struct m0_xcode_type **out)
{
	struct top *t = par->xo_ptr;

	M0_UT_ASSERT(par->xo_type == &xut_top.xt);

	if (!failure) {
		*out = t->t_flag == 0xf ? &M0_XT_U32 : &M0_XT_U64;
		return 0;
	} else
		return -ENOENT;
}

static int xcode_init(void)
{
	xut_v.xt.xct_child[0] = (struct m0_xcode_field){
		.xf_name   = "v_nr",
		.xf_type   = &M0_XT_U32,
		.xf_offset = offsetof(struct v, v_nr)
	};
	xut_v.xt.xct_child[1] = (struct m0_xcode_field){
		.xf_name   = "v_data",
		.xf_type   = &M0_XT_U8,
		.xf_offset = offsetof(struct v, v_data)
	};

	xut_foo.xt.xct_child[0] = (struct m0_xcode_field){
		.xf_name   = "f_x",
		.xf_type   = &M0_XT_U64,
		.xf_offset = offsetof(struct foo, f_x)
	};
	xut_foo.xt.xct_child[1] = (struct m0_xcode_field){
		.xf_name   = "f_y",
		.xf_type   = &M0_XT_U64,
		.xf_offset = offsetof(struct foo, f_y)
	};

	xut_top.xt.xct_child[0] = (struct m0_xcode_field){
		.xf_name   = "t_foo",
		.xf_type   = &xut_foo.xt,
		.xf_offset = offsetof(struct top, t_foo)
	};
	xut_top.xt.xct_child[1] = (struct m0_xcode_field){
		.xf_name   = "t_flag",
		.xf_type   = &M0_XT_U32,
		.xf_offset = offsetof(struct top, t_flag)
	};
	xut_top.xt.xct_child[2] = (struct m0_xcode_field){
		.xf_name   = "t_v",
		.xf_type   = &xut_v.xt,
		.xf_offset = offsetof(struct top, t_v)
	};
	xut_top.xt.xct_child[3] = (struct m0_xcode_field){
		.xf_name   = "t_def",
		.xf_type   = &xut_tdef.xt,
		.xf_offset = offsetof(struct top, t_def)
	};
	xut_top.xt.xct_child[4] = (struct m0_xcode_field){
		.xf_name   = "t_un",
		.xf_type   = &xut_un.xt,
		.xf_offset = offsetof(struct top, t_un)
	};
	xut_top.xt.xct_child[5] = (struct m0_xcode_field){
		.xf_name   = "t_opaq",
		.xf_type   = &M0_XT_OPAQUE,
		.xf_opaque = opaq_type,
		.xf_offset = offsetof(struct top, t_opaq)
	};

	xut_tdef.xt.xct_child[0] = (struct m0_xcode_field){
		.xf_name   = "def",
		.xf_type   = &M0_XT_U32,
		.xf_offset = 0
	};

	xut_un.xt.xct_child[0] = (struct m0_xcode_field){
		.xf_name   = "u_tag",
		.xf_type   = &M0_XT_U32,
		.xf_offset = offsetof(struct un, u_tag)
	};
	xut_un.xt.xct_child[1] = (struct m0_xcode_field){
		.xf_name   = "u_x",
		.xf_type   = &M0_XT_U64,
		.xf_tag    = 1,
		.xf_offset = offsetof(struct un, u.u_x)
	};
	xut_un.xt.xct_child[2] = (struct m0_xcode_field){
		.xf_name   = "u_y",
		.xf_type   = &M0_XT_U8,
		.xf_tag    = 4,
		.xf_offset = offsetof(struct un, u.u_y)
	};

	TD.t_foo.f_x  =  T.t_foo.f_x;
	TD.t_foo.f_y  =  T.t_foo.f_y;
	TD.t_flag     =  T.t_flag;
	TD.t_v.v_nr   =  T.t_v.v_nr;
	M0_ASSERT(T.t_v.v_nr == ARRAY_SIZE(TD.t_v.v_data));
	memcpy(TD.t_v.v_data, T.t_v.v_data, T.t_v.v_nr);
	TD.t_def      =  T.t_def;
	TD.t_un.u_tag =  T.t_un.u_tag;
	TD.t_un.u_y   =  T.t_un.u.u_y;
	TD.t_opaq     = *T.t_opaq.o_32;

	return 0;
}

#ifndef __KERNEL__
__attribute__((unused)) static void it_print(const struct m0_xcode_cursor *it)
{
	int i;
	const struct m0_xcode_cursor_frame *f;

	for (i = 0, f = &it->xcu_stack[0]; i < it->xcu_depth; ++i, ++f) {
		printf(".%s[%lu]",
		       f->s_obj.xo_type->xct_child[f->s_fieldno].xf_name,
		       f->s_elno);
	}
	printf(":%s ", m0_xcode_aggr_name[f->s_obj.xo_type->xct_aggr]);
	if (f->s_obj.xo_type->xct_aggr == M0_XA_ATOM) {
		switch (f->s_obj.xo_type->xct_atype) {
		case M0_XAT_VOID:
			printf("void");
			break;
		case M0_XAT_U8:
			printf("%c", *(char *)f->s_obj.xo_ptr);
			break;
		case M0_XAT_U32:
			printf("%x", *(uint32_t *)f->s_obj.xo_ptr);
			break;
		case M0_XAT_U64:
			printf("%x", (unsigned)*(uint64_t *)f->s_obj.xo_ptr);
			break;
		default:
			M0_IMPOSSIBLE("atom");
		}
	}
	printf("\n");
}
#endif

static void chk(struct m0_xcode_cursor *it, int depth,
		const struct m0_xcode_type *xt,
		void *addr, int fieldno, int elno,
		enum m0_xcode_cursor_flag flag)
{
	int                           rc;
	struct m0_xcode_obj          *obj;
	struct m0_xcode_cursor_frame *f;

	rc = m0_xcode_next(it);
	M0_UT_ASSERT(rc > 0);

	M0_UT_ASSERT(it->xcu_depth == depth);
	M0_UT_ASSERT(IS_IN_ARRAY(depth, it->xcu_stack));

	f   = m0_xcode_cursor_top(it);
	obj = &f->s_obj;

	M0_UT_ASSERT(obj->xo_type == xt);
	M0_UT_ASSERT(obj->xo_ptr  == addr);
	M0_UT_ASSERT(f->s_fieldno == fieldno);
	M0_UT_ASSERT(f->s_elno    == elno);
	M0_UT_ASSERT(f->s_flag    == flag);
}

static void xcode_cursor_test(void)
{
	int                    i;
	struct m0_xcode_cursor it;

	M0_SET0(&it);

	it.xcu_stack[0].s_obj.xo_type = &xut_top.xt;
	it.xcu_stack[0].s_obj.xo_ptr  = &T;

	chk(&it, 0, &xut_top.xt, &T, 0, 0, M0_XCODE_CURSOR_PRE);
	chk(&it, 1, &xut_foo.xt, &T.t_foo, 0, 0, M0_XCODE_CURSOR_PRE);
	chk(&it, 2, &M0_XT_U64, &T.t_foo.f_x, 0, 0, M0_XCODE_CURSOR_PRE);
	chk(&it, 2, &M0_XT_U64, &T.t_foo.f_x, 0, 0, M0_XCODE_CURSOR_POST);
	chk(&it, 1, &xut_foo.xt, &T.t_foo, 0, 0, M0_XCODE_CURSOR_IN);
	chk(&it, 2, &M0_XT_U64, &T.t_foo.f_y, 0, 0, M0_XCODE_CURSOR_PRE);
	chk(&it, 2, &M0_XT_U64, &T.t_foo.f_y, 0, 0, M0_XCODE_CURSOR_POST);
	chk(&it, 1, &xut_foo.xt, &T.t_foo, 1, 0, M0_XCODE_CURSOR_IN);
	chk(&it, 1, &xut_foo.xt, &T.t_foo, 2, 0, M0_XCODE_CURSOR_POST);
	chk(&it, 0, &xut_top.xt, &T, 0, 0, M0_XCODE_CURSOR_IN);
	chk(&it, 1, &M0_XT_U32, &T.t_flag, 0, 0, M0_XCODE_CURSOR_PRE);
	chk(&it, 1, &M0_XT_U32, &T.t_flag, 0, 0, M0_XCODE_CURSOR_POST);
	chk(&it, 0, &xut_top.xt, &T, 1, 0, M0_XCODE_CURSOR_IN);
	chk(&it, 1, &xut_v.xt, &T.t_v, 0, 0, M0_XCODE_CURSOR_PRE);
	chk(&it, 2, &M0_XT_U32, &T.t_v.v_nr, 0, 0, M0_XCODE_CURSOR_PRE);
	chk(&it, 2, &M0_XT_U32, &T.t_v.v_nr, 0, 0, M0_XCODE_CURSOR_POST);
	chk(&it, 1, &xut_v.xt, &T.t_v, 0, 0, M0_XCODE_CURSOR_IN);
	for (i = 0; i < ARRAY_SIZE(data); ++i) {
		chk(&it, 2, &M0_XT_U8,
		    &T.t_v.v_data[i], 0, 0, M0_XCODE_CURSOR_PRE);
		chk(&it, 2, &M0_XT_U8,
		    &T.t_v.v_data[i], 0, 0, M0_XCODE_CURSOR_POST);
		M0_UT_ASSERT(*(char *)it.xcu_stack[2].s_obj.xo_ptr == data[i]);
		chk(&it, 1, &xut_v.xt, &T.t_v, 1, i, M0_XCODE_CURSOR_IN);
	}
	chk(&it, 1, &xut_v.xt, &T.t_v, 2, 0, M0_XCODE_CURSOR_POST);
	chk(&it, 0, &xut_top.xt, &T, 2, 0, M0_XCODE_CURSOR_IN);
	chk(&it, 1, &xut_tdef.xt, &T.t_def, 0, 0, M0_XCODE_CURSOR_PRE);
	chk(&it, 2, &M0_XT_U32, &T.t_def, 0, 0, M0_XCODE_CURSOR_PRE);
	chk(&it, 2, &M0_XT_U32, &T.t_def, 0, 0, M0_XCODE_CURSOR_POST);
	chk(&it, 1, &xut_tdef.xt, &T.t_def, 0, 0, M0_XCODE_CURSOR_IN);
	chk(&it, 1, &xut_tdef.xt, &T.t_def, 1, 0, M0_XCODE_CURSOR_POST);
	chk(&it, 0, &xut_top.xt, &T, 3, 0, M0_XCODE_CURSOR_IN);
	chk(&it, 1, &xut_un.xt, &T.t_un, 0, 0, M0_XCODE_CURSOR_PRE);
	chk(&it, 2, &M0_XT_U32, &T.t_un.u_tag, 0, 0, M0_XCODE_CURSOR_PRE);
	chk(&it, 2, &M0_XT_U32, &T.t_un.u_tag, 0, 0, M0_XCODE_CURSOR_POST);
	chk(&it, 1, &xut_un.xt, &T.t_un, 0, 0, M0_XCODE_CURSOR_IN);
	chk(&it, 2, &M0_XT_U8, &T.t_un.u.u_y, 0, 0, M0_XCODE_CURSOR_PRE);
	chk(&it, 2, &M0_XT_U8, &T.t_un.u.u_y, 0, 0, M0_XCODE_CURSOR_POST);
	chk(&it, 1, &xut_un.xt, &T.t_un, 2, 0, M0_XCODE_CURSOR_IN);
	chk(&it, 1, &xut_un.xt, &T.t_un, 3, 0, M0_XCODE_CURSOR_POST);
	chk(&it, 0, &xut_top.xt, &T, 4, 0, M0_XCODE_CURSOR_IN);
	chk(&it, 1, &M0_XT_U32, T.t_opaq.o_32, 0, 0, M0_XCODE_CURSOR_PRE);
	chk(&it, 1, &M0_XT_U32, T.t_opaq.o_32, 0, 0, M0_XCODE_CURSOR_POST);
	chk(&it, 0, &xut_top.xt, &T, 5, 0, M0_XCODE_CURSOR_IN);
	chk(&it, 0, &xut_top.xt, &T, 6, 0, M0_XCODE_CURSOR_POST);

	M0_UT_ASSERT(m0_xcode_next(&it) == 0);
}

static void xcode_length_test(void)
{
	int result;

	m0_xcode_ctx_init(&ctx, &(struct m0_xcode_obj){ &xut_top.xt, &T });
	result = m0_xcode_length(&ctx);
	M0_UT_ASSERT(result == sizeof TD);
}

static void xcode_encode_test(void)
{
	int result;

	m0_xcode_ctx_init(&ctx, &(struct m0_xcode_obj){ &xut_top.xt, &T });
	m0_bufvec_cursor_init(&ctx.xcx_buf, &bvec);
	result = m0_xcode_encode(&ctx);
	M0_UT_ASSERT(result == 0);

	M0_UT_ASSERT(memcmp(&TD, ebuf, sizeof TD) == 0);
}

static void xcode_opaque_test(void)
{
	int result;

	failure = 1;
	m0_xcode_ctx_init(&ctx, &(struct m0_xcode_obj){ &xut_top.xt, &T });
	result = m0_xcode_length(&ctx);
	M0_UT_ASSERT(result == -ENOENT);
	failure = 0;
}

static void decode(struct m0_xcode_obj *obj)
{
	int result;

	m0_xcode_ctx_init(&ctx, &(struct m0_xcode_obj){ &xut_top.xt, NULL });
	ctx.xcx_alloc = m0_xcode_alloc;
	m0_bufvec_cursor_init(&ctx.xcx_buf, &bvec);

	result = m0_xcode_decode(&ctx);
	M0_UT_ASSERT(result == 0);

	*obj = ctx.xcx_it.xcu_stack[0].s_obj;
}

static void xcode_decode_test(void)
{
	struct m0_xcode_obj decoded;
	struct top *TT;

	decode(&decoded);
	TT = decoded.xo_ptr;
	M0_UT_ASSERT( TT != NULL);
	M0_UT_ASSERT( TT->t_foo.f_x    ==  T.t_foo.f_x);
	M0_UT_ASSERT( TT->t_foo.f_y    ==  T.t_foo.f_y);
	M0_UT_ASSERT( TT->t_flag       ==  T.t_flag);
	M0_UT_ASSERT( TT->t_v.v_nr     ==  T.t_v.v_nr);
	M0_UT_ASSERT(memcmp(TT->t_v.v_data, T.t_v.v_data, T.t_v.v_nr) == 0);
	M0_UT_ASSERT( TT->t_def        ==  T.t_def);
	M0_UT_ASSERT( TT->t_un.u_tag   ==  T.t_un.u_tag);
	M0_UT_ASSERT( TT->t_un.u.u_y   ==  T.t_un.u.u_y);
	M0_UT_ASSERT(*TT->t_opaq.o_32  == *T.t_opaq.o_32);

	m0_xcode_free_obj(&decoded);
}

enum {
	FSIZE = sizeof(uint64_t) + sizeof(uint64_t)
};

static char             foo_buf[FSIZE];
static void            *foo_addr  = foo_buf;
static m0_bcount_t      foo_count = ARRAY_SIZE(foo_buf);
static struct m0_bufvec foo_bvec  = M0_BUFVEC_INIT_BUF(&foo_addr, &foo_count);

static int foo_length(struct m0_xcode_ctx *ctx, const void *obj)
{
	return ARRAY_SIZE(foo_buf);
}

static void foo_xor(char *buf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(foo_buf); ++i)
		buf[i] ^= 42;
}

static int foo_encode(struct m0_xcode_ctx *ctx, const void *obj)
{
	struct m0_bufvec_cursor cur;

	m0_bufvec_cursor_init(&cur, &foo_bvec);
	memcpy(foo_buf, obj, sizeof(struct foo));
	foo_xor(foo_buf);
	return m0_bufvec_cursor_copy(&ctx->xcx_buf, &cur, FSIZE) != FSIZE ?
		-EPROTO : 0;
}

static int foo_decode(struct m0_xcode_ctx *ctx, void *obj)
{
	struct m0_bufvec_cursor cur;

	m0_bufvec_cursor_init(&cur, &foo_bvec);
	if (m0_bufvec_cursor_copy(&cur, &ctx->xcx_buf, FSIZE) == FSIZE) {
		foo_xor(foo_buf);
		memcpy(obj, foo_buf, sizeof(struct foo));
		return 0;
	} else
		return -EPROTO;
}

static const struct m0_xcode_type_ops foo_ops = {
	.xto_length = foo_length,
	.xto_encode = foo_encode,
	.xto_decode = foo_decode
};

static void xcode_nonstandard_test(void)
{
	int result;

	xut_foo.xt.xct_ops = &foo_ops;

	m0_xcode_ctx_init(&ctx, &(struct m0_xcode_obj){ &xut_top.xt, &T });
	result = m0_xcode_length(&ctx);
	M0_UT_ASSERT(result == sizeof TD);

	m0_xcode_ctx_init(&ctx, &(struct m0_xcode_obj){ &xut_top.xt, &T });
	m0_bufvec_cursor_init(&ctx.xcx_buf, &bvec);
	result = m0_xcode_encode(&ctx);
	M0_UT_ASSERT(result == 0);

	foo_xor(ebuf);
	M0_UT_ASSERT(memcmp(&TD, ebuf, sizeof TD) == 0);
	foo_xor(ebuf);
	xcode_decode_test();
	xut_foo.xt.xct_ops = NULL;
}

static void xcode_cmp_test(void)
{
	struct m0_xcode_obj obj0;
	struct m0_xcode_obj obj1;
	struct top *t0;
	struct top *t1;
	int    cmp;

	xcode_encode_test();

	decode(&obj0);
	decode(&obj1);

	t0 = obj0.xo_ptr;
	t1 = obj1.xo_ptr;

	cmp = m0_xcode_cmp(&obj0, &obj0);
	M0_UT_ASSERT(cmp == 0);

	cmp = m0_xcode_cmp(&obj0, &obj1);
	M0_UT_ASSERT(cmp == 0);

	cmp = m0_xcode_cmp(&obj1, &obj0);
	M0_UT_ASSERT(cmp == 0);

	t1->t_foo.f_x--;
	cmp = m0_xcode_cmp(&obj0, &obj1);
	M0_UT_ASSERT(cmp > 0);
	cmp = m0_xcode_cmp(&obj1, &obj0);
	M0_UT_ASSERT(cmp < 0);

	t1->t_foo.f_x++;
	cmp = m0_xcode_cmp(&obj0, &obj1);
	M0_UT_ASSERT(cmp == 0);

	t1->t_v.v_data[0] = 'J';
	cmp = m0_xcode_cmp(&obj0, &obj1);
	M0_UT_ASSERT(cmp < 0);
	t1->t_v.v_data[0] = t0->t_v.v_data[0];

	t1->t_v.v_nr++;
	cmp = m0_xcode_cmp(&obj0, &obj1);
	M0_UT_ASSERT(cmp < 0);
	cmp = m0_xcode_cmp(&obj1, &obj0);
	M0_UT_ASSERT(cmp > 0);
	t1->t_v.v_nr--;

	m0_xcode_free_obj(&obj0);
	m0_xcode_free_obj(&obj1);
}

#define OBJ(xt, ptr) (&(struct m0_xcode_obj){ .xo_type = (xt), .xo_ptr = (ptr) })

static void xcode_read_test(void)
{
	int        result;
	struct foo F;
	struct un  U;
	struct v   V;
	struct top T;

	M0_SET0(&F);
	result = m0_xcode_read(OBJ(&xut_foo.xt, &F), "(10, 0xff)");
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(F.f_x == 10);
	M0_UT_ASSERT(F.f_y == 0xff);

	M0_SET0(&F);
	result = m0_xcode_read(OBJ(&xut_foo.xt, &F), " ( 10 , 0xff ) ");
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(F.f_x == 10);
	M0_UT_ASSERT(F.f_y == 0xff);

	M0_SET0(&F);
	result = m0_xcode_read(OBJ(&xut_foo.xt, &F), "(10,010)");
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(F.f_x == 10);
	M0_UT_ASSERT(F.f_y == 8);

	M0_SET0(&F);
	result = m0_xcode_read(OBJ(&xut_foo.xt, &F), " ( 10 , 0xff ) rest");
	M0_UT_ASSERT(result == -EINVAL);

	M0_SET0(&F);
	result = m0_xcode_read(OBJ(&xut_foo.xt, &F), "(10,)");
	M0_UT_ASSERT(result == -EPROTO);

	M0_SET0(&F);
	result = m0_xcode_read(OBJ(&xut_foo.xt, &F), "(10 12)");
	M0_UT_ASSERT(result == -EPROTO);

	M0_SET0(&F);
	result = m0_xcode_read(OBJ(&xut_foo.xt, &F), "()");
	M0_UT_ASSERT(result == -EPROTO);

	M0_SET0(&F);
	result = m0_xcode_read(OBJ(&xut_foo.xt, &F), "");
	M0_UT_ASSERT(result == -EPROTO);

	M0_SET0(&U);
	result = m0_xcode_read(OBJ(&xut_un.xt, &U), "{1| 42}");
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(U.u_tag == 1);
	M0_UT_ASSERT(U.u.u_x == 42);

	M0_SET0(&U);
	result = m0_xcode_read(OBJ(&xut_un.xt, &U), "{4| 8}");
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(U.u_tag == 4);
	M0_UT_ASSERT(U.u.u_y == 8);

	M0_SET0(&U);
	result = m0_xcode_read(OBJ(&xut_un.xt, &U), "{3| 0}");
	M0_UT_ASSERT(result == -EPROTO);

	M0_SET0(&U);
	result = m0_xcode_read(OBJ(&xut_un.xt, &U), "{3}");
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(U.u_tag == 3);

	M0_SET0(&V);
	result = m0_xcode_read(OBJ(&xut_v.xt, &V), "[0]");
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(V.v_nr == 0);

	M0_SET0(&V);
	result = m0_xcode_read(OBJ(&xut_v.xt, &V), "[1: 42]");
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(V.v_nr == 1);
	M0_UT_ASSERT(V.v_data[0] == 42);

	result = m0_xcode_read(OBJ(&xut_v.xt, &V), "[3: 42, 43, 44]");
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(V.v_nr == 3);
	M0_UT_ASSERT(V.v_data[0] == 42);
	M0_UT_ASSERT(V.v_data[1] == 43);
	M0_UT_ASSERT(V.v_data[2] == 44);

	M0_SET0(&V);
	result = m0_xcode_read(OBJ(&xut_v.xt, &V), "\"a\"");
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(V.v_nr == 1);
	M0_UT_ASSERT(strncmp(V.v_data, "a", 1) == 0);

	M0_SET0(&V);
	result = m0_xcode_read(OBJ(&xut_v.xt, &V), "\"abcdef\"");
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(V.v_nr == 6);
	M0_UT_ASSERT(strncmp(V.v_data, "abcdef", 6) == 0);

	M0_SET0(&V);
	result = m0_xcode_read(OBJ(&xut_v.xt, &V), "\"\"");
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(V.v_nr == 0);

	M0_SET0(&V);
	result = m0_xcode_read(OBJ(&xut_v.xt, &V), "\"");
	M0_UT_ASSERT(result == -EPROTO);

	M0_SET0(&T);
	result = m0_xcode_read(OBJ(&xut_top.xt, &T), ""
"((1, 2),"
" 8,"
" [4: 1, 2, 3, 4],"
" 4,"
" {1| 42},"
" 7)");
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(memcmp(&T, &(struct top){
		.t_foo  = { 1, 2 },
		.t_flag = 8,
		.t_v    = { .v_nr = 4, .v_data = T.t_v.v_data },
		.t_def  = 4,
		.t_un   = { .u_tag = 1, .u = { .u_x = 42 }},
		.t_opaq = { .o_32 = T.t_opaq.o_32 }}, sizeof T) == 0);
}

#ifndef __KERNEL__
static void xcode_print_test(void)
{
	char buf[300];
	const char *s0;
	const char *s1;
	int         rc;
	char        data[] = { 1, 2, 3, 4 };
	uint64_t    o64    = 7;
	struct top  T = (struct top){
		.t_foo  = { 1, 2 },
		.t_flag = 8,
		.t_v    = { .v_nr = 4, .v_data = data },
		.t_def  = 4,
		.t_un   = { .u_tag = 1, .u = { .u_x = 42 }},
		.t_opaq = { .o_64 = &o64 }
	};

	rc = m0_xcode_print(OBJ(&xut_top.xt, &T), buf, ARRAY_SIZE(buf));
	M0_UT_ASSERT(rc == strlen(buf));
	for (s0 = buf, s1 = ""
		     "((1, 2),"
		     " 8,"
		     " [4: 1, 2, 3, 4],"
		     " 4,"
		     " {1| 2a},"
		     " 7)"; *s0 != 0 && *s1 != 0; ++s0, ++s1) {
		while (isspace(*s0))
			++s0;
		while (isspace(*s1))
			++s1;
		M0_UT_ASSERT(*s0 == *s1);
	}
	M0_UT_ASSERT(*s0 == 0);
	M0_UT_ASSERT(*s1 == 0);

	rc = m0_xcode_print(OBJ(&xut_top.xt, &T), NULL, 0);
	M0_UT_ASSERT(rc == strlen(buf));
}
#endif

static void xcode_find_test(void)
{
	struct m0_xcode_obj top = { &xut_top.xt, &T };
	void               *place;
	int                 result;

	result = m0_xcode_find(&top, &xut_top.xt, &place);
	M0_UT_ASSERT(result == 0 && place == &T);

	result = m0_xcode_find(&top, &xut_foo.xt, &place);
	M0_UT_ASSERT(result == 0 && place == &T.t_foo);

	result = m0_xcode_find(&top, &xut_v.xt, &place);
	M0_UT_ASSERT(result == 0 && place == &T.t_v);

	result = m0_xcode_find(&top, &M0_XT_U64, &place);
	M0_UT_ASSERT(result == 0 && place == &T.t_foo.f_x);

	result = m0_xcode_find(&top, &M0_XT_U32, &place);
	M0_UT_ASSERT(result == 0 && place == &T.t_flag);

	result = m0_xcode_find(&top, &M0_XT_VOID, &place);
	M0_UT_ASSERT(result == -ENOENT);
}

struct m0_ut_suite xcode_ut = {
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
#ifndef __KERNEL__
		{ "xcode-print",  xcode_print_test },
#endif
		{ "xcode-find",   xcode_find_test },
                { NULL, NULL }
        }
};
M0_EXPORTED(xcode_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
