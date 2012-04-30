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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 06/29/2010
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "lib/cdefs.h"
#include "lib/assert.h"
#include "fop/fop.h"

#include "usunrpc.h"

/**
   @addtogroup usunrpc User Level Sun RPC

   <b>Fop xdr</b>

   See head comment in fop_format.h for overview of fop formats, see
   net/ksunrpc/kxdr.c for description of kernel level xdr functions.

   User level xdr functions are quite similar to kxdr (the same fop format tree
   processing idea) with a few notable exceptions:

   @li there is no "reply preparation" at the moment;

   @li user level sunrpc library requires xdr functions with the prototype

       bool_t xdr_foo(XDR *xdrs, void *obj);

   where, obj is a pointer to data. There is no way to pass a pointer to fop or
   fop type to the xdr function (compare with kernel xdr functions usage of
   kxdr_ctx). Current solution is to generate (in fop2c, see c2_fop_comp_ulay())
   an xdr function for each fop type:

       bool_t xdr_foo(XDR *xdrs, void *obj) {
               return c2_fop_type_uxdr(&foo_fop_type, xdrs, obj);
       }

   Pointer to this function is installed as
   c2_fop_field_type::fft_layout::fm_uxdr. This allows tree traversing code to
   call xdr functions of children nodes, see ftype_field_xdr().

   @{
 */

/*
 * User level xdr code.
 */

static xdrproc_t ftype_field_xdr(const struct c2_fop_field_type *ftype,
				 int fieldno)
{
	C2_ASSERT(fieldno < ftype->fft_nr);
	return ftype->fft_child[fieldno]->ff_type->fft_layout->fm_uxdr;
}

static bool ftype_subxdr(const struct c2_fop_field_type *ftype,
			 XDR *xdrs, void *obj, int fieldno)
{
	return ftype_field_xdr(ftype, fieldno)
		(xdrs, c2_fop_type_field_addr(ftype, obj, fieldno, 0));
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
	char **buffer;

	buffer = c2_fop_type_field_addr(ftype, obj, 1, ~0);
	if (ftype->fft_child[1]->ff_type == &C2_FOP_TYPE_BYTE) {
		return xdr_bytes(xdrs, buffer, obj, ~0);
	} else {
		struct c2_fop_memlayout *ellay;

		ellay = ftype->fft_child[1]->ff_type->fft_layout;
		return xdr_array(xdrs, buffer, obj, ~0,
				 ellay->fm_sizeof, ftype_field_xdr(ftype, 1));
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
	C2_ASSERT(IS_IN_ARRAY(ftype->fft_u.u_atom.a_type, atom_xdr));
	return atom_xdr[ftype->fft_u.u_atom.a_type](xdrs, obj);
}

static bool (*uxdr_aggr[FFA_NR])(const struct c2_fop_field_type *,
				 XDR *, void *) = {
	[FFA_RECORD]   = &uxdr_record,
	[FFA_UNION]    = &uxdr_union,
	[FFA_SEQUENCE] = &uxdr_sequence,
	[FFA_TYPEDEF]  = &uxdr_typedef,
	[FFA_ATOM]     = &uxdr_atom
};

bool_t c2_fop_type_uxdr(const struct c2_fop_field_type *ftype,
			XDR *xdrs, void *obj)
{
	C2_ASSERT(IS_IN_ARRAY(ftype->fft_aggr, uxdr_aggr));
	return uxdr_aggr[ftype->fft_aggr](ftype, xdrs, obj);
}

bool_t c2_fop_uxdrproc(XDR *xdrs, struct c2_fop *fop)
{
	return c2_fop_type_uxdr(fop->f_type->ft_top, xdrs, c2_fop_data(fop));
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
