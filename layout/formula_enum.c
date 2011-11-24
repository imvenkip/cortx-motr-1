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
 * Original author: Trupti Patil <trupti_patil@xyratex.com>
 * Original creation date: 11/16/2011
 */

#include "layout/formula_enum.h"

/**
   @addtogroup formula_enum

   @{
*/

/**
   Implementation of leto_decode() for formula enumeration type.
   Continues to decode layout representation stored in the buffer and
   to create the layout.
*/
static int layout_formula_enum_decode(const struct c2_bufvec_cursor *cur,
				      struct c2_layout **out)
{
   /**
	@code
	Read formula enumeration type specific fields like formula
	from the buffer.

	@endcode
   */
	return 0;
}

/**
   Implementation of leto_encode() for formula enumeration type.
   Continues to store layout representation in the buffer.
*/
static int layout_formula_enum_encode(const struct c2_layout *l,
				      struct c2_bufvec_cursor *cur)
{
   /**
	@code
	Read formula enumeration type specific fields like formula.
	@endcode
   */
	return 0;
}

/**
   Do not need functions like formula_rec_add, lto_rec_delete,
   lto_rec_update and lto_rec_lookup unless we want to store
   attributes (applicable only for PDCLIUST type of layout
   with FORMULA enumeration type) in a table different than
   layouts.
*/


static const struct c2_layout_enum_type_ops formula_ops = {
	.leto_decode     = layout_formula_enum_decode,
	.leto_encode     = layout_formula_enum_encode,
	.leto_rec_add    = NULL,
	.leto_rec_delete = NULL,
	.leto_rec_update = NULL,
	.leto_rec_lookup = NULL,
};

const struct c2_layout_enum_type c2_layout_formula_enum_type = {
	.let_ops = &formula_ops
};

static const struct c2_layout_formula_ops nkp_ops = {
	.lfo_subst = NULL
};

const struct c2_layout_formula c2_formula_NKP_formula = {
	.lf_id   = { .u_hi = 0x5041524954594445, /* PARITYDE */
		     .u_lo = 0x434c55535445522e  /* CLUSTER. */
	},
	.lf_ops  = &nkp_ops
};

/** @} end group formula_enum */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
