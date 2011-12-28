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
 * Original creation date: 25-Dec-2011
 */

#include "lib/errno.h"
#include "lib/assert.h"

#include "xcode/xcode.h"

/**
   @addtogroup xcode

   @{
 */

static bool field_invariant(const struct c2_xcode_type *xt,
			    const struct c2_xcode_field *field)
{
	return ergo(xt->xct_aggr == C2_XA_OPAQUE, field->xf_u.u_type != NULL);
}

bool c2_xcode_type_invariant(const struct c2_xcode_type *xt)
{
	size_t   i;
	uint32_t offset;

	static const size_t min[C2_XA_NR] = {
		[C2_XA_RECORD]   = 0,
		[C2_XA_UNION]    = 2,
		[C2_XA_SEQUENCE] = 2,
		[C2_XA_TYPEDEF]  = 1,
		[C2_XA_OPAQUE]   = 0,
		[C2_XA_ATOM]     = 0
	};

	static const size_t max[C2_XA_NR] = {
		[C2_XA_RECORD]   = ~0ULL,
		[C2_XA_UNION]    = ~0ULL,
		[C2_XA_SEQUENCE] = 2,
		[C2_XA_TYPEDEF]  = 1,
		[C2_XA_OPAQUE]   = 0,
		[C2_XA_ATOM]     = 0
	};

	if (!(0 <= xt->xct_aggr && xt->xct_aggr < C2_XA_NR))
		return false;

	if (xt->xct_nr < min[xt->xct_aggr] || xt->xct_nr > max[xt->xct_aggr])
		return false;

	for (i = 0; i < xt->xct_nr; ++i) {
		const struct c2_xcode_field *f;

		f = &xt->xct_child[i];
		if (f->xf_name == NULL || f->xf_type == NULL)
			return false;
		if (!field_invariant(xt, f))
			return false;
		if (i > 0 && f->xf_offset < offset)
			return false;
		offset = f->xf_offset;
		if (offset > xt->xct_sizeof)
			return false;
	}
	switch (xt->xct_aggr) {
	case C2_XA_RECORD:
	case C2_XA_TYPEDEF:
		break;
	case C2_XA_UNION:
	case C2_XA_SEQUENCE:
		if (xt->xct_child[0].xf_type != &C2_XT_U32)
			return false;
		break;
	case C2_XA_OPAQUE:
		if (xt->xct_sizeof != sizeof (void *))
			return false;
		break;
	case C2_XA_ATOM:
		if (!(0 <= xt->xct_atype && xt->xct_atype < C2_XAT_NR))
			return false;
		break;
	default:
		return false;
	}
	return true;
}

enum xcode_op {
	XO_ENC,
	XO_DEC,
	XO_LEN,
	XO_NR
};

struct c2_walk_ctx {
	struct c2_xcode_ctx        *wc_p;
	enum xcode_op               wc_op;
	struct c2_xcode_obj        *wc_obj;
	c2_bcount_t                *wc_len;
};

static int (*ctx_disp[C2_XA_NR])(struct c2_walk_ctx *wc);

static int ctx_walk(struct c2_walk_ctx *wc)
{
	const struct c2_xcode_type_ops *ops;

	C2_ASSERT(c2_xcode_type_invariant(wc->wc_obj->xo_type));

	ops = wc->wc_obj->xo_type->xct_ops;
	if (ops != NULL) {
		void *addr = wc->wc_obj->xo_ptr;

		switch (wc->wc_op) {
		case XO_ENC:
			if (ops->xto_encode != NULL)
				return ops->xto_encode(wc->wc_p, addr);
			break;
		case XO_DEC:
			if (ops->xto_decode != NULL)
				return ops->xto_decode(wc->wc_p, addr);
			break;
		case XO_LEN:
			if (ops->xto_length != NULL)
				return ops->xto_length(wc->wc_p, addr);
			break;
		default:
			C2_IMPOSSIBLE("op");
		}
	}
	return ctx_disp[wc->wc_obj->xo_type->xct_aggr](wc);
}

static int xcode_op(struct c2_xcode_ctx *ctx, struct c2_xcode_obj *obj,
		    enum xcode_op op, c2_bcount_t *len)
{
	struct c2_walk_ctx wc = {
		.wc_p   = ctx,
		.wc_op  = op,
		.wc_obj = obj,
		.wc_len = len
	};
	return ctx_walk(&wc);
}

static int field_walk(struct c2_walk_ctx *wc, int fieldno, uint32_t elno)
{
	struct c2_xcode_obj subobj;
	struct c2_walk_ctx  subctx = {
		.wc_p   = wc->wc_p,
		.wc_op  = wc->wc_op,
		.wc_obj = &subobj
	};
	c2_xcode_subobj(&subobj, wc->wc_obj, fieldno, elno);
	return ctx_walk(&subctx);
}

static int record(struct c2_walk_ctx *wc)
{
	size_t i;
	int    result;

	for (i = 0, result = 0;
	     i < wc->wc_obj->xo_type->xct_nr && result == 0; ++i)
		result = field_walk(wc, i, 0);
	return result;
}

static uint32_t head_value(const struct c2_walk_ctx *wc)
{
	return c2_xcode_tag(wc->wc_obj);
}

static int head(struct c2_walk_ctx *wc)
{
	return field_walk(wc, 0, 0);
}

static int union_walk(struct c2_walk_ctx *wc)
{
	uint32_t discr;
	size_t   i;
	int      result;

	result = head(wc);
	if (result != 0)
		return result;
	discr = head_value(wc);
	for (i = 1; i < wc->wc_obj->xo_type->xct_nr; ++i) {
		if (wc->wc_obj->xo_type->xct_child[i].xf_u.u_tag == discr)
			return field_walk(wc, i, 0);
	}
	return -EPROTO;
}

static int sequence(struct c2_walk_ctx *wc)
{
	uint32_t i;
	uint32_t nr;
	int      result;

	result = head(wc);
	if (result != 0)
		return result;
	nr = head_value(wc);
	for (i = 0, result = 0; i < nr && result == 0; ++i)
		result = field_walk(wc, 1, i);
	return result;
}

static int opaque(struct c2_walk_ctx *wc)
{
	C2_IMPOSSIBLE("opaque");
}

static int data_get(struct c2_bufvec_cursor *src, void *ptr, c2_bcount_t nob)
{
	struct c2_bufvec        bv = C2_BUFVEC_INIT_BUF(&ptr, &nob);
	struct c2_bufvec_cursor dst;

	c2_bufvec_cursor_init(&dst, &bv);
	return c2_bufvec_cursor_copy(&dst, src, nob) == nob ? 0 : -EPROTO;
}

static int data_put(struct c2_bufvec_cursor *dst, void *ptr, c2_bcount_t nob)
{
	struct c2_bufvec        bv = C2_BUFVEC_INIT_BUF(&ptr, &nob);
	struct c2_bufvec_cursor src;

	c2_bufvec_cursor_init(&src, &bv);
	return c2_bufvec_cursor_copy(dst, &src, nob) == nob ? 0 : -EPROTO;
}

static int a_void(struct c2_xcode_ctx *ctx, void *obj)
{
	return 0;
}

static int a_byte_enc(struct c2_xcode_ctx *ctx, void *obj)
{
	return data_put(&ctx->xcx_it, obj, 1);
}

static int a_byte_dec(struct c2_xcode_ctx *ctx, void *obj)
{
	return data_get(&ctx->xcx_it, obj, 1);
}

static int a_u32_enc(struct c2_xcode_ctx *ctx, void *obj)
{
	uint32_t datum = *(uint32_t *)obj;

	/* XXX endianness */
	datum = datum;
	return data_put(&ctx->xcx_it, &datum, 4);
}

static int a_u32_dec(struct c2_xcode_ctx *ctx, void *obj)
{
	uint32_t datum;
	int      result;

	result = data_get(&ctx->xcx_it, &datum, 4);
	if (result == 0) {
		/* XXX endianness */
		datum = datum;
		*(uint32_t *)obj = datum;
	}
	return result;
}

static int a_u64_enc(struct c2_xcode_ctx *ctx, void *obj)
{
	uint64_t datum = *(uint64_t *)obj;

	/* XXX endianness */
	datum = datum;
	return data_put(&ctx->xcx_it, &datum, 8);
}

static int a_u64_dec(struct c2_xcode_ctx *ctx, void *obj)
{
	uint64_t datum;
	int      result;

	result = data_get(&ctx->xcx_it, &datum, 8);
	if (result == 0) {
		/* XXX endianness */
		datum = datum;
		*(uint64_t *)obj = datum;
	}
	return result;
}

static int (*atom_disp[C2_XAT_NR][XO_NR - 1])(struct c2_xcode_ctx *ctx,
					      void *obj) = {
	[C2_XAT_VOID] = { a_void, a_void },
	[C2_XAT_BYTE] = { a_byte_enc, a_byte_dec },
	[C2_XAT_U32]  = { a_u32_enc, a_u32_dec },
	[C2_XAT_U64]  = { a_u64_enc, a_u64_dec }
};

static int atom(struct c2_walk_ctx *wc)
{
	struct c2_xcode_obj        *obj;
	const struct c2_xcode_type *xt;

	obj = wc->wc_obj;
	xt  = obj->xo_type;

	if (wc->wc_op != XO_LEN) {
		return atom_disp[xt->xct_atype][wc->wc_op](wc->wc_p,
							   obj->xo_ptr);
	} else {
		wc->wc_len += xt->xct_sizeof;
		return 0;
	}
}

static int (*ctx_disp[C2_XA_NR])(struct c2_walk_ctx *wc) = {
	[C2_XA_RECORD]   = record,
	[C2_XA_UNION]    = union_walk,
	[C2_XA_SEQUENCE] = sequence,
	[C2_XA_TYPEDEF]  = head,
	[C2_XA_OPAQUE]   = opaque,
	[C2_XA_ATOM]     = atom
};

int c2_xcode_decode(struct c2_xcode_ctx *ctx, struct c2_xcode_obj *obj)
{
	return xcode_op(ctx, obj, XO_DEC, NULL);
}

int c2_xcode_encode(struct c2_xcode_ctx *ctx, const struct c2_xcode_obj *obj)
{
	return xcode_op(ctx, (struct c2_xcode_obj *)obj, XO_ENC, NULL);
}

int c2_xcode_length(struct c2_xcode_ctx *ctx, const struct c2_xcode_obj *obj)
{
	c2_bcount_t len = 0;

	return xcode_op(ctx, (struct c2_xcode_obj *)obj, XO_ENC, &len) ?: len;
}

void *c2_xcode_addr(const struct c2_xcode_obj *obj, int fileno, uint32_t elno)
{
	char                        *addr = (char *)obj->xo_ptr;
	const struct c2_xcode_type  *xt   = obj->xo_type;
	const struct c2_xcode_field *f    = &xt->xct_child[fileno];
	const struct c2_xcode_type  *ct   = f->xf_type;

	C2_ASSERT(fileno < xt->xct_nr);
	addr += f->xf_offset;
	if (xt->xct_aggr == C2_XA_SEQUENCE && fileno == 1 && elno != ~0)
		addr = *((char **)addr) + elno * ct->xct_sizeof;
	else if (ct->xct_aggr == C2_XA_OPAQUE)
		addr = *((char **)addr);
	return addr;
}

void c2_xcode_subobj(struct c2_xcode_obj *subobj, const struct c2_xcode_obj *obj,
		     int fieldno, uint32_t elno)
{
	const struct c2_xcode_field *f;

	C2_PRE(0 <= fieldno && fieldno < obj->xo_type->xct_nr);

	f = &obj->xo_type->xct_child[fieldno];

	subobj->xo_ptr  = c2_xcode_addr(obj, fieldno, elno);
	subobj->xo_type = f->xf_type->xct_aggr == C2_XA_OPAQUE ?
		f->xf_u.u_type(obj) : f->xf_type;
}

uint32_t c2_xcode_tag(struct c2_xcode_obj *obj)
{
	C2_PRE(obj->xo_type->xct_aggr == C2_XA_SEQUENCE ||
	       obj->xo_type->xct_aggr == C2_XA_UNION);

	return *C2_XCODE_VAL(obj, 0, 0, uint32_t);
}

const struct c2_xcode_type C2_XT_VOID = {
	.xct_aggr   = C2_XA_ATOM,
	.xct_name   = "void",
	.xct_atype  = C2_XAT_VOID,
	.xct_sizeof = 0,
	.xct_nr     = 0
};

const struct c2_xcode_type C2_XT_BYTE = {
	.xct_aggr   = C2_XA_ATOM,
	.xct_name   = "byte",
	.xct_atype  = C2_XAT_BYTE,
	.xct_sizeof = 1,
	.xct_nr     = 0
};

const struct c2_xcode_type C2_XT_U32 = {
	.xct_aggr   = C2_XA_ATOM,
	.xct_name   = "u32",
	.xct_atype  = C2_XAT_U32,
	.xct_sizeof = 4,
	.xct_nr     = 0
};

const struct c2_xcode_type C2_XT_U64 = {
	.xct_aggr   = C2_XA_ATOM,
	.xct_name   = "u64",
	.xct_atype  = C2_XAT_U64,
	.xct_sizeof = 8,
	.xct_nr     = 0
};

const char *c2_xcode_aggr_name[C2_XA_NR] = {
	[C2_XA_RECORD]   = "record",
	[C2_XA_UNION]    = "union",
	[C2_XA_SEQUENCE] = "sequence",
	[C2_XA_TYPEDEF]  = "typedef",
	[C2_XA_OPAQUE]   = "opaque",
	[C2_XA_ATOM]     = "atom"
};

const char *c2_xcode_atom_type_name[C2_XAT_NR] = {
	[C2_XAT_VOID] = "void",
	[C2_XAT_BYTE] = "byte",
	[C2_XAT_U32]  = "u32",
	[C2_XAT_U64]  = "u64",
};

const char *c2_xcode_endianness_name[C2_XEND_NR] = {
	[C2_XEND_LE] = "le",
	[C2_XEND_BE] = "be"
};

const char *c2_xcode_cursor_flag_name[C2_XCODE_CURSOR_NR] = {
	[C2_XCODE_CURSOR_NONE] = "none",
	[C2_XCODE_CURSOR_PRE]  = "pre",
	[C2_XCODE_CURSOR_IN]   = "in",
	[C2_XCODE_CURSOR_POST] = "post"
};

/** @} end of xcode group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
