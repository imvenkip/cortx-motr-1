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
 * Original author: Subhash Arya <subhash_arya@xyratex.com>
 * Original creation date: 08/21/2011
 */

#include "lib/cdefs.h"
#include "fop/fop.h"
#include "xcode/bufvec_xcode.h"
#include "lib/errno.h"
#include "lib/memory.h"
/**
   @addtogroup xcode

   <b>Fop xcode</b>

   See head comment in fop_format.h for overview of fop formats.

   This file defines "universal" fop-to-bufvec encode/decode functions.
   Main entry point is c2_bufvec_fop() which encodes/decodes fop data into/from
   a bufvec. Encode/Decode operations are implemented by recursively descending
   through the fop format tree. The implementation logic is the same that has
   been used in net/ksunrpc/kxdr.c.

   When handling a non-leaf (i.e., "aggregating") node of a fop format tree,
   control branches though the xcode_disp[] function pointer array, using
   aggregation type as an index. When a leaf (i.e., "atomic") node is reached,
   control branches through the atom_xcode[] function pointer array, using atom
   type as an index.

   @{
 */

/* Generic xcode function prototype for fop field types */
typedef int (*c2_xcode_foptype_t)(struct c2_fop_field_type *ftype,
				  struct c2_bufvec_cursor *cur, void *data,
				  enum c2_bufvec_what what);

/** Array of xcode function pointers for each supported fop field type */
static	c2_xcode_foptype_t xcode_disp[FFA_NR];

/** Array of xcode function pointers for atomic fop field types */
static int (*atom_xcode[FPF_NR])(struct c2_bufvec_cursor *cur, void *obj,
				 enum c2_bufvec_what);

/**
  Calls the dispatch function for a non-leaf node of a fop format tree. The
  aggregation type is used as the index.
*/
static c2_xcode_foptype_t ftype_field_xcode(struct c2_fop_field_type *fftype,
				     int fieldno, enum c2_bufvec_what what)
{
	C2_PRE(fftype != NULL);
	C2_PRE(fieldno < fftype->fft_nr);

	return xcode_disp[fftype->fft_child[fieldno]->ff_type->fft_aggr];
}

/**
   Helper function for handling non-leaf node of a fop format tree. Internally
   calls dispatch function for specific aggregation type using it as index.
   @see xcode_disp
*/
static int ftype_sub_xcode(struct c2_fop_field_type *fftype,
			   struct c2_bufvec_cursor *cur, void *obj,
			   int fieldno, uint32_t elno,
			   enum c2_bufvec_what what)
{
	struct c2_fop_field_type        *ff_subtype;

	C2_PRE(fftype != NULL);
	C2_PRE(cur != NULL);
	C2_PRE(obj != NULL);

	ff_subtype = fftype->fft_child[fieldno]->ff_type;
	return ftype_field_xcode(fftype, fieldno, what)
	(ff_subtype, cur, c2_fop_type_field_addr(fftype, obj, fieldno, elno),
	what);
}

/**
 Encode/Decode a record fop field type into a bufvec. We traverse each field
 of the record recursively, till a atomic field is reached and xcode it into
 the bufvec.
*/
static int xcode_bufvec_record(struct c2_fop_field_type *fftype,
			       struct c2_bufvec_cursor *cur,
			       void *obj, enum c2_bufvec_what what)
{
	size_t i;
	int    rc;

	for (rc = 0, i = 0; rc == 0 && i < fftype->fft_nr; ++i)
		rc = ftype_sub_xcode(fftype, cur, obj, i, 0, what);
	return rc;
}


/** Checks if the fop field type is a byte array */
static bool xcode_is_byte_array(const struct c2_fop_field_type *fftype)
{
	C2_ASSERT(fftype->fft_aggr == FFA_SEQUENCE);
	return fftype->fft_child[1]->ff_type == &C2_FOP_TYPE_BYTE;
}

/**
  Encode/Decodes fop data which belongs to a sequence type fop field into a
  bufvec. If the sequence is a plain byte array, it calls corresponding function
  to encode/decode bytes into/from a bufvec. If not, it traverses the fop field
  tree and calls the corresponding xcode function for that field type.
*/
static int xcode_bufvec_sequence(struct c2_fop_field_type *fftype,
				 struct c2_bufvec_cursor *cur, void *obj,
				 enum c2_bufvec_what what)
{
	struct   fop_sequence {
		uint32_t         fs_count;
		void		*fs_data;
	};

	struct fop_sequence     *fseq;
	int                      rc;
	uint32_t                 nr;
	int		         i;
	void		        *s_data;
	size_t			 elsize;
	struct c2_fop_memlayout *ellay;

	C2_PRE(fftype != NULL);
	C2_PRE(cur != NULL);
	C2_PRE(obj != NULL);

	fseq = obj;
	if (what == C2_BUFVEC_ENCODE) {
		nr = fseq->fs_count;
		/* First encode the "count" field into the bufvec */
		rc = atom_xcode[FPF_U32](cur, &nr, C2_BUFVEC_ENCODE);
		if (rc != 0)
			return rc;
		/*
		 * Check if its a byte array and call the byte array encode
		 *  function
		 */
		if (xcode_is_byte_array(fftype))
			return c2_bufvec_bytes(cur, (char **)&fseq->fs_data, nr,
					       ~0, what);
	} else if (what  == C2_BUFVEC_DECODE) {
		rc = atom_xcode[FPF_U32](cur, &nr, C2_BUFVEC_DECODE);
			if(rc != 0)
				return rc;
		fseq->fs_count = nr;
		if(xcode_is_byte_array(fftype)) {
			char 	  **b_seq;

			b_seq = (char **)&fseq->fs_data;
			C2_ALLOC_ARR(*b_seq, nr);
			if (*b_seq == NULL)
				return -ENOMEM;
			return c2_bufvec_bytes(cur, b_seq, (size_t)nr, ~0,
			       C2_BUFVEC_DECODE);
		}
		ellay = fftype->fft_child[1]->ff_type->fft_layout;
		elsize = ellay->fm_sizeof;
		s_data = c2_alloc(elsize * nr);
		if(s_data == NULL)
			return -ENOMEM;
		fseq->fs_data = s_data;
	}
	for (rc = 0, i = 0; rc == 0 && i < nr; ++i)
		rc = ftype_sub_xcode(fftype, cur, fseq, 1, i, what);

	return rc;
}

/**
  Enc/Decodes a typedef fop field into/from a bufvec.
*/
static int xcode_bufvec_typedef(struct c2_fop_field_type *fftype,
				struct c2_bufvec_cursor *cur, void *obj,
				enum c2_bufvec_what what )
{
	C2_PRE(fftype != NULL);
	C2_PRE(cur != NULL);
	C2_PRE(obj != NULL);

	return ftype_sub_xcode(fftype, cur, obj, 0, 0, what);
}

/** XXX: Currently unions are not supported. */
int xcode_bufvec_union(struct c2_fop_field_type *fftype,
		       struct c2_bufvec_cursor *cur, void *obj,
		       enum c2_bufvec_what what)
{
	return -EIO;
}

/**
  Dispatcher array of xcode funtions for atomic field types.
*/
static int (*atom_xcode[FPF_NR])(struct c2_bufvec_cursor *cur, void *obj,
			         enum c2_bufvec_what what) = {
	[FPF_VOID]  =  NULL,
	[FPF_BYTE]  =  (void *)&c2_bufvec_byte,
	[FPF_U32]   =  (void *)&c2_bufvec_uint32,
	[FPF_U64]   =  (void *)&c2_bufvec_uint64
};

/**
  Helper function to dispatch the xcode functions for atomic fop field types
*/
static int xcode_bufvec_atom(struct c2_fop_field_type *fftype,
		             struct c2_bufvec_cursor *cur, void *obj,
		             enum c2_bufvec_what what)
{
	C2_PRE(fftype != NULL);
	C2_PRE(cur != NULL);
	C2_PRE(obj != NULL);
	C2_PRE(fftype->fft_u.u_atom.a_type < ARRAY_SIZE(atom_xcode));

	return atom_xcode[fftype->fft_u.u_atom.a_type](cur, obj, what);
}

/** Dispatcher array of xcode function pointers for various fop field types */
static c2_xcode_foptype_t xcode_disp[FFA_NR] = {
	[FFA_RECORD]   = &xcode_bufvec_record,
	[FFA_UNION]    = &xcode_bufvec_union,
	[FFA_SEQUENCE] = &xcode_bufvec_sequence,
	[FFA_TYPEDEF]  = &xcode_bufvec_typedef,
	[FFA_ATOM]     = &xcode_bufvec_atom
};

/**
  Calls correspoding xcode function based on the fop top level field type.

  @param cur current position of the bufvec cursor.
  @param fftype Top level field type for the fop is to be encoded/decoded.
  @param data  Pointer to the data to be encoded/decoded.
  @param what The type of operation to be performed - encode or decode.
  @retval 0 On success.
  @retval -errno on failure.
*/
int c2_bufvec_fop_type(struct c2_bufvec_cursor *cur,
		       struct c2_fop_field_type *fftype, void *data,
		       enum c2_bufvec_what what)
{
	C2_PRE(cur != NULL);
	C2_PRE(fftype != NULL);
	C2_PRE(data != NULL);
	C2_PRE(fftype->fft_aggr < ARRAY_SIZE(xcode_disp));

	return xcode_disp[fftype->fft_aggr](fftype, cur, data, what);
}

int c2_bufvec_fop(struct c2_bufvec_cursor *cur, struct c2_fop *fop,
		  enum c2_bufvec_what what)
{
	C2_PRE(cur != NULL);
	C2_PRE(fop != NULL);

	return c2_bufvec_fop_type(cur, fop->f_type->ft_top, c2_fop_data(fop),
				  what);
}
C2_EXPORTED(c2_bufvec_fop);
/** @} */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
