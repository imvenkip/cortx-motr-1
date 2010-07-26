/* -*- C -*- */

#include <linux/sunrpc/clnt.h>

#include "lib/cdefs.h"
#include "lib/kdef.h"
#include "fop/fop.h"
#include "ksunrpc.h"

/**
   @addtogroup ksunrpc

   <b>Fop xdr</b>

   See head comment in fop_format.h for overview of fop formats.

   This file defines "universal" fop xdr functions for the Linux kernel.

   Main entry points c2_kcall_dec() and c2_kcall_enc() decode and encode rpc
   calls respectively. For each fop type there are three xdr-related operations
   (see enum kxdr_what):

   @li encoding (KENC): serialize fop data to the rpc send buffer, according to
   fop type. This operation is called on c2_knet_call::ac_arg fop before rpc is
   sent;

   @li decoding (KDEC): read data from the rpc receive buffer and build fop
   instance. This operation is called on c2_knet_call::ac_ret for after rpc
   reply was received;

   @li reply preparing (KREP): prepare for receipt a fop of this type as a
   reply. This operation is called on c2_knet::ac_ret before rpc is sent. This
   operation is necessary to prepare receive buffer where incoming data are
   stored. For example "read" type operations must attach data pages to the
   reply buffer, see kxdr_sequence_rep().

   All three xdr operations are implemented similarly, by recursively descending
   through the fop format tree.

   When handling a non-leaf (i.e., "aggregating") node of a fop format tree,
   control branches though the kxdr_disp[] function pointer array, using
   aggregation type as an index. When a leaf (i.e., "atomic") node is reached,
   control branches through the atom_kxdr[] function pointer array, using atom
   type as an index.

   Serialization state is recorded in struct kxdr_ctx (similar to XDR type of
   user level SUNRPC).

   A few of points worth mentioning:

   @li an array of bytes is handled specially to optimize large data transfers,
   see calls to kxdr_is_byte_array();

   @li "reply preparing" operation shares a lot of code with decoding operation
   (see kxdr_disp[KREP] values), but at the atomic field level, instead of
   actual decoding (there is nothing to decode yet, because there is no reply),
   reply preparing increments the counter of bytes that _would_ be used in the
   receive buffer on actual reply. kxdr_sequence_rep() uses this information to
   attach data pages at the correct offset;

   @li very limited class of format is accepted. Unions are not supported at all
   at the moment (kxdr_union(), this is easy to fix though). Variable size array
   must be the last field in the fop and it must be a byte array.

   @{
 */

enum kxdr_what {
	KENC,
	KDEC,
	KREP,
	KNR
};

struct kxdr_ctx {
	const struct c2_fop_field_type *kc_type;
	struct xdr_stream              *kc_xdr;
	struct rpc_rqst                *kc_req;
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

static int ftype_subxdr(struct kxdr_ctx *ctx, void *obj, int fieldno)
{
	const struct c2_fop_field_type *ftype;
	struct kxdr_ctx                 subctx;

	ftype = ctx->kc_type;
	subctx = *ctx;
	subctx.kc_type = ftype->fft_child[fieldno]->ff_type;
	return ftype_field_kxdr(ftype, fieldno, ctx->kc_what)
		(&subctx, c2_fop_type_field_addr(ftype, obj, fieldno));
}

static int kxdr_record(struct kxdr_ctx *ctx, void *obj)
{
	size_t i;
	int    result;

	for (result = 0, i = 0; result == 0 && i < ctx->kc_type->fft_nr; ++i)
		result = ftype_subxdr(ctx, obj, i);
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
			result = ftype_subxdr(ctx, obj, 1);
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
		result = 0;
	} else {
		uint32_t i;

		for (result = 0, i = 0; result == 0 && i < nr; ++i)
			result = ftype_subxdr(ctx, obj, 1);
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
		auth = ctx->kc_req->rq_task->tk_msg.rpc_cred->cr_auth;
		offset = ((RPC_REPHDRSIZE + auth->au_rslack + 3) << 2) + 
			*ctx->kc_nob;
		xdr_inline_pages(&ctx->kc_req->rq_rcv_buf, offset,
				 ps->ps_pages, ps->ps_pgoff, ps->ps_nr);
		result = 0;
	} else {
		result = -EIO;
	}
	return result;
}

static int kxdr_typedef(struct kxdr_ctx *ctx, void *obj)
{
	return ftype_subxdr(ctx, obj, 0);
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
		.kc_req   = req,
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
		break;
	}
	return kxdr_disp[what][ftype->fft_aggr](&ctx, obj);
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

int c2_kcall_enc(void *req, __be32 *data, struct c2_knet_call *kcall)
{
	int result;

	result = c2_fop_kenc(req, data, kcall->ac_arg);
	if (result == 0)
		result = c2_fop_krep(req, data, kcall->ac_ret);
	return result;
}

int c2_kcall_dec(void *req, __be32 *data, struct c2_knet_call *kcall)
{
	return c2_fop_kdec(req, data, kcall->ac_ret);
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
