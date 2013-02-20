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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 25-Dec-2011
 */

#include "lib/bob.h"
#include "lib/misc.h"                           /* M0_SET0 */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"

#include "xcode/xcode.h"

/**
   @addtogroup xcode

   @{
 */

static bool is_pointer(const struct m0_xcode_type *xt,
		       const struct m0_xcode_field *field)
{
	return xt->xct_aggr == M0_XA_SEQUENCE && field == &xt->xct_child[1];
}

static bool field_invariant(const struct m0_xcode_type *xt,
                            const struct m0_xcode_field *field)
{
        return
                field->xf_name != NULL && field->xf_type != NULL &&
                ergo(xt == &M0_XT_OPAQUE, field->xf_opaque != NULL) &&
                field->xf_offset +
                (is_pointer(xt, field) ?
                 sizeof(void *) : field->xf_type->xct_sizeof) <= xt->xct_sizeof;
}

M0_INTERNAL bool m0_xcode_type_invariant(const struct m0_xcode_type *xt)
{
	size_t   i;
	size_t   prev;
	uint32_t offset;

	static const size_t min[M0_XA_NR] = {
		[M0_XA_RECORD]   = 0,
		[M0_XA_UNION]    = 2,
		[M0_XA_SEQUENCE] = 2,
		[M0_XA_TYPEDEF]  = 1,
		[M0_XA_OPAQUE]   = 0,
		[M0_XA_ATOM]     = 0
	};

	static const size_t max[M0_XA_NR] = {
		[M0_XA_RECORD]   = ~0ULL,
		[M0_XA_UNION]    = ~0ULL,
		[M0_XA_SEQUENCE] = 2,
		[M0_XA_TYPEDEF]  = 1,
		[M0_XA_OPAQUE]   = 0,
		[M0_XA_ATOM]     = 0
	};

	if (!(0 <= xt->xct_aggr && xt->xct_aggr < M0_XA_NR))
		return false;

	if (xt->xct_nr < min[xt->xct_aggr] || xt->xct_nr > max[xt->xct_aggr])
		return false;

	for (i = 0, offset = 0, prev = 0; i < xt->xct_nr; ++i) {
		const struct m0_xcode_field *f;

		f = &xt->xct_child[i];
		if (!field_invariant(xt, f))
			return false;

		/* field doesn't overlap with the previous one */
		if (i > 0 && offset +
		    xt->xct_child[prev].xf_type->xct_sizeof > f->xf_offset)
			return false;

		/* update the previous field offset: for UNION all branches
		   follow the first field. */
		if (i == 0 || xt->xct_aggr != M0_XA_UNION) {
			offset = f->xf_offset;
			prev   = i;
		}
	}
	switch (xt->xct_aggr) {
	case M0_XA_RECORD:
	case M0_XA_TYPEDEF:
		break;
	case M0_XA_UNION:
	case M0_XA_SEQUENCE:
		if (xt->xct_child[0].xf_type->xct_aggr != M0_XA_ATOM)
			return false;
		break;
	case M0_XA_OPAQUE:
		if (xt != &M0_XT_OPAQUE)
			return false;
		if (xt->xct_sizeof != sizeof (void *))
			return false;
		break;
	case M0_XA_ATOM:
		if (!(0 <= xt->xct_atype && xt->xct_atype < M0_XAT_NR))
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

static void **allocp(struct m0_xcode_cursor *it, size_t *out)
{
	const struct m0_xcode_cursor_frame *prev;
	const struct m0_xcode_obj          *par;
	const struct m0_xcode_type         *xt;
	const struct m0_xcode_type         *pt;
	struct m0_xcode_cursor_frame       *top;
	struct m0_xcode_obj                *obj;
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
	top  = m0_xcode_cursor_top(it);
	prev = top - 1;
	obj  = &top->s_obj;  /* an object being decoded */
	par  = &prev->s_obj; /* obj's parent object */
	xt   = obj->xo_type;
	pt   = par->xo_type;
	size = xt->xct_sizeof;

	if (it->xcu_depth == 0) {
		/* allocate top-most object */
		nob = size;
		slot = &obj->xo_ptr;
	} else {
		if (pt->xct_aggr == M0_XA_SEQUENCE &&
		    prev->s_fieldno == 1 && prev->s_elno == 0 &&
		    m0_xcode_tag(par) > 0)
			/* allocate array */
			nob = m0_xcode_tag(par) * size;
		else if (pt->xct_child[prev->s_fieldno].xf_type == &M0_XT_OPAQUE)
			/*
			 * allocate the object referenced by an opaque
			 * pointer. At this moment "xt" is the type of the
			 * pointed object.
			 */
			nob = size;
		slot = m0_xcode_addr(par, prev->s_fieldno, ~0ULL);
	}
	*out = nob;
	return slot;
}

M0_INTERNAL ssize_t
m0_xcode_alloc_obj(struct m0_xcode_cursor *it,
		   void *(*alloc)(struct m0_xcode_cursor *, size_t))
{
	struct m0_xcode_obj  *obj;
	size_t                nob = 0;
	void                **slot;

	obj  = &m0_xcode_cursor_top(it)->s_obj;  /* an object being decoded */

	slot = allocp(it, &nob);
	if (nob != 0 && *slot == NULL) {
		M0_ASSERT(obj->xo_ptr == NULL);

		obj->xo_ptr = *slot = alloc(it, nob);
		if (obj->xo_ptr == NULL)
			return -ENOMEM;
	}
	return 0;
}

/**
   Common xcoding function, implementing encoding, decoding and sizing.
 */
static int ctx_walk(struct m0_xcode_ctx *ctx, enum xcode_op op)
{
	void                   *ptr;
	m0_bcount_t             size;
	int                     length = 0;
	int                     result;
	struct m0_bufvec        area   = M0_BUFVEC_INIT_BUF(&ptr, &size);
	struct m0_bufvec_cursor mem;
	struct m0_xcode_cursor *it     = &ctx->xcx_it;

	M0_PRE(M0_IN(op, (XO_ENC, XO_DEC, XO_LEN)));

	while ((result = m0_xcode_next(it)) > 0) {
		const struct m0_xcode_type     *xt;
		const struct m0_xcode_type_ops *ops;
		struct m0_xcode_obj            *cur;
		struct m0_xcode_cursor_frame   *top;

		top = m0_xcode_cursor_top(it);

		if (top->s_flag != M0_XCODE_CURSOR_PRE)
			continue;

		cur = &top->s_obj;

		if (op == XO_DEC) {
			result = m0_xcode_alloc_obj(it, ctx->xcx_alloc);
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
				M0_IMPOSSIBLE("op");
			}
			m0_xcode_skip(it);
		} else if (xt->xct_aggr == M0_XA_ATOM) {
			size = xt->xct_sizeof;

			if (op == XO_LEN)
				length += size;
			else {
				struct m0_bufvec_cursor *src;
				struct m0_bufvec_cursor *dst;

				m0_bufvec_cursor_init(&mem, &area);
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
					M0_IMPOSSIBLE("op");
					src = dst = 0;
					break;
				}
				if (m0_bufvec_cursor_copy(dst,
							  src, size) != size)
					result = -EPROTO;
			}
		}
		if (result < 0)
			break;
	}
	if (op == XO_LEN)
		result = result ?: length;
	return result;
}

M0_INTERNAL void m0_xcode_ctx_init(struct m0_xcode_ctx *ctx,
				   const struct m0_xcode_obj *obj)
{
	M0_SET0(ctx);
	m0_xcode_cursor_init(&ctx->xcx_it, obj);
}

M0_INTERNAL int m0_xcode_decode(struct m0_xcode_ctx *ctx)
{
	return ctx_walk(ctx, XO_DEC);
}

M0_INTERNAL int m0_xcode_encode(struct m0_xcode_ctx *ctx)
{
	return ctx_walk(ctx, XO_ENC);
}

M0_INTERNAL int m0_xcode_length(struct m0_xcode_ctx *ctx)
{
	return ctx_walk(ctx, XO_LEN);
}

M0_INTERNAL int m0_xcode_encdec(struct m0_xcode_ctx *ctx,
				struct m0_xcode_obj *obj,
				struct m0_bufvec_cursor *cur,
				enum m0_bufvec_what what)
{
	int result;

	M0_PRE(obj->xo_ptr != NULL);

	m0_xcode_ctx_init(ctx, obj);
	ctx->xcx_buf   = *cur;
	ctx->xcx_alloc = m0_xcode_alloc;

	result = what == M0_BUFVEC_ENCODE ? m0_xcode_encode(ctx) :
					    m0_xcode_decode(ctx);
	if (result == 0) {
		*cur = ctx->xcx_buf;
		if (what == M0_BUFVEC_DECODE) {
			memcpy(obj->xo_ptr, m0_xcode_ctx_top(ctx),
			       obj->xo_type->xct_sizeof);
		}
	}
	return result;
}

M0_INTERNAL int m0_xcode_data_size(struct m0_xcode_ctx *ctx,
				   const struct m0_xcode_obj *obj)
{
	m0_xcode_ctx_init(ctx, obj);
	return m0_xcode_length(ctx);
}

M0_INTERNAL void *m0_xcode_alloc(struct m0_xcode_cursor *it, size_t nob)
{
	return m0_alloc(nob);
}

M0_INTERNAL void m0_xcode_free(struct m0_xcode_obj *obj)
{
	int                    result;
	struct m0_xcode_cursor it;

	M0_SET0(&it);
	m0_xcode_cursor_top(&it)->s_obj = *obj;

	while ((result = m0_xcode_next(&it)) > 0) {
		struct m0_xcode_cursor_frame *top = m0_xcode_cursor_top(&it);
		size_t                        nob = 0;
		void                        **slot;

		if (top->s_flag == M0_XCODE_CURSOR_POST) {
			slot = allocp(&it, &nob);
			if (nob != 0 && *slot != NULL)
				m0_free(*slot);
		}
	}
}

M0_INTERNAL int m0_xcode_cmp(const struct m0_xcode_obj *o0,
			     const struct m0_xcode_obj *o1)
{
	int                    result;
	struct m0_xcode_cursor it0;
	struct m0_xcode_cursor it1;

	M0_PRE(o0->xo_type == o1->xo_type);

	m0_xcode_cursor_init(&it0, o0);
	m0_xcode_cursor_init(&it1, o1);

	while ((result = m0_xcode_next(&it0)) > 0) {
		struct m0_xcode_cursor_frame *t0;
		struct m0_xcode_cursor_frame *t1;
		struct m0_xcode_obj          *s0;
		struct m0_xcode_obj          *s1;
		const struct m0_xcode_type   *xt;

		result = m0_xcode_next(&it1);
		M0_ASSERT(result > 0);

		t0 = m0_xcode_cursor_top(&it0);
		t1 = m0_xcode_cursor_top(&it1);
		M0_ASSERT(t0->s_flag == t1->s_flag);
		s0 = &t0->s_obj;
		s1 = &t1->s_obj;
		xt = s0->xo_type;
		M0_ASSERT(xt == s1->xo_type);

		if (t0->s_flag == M0_XCODE_CURSOR_PRE &&
		    xt->xct_aggr == M0_XA_ATOM) {
			result = memcmp(s0->xo_ptr, s1->xo_ptr, xt->xct_sizeof);
			if (result != 0)
				return result;
		}
	}
	return 0;
}

M0_INTERNAL void *m0_xcode_addr(const struct m0_xcode_obj *obj, int fileno,
				uint64_t elno)
{
	char                        *addr = (char *)obj->xo_ptr;
	const struct m0_xcode_type  *xt   = obj->xo_type;
	const struct m0_xcode_field *f    = &xt->xct_child[fileno];
	const struct m0_xcode_type  *ct   = f->xf_type;

	M0_ASSERT(fileno < xt->xct_nr);
	addr += f->xf_offset;
	if (xt->xct_aggr == M0_XA_SEQUENCE && fileno == 1 &&
	    elno != ~0ULL)
		addr = *((char **)addr) + elno * ct->xct_sizeof;
	else if (ct == &M0_XT_OPAQUE && elno != ~0ULL)
		addr = *((char **)addr);
	return addr;
}

M0_INTERNAL int m0_xcode_subobj(struct m0_xcode_obj *subobj,
				const struct m0_xcode_obj *obj, int fieldno,
				uint64_t elno)
{
	const struct m0_xcode_field *f;
	int                          result;

	M0_PRE(0 <= fieldno && fieldno < obj->xo_type->xct_nr);

	f = &obj->xo_type->xct_child[fieldno];

	subobj->xo_ptr = m0_xcode_addr(obj, fieldno, elno);
	if (f->xf_type == &M0_XT_OPAQUE) {
		result = f->xf_opaque(obj, &subobj->xo_type);
	} else {
		subobj->xo_type = f->xf_type;
		result = 0;
	}
	return result;
}

M0_INTERNAL uint64_t m0_xcode_tag(const struct m0_xcode_obj *obj)
{
	const struct m0_xcode_type  *xt = obj->xo_type;
	const struct m0_xcode_field *f  = &xt->xct_child[0];
	uint64_t                     tag;

	M0_PRE(xt->xct_aggr == M0_XA_SEQUENCE || xt->xct_aggr == M0_XA_UNION);
	M0_PRE(f->xf_type->xct_aggr == M0_XA_ATOM);

	switch (f->xf_type->xct_atype) {
	case M0_XAT_VOID:
		tag = f->xf_tag;
		break;
	case M0_XAT_U8:
		tag = *M0_XCODE_VAL(obj, 0, 0, uint8_t);
		break;
	case M0_XAT_U32:
		tag = *M0_XCODE_VAL(obj, 0, 0, uint32_t);
		break;
	case M0_XAT_U64:
		tag = *M0_XCODE_VAL(obj, 0, 0, uint64_t);
		break;
	default:
		M0_IMPOSSIBLE("atype");
		tag = 0;
		break;
	}
	return tag;
}

M0_INTERNAL void m0_xcode_bob_type_init(struct m0_bob_type *bt,
					const struct m0_xcode_type *xt,
					size_t magix_field, uint64_t magix)
{
	const struct m0_xcode_field *mf = &xt->xct_child[magix_field];

	M0_PRE(magix_field < xt->xct_nr);
	M0_PRE(xt->xct_aggr == M0_XA_RECORD);
	M0_PRE(mf->xf_type == &M0_XT_U64);

	bt->bt_name         = xt->xct_name;
	bt->bt_magix        = magix;
	bt->bt_magix_offset = mf->xf_offset;
}

M0_INTERNAL void *m0_xcode_ctx_top(const struct m0_xcode_ctx *ctx)
{
	return ctx->xcx_it.xcu_stack[0].s_obj.xo_ptr;
}

const struct m0_xcode_type M0_XT_VOID = {
	.xct_aggr   = M0_XA_ATOM,
	.xct_name   = "void",
	.xct_atype  = M0_XAT_VOID,
	.xct_sizeof = 0,
	.xct_nr     = 0
};

const struct m0_xcode_type M0_XT_U8 = {
	.xct_aggr   = M0_XA_ATOM,
	.xct_name   = "u8",
	.xct_atype  = M0_XAT_U8,
	.xct_sizeof = sizeof(uint8_t),
	.xct_nr     = 0
};

const struct m0_xcode_type M0_XT_U32 = {
	.xct_aggr   = M0_XA_ATOM,
	.xct_name   = "u32",
	.xct_atype  = M0_XAT_U32,
	.xct_sizeof = sizeof(uint32_t),
	.xct_nr     = 0
};

const struct m0_xcode_type M0_XT_U64 = {
	.xct_aggr   = M0_XA_ATOM,
	.xct_name   = "u64",
	.xct_atype  = M0_XAT_U64,
	.xct_sizeof = sizeof(uint64_t),
	.xct_nr     = 0
};

const struct m0_xcode_type M0_XT_OPAQUE = {
	.xct_aggr   = M0_XA_OPAQUE,
	.xct_name   = "opaque",
	.xct_sizeof = sizeof (void *),
	.xct_nr     = 0
};

const char *m0_xcode_aggr_name[M0_XA_NR] = {
	[M0_XA_RECORD]   = "record",
	[M0_XA_UNION]    = "union",
	[M0_XA_SEQUENCE] = "sequence",
	[M0_XA_TYPEDEF]  = "typedef",
	[M0_XA_OPAQUE]   = "opaque",
	[M0_XA_ATOM]     = "atom"
};

const char *m0_xcode_atom_type_name[M0_XAT_NR] = {
	[M0_XAT_VOID] = "void",
	[M0_XAT_U8]   = "u8",
	[M0_XAT_U32]  = "u32",
	[M0_XAT_U64]  = "u64",
};

const char *m0_xcode_endianness_name[M0_XEND_NR] = {
	[M0_XEND_LE] = "le",
	[M0_XEND_BE] = "be"
};

const char *m0_xcode_cursor_flag_name[M0_XCODE_CURSOR_NR] = {
	[M0_XCODE_CURSOR_NONE] = "none",
	[M0_XCODE_CURSOR_PRE]  = "pre",
	[M0_XCODE_CURSOR_IN]   = "in",
	[M0_XCODE_CURSOR_POST] = "post"
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
