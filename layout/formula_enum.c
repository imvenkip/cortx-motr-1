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

#include "lib/tlist.h"	/* struct c2_tl */
#include "layout/formula_enum.h"

/**
   @addtogroup formula_enum

   Notes:
   - Layout enumeration type specific register/unregister methods are not
     required for "formula" enumeration type, since the layout schema does not
     contain any separate tables specifically for "formula" enumeration type.
   - Layout enumeration type specific encode/decode methods are not required
     for "formula" enumeration type, since the attributes required specifically
     for a formula are:
        - stored in the pl_attr structure which is part of in-memory structure
          c2_pdclust_layout itself.
        - stored in the layouts table itself.
   @{
*/

/**
   Enumerate the COB identifiers for a layout with FORMULA enum type.
*/
int formula_enumerate(const struct c2_layout_enum *le, struct c2_tl *list)
{
   /**
	@code
	The layout is le->le_lptr->l_id.
	Use c2_ldb_rec_lookup() to read the layout with that layout id.

	It would tell that the layout is with the FORMULA enumeration type
	and will provide the required attributes.

	Obtain the container of le that is of the type c2_lay_formula_enum.
	c2_lay_formula_enum::lfe_actuals would contain the input parameters for
	this formula for this specific instance of c2_lay_formula_enum.

	Now invoke c2_lay_formula_enum::lfe_form->lf_ops->lfo_subst() with
	the attributes and parameters obtained here.
	This will result into COB identifiers enumeration, in the form of a
	c2_tl list. Store it in list that is out argument of this routine.
	@endcode
   */
	return 0;
}

/**
   Implementation of leo_nr for FORMULA enumeration.
   Rerurns number of objects in the enumeration.
*/
uint32_t formula_nr(const struct c2_layout_enum *le)
{
   /**
	@code
	c2_tl *list;

	formula_enumerate(le, list);

	Provide number of objects for that layout enumeration.
	@endcode
   */
	return 0;
}

/**
   Implementation of leo_get for FORMULA enumeration.
   Rerurns idx-th object from the enumeration.
*/
void formula_get(const struct c2_layout_enum *le,
		 uint32_t idx,
		 struct c2_fid *out)
{
   /**
	@code
	c2_tl *list;

	formula_enumerate(le, list);

	Provide idx-th object from that layout enumeration.
	@endcode
   */
}


const struct c2_layout_enum_type c2_lay_formula_enum_type = {
	.let_name	= "formula",
	.let_id		= 0x464F524D554C4145, /* FORMULAE */
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
