/* -*- C -*- */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/cdefs.h"
#include "lib/assert.h"
#include "fop/fop.h"
#include "fop/fop_format.h"

#include "usunrpc.h"

/**
   @addtogroup usunrpc User Level Sun RPC
   @{
 */

/*
 * User level xdr code.
 */

static void *ftype_field(void *obj, 
			 const struct c2_fop_field_type *ftype, int i)
{
	C2_ASSERT(i < ftype->fft_nr);
	return ((char *)obj) + ftype->fft_layout->fm_child[i].ch_offset;
}

static xdrproc_t ftype_field_xdr(const struct c2_fop_field_type *ftype, 
				 int fieldno)
{
	C2_ASSERT(fieldno < ftype->fft_nr);
	return ftype->fft_child[fieldno]->ff_type->fft_layout->fm_uxdr;
}

static bool ftype_subxdr(const struct c2_fop_field_type *ftype, 
			 XDR *xdrs, void *obj, int fieldno)
{
	return ftype_field_xdr(ftype, fieldno)(xdrs, ftype_field(obj, ftype, 
								 fieldno));
}

static bool uxdr_record(const struct c2_fop_field_type *ftype, 
			XDR *xdrs, void *obj)
{
	size_t i;
	bool   success;

	for (success = true, i = 0; success && i < ftype->fft_nr; ++i)
		success = ftype_subxdr(ftype, xdrs, obj, i);
	return success;
}

static bool uxdr_union(const struct c2_fop_field_type *ftype, 
		       XDR *xdrs, void *obj)
{
	uint32_t discr;
	bool     success;
	size_t   i;

	success = xdr_u_int(xdrs, obj);
	if (success) {
		discr = *(uint32_t *)obj;
		for (success = false, i = 0; i < ftype->fft_nr; ++i) {
			if (discr == ftype->fft_child[i]->ff_tag) {
				success = ftype_subxdr(ftype, xdrs, obj, i);
				break;
			}
		}
	}
	return success;
}

static bool uxdr_sequence(const struct c2_fop_field_type *ftype, 
			  XDR *xdrs, void *obj)
{
	if (ftype->fft_child[0]->ff_type == &C2_FOP_TYPE_BYTE)
		return xdr_bytes(xdrs, ftype_field(obj, ftype, 0), obj, ~0);
	else {
		struct c2_fop_memlayout *ellay;

		ellay = ftype->fft_child[0]->ff_type->fft_layout;
		return xdr_array(xdrs, ftype_field(obj, ftype, 0), obj, ~0,
				 ellay->fm_sizeof, ftype_field_xdr(ftype, 0));
	}
}

static bool uxdr_typedef(const struct c2_fop_field_type *ftype, 
			 XDR *xdrs, void *obj)
{
	return ftype_subxdr(ftype, xdrs, obj, 0);
}

static const xdrproc_t atom_xdr[FPF_NR] = {
	[FPF_VOID] = (xdrproc_t)&xdr_void,
	[FPF_BYTE] = NULL,
	[FPF_U32]  = (xdrproc_t)&xdr_uint32_t,
	[FPF_U64]  = (xdrproc_t)&xdr_uint64_t
};

static bool uxdr_atom(const struct c2_fop_field_type *ftype, 
		      XDR *xdrs, void *obj)
{
	C2_ASSERT(ftype->fft_u.u_atom.a_type < ARRAY_SIZE(atom_xdr));
	return atom_xdr[ftype->fft_u.u_atom.a_type](xdrs, obj);
}

static bool (*uxdr_aggr[])(const struct c2_fop_field_type *, XDR *, void *) = {
	[FFA_RECORD]   = &uxdr_record,
	[FFA_UNION]    = &uxdr_union,
	[FFA_SEQUENCE] = &uxdr_sequence,
	[FFA_TYPEDEF]  = &uxdr_typedef,
	[FFA_ATOM]     = &uxdr_atom
};

bool_t c2_uxdr_fop(const struct c2_fop_field_type *ftype, XDR *xdrs, void *obj)
{
	C2_ASSERT(ftype->fft_aggr < ARRAY_SIZE(uxdr_aggr));
	return uxdr_aggr[ftype->fft_aggr](ftype, xdrs, obj);
}

/** @} end of group usunrpc */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
