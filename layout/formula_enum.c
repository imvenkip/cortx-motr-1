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

   Note: Layout enumeration type specific encode/decode methods are not
   required for "formula" enumeration type, since the attributes required
   specifically for a formula are:
   - stored in the pl_attr structure which is part of in-memory structure
     c2_pdclust_layout itself.
   - stored in the layouts table itself.

   @{
*/

const struct c2_lay_enum_type c2_lay_formula_enum_type = {
	.let_ops	= NULL
};

static const struct c2_lay_formula_ops nkp_ops = {
	.lfo_subst	= NULL
};

/** @todo Check dat types for all the ids. */
const struct c2_lay_formula c2_formula_NKP_formula = {
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
