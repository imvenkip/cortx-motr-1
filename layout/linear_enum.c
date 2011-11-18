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

#include "layout/linear_enum.h""

/**
   @addtogroup linear_enum

   @{
*/

/** 
   Implementation of leto_decode() for linear enumeration type.
*/
static int layout_linear_enum_decode(const struct c2_bufvec_curser *cur,
				     struct c2_layout **out)
{
   @code
	Read linear enumeration type specific fields like formula.
   @endcode
}

/** 
   Implementation of leto_encode() for linear enumeration type.
*/
static int layout_linear_enum_encode(const struct c2_layout *l, 
				     c2_bufvec_curser *cur)
{
   @code
	Read linear enumeration type specific fields like formula.
   @endcode
}

static const struct c2_layout_enum_type_ops lin_ops = {
	.leto_decode = layout_linear_enum_decode,
	.leto_encode = layout_linear_enum__encode 
};

const struct c2_layout_enum_type c2_layout_linear_enum_type = {
	.let_ops = &lin_ops
};

static const struct c2_layout_linear_formula_ops nkp_ops = {
	.llinfo_subst = layout_linear_substitute
};

const struct c2_layout_linear_formula c2_linear_formula = {
	.lf_type = &c2_pdclust_layout_type,
	.lf_id   = { .u_hi = 0x5041524954594445, /* PARITYDE */
		     .u_lo = 0x434c55535445522e  /* CLUSTER. */
	},
	.lf_ops  = &nkp_ops
};

/** @} end of group linear_enum */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
