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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 07/01/2010
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/svc.h>
#include <linux/pagemap.h> /* PAGE_CACHE_SIZE */

#include "lib/cdefs.h"
#include "fop/fop.h"
#include "net/ksunrpc/ksunrpc.h"

/**
   @addtogroup ksunrpc

   <b>Fop xdr</b>

   See head comment in fop_format.h for overview of fop formats.

   This file defines "universal" fop xdr functions for the Linux kernel.

   Main entry points c2_kcall_dec() and c2_kcall_enc() decode and encode rpc
   calls respectively. The entry points c2_svc_rqst_dec() and c2_svc_rqst_enc()
   decode and encode service requests respectively.  For each fop type there are
   four xdr-related operations (see enum kxdr_what):

   @li encoding (KENC): serialize fop data to the rpc send buffer, according to
   fop type. This operation is called on c2_knet_call::ac_arg fop before rpc is
   sent;

   @li client decoding (KDEC): read data from the rpc receive buffer and build
   fop instance. This operation is called on c2_knet_call::ac_ret for after rpc
   reply was received;

   @li reply preparing (KREP): prepare for receipt a fop of this type as a
   reply. This operation is called on c2_knet::ac_ret before rpc is sent. This
   operation is necessary to prepare receive buffer where incoming data are
   stored. For example "read" type operations must attach data pages to the
   reply buffer, see kxdr_sequence_rep().

   @li service decoding (KARG): read data from the rpc service arg buffer and
   build fop instance. This operation is called on fop of appropriate
   c2_net_op_table::top_fopt type after rpc request was received in server.
   Internally, this is very similar to KDEC, except for sequence handling,
   since there is no way to pre-allocate pages as is done in KREP.  Only
   a single sequence is supported per service request.

   All four xdr operations are implemented similarly, by recursively descending
   through the fop format tree.

   When handling a non-leaf (i.e., "aggregating") node of a fop format tree,
   control branches though the kxdr_disp[] function pointer array, using
   aggregation type as an index. When a leaf (i.e., "atomic") node is reached,
   control branches through the atom_kxdr[] function pointer array, using atom
   type as an index.

   Serialization state is recorded in struct kxdr_ctx (similar to XDR type of
   user level SUNRPC).

   A few of points worth mentioning:

   - an array of bytes is handled specially to optimize large data transfers,
   see calls to kxdr_is_byte_array();

   - "reply preparing" operation shares a lot of code with decoding operation
   (see kxdr_disp[KREP] values), but at the atomic field level, instead of
   actual decoding (there is nothing to decode yet, because there is no reply),
   reply preparing increments the counter of bytes that _would_ be used in the
   receive buffer on actual reply. kxdr_sequence_rep() uses this information to
   attach data pages at the correct offset;

   - very limited class of format is accepted. Unions are not supported at all
   at the moment (kxdr_union(), this is easy to fix though). Variable size array
   must be the last field in the fop and it must be a byte array.

   - allocation of intermediate data structures (arrays to hold sequences) is
   not implemented.

   @{
 */

enum kxdr_what {
	KENC,
	KDEC,
	KREP,
	KARG,
	KNR
};

struct kxdr_ctx {
	const struct c2_fop_field_type *kc_type;
	struct xdr_stream              *kc_xdr;
	struct rpc_rqst                *kc_creq;
	struct svc_rqst                *kc_sreq;
	enum kxdr_what                  kc_what;
	uint32_t                       *kc_nob;
};

typedef int (*c2_kxdrproc_t)(struct kxdr_ctx *, void *);

static const c2_kxdrproc_t kxdr_disp[KNR][FFA_NR];
static int (*atom_kxdr[KNR][FPF_NR])(struct xdr_stream *xdr, void *obj);

int c2_kvoid_encode(struct xdr_stream *xdr, void *val)
{
	return 0;
}

int c2_kvoid_decode(struct xdr_stream *xdr, void *val)
{
	return 0;
}

int c2_ku32_encode(struct xdr_stream *xdr, uint32_t *val)
{
	uint32_t *p;

	p = xdr_reserve_space(xdr, 4);
	if (p != NULL)
		*p = htonl(*val);
	return p != NULL ? 0 : -EIO;
}

int c2_ku32_decode(struct xdr_stream *xdr, uint32_t *val)
{
	uint32_t *p;

	p = xdr_inline_decode(xdr, 4);
	if (p != NULL)
		*val = ntohl(*p);
	return p != NULL ? 0 : -EIO;
}

int c2_ku64_encode(struct xdr_stream *xdr, uint64_t *val)
{
	uint32_t *p;

	p = xdr_reserve_space(xdr, 8);
	if (p != NULL) {
		p[0] = htonl((uint32_t)(*val >> 32));
		p[1] = htonl((uint32_t)*val);
	}
	return p != NULL ? 0 : -EIO;
}

int c2_ku64_decode(struct xdr_stream *xdr, uint64_t *val)
{
	uint32_t *p;

	p = xdr_inline_decode(xdr, 8);
	if (p != NULL)
		*val = (((uint64_t)ntohl(p[0])) << 32) | ntohl(p[1]);
	return p != NULL ? 0 : -EIO;
}

/* copied from usunrpc/uxdr.c */
static c2_kxdrproc_t ftype_field_kxdr(const struct c2_fop_field_type *ftype,
				      int fieldno, enum kxdr_what what)
{
	C2_ASSERT(fieldno < ftype->fft_nr);
	return kxdr_disp[what][ftype->fft_child[fieldno]->ff_type->fft_aggr];
}

static int ftype_subxdr(struct kxdr_ctx *ctx, void *obj, int fieldno,
			uint32_t elno)
{
	const struct c2_fop_field_type *ftype;
	struct kxdr_ctx                 subctx;

	ftype = ctx->kc_type;
	subctx = *ctx;
	subctx.kc_type = ftype->fft_child[fieldno]->ff_type;
	return ftype_field_kxdr(ftype, fieldno, ctx->kc_what)
		(&subctx, c2_fop_type_field_addr(ftype, obj, fieldno, elno));
}

static int kxdr_record(struct kxdr_ctx *ctx, void *obj)
{
	size_t i;
	int    result;

	for (result = 0, i = 0; result == 0 && i < ctx->kc_type->fft_nr; ++i)
		result = ftype_subxdr(ctx, obj, i, 0);
	return result;
}

static int kxdr_union(struct kxdr_ctx *ctx, void *obj)
{
	return -EIO;
}

struct page_sequence {
	uint32_t      ps_nr;
	uint32_t      ps_pgoff;
	struct page **ps_pages;
};

static bool kxdr_is_byte_array(const struct kxdr_ctx *ctx)
{
	C2_ASSERT(ctx->kc_type->fft_aggr == FFA_SEQUENCE);
	return ctx->kc_type->fft_child[1]->ff_type == &C2_FOP_TYPE_BYTE;
}

static int kxdr_sequence_enc(struct kxdr_ctx *ctx, void *obj)
{
	int result;
	struct page_sequence *ps = obj;

	result = atom_kxdr[ctx->kc_what][FPF_U32](ctx->kc_xdr, &ps->ps_nr);
	if (result != 0)
		return result;

	if (kxdr_is_byte_array(ctx)) {
		xdr_write_pages(ctx->kc_xdr,
				ps->ps_pages, ps->ps_pgoff, ps->ps_nr);
	} else {
		uint32_t i;

		for (result = 0, i = 0; result == 0 && i < ps->ps_nr; ++i)
			result = ftype_subxdr(ctx, obj, 1, i);
	}
	return result;
}

static int kxdr_sequence_dec(struct kxdr_ctx *ctx, void *obj)
{
	int      result;
	uint32_t nr;

	result = atom_kxdr[ctx->kc_what][FPF_U32](ctx->kc_xdr, obj);
	if (result != 0)
		return result;

	nr = *(uint32_t *)obj;
	if (kxdr_is_byte_array(ctx)) {
		xdr_read_pages(ctx->kc_xdr, nr);
	} else {
		uint32_t i;

		for (result = 0, i = 0; result == 0 && i < nr; ++i)
			result = ftype_subxdr(ctx, obj, 1, i);
	}
	return result;
}

static int kxdr_sequence_rep(struct kxdr_ctx *ctx, void *obj)
{
	int                   result;
	struct page_sequence *ps = obj;

	*ctx->kc_nob += 4;
	if (kxdr_is_byte_array(ctx)) {
		struct rpc_auth *auth;
		uint32_t         offset;

		/* Believe or not this is how kernel rpc users are supposed to
		   indicate size of a reply. */
#ifdef HAVE_CRED_IN_REQ
		auth = ctx->kc_creq->rq_cred->cr_auth;
#else
		auth = ctx->kc_creq->rq_task->tk_msg.rpc_cred->cr_auth;
#endif
		offset = ((RPC_REPHDRSIZE + auth->au_rslack + 3) << 2) +
			*ctx->kc_nob;
		xdr_inline_pages(&ctx->kc_creq->rq_rcv_buf, offset,
				 ps->ps_pages, ps->ps_pgoff, ps->ps_nr);
		result = 0;
	} else {
		result = -EIO;
	}
	return result;
}

static int kxdr_sequence_arg(struct kxdr_ctx *ctx, void *obj)
{
	int result;
	struct page_sequence *ps = obj;

	result = atom_kxdr[ctx->kc_what][FPF_U32](ctx->kc_xdr, obj);
	if (result != 0)
		return result;

	if (kxdr_is_byte_array(ctx)) {
		/* Only supports bulk service with a single sequence */
		ps->ps_pgoff =
		    (unsigned long) ctx->kc_xdr->p & (PAGE_CACHE_SIZE - 1);
		ps->ps_pages = ctx->kc_sreq->rq_pages;
	} else {
		uint32_t i;

		for (result = 0, i = 0; result == 0 && i < ps->ps_nr; ++i)
			result = ftype_subxdr(ctx, obj, 1, i);
	}
	return result;
}

static int kxdr_typedef(struct kxdr_ctx *ctx, void *obj)
{
	return ftype_subxdr(ctx, obj, 0, 0);
}

static int (*atom_kxdr[KNR][FPF_NR])(struct xdr_stream *xdr, void *obj) = {
	[KENC] = {
		[FPF_VOID] = (void *)&c2_kvoid_encode,
		[FPF_BYTE] = NULL,
		[FPF_U32]  = (void *)&c2_ku32_encode,
		[FPF_U64]  = (void *)&c2_ku64_encode
	},
	[KDEC] = {
		[FPF_VOID] = (void *)&c2_kvoid_decode,
		[FPF_BYTE] = NULL,
		[FPF_U32]  = (void *)&c2_ku32_decode,
		[FPF_U64]  = (void *)&c2_ku64_decode
	},
	[KARG] = {
		[FPF_VOID] = (void *)&c2_kvoid_decode,
		[FPF_BYTE] = NULL,
		[FPF_U32]  = (void *)&c2_ku32_decode,
		[FPF_U64]  = (void *)&c2_ku64_decode
	}
};

static int kxdr_atom(struct kxdr_ctx *ctx, void *obj)
{
	C2_ASSERT(ctx->kc_type->fft_u.u_atom.a_type <
		  ARRAY_SIZE(atom_kxdr[ctx->kc_what]));
	return atom_kxdr[ctx->kc_what][ctx->kc_type->fft_u.u_atom.a_type]
		(ctx->kc_xdr, obj);
}

static int kxdr_atom_rep(struct kxdr_ctx *ctx, void *obj)
{
	*ctx->kc_nob += ctx->kc_type->fft_layout->fm_sizeof;
	return 0;
}

static const c2_kxdrproc_t kxdr_disp[KNR][FFA_NR] =
{
	[KENC] = {
		[FFA_RECORD]   = kxdr_record,
		[FFA_UNION]    = kxdr_union,
		[FFA_SEQUENCE] = kxdr_sequence_enc,
		[FFA_TYPEDEF]  = kxdr_typedef,
		[FFA_ATOM]     = kxdr_atom
	},
	[KDEC] = {
		[FFA_RECORD]   = kxdr_record,
		[FFA_UNION]    = kxdr_union,
		[FFA_SEQUENCE] = kxdr_sequence_dec,
		[FFA_TYPEDEF]  = kxdr_typedef,
		[FFA_ATOM]     = kxdr_atom
	},
	[KREP] = {
		[FFA_RECORD]   = kxdr_record,
		[FFA_UNION]    = kxdr_union,
		[FFA_SEQUENCE] = kxdr_sequence_rep,
		[FFA_TYPEDEF]  = kxdr_typedef,
		[FFA_ATOM]     = kxdr_atom_rep
	},
	[KARG] = {
		[FFA_RECORD]   = kxdr_record,
		[FFA_UNION]    = kxdr_union,
		[FFA_SEQUENCE] = kxdr_sequence_arg,
		[FFA_TYPEDEF]  = kxdr_typedef,
		[FFA_ATOM]     = kxdr_atom
	}
};

static int c2_fop_type_encdec(const struct c2_fop_field_type *ftype,
			      struct rpc_rqst *req, __be32 *data, void *obj,
			      enum kxdr_what what)
{
	int nob = 0;
	struct xdr_stream xdr;
	struct kxdr_ctx   ctx = {
		.kc_type = ftype,
		.kc_xdr   = &xdr,
		.kc_creq  = req,
		.kc_what  = what,
		.kc_nob   = &nob
	};

	C2_ASSERT(ftype->fft_aggr < ARRAY_SIZE(kxdr_disp));

	switch (what) {
	case KENC:
		xdr_init_encode(&xdr, &req->rq_snd_buf, data);
		break;
	case KDEC:
		xdr_init_decode(&xdr, &req->rq_rcv_buf, data);
	default:
		C2_ASSERT(what != KARG);
		break;
	}
	return kxdr_disp[what][ftype->fft_aggr](&ctx, obj);
}

static int c2_fop_encdec_buffer(const struct c2_fop_type_format *ftf,
				void *buffer, void *obj, enum kxdr_what what)
{
	const struct c2_fop_field_type *ftype = ftf->ftf_out;
	size_t len = ftf->ftf_layout->fm_sizeof;
	/* see code in kernel net/sunrpc for XDR structure initialization */
	struct xdr_buf xb = {
		.head = {
			[0] = {
				.iov_base = buffer,
				.iov_len  = len,
			},
		},
		.buflen = len,
	};
	struct xdr_stream xdr = {
		.buf = &xb,
		.iov = xb.head,
		.end = (__be32 *)((char *)buffer + len),
		.p   = buffer,
	};
	int nob = 0;
	struct kxdr_ctx   ctx = {
		.kc_type = ftype,
		.kc_xdr  = &xdr,
		.kc_what = what,
		.kc_nob  = &nob
	};
	int rc;

	C2_ASSERT(ftype->fft_aggr < ARRAY_SIZE(kxdr_disp));

	switch (what) {
	case KENC:
	case KDEC:
		break;
	default:
		C2_IMPOSSIBLE("only KENC and KDEC supported");
		break;
	}
	rc = kxdr_disp[what][ftype->fft_aggr](&ctx, obj);
	C2_POST(ergo(what == KENC, xb.len <= len));
	return rc;
}

/**
   XDR encode a record or atomic FOP into a buffer.
   Will not work for embedded sequences.
   @param ftype  Pointer to FOP type format.
   @param buffer Buffer pointer.  It is assumed that the buffer is of size
   ftype->ftf_layout->fm_sizeof and is large enough to encode the FOP.
   @param obj    Pointer to FOP object.
 */
int c2_fop_encode_buffer(const struct c2_fop_type_format *ftf,
			 void *buffer, void *obj)
{
	return c2_fop_encdec_buffer(ftf, buffer, obj, KENC);
}

/**
   XDR decode a record or atomic FOP from a buffer previously encoded
   with c2_fop_encode_buffer().
   @param ftype  Pointer to FOP type format.
   @param buffer Buffer pointer.
   @param obj    Pointer to FOP object.
 */
int c2_fop_decode_buffer(const struct c2_fop_type_format *ftf,
			 void *buffer, void *obj)
{
	return c2_fop_encdec_buffer(ftf, buffer, obj, KDEC);
}

static int c2_fop_kenc(void *req, __be32 *data, struct c2_fop *fop)
{
	return c2_fop_type_encdec(fop->f_type->ft_top,
				  req, data, c2_fop_data(fop), KENC);
}

static int c2_fop_kdec(void *req, __be32 *data, struct c2_fop *fop)
{
	return c2_fop_type_encdec(fop->f_type->ft_top,
				  req, data, c2_fop_data(fop), KDEC);
}

static int c2_fop_krep(void *req, __be32 *data, struct c2_fop *fop)
{
	return c2_fop_type_encdec(fop->f_type->ft_top,
				  req, data, c2_fop_data(fop), KREP);
}

int c2_kcall_enc(void *req, __be32 *data, struct c2_net_call *kcall)
{
	int result;

	result = c2_fop_kenc(req, data, kcall->ac_arg);
	if (result == 0)
		result = c2_fop_krep(req, data, kcall->ac_ret);
	return result;
}

int c2_kcall_dec(void *req, __be32 *data, struct c2_net_call *kcall)
{
	return c2_fop_kdec(req, data, kcall->ac_ret);
}

static int c2_svc_rqst_encdec(const struct c2_fop_field_type *ftype,
			      struct svc_rqst *rqstp, __be32 *data, void *obj,
			      enum kxdr_what what)
{
	int nob = 0;
	struct xdr_stream xdr;
	struct kxdr_ctx   ctx = {
		.kc_type = ftype,
		.kc_xdr   = &xdr,
		.kc_sreq  = rqstp,
		.kc_what  = what,
		.kc_nob   = &nob
	};

	C2_ASSERT(ftype->fft_aggr < ARRAY_SIZE(kxdr_disp));
	C2_ASSERT(what == KENC || what == KARG);

	switch (what) {
	case KENC:
		xdr_init_encode(&xdr, &rqstp->rq_res, data);
		break;
	case KARG:
		xdr_init_decode(&xdr, &rqstp->rq_arg, data);
	default:
		break;
	}
	return kxdr_disp[what][ftype->fft_aggr](&ctx, obj);
}

int c2_svc_rqst_dec(void *req, __be32 *data, struct c2_fop *fop)
{
	return c2_svc_rqst_encdec(fop->f_type->ft_top,
				  req, data, c2_fop_data(fop), KARG);
}

int c2_svc_rqst_enc(void *req, __be32 *data, struct c2_fop *fop)
{
	return c2_svc_rqst_encdec(fop->f_type->ft_top,
				  req, data, c2_fop_data(fop), KENC);
}

/** @} end of group ksunrpc */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
