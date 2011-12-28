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

#include <stdio.h>                          /* printf */

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

static struct static_xt xut_opaq = {
	.xt = {
		.xct_aggr   = C2_XA_OPAQUE,
		.xct_name   = "opaq",
		.xct_sizeof = sizeof (void *),
		.xct_nr     = 0
	}
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

static const struct c2_xcode_type *opaq_type(const struct c2_xcode_obj *par)
{
	struct top *t = par->xo_ptr;

	C2_UT_ASSERT(par->xo_type == &xut_top.xt);

	return t->t_flag == 0xf ? &C2_XT_U32 : &C2_XT_U64;
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
		.xf_type   = &C2_XT_BYTE,
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
		.xf_type   = &xut_opaq.xt,
		.xf_u      = { .u_type = opaq_type },
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
		.xf_u      = { .u_tag = 1 },
		.xf_offset = offsetof(struct un, u.u_x)
	};
	xut_un.xt.xct_child[2] = (struct c2_xcode_field){
		.xf_name   = "u_y",
		.xf_type   = &C2_XT_BYTE,
		.xf_u      = { .u_tag = 4 },
		.xf_offset = offsetof(struct un, u.u_y)
	};
	return 0;
}

__attribute__((unused)) static void it_print(const struct c2_xcode_cursor *it)
{
	int i;
	const struct c2_xcode_cursor_frame *f;

	for (i = 0, f = &it->xcu_stack[0]; i < it->xcu_depth; ++i, ++f) {
		printf(".%s[%u]",
		       f->s_obj.xo_type->xct_child[f->s_fieldno].xf_name,
		       f->s_elno);
	}
	printf(":%s ", c2_xcode_aggr_name[f->s_obj.xo_type->xct_aggr]);
	if (f->s_obj.xo_type->xct_aggr == C2_XA_ATOM) {
		switch (f->s_obj.xo_type->xct_atype) {
		case C2_XAT_VOID:
			printf("void");
			break;
		case C2_XAT_BYTE:
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

	f   = &it->xcu_stack[it->xcu_depth];
	obj = &f->s_obj;

	C2_UT_ASSERT(obj->xo_type == xt);
	C2_UT_ASSERT(obj->xo_ptr  == addr);
	C2_UT_ASSERT(f->s_fieldno == fieldno);
	C2_UT_ASSERT(f->s_elno    == elno);
	C2_UT_ASSERT(f->s_flag    == flag);
}

static void xcode_cursor_test(void)
{
	struct top T;
	struct c2_xcode_cursor it;
	char data[] = "Hello, world!\n";
	int i;

	T.t_foo.f_x = 7;
	T.t_foo.f_y = 8;
	T.t_flag    = 0xF;
	T.t_v.v_nr    = sizeof data;
	T.t_v.v_data  = data;
	T.t_un.u_tag  = 4;

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
		chk(&it, 2, &C2_XT_BYTE,
		    &T.t_v.v_data[i], 0, 0, C2_XCODE_CURSOR_PRE);
		chk(&it, 2, &C2_XT_BYTE,
		    &T.t_v.v_data[i], 0, 0, C2_XCODE_CURSOR_POST);
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
	chk(&it, 2, &C2_XT_BYTE, &T.t_un.u.u_y, 0, 0, C2_XCODE_CURSOR_PRE);
	chk(&it, 2, &C2_XT_BYTE, &T.t_un.u.u_y, 0, 0, C2_XCODE_CURSOR_POST);
	chk(&it, 1, &xut_un.xt, &T.t_un, 2, 0, C2_XCODE_CURSOR_IN);
	chk(&it, 1, &xut_un.xt, &T.t_un, 3, 0, C2_XCODE_CURSOR_POST);
	chk(&it, 0, &xut_top.xt, &T, 4, 0, C2_XCODE_CURSOR_IN);
	chk(&it, 1, &C2_XT_U32, &T.t_opaq.o_32, 0, 0, C2_XCODE_CURSOR_PRE);
	chk(&it, 1, &C2_XT_U32, &T.t_opaq.o_32, 0, 0, C2_XCODE_CURSOR_POST);
	chk(&it, 0, &xut_top.xt, &T, 5, 0, C2_XCODE_CURSOR_IN);
	chk(&it, 0, &xut_top.xt, &T, 6, 0, C2_XCODE_CURSOR_POST);

	C2_UT_ASSERT(c2_xcode_next(&it) == 0);
}

const struct c2_test_suite xcode_ut = {
        .ts_name = "xcode-ut",
        .ts_init = xcode_init,
        .ts_fini = NULL,
        .ts_tests = {
                { "xcode-cursor", xcode_cursor_test },
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


