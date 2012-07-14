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

   This file defines generic functions to calculate the onwire fop size.
   Main entry point is c2_xcode_fop_size_get() which calculates the onwire
   size (in bytes) of the fop data that would be encode/decoded into/from
   a bufvec. The size is calculated by recursively descending through the fop
   format tree.

   When handling a non-leaf (i.e., "aggregating") node of a fop format tree,
   control branches though the xcode_size_disp[] function pointer array, using
   aggregation type as an index.

   @{
 */


/* Generic function prototype for calculating the size of fop field types */
typedef void (*c2_xcode_foptype_sizeget_t)(size_t *size,
	     struct c2_fop_field_type *ftype, void *fop_data);

/**
   Array of function pointers to calcluate onwire size for each supported
   fop field type
*/
static	c2_xcode_foptype_sizeget_t xcode_size_disp[FFA_NR];


/**
   Local function declarations for various fop field types.See individual
   headers for more details.
*/
static void xcode_record_size_get(size_t *size,
            struct c2_fop_field_type *fftype, void *fop_data);

static void xcode_union_size_get(size_t *size, struct c2_fop_field_type *fftype,
				void *fop_data);

static void xcode_typedef_size_get(size_t *size,
	    struct c2_fop_field_type *fftype, void *fop_data);

static void xcode_atom_size_get(size_t *size, struct c2_fop_field_type *fftype,
			       void *fop_data);

static void xcode_sequence_size_get(size_t *size,
	    struct c2_fop_field_type *fftype, void *fop_data);

/** Dispatcher array of "size_get" vectors for various fop field types */
static c2_xcode_foptype_sizeget_t xcode_size_disp[FFA_NR] = {
	[FFA_RECORD]   = xcode_record_size_get,
	[FFA_UNION]    = xcode_union_size_get,
	[FFA_SEQUENCE] = xcode_sequence_size_get,
	[FFA_TYPEDEF]  = xcode_typedef_size_get,
	[FFA_ATOM]     = xcode_atom_size_get
};

/** Calculates the onwire size of the atomic field type. Currently, each
    xcode unit uses 8 bytes to satisfy the alignment requirements.
    @see xcode/bufvec_xcode.h

    @param size Pointer to calculated onwire size value for the fop.
    @param fftype Field type for the fop for which the size is to be calculated.
    @param fop_data pointer to fop data.

*/
static void xcode_atom_size_get(size_t *size, struct c2_fop_field_type *fftype,
				  void *fop_data)
{
	C2_PRE(size != NULL);

	*size += BYTES_PER_XCODE_UNIT;
}

/**
   Helper function for handling non-leaf node of a fop format tree. Internally
   calls dispatch function for specific aggregation type using it as index.
   @see xcode_size_disp

   @param size Pointer to calculated onwire size value for the fop.
   @param fftype Field type for the fop for which the size is to be calculated.
   @param fieldno The fop field no.
   @param elno The element number for which the size is to be calculated,
    usually non-zero for sequence type fop fields.

*/
static void xcode_subtype_size(size_t *size, struct c2_fop_field_type *fftype,
			      void *fop_data, uint32_t fieldno, uint32_t elno)
{
	struct c2_fop_field_type        *ff_subtype;
	uint32_t			 fop_field_aggr_type;
	void				*obj_field_addr;

	C2_PRE(fftype != NULL);
	C2_PRE(fop_data != NULL);
	C2_PRE(fieldno < fftype->fft_nr);
	C2_PRE(size != NULL);

	obj_field_addr = c2_fop_type_field_addr(fftype, fop_data, fieldno,
						elno);
	ff_subtype = fftype->fft_child[fieldno]->ff_type;
	fop_field_aggr_type = ff_subtype->fft_aggr;
	return xcode_size_disp[fop_field_aggr_type]
	      (size, ff_subtype, obj_field_addr);
}

/**
   Calculates the size of record onwire fop field type into a bufvec. We
   traverse each field of the record recursively, till a atomic field is
   reached, calculate its size and add it to overall size.

    @param size Pointer to calculated onwire size value for the fop.
    @param fftype Field type for the fop for which the size is to be calculated.
    @param fop_data pointer to fop data.

*/
static void xcode_record_size_get(size_t *size,
				  struct c2_fop_field_type *fftype,
				  void *fop_data)
{
	size_t fft_cnt;
	size_t rec_size = 0;

	C2_PRE(size != NULL);
	C2_PRE(fftype != NULL);
	C2_PRE(fop_data != NULL);

	for (fft_cnt = 0; fft_cnt < fftype->fft_nr;
	     ++fft_cnt) {
		 /* A fop record based field cannot contain more than 1 element
		   unlike a sequence based field. We pass this(ELEMENT_ZERO) as
		   a parameter to xcode_subtype_size */
		 xcode_subtype_size(&rec_size, fftype, fop_data, fft_cnt,
				     ELEMENT_ZERO);
	}
	C2_ASSERT(rec_size != 0);
	*size += rec_size;
}


/**
   Calculates the size of an onwire byte array. The onwire size of the byte
   array is calculated to be the sum of "count" + "pad_bytes" (if any). The
   pad bytes are required to keep the byte array 8 byte aligned.

    @param count No of bytes in the byte array.

    @retval The onwire size of the byte array (sum of count and pad_bytes).
*/
static size_t xcode_byte_seq_size_get(uint32_t count)
{
	size_t		size;

	C2_PRE(count != 0);

        size  = (count + MAX_PAD_BYTES) & XCODE_UNIT_ALIGNED_MASK;
	return size;
}

/**
  Calculates the onwire size of fop data which belongs to a sequence type fop
  field . If the sequence is a plain byte array, it calls corresponding function
  to calculate onwire size of  byte-array. If not, it traverses the fop field
  tree and calls the corresponding function to calculate size for that field
  type.

   @param size Pointer to calculated onwire size value for the fop.
   @param fftype Field type for the fop for which the size is to be calculated.
   @param fop_data pointer to fop data.

*/
static void xcode_sequence_size_get(size_t *size,
	    struct c2_fop_field_type *fftype, void *fop_data)
{

	struct c2_fop_sequence   *fseq;
	uint32_t                 nr;
	int		         cnt;
	size_t			 seq_size = 0;

	C2_PRE(fftype != NULL);
	C2_PRE(fop_data != NULL);
	C2_PRE(size != NULL);

	fseq = fop_data;
	nr = fseq->fs_count;
	xcode_atom_size_get(size, fftype, fop_data);
	/*
	 * Check if its a byte array and call function to calc byte array size.
	 */
	if (c2_xcode_is_byte_array(fftype))
		seq_size = xcode_byte_seq_size_get(nr);
	else {
		/*
		 * This is an sequence of atomic types or aggr types(record,
		 * sequence etc). Traverse through each subtype and calculate
		 * size.
		 */
		for (cnt = 0; cnt < nr; ++cnt) {
			/*
			 * A fop sequence's "zeroth" field contains the count
			 * and "first" field contains the actual data. We pass
			 * this as an argument to xcode_subtype_size
			 */
			xcode_subtype_size(&seq_size, fftype, fseq,
					   FOP_FIELD_ONE, cnt);
		}
	}
	*size += seq_size;
}

/**
   Calculates the onwire size of typedef fop field.

   @param size Pointer to calculated onwire size value for the fop.
   @param fftype Field type for the fop for which the size is to be calculated.
   @param fop_data pointer to fop data.

   @retval 0 On success.
   @retval -errno on failure.
*/
static void xcode_typedef_size_get(size_t *size,
	    struct c2_fop_field_type *fftype, void *fop_data)
{
	C2_PRE(fftype != NULL);
	C2_PRE(fop_data != NULL);

	/* A fop typedef field contains just one field and one element. These
	 * are passed as arguments to xcode_subtype_size (FOP_FIELD_ZERO,
	 * ELEMENT_ZERO.
	 */
	return xcode_subtype_size(size, fftype, fop_data, FOP_FIELD_ZERO,
				  ELEMENT_ZERO);
}

/** XXX: Currently unions are not supported. */
static void xcode_union_size_get(size_t *size, struct c2_fop_field_type *fftype,
				 void *fop_data)
{
	return;
}

/**
  Calls correspoding function to calculate onwire size based on the fop top
  level field type.

   @param size Pointer to calculated onwire size value for the fop.
   @param fftype Field type for the fop for which the size is to be calculated.
   @param fop_data pointer to fop data.

   @pre fftype != NULL;
   @pre data != NULL;
   @pre fftype->fft_aggr < ARRAY_SIZE(xcode_size_disp);

   @retval 0 On success.
   @retval -errno on failure.
*/
void c2_xcode_fop_type_size_get(size_t *size,
		                struct c2_fop_field_type *fftype, void *data)
{
	C2_PRE(fftype != NULL);
	C2_PRE(data != NULL);
	C2_PRE(fftype->fft_aggr < ARRAY_SIZE(xcode_size_disp));

	return xcode_size_disp[fftype->fft_aggr](size, fftype, data);
}

/**
  Calculates the onwire size of fop data. This function internally calls
  the fop field type specific functions to calculate the size

  @param fop The data for this fop is to be encoded/decoded.

  @retval Onwire size of the fop in bytes.
*/

size_t c2_xcode_fop_size_get(struct c2_fop *fop)
{
	size_t		size = 0;

	C2_PRE(fop != NULL);

	if (fop->f_type->ft_top != NULL) {
		c2_xcode_fop_type_size_get(&size, fop->f_type->ft_top,
					   c2_fop_data(fop));
	} else {
		c2_xcode_ctx_init(&fop->f_type->ft_xc_ctx,
				  &(struct c2_xcode_obj){
					  *fop->f_type->ft_xc_type,
						  c2_fop_data(fop)});
		size = c2_xcode_length(&fop->f_type->ft_xc_ctx);
	}
	return size;
}
C2_EXPORTED(c2_xcode_fop_size_get);

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
