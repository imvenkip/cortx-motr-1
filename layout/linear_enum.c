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
#include "layout/linear_enum.h"

/**
   @addtogroup linear_enum

   Notes:
   - Layout enumeration type specific register/unregister methods are not
     required for "linear" enumeration type, since the layout schema does not
     contain any separate tables specifically for "linear" enumeration type.
   - Layout enumeration type specific encode/decode methods are not required
     for "linear" enumeration type, since the attributes required specifically
     for a linear are:
        - stored in the pl_attr structure which is part of in-memory structure
          c2_pdclust_layout itself.
        - stored in the layouts table itself.
   @{
*/

/**
   Enumerate the COB identifiers for a layout with LINEAR enum type.
*/
static int __attribute__ ((unused)) linear_enumerate(
			     const struct c2_layout_enum *le,
			     struct c2_tl *outlist,
			     struct c2_fid *gfid)

{
   /**
	@code
	The layouti id is le->le_lptr->l_id.
	Use c2_ldb_rec_lookup() to read the layout with that layout id.

	It would tell that the layout is with the LINEAR enumeration type
	and will provide the required attributes.

	Now derive list of COB identifiers

	Invoke
	c2_layout_linear_enum->lline_enum->le_lptr->l_type->lt_ops->lto_subst(),
	by passing it the attributes obtained by reading the layout and the
	parameter gfid. This will result into COB identifiers enumeration, in
	the form of a c2_tl list, stored in the list that is out argument of
	this routine.
	@endcode
   */
	return 0;
}

/**
   Implementation of leo_nr for LINEAR enumeration.
   Rerurns number of objects in the enumeration.
*/
static uint32_t linear_nr(const struct c2_layout_enum *le,
			  struct c2_fid *gfid)
{
   /**
	@code
	c2_tl *list;

	linear_enumerate(le, list, gfid);

	Provide number of objects for that layout enumeration.
	@endcode
   */
	return 0;
}

/**
   Implementation of leo_get for LINEAR enumeration.
   Rerurns idx-th object from the enumeration.
*/
static void linear_get(const struct c2_layout_enum *le,
		       uint32_t idx,
		       struct c2_fid *gfid,
		       struct c2_fid *out)
{
   /**
	@code
	c2_tl *list;

	linear_enumerate(le, list, gfid);

	Provide idx-th object from that layout enumeration.
	@endcode
   */
}

static const struct c2_layout_enum_ops linear_enum_ops = {
	.leo_nr		= linear_nr,
	.leo_get	= linear_get
};

/**
   @note Layout enum type specific implementation of leto_register,
   leto_unregister, leto_decode and leto_encode methods is not required
   for linear enumeration type.
*/
const struct c2_layout_enum_type c2_linear_enum_type = {
	.let_name	= "linear",
	.let_id		= 0x4C494E454152454E, /* LINEAREN */
	.let_ops	= NULL
};

/** @} end group linear_enum */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
