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
 * Original creation date: 07/01/2010
 */

#include "lib/cdefs.h"
#include "fop/fop.h"
#include "xcode/bufvec_xcode.h"
#include "lib/errno.h"
/**
   @addtogroup xcode

   <b>Fop xcode</b>

   See head comment in fop_format.h for overview of fop formats.

   This file defines "universal" fop encode/decode functions.

   @{
 */
typedef int (*c2_xcode_foptype_t)(struct c2_fop_type *ftype,
				  struct c2_bufvec_cursor *cur, void *data,
				  enum c2_bufvec_what what);

static const c2_xcode_foptype_t xcode_disp[FFA_NR];


static int (*atom_xcode[FPF_NR])(struct c2_bufvec_cursor *cur, void *obj,
				      enum c2_bufvec_what);

static c2_xcode_foptype_t ftype_field_xcode(struct c2_fop_field_type *fftype,
				     int fieldno, enum c2_bufvec_what what)
{
	C2_ASSERT(fieldno < fftype->fft_nr);
	return xcode_disp[fftype->fft_child[fieldno]->ff_type->fft_aggr];
}

static int ftype_sub_xcode(struct c2_fop_field_type *fftype,
			   struct c2_bufvec_cursor *cur, void *obj,
			   int fieldno, uint32_t elno,
			   enum c2_bufvec_what what)
{
	struct c2_fop_field_type        *ff_subtype;

	ff_subtype = fftype->fft_child[fieldno]->ff_type;
	return ftype_field_xcode(fftype, fieldno, what)
	(ff_subtype, cur, c2_fop_type_field_addr(fftype, obj, fieldno, elno),
	what);
}

static int xcode_bufvec_record(struct c2_fop_field_type *fftype,
			       struct c2_bufvec_cursor *cur,
			       void *obj, enum c2_bufvec_what what)
{
	size_t i;
	int    result;

	for (result = 0, i = 0; result == 0 && i < fftype->fft_nr; ++i)
		result = ftype_sub_xcode(fftype, cur, obj, i, 0, what);
	return result;
}

static int xcode_bufvec_union(struct c2_fop_field_type *fftype,
		       	      struct c2_bufvec_cursor *cur, void *obj,
			      enum c2_bufvec_what what)
{
	return -EIO;
}

static bool xcode_is_byte_array(const struct c2_fop_field_type *fftype)
{
	C2_ASSERT(fftype->fft_aggr == FFA_SEQUENCE);
	return fftype->fft_child[1]->ff_type == &C2_FOP_TYPE_BYTE;
}

static int xcode_bufvec_sequence(struct c2_fop_field_type *fftype,
				 struct c2_bufvec_cursor *cur, void *obj,
				 enum c2_bufvec_what what)
{
	struct   fop_sequence {
		uint32_t 	count;
		void		*buf;
	};

	struct fop_sequence *fseq;
	int      	     rc;
	uint32_t 	     nr;
	int		     i;

	fseq = obj;
	if (what == C2_BUFVEC_ENCODE) {
		/*Call generic encode function for array */
		nr = fseq->count;
		rc = atom_xcode[FPF_U32]( cur, &nr, C2_BUFVEC_ENCODE);
		if (rc != 0)
			return rc;
		/* Check if its a byte array */
		if (xcode_is_byte_array(fftype))
			/* Call byte array encode function */
			return c2_bufvec_bytes(cur, (char **)&obj, (size_t)nr,
			~0, enum c2_bufvec_what what);
		} else if (what  == C2_BUFVEC_DECODE) {
		rc = atom_xcode[FPF_U32]( cur, &nr, C2_BUFVEC_DECODE);
			if(rc != 0)
				return rc;
		fseq->count = nr;
		/* Check if its a byte array  */
		if(xcode_is_byte_array(fftype))
			 return c2_bufvec_bytes(cur, (char **)&obj, (size_t)nr,
			 			~0,C2_BUFVEC_DECODE);
	}
	for (rc = 0, i = 0; rc == 0 && i < nr; ++i)
			rc = ftype_sub_xcode(fftype, cur, obj, 1, i, what);
	return rc;
}

static int xcode_bufvec_typedef(struct c2_fop_field_type *fftype,
				struct c2_bufvec_cursor *cur, void *obj,
				enum c2_bufvec_what what )
{
	return ftype_sub_xcode(fftype, cur, obj, 0, 0, what);
}

static int (*atom_xcode[FPF_NR])(struct c2_bufvec_cursor *cur, void *obj,
	    			enum c2_bufvec_what what) = {
	[FPF_VOID] =  NULL,
	[FPF_BYTE]  =  (void *)&c2_bufvec_byte,
	[FPF_U32]  =  (void *)&c2_bufvec_uint32,
	[FPF_U64]  =  (void *)&c2_bufvec_uint64
};


static int xcode_bufvec_atom(struct c2_fop_field_type *fftype,
		             struct c2_bufvec_cursor *cur, void *obj,
		             enum c2_bufvec_what what)
{
	C2_ASSERT(fftype->fft_u.u_atom.a_type <
		  ARRAY_SIZE(atom_xcode[]));
	return atom_xcode[fftype->fft_u.u_atom.a_type](cur, obj, what);
}
/*
static int kxdr_atom_rep(struct kxdr_ctx *ctx, void *obj)
{
	*ctx->kc_nob += ctx->kc_type->fft_layout->fm_sizeof;
	return 0;
}
*/
static const c2_xcode_foptype_t xcode_disp[FFA_NR] = {
	[FFA_RECORD]   = xcode_bufvec_record,
	[FFA_UNION]    = xcode_bufvec_union,
	[FFA_SEQUENCE] = xcode_bufvec_sequence,
	[FFA_TYPEDEF]  = xcode_bufvec_typedef,
	[FFA_ATOM]     = xcode_bufvec_atom
};

int c2_bufvec_fop_type(struct c2_bufvec_cursor *cur,
		       const struct c2_fop_field_type *fftype, void *data,
		       enum kxdr_what what)
{

	C2_ASSERT(ftype->fft_aggr < ARRAY_SIZE(xcode_disp));
	return xcode_disp[ftype->fft_aggr](fftype, cur, data, what);
}

int c2_bufvec_fop(struct c2_bufvec_cursor *cur, void *data,
		  enum c2_bufvec_what what);
{
	return c2_bufvec_fop_type(cur, fop->f_type->ft_top, c2_fop_data(fop),
				  what);
}

static int c2_fop_decode(void *req, __be32 *data, struct c2_fop *fop)
{
	return c2_fop_type_encdec(fop->f_type->ft_top,
				  req, data, c2_fop_data(fop), KDEC);
}



/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
