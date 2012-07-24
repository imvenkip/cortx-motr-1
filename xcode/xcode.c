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

#include "lib/bob.h"
#include "lib/misc.h"                           /* C2_SET0 */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"

#include "xcode/xcode.h"

/**
   @addtogroup xcode

   @{
 */

static bool field_invariant(const struct c2_xcode_type *xt,
			    const struct c2_xcode_field *field)
{
	return
		field->xf_name != NULL && field->xf_type != NULL &&
		ergo(xt == &C2_XT_OPAQUE, field->xf_opaque != NULL) &&
		ergo(!(field->xf_type->xct_aggr == C2_XA_SEQUENCE ||
		       xt->xct_aggr == C2_XA_SEQUENCE || field->xf_type !=
		       C2_XAT_VOID),
		     field->xf_offset + field->xf_type->xct_sizeof <=
		     xt->xct_sizeof);
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

	for (i = 0, offset = 0; i < xt->xct_nr; ++i) {
		const struct c2_xcode_field *f;

		f = &xt->xct_child[i];
		if (!field_invariant(xt, f))
			return false;
		/* field doesn't overlap with the previous one */
		if (i > 0 && offset +
		    xt->xct_child[i - 1].xf_type->xct_sizeof > f->xf_offset)
			return false;
		/* update the previous field offset: for UNION all branches
		   follow the first field. */
		if (i == 0 || xt->xct_aggr != C2_XA_UNION)
			offset = f->xf_offset;
	}
	switch (xt->xct_aggr) {
	case C2_XA_RECORD:
	case C2_XA_TYPEDEF:
		break;
	case C2_XA_UNION:
	case C2_XA_SEQUENCE:
		if (xt->xct_child[0].xf_type->xct_aggr != C2_XA_ATOM)
			return false;
		break;
	case C2_XA_OPAQUE:
		if (xt != &C2_XT_OPAQUE)
			return false;
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

#include "xcode/cursor.c"

enum xcode_op {
	XO_ENC,
	XO_DEC,
	XO_LEN,
	XO_NR
};

/**
   Handles memory allocation during decoding.

   This function takes an xcode iteration cursor and, if necessary, allocates
   memory where the object currently being decoded will reside.

   The pointer to the allocated memory is returned in c2_xcode_obj::xo_ptr. In
   addition, this pointer is stored at the appropriate offset in the parent
   object.
 */
static ssize_t xcode_alloc(struct c2_xcode_ctx *ctx)
{
	const struct c2_xcode_cursor_frame *prev;
	const struct c2_xcode_obj          *par;
	const struct c2_xcode_type         *xt;
	const struct c2_xcode_type         *pt;
	struct c2_xcode_cursor_frame       *top;
	struct c2_xcode_obj                *obj;
	size_t                              nob;
	size_t                              size;
	void                              **slot;

	/*
	 * New memory has to be allocated in 3 cases:
	 *
	 * - to decode topmost object (this is different from sunrpc XDR
	 *   interfaces, where topmost object is pre-allocated by the caller);
	 *
	 * - to store an array: a SEQUENCE object has the following in-memory
	 *   structure:
	 *
	 *       struct  {
	 *               scalar_t     count;
	 *               struct elem *data;
	 *
	 *       };
	 *
	 *   This function allocates count * sizeof(struct elem) bytes to hold
	 *   "data";
	 *
	 * - to store an object pointed to by an opaque pointer.
	 *
	 */

	nob  = 0;
	top  = c2_xcode_cursor_top(&ctx->xcx_it);
	prev = top - 1;
	obj  = &top->s_obj;  /* an object being decoded */
	par  = &prev->s_obj; /* obj's parent object */
	xt   = obj->xo_type;
	pt   = par->xo_type;
	size = xt->xct_sizeof;

	if (ctx->xcx_it.xcu_depth == 0) {
		/* allocate top-most object */
		nob = size;
		slot = &obj->xo_ptr;
	} else {
		if (pt->xct_aggr == C2_XA_SEQUENCE &&
		    prev->s_fieldno == 1 && prev->s_elno == 0)
			/* allocate array */
			nob = c2_xcode_tag(par) * size;
		else if (pt->xct_child[prev->s_fieldno].xf_type == &C2_XT_OPAQUE)
			/*
			 * allocate the object referenced by an opaque
			 * pointer. At this moment "xt" is the type of the
			 * pointed object.
			 */
			nob = size;
		slot = c2_xcode_addr(par, prev->s_fieldno, ~0ULL);
	}

	if (nob != 0) {
		C2_ASSERT(obj->xo_ptr == NULL);

		obj->xo_ptr = *slot = ctx->xcx_alloc(ctx, nob);
		if (obj->xo_ptr == NULL)
			return -ENOMEM;
	}
	return 0;
}

/**
   Common xcoding function, implementing encoding, decoding and sizing.
 */
static int ctx_walk(struct c2_xcode_ctx *ctx, enum xcode_op op)
{
	void                   *ptr;
	c2_bcount_t             size;
	int                     length = 0;
	int                     result;
	struct c2_bufvec        area   = C2_BUFVEC_INIT_BUF(&ptr, &size);
	struct c2_bufvec_cursor mem;
	struct c2_xcode_cursor *it     = &ctx->xcx_it;

	while ((result = c2_xcode_next(it)) > 0) {
		const struct c2_xcode_type     *xt;
		const struct c2_xcode_type_ops *ops;
		struct c2_xcode_obj            *cur;
		struct c2_xcode_cursor_frame   *top;

		top = c2_xcode_cursor_top(it);

		if (top->s_flag != C2_XCODE_CURSOR_PRE)
			continue;

		cur = &top->s_obj;

		if (op == XO_DEC) {
			result = xcode_alloc(ctx);
			if (result != 0)
				return result;
		}

		xt  = cur->xo_type;
		ptr = cur->xo_ptr;
		ops = xt->xct_ops;

		if (ops != NULL) {
			switch (op) {
			case XO_ENC:
				if (ops->xto_encode != NULL)
					result = ops->xto_encode(ctx, ptr);
				break;
			case XO_DEC:
				if (ops->xto_decode != NULL)
					result = ops->xto_decode(ctx, ptr);
				break;
			case XO_LEN:
				if (ops->xto_length != NULL)
					length += ops->xto_length(ctx, ptr);
				break;
			default:
				C2_IMPOSSIBLE("op");
			}
			c2_xcode_skip(it);
		} else if (xt->xct_aggr == C2_XA_ATOM) {
			size = xt->xct_sizeof;

			if (op == XO_LEN)
				length += size;
			else {
				struct c2_bufvec_cursor *src;
				struct c2_bufvec_cursor *dst;

				c2_bufvec_cursor_init(&mem, &area);
				/* XXX endianness and sharing */
				switch (op) {
				case XO_ENC:
					src = &mem;
					dst = &ctx->xcx_buf;
					break;
				case XO_DEC:
					dst = &mem;
					src = &ctx->xcx_buf;
					break;
				default:
					C2_IMPOSSIBLE("op");
				}
				if (c2_bufvec_cursor_copy(dst,
							  src, size) != size)
					result = -EPROTO;
			}
		}
		if (result < 0)
			break;
	}
	if (result > 0)
		result = 0;
	if (op == XO_LEN)
		result = result ?: length;
	return result;
}

void c2_xcode_ctx_init(struct c2_xcode_ctx *ctx, const struct c2_xcode_obj *obj)
{
	C2_SET0(ctx);
	c2_xcode_cursor_top(&ctx->xcx_it)->s_obj = *obj;
}

int c2_xcode_decode(struct c2_xcode_ctx *ctx)
{
	return ctx_walk(ctx, XO_DEC);
}

int c2_xcode_encode(struct c2_xcode_ctx *ctx)
{
	return ctx_walk(ctx, XO_ENC);
}

int c2_xcode_length(struct c2_xcode_ctx *ctx)
{
	return ctx_walk(ctx, XO_LEN);
}

void *c2_xcode_alloc(struct c2_xcode_ctx *ctx, size_t nob)
{
	return c2_alloc(nob);
}

void *c2_xcode_addr(const struct c2_xcode_obj *obj, int fileno, uint64_t elno)
{
	char                        *addr = (char *)obj->xo_ptr;
	const struct c2_xcode_type  *xt   = obj->xo_type;
	const struct c2_xcode_field *f    = &xt->xct_child[fileno];
	const struct c2_xcode_type  *ct   = f->xf_type;

	C2_ASSERT(fileno < xt->xct_nr);
	if (addr != NULL) {
		addr += f->xf_offset;
		if (xt->xct_aggr == C2_XA_SEQUENCE && fileno == 1 &&
		    elno != ~0ULL)
			addr = *((char **)addr) + elno * ct->xct_sizeof;
		else if (ct == &C2_XT_OPAQUE && elno != ~0ULL)
			addr = *((char **)addr);
	}

	return addr;
}

int c2_xcode_subobj(struct c2_xcode_obj *subobj, const struct c2_xcode_obj *obj,
		    int fieldno, uint64_t elno)
{
	const struct c2_xcode_field *f;
	int                          result;

	C2_PRE(0 <= fieldno && fieldno < obj->xo_type->xct_nr);

	f = &obj->xo_type->xct_child[fieldno];

	subobj->xo_ptr = c2_xcode_addr(obj, fieldno, elno);
	if (f->xf_type == &C2_XT_OPAQUE) {
		result = f->xf_opaque(obj, &subobj->xo_type);
	} else {
		subobj->xo_type = f->xf_type;
		result = 0;
	}
	return result;
}

uint64_t c2_xcode_tag(const struct c2_xcode_obj *obj)
{
	const struct c2_xcode_type  *xt = obj->xo_type;
	const struct c2_xcode_field *f  = &xt->xct_child[0];
	uint64_t                     tag;

	C2_PRE(xt->xct_aggr == C2_XA_SEQUENCE || xt->xct_aggr == C2_XA_UNION);
	C2_PRE(f->xf_type->xct_aggr == C2_XA_ATOM);

	if (obj->xo_ptr != NULL) {
		switch (f->xf_type->xct_atype) {
		case C2_XAT_VOID:
			tag = f->xf_tag;
			break;
		case C2_XAT_U8:
			tag = *C2_XCODE_VAL(obj, 0, 0, uint8_t);
			break;
		case C2_XAT_U32:
			tag = *C2_XCODE_VAL(obj, 0, 0, uint32_t);
			break;
		case C2_XAT_U64:
			tag = *C2_XCODE_VAL(obj, 0, 0, uint64_t);
			break;
		default:
			C2_IMPOSSIBLE("atype");

		}
	} else
		tag = 0;

	return tag;
}

void c2_xcode_bob_type_init(struct c2_bob_type *bt,
			    const struct c2_xcode_type *xt,
			    size_t magix_field, uint64_t magix)
{
	const struct c2_xcode_field *mf = &xt->xct_child[magix_field];

	C2_PRE(magix_field < xt->xct_nr);
	C2_PRE(xt->xct_aggr == C2_XA_RECORD);
	C2_PRE(mf->xf_type == &C2_XT_U64);

	bt->bt_name         = xt->xct_name;
	bt->bt_magix        = magix;
	bt->bt_magix_offset = mf->xf_offset;
}

const struct c2_xcode_type C2_XT_VOID = {
	.xct_aggr   = C2_XA_ATOM,
	.xct_name   = "void",
	.xct_atype  = C2_XAT_VOID,
	.xct_sizeof = sizeof(void),
	.xct_nr     = 0
};

const struct c2_xcode_type C2_XT_U8 = {
	.xct_aggr   = C2_XA_ATOM,
	.xct_name   = "u8",
	.xct_atype  = C2_XAT_U8,
	.xct_sizeof = sizeof(uint8_t),
	.xct_nr     = 0
};

const struct c2_xcode_type C2_XT_U32 = {
	.xct_aggr   = C2_XA_ATOM,
	.xct_name   = "u32",
	.xct_atype  = C2_XAT_U32,
	.xct_sizeof = sizeof(uint32_t),
	.xct_nr     = 0
};

const struct c2_xcode_type C2_XT_U64 = {
	.xct_aggr   = C2_XA_ATOM,
	.xct_name   = "u64",
	.xct_atype  = C2_XAT_U64,
	.xct_sizeof = sizeof(uint64_t),
	.xct_nr     = 0
};

const struct c2_xcode_type C2_XT_OPAQUE = {
	.xct_aggr   = C2_XA_OPAQUE,
	.xct_name   = "opaque",
	.xct_sizeof = sizeof (void *),
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
	[C2_XAT_U8]   = "u8",
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
