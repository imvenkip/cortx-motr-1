/* -*- C -*- */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/smp_lock.h>
#include <linux/uaccess.h>
#include <linux/vfs.h>
#include <linux/param.h>
#include <linux/time.h>
#include <linux/utsname.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xdr.h>

#include "lib/cdefs.h"
#include "lib/kdef.h"
#include "fop/fop.h"
#include "ksunrpc.h"

/**
   @addtogroup ksunrpc
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

int c2_kvoid_encode(struct xdr_stream *xdr, uint32_t **p, void *val)
{
	return 0;
}

int c2_kvoid_decode(struct xdr_stream *xdr, uint32_t **p, void *val)
{
	return true;
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

static int kxdr_sequence_enc(struct kxdr_ctx *ctx, void *obj)
{
	int result;
	struct page_sequence *ps = obj;

	result = atom_kxdr[ctx->kc_what][FPF_U32](ctx->kc_xdr, &ps->ps_nr);
	if (result != 0)
		return result;

	if (ctx->kc_type->fft_child[0]->ff_type == &C2_FOP_TYPE_BYTE) {
		xdr_write_pages(ctx->kc_xdr, 
				ps->ps_pages, ps->ps_pgoff, ps->ps_nr);
	} else {
		uint32_t i;

		for (result = 0, i = 0; result == 0 && i < ps->ps_nr; ++i)
			result = ftype_subxdr(ctx, obj, 0);
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
	if (ctx->kc_type->fft_child[0]->ff_type == &C2_FOP_TYPE_BYTE) {
		xdr_read_pages(ctx->kc_xdr, nr);
	} else {
		uint32_t i;

		for (result = 0, i = 0; result == 0 && i < nr; ++i)
			result = ftype_subxdr(ctx, obj, 0);
	}
	return result;
}

static int kxdr_sequence_rep(struct kxdr_ctx *ctx, void *obj)
{
	int                   result;
	struct page_sequence *ps = obj;

	result = atom_kxdr[ctx->kc_what][FPF_U32](ctx->kc_xdr, &ps->ps_nr);
	if (result != 0)
		return result;

	if (ctx->kc_type->fft_child[0]->ff_type == &C2_FOP_TYPE_BYTE) {
		xdr_inline_pages(&ctx->kc_req->rq_rcv_buf, *ctx->kc_nob,
				 ps->ps_pages, ps->ps_pgoff, ps->ps_nr);
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
	[KDEC] = {
		[FPF_VOID] = (void *)&c2_kvoid_encode,
		[FPF_BYTE] = NULL,
		[FPF_U32]  = (void *)&c2_ku32_encode,
		[FPF_U64]  = (void *)&c2_ku64_encode
	},
	[KENC] = {
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
	ctx->kc_nob += ctx->kc_type->fft_layout->fm_sizeof;
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

int c2_fop_type_encdec(const struct c2_fop_field_type *ftype,
		       struct rpc_rqst *req, __be32 *data, void *obj,
		       enum kxdr_what what)
{
	int nob;
	struct xdr_stream xdr;
	struct kxdr_ctx   ctx = {
		.kc_type = ftype,
		.kc_xdr   = &xdr,
		.kc_req   = req,
		.kc_what  = what,
		.kc_nob   = &nob
	};

	C2_ASSERT(ftype->fft_aggr < ARRAY_SIZE(kxdr_disp));

	xdr_init_encode(&xdr, &req->rq_snd_buf, data);
	return kxdr_disp[what][ftype->fft_aggr](&ctx, obj);
}

int c2_fop_kenc(void *req, __be32 *data, struct c2_fop *fop)
{
	return c2_fop_type_encdec(fop->f_type->ft_top, 
				  req, data, c2_fop_data(fop), KENC);
}

int c2_fop_kdec(void *req, __be32 *data, struct c2_fop *fop)
{
	return c2_fop_type_encdec(fop->f_type->ft_top, 
				  req, data, c2_fop_data(fop), KDEC);
}

int c2_fop_krep(void *req, __be32 *data, struct c2_fop *fop)
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
