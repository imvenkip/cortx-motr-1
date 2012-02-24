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

   See head comments in fop_format.h for overview of fop formats.

   This file defines "universal" fop-to-bufvec encode/decode functions.
   Main entry point is c2_bufvec_fop() which encodes/decodes fop data into/from
   a bufvec. Encode/Decode operations are implemented by recursively descending
   through the fop format tree. The implementation logic is the same that has
   been used in net/ksunrpc/kxdr.c.

   When handling a non-leaf (i.e., "aggregating") node of a fop format tree,
   control branches though the xcode_type_disp[] function pointer array, using
   aggregation type as an index. When a leaf (i.e., "atomic") node is reached,
   control branches through the xcode_atom_disp[] function pointer array, using
   atomic type as an index.

   @{
 */

/* Generic xcode function prototype for fop field types */
typedef int (*c2_xcode_foptype_t)(struct c2_fop_field_type *ftype,
				  struct c2_bufvec_cursor *cur, void *fop_data,
				  enum c2_bufvec_what what);

/** Array of xcode function pointers for each supported fop field type */
static	c2_xcode_foptype_t xcode_type_disp[FFA_NR];

/** Array of xcode function pointers for atomic fop field types */
static int (*xcode_atom_disp[FPF_NR])(struct c2_bufvec_cursor *cur,
	    void *fop_data, enum c2_bufvec_what);

/**
  Dispatcher array of xcode funtions for atomic field types.
*/
static int (*xcode_atom_disp[FPF_NR])(struct c2_bufvec_cursor *cur,
	    void *fop_data,enum c2_bufvec_what what) = {
	[FPF_VOID]  =  NULL,
	[FPF_BYTE]  =  (void *)&c2_bufvec_byte,
	[FPF_U32]   =  (void *)&c2_bufvec_uint32,
	[FPF_U64]   =  (void *)&c2_bufvec_uint64
};

/**
   Local xcode function declarations for various fop field types.See individual
   headers for more details.
*/
static int xcode_bufvec_record(struct c2_fop_field_type *fftype,
                               struct c2_bufvec_cursor *cur,
			       void *fop_data, enum c2_bufvec_what what);

static int xcode_bufvec_union(struct c2_fop_field_type *fftype,
			      struct c2_bufvec_cursor *cur, void *fop_data,
		              enum c2_bufvec_what what);

static int xcode_bufvec_typedef(struct c2_fop_field_type *fftype,
				struct c2_bufvec_cursor *cur, void *fop_data,
				enum c2_bufvec_what what);

static int xcode_bufvec_atom(struct c2_fop_field_type *fftype,
		             struct c2_bufvec_cursor *cur, void *fop_data,
		             enum c2_bufvec_what what);

static int xcode_bufvec_sequence(struct c2_fop_field_type *fftype,
				 struct c2_bufvec_cursor *cur, void *fop_data,
				 enum c2_bufvec_what what);

/** Dispatcher array of xcode function pointers for various fop field types */
static c2_xcode_foptype_t xcode_type_disp[FFA_NR] = {
	[FFA_RECORD]   = &xcode_bufvec_record,
	[FFA_UNION]    = &xcode_bufvec_union,
	[FFA_SEQUENCE] = &xcode_bufvec_sequence,
	[FFA_TYPEDEF]  = &xcode_bufvec_typedef,
	[FFA_ATOM]     = &xcode_bufvec_atom
};

/**
   Helper function for handling non-leaf node of a fop format tree. Internally
   calls dispatch function for specific aggregation type using it as index.
   @see xcode_type_disp

   @param fftype field type for the fop is to be encoded/decoded.
   @param cur current position of bufvec cursor.
   @param fop_data pointer to fop data to be encode/decoded.
   @param fieldno The fop field no.
   @param elno The element no to be encoded/decoded, usually non-zero for
    sequence type fop fields.
   @param what The type of operation to be performed - encode or decode.

   @retval 0 On success.
   @retval -errno on failure.
*/
static int xcode_fop_subtype(struct c2_fop_field_type *fftype,
			     struct c2_bufvec_cursor *cur, void *fop_data,
			     uint32_t fieldno, uint32_t elno,
			     enum c2_bufvec_what what)
{
	struct c2_fop_field_type        *ff_subtype;
	uint32_t			 fop_field_aggr_type;
	void				*obj_field_addr;

	C2_PRE(fftype != NULL);
	C2_PRE(cur != NULL);
	C2_PRE(fop_data != NULL);
	C2_PRE(fieldno < fftype->fft_nr);

	obj_field_addr = c2_fop_type_field_addr(fftype, fop_data, fieldno,
						elno);
	ff_subtype = fftype->fft_child[fieldno]->ff_type;
	fop_field_aggr_type = ff_subtype->fft_aggr;
	return xcode_type_disp[fop_field_aggr_type]
			      (ff_subtype, cur, obj_field_addr, what);
}

/**
   Encode/Decode a record fop field type into a bufvec. We traverse each field
   of the record recursively, till a atomic field is reached and xcode it into
   the bufvec.

   @param fftype field type for the fop is to be encoded/decoded.
   @param cur current position of bufvec cursor.
   @param fop_data pointer to fop data to be encode/decoded.
   @param what The type of operation to be performed - encode or decode.

   @retval 0 On success.
   @retval -errno on failure.
*/
static int xcode_bufvec_record(struct c2_fop_field_type *fftype,
			       struct c2_bufvec_cursor *cur,
			       void *fop_data, enum c2_bufvec_what what)
{
	size_t fft_cnt;
	int    rc;

	for (rc = 0, fft_cnt = 0; rc == 0 && fft_cnt < fftype->fft_nr;
	     ++fft_cnt)
		rc = xcode_fop_subtype(fftype, cur, fop_data, fft_cnt,
				       ELEMENT_ZERO, what);
	return rc;
}

/**
  Returns true if the current fop field type is a byte array

  @param fftype the fop field type.

  @retval true if field type is a byte array.
  @retval false if field type is not a byte array.
*/
bool c2_xcode_is_byte_array(const struct c2_fop_field_type *fftype)
{
	C2_ASSERT(fftype->fft_aggr == FFA_SEQUENCE);
	return fftype->fft_child[1]->ff_type == &C2_FOP_TYPE_BYTE;
}

/**
   Encode/Decode a byte array into a bufvec. While decoding from a bufvec,
   memory required by the array is calculated and allocated. Freeing
   this memory is the responsibility of upper layers.

   @param fftype field type for the fop is to be encoded/decoded.
   @param cur current position of bufvec cursor.
   @param fop_data pointer to fop data to be encode/decoded.
   @param what The type of operation to be performed - encode or decode.

   @retval 0 On success.
   @retval -errno on failure.
*/
static int xcode_bufvec_byte_seq(struct c2_bufvec_cursor *cur, char **b_seq,
				  uint32_t nr, enum c2_bufvec_what what)
{
	C2_PRE(cur != NULL);
	C2_PRE(b_seq != NULL);

	if(what == C2_BUFVEC_DECODE) {
		C2_ALLOC_ARR(*b_seq, nr);
                if (*b_seq == NULL)
			return -ENOMEM;
		}
	return c2_bufvec_bytes(cur, b_seq, (size_t)nr, ~0, what);
}

/**
  Encodes/Decodes fop data which belongs to a sequence type fop field into a
  bufvec. If the sequence is a plain byte array, it calls corresponding function
  to encode/decode bytes into/from a bufvec. If not, it traverses the fop field
  tree and calls the corresponding xcode function for that field type.
  While decode, the memory required by the sequence data  is calculated and
  allocated. Freeing this memory is the responsibility of upper layers.

  @param fftype field type for the fop is to be encoded/decoded.
  @param cur current position of bufvec cursor.
  @param fop_data pointer to fop sequence data to be encode/decoded.
  @param what The type of operation to be performed - encode or decode.

  @retval 0 On success.
  @retval -errno on failure.
*/
static int xcode_bufvec_sequence(struct c2_fop_field_type *fftype,
				 struct c2_bufvec_cursor *cur, void *fop_data,
				 enum c2_bufvec_what what)
{

	struct c2_fop_sequence  *fseq;
	int                      rc;
	uint32_t                 nr;
	int		         cnt;
	void		        *s_data;
	size_t			 elsize;
	struct c2_fop_memlayout *ellay;

	C2_PRE(fftype != NULL);
	C2_PRE(cur != NULL);
	C2_PRE(fop_data != NULL);

	fseq = fop_data;
	if (what == C2_BUFVEC_ENCODE) {
		nr = fseq->fs_count;
		/* First encode the "count" field into the bufvec */
		rc = xcode_atom_disp[FPF_U32](cur, &nr, C2_BUFVEC_ENCODE);
		if (rc != 0)
			return rc;
		/*
		 * Check if its a byte array and call the byte array encode
		 *  function.
		 */
		if (c2_xcode_is_byte_array(fftype))
			return xcode_bufvec_byte_seq(cur,
				(char **)&fseq->fs_data, nr, what);
	} else if (what  == C2_BUFVEC_DECODE) {
		rc = xcode_atom_disp[FPF_U32](cur, &nr, C2_BUFVEC_DECODE);
			if(rc != 0)
				return rc;
		fseq->fs_count = nr;
		/* Detect if it's byte sequence */
		if(c2_xcode_is_byte_array(fftype)) {
			return xcode_bufvec_byte_seq(cur,
				(char **)&fseq->fs_data, nr, what);
		}
		/*
		 * This is an sequence of atomic types or aggr types(record,
		 * sequence etc). During decode, calculate and allocate memory
		 * for the sequence based on its in-memory layout.
		 */
		ellay = fftype->fft_child[1]->ff_type->fft_layout;
		elsize = ellay->fm_sizeof;
		s_data = c2_alloc(elsize * nr);
		if(s_data == NULL)
			return -ENOMEM;
		fseq->fs_data = s_data;
	}
	for (rc = 0, cnt = 0; rc == 0 && cnt < nr; ++cnt)
		rc = xcode_fop_subtype(fftype, cur, fseq, FOP_FIELD_ONE,
		                       cnt, what);

	return rc;
}

/**
  Enc/Decodes a typedef fop field into/from a bufvec.

  @param fftype field type for the fop is to be encoded/decoded.
  @param cur current position of bufvec cursor.
  @param fop_data pointer to fop typedef data to be encode/decoded.
  @param what The type of operation to be performed - encode or decode.

  @retval 0 On success.
  @retval -errno on failure.
*/
static int xcode_bufvec_typedef(struct c2_fop_field_type *fftype,
				struct c2_bufvec_cursor *cur, void *fop_data,
				enum c2_bufvec_what what )
{
	C2_PRE(fftype != NULL);
	C2_PRE(cur != NULL);
	C2_PRE(fop_data != NULL);

	return xcode_fop_subtype(fftype, cur, fop_data, FOP_FIELD_ZERO,
				 ELEMENT_ZERO, what);
}

/** XXX: Currently unions are not supported. */
int xcode_bufvec_union(struct c2_fop_field_type *fftype,
		       struct c2_bufvec_cursor *cur, void *fop_data,
		       enum c2_bufvec_what what)
{
	return -EIO;
}

/**
  Helper function to dispatch the xcode functions for atomic fop field types

  @param fftype field type for the fop is to be encoded/decoded.
  @param cur current position of bufvec cursor.
  @param fop_data pointer to fop atomic data to be encode/decoded.
  @param what The type of operation to be performed - encode or decode.

  @retval 0 On success.
  @retval -errno on failure.
*/
static int xcode_bufvec_atom(struct c2_fop_field_type *fftype,
		             struct c2_bufvec_cursor *cur, void *fop_data,
		             enum c2_bufvec_what what)
{
	C2_PRE(fftype != NULL);
	C2_PRE(cur != NULL);
	C2_PRE(fop_data != NULL);
	C2_PRE(fftype->fft_u.u_atom.a_type < ARRAY_SIZE(xcode_atom_disp));

	return xcode_atom_disp[fftype->fft_u.u_atom.a_type](cur, fop_data, what);
}


/**
  Calls correspoding xcode function based on the fop top level field type.

  @param cur current position of the bufvec cursor.
  @param fftype Top level field type for the fop is to be encoded/decoded.
  @param data  Pointer to the data to be encoded/decoded.
  @param what The type of operation to be performed - encode or decode.

  @pre cur != NULL;
  @pre fftype != NULL;
  @pre data != NULL;
  @pre fftype->fft_aggr < ARRAY_SIZE(xcode_type_disp);

  @retval 0 On success.
  @retval -errno on failure.
*/
int c2_xcode_bufvec_fop_type(struct c2_bufvec_cursor *cur,
		       struct c2_fop_field_type *fftype, void *data,
		       enum c2_bufvec_what what)
{
	C2_PRE(cur != NULL);
	C2_PRE(fftype != NULL);
	C2_PRE(data != NULL);
	C2_PRE(fftype->fft_aggr < ARRAY_SIZE(xcode_type_disp));

	return xcode_type_disp[fftype->fft_aggr](fftype, cur, data, what);
}

/**
  Encode/Decode a fop data into a bufvec. This function internally calls
  the type specific encode/decode functions for a fop.

  @param vc current position of the bufvec cursor.
  @param fop The data for this fop is to be encoded/decoded.
  @param what The type of operation to be performed - encode or decode.

  @retval 0 On success.
  @retval -errno on failure.
*/
int c2_xcode_bufvec_fop(struct c2_bufvec_cursor *cur, struct c2_fop *fop,
		  enum c2_bufvec_what what)
{
	C2_PRE(cur != NULL);
	C2_PRE(fop != NULL);

	return c2_xcode_bufvec_fop_type(cur, fop->f_type->ft_top,
	                                c2_fop_data(fop), what);
}
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
