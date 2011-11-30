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

#include "layout/list_enum.h"

/**
   @addtogroup list_enum

   A layout with list enumeration type contains list of component
   object identifiers in itself.
   @{
*/

/**
   Implementation of leto_decode() for list enumeration type.

   Continues to build the in-memory layout object from its representation
   either 'stored in the Layout DB' or 'received over the network'.

   @param fromDB - This flag indicates if the in-memory layout object is
   being decoded 'from its representation stored in the Layout DB' or
   'from its representation received over the network'.
*/
static int lay_list_enum_decode(bool fromDB, uint64_t lid,
				const struct c2_bufvec_cursor *cur,
				struct c2_lay **out)
{
   /**
	@code
	if (fromDB)
		C2_PRE(lid != 0);
	C2_PRE(cur != NULL);

	if (fromDB) {
		Read all the COB identifiers belonging to the layout with the
		layout id 'lid', from the cob_lists table and store those in
		the buffer pointed by cur.

		Set the cursor cur to point at the beginning of the list of COB
		identifiers.
	}

	Parse the cob identifiers list from the buffer (pointed by cur) and
	store it in the c2_lay_list_enum::lle_list_of_cobs.
	@endcode
   */
	return 0;
}

/**
   Implementation of leto_encode() for list enumeration type.
*/
static int lay_list_enum_encode(const struct c2_lay *l,
				   struct c2_bufvec_cursor *cur)
{
   /**
	@code
        Read list enumeration type specific fields like list of
	component object identifiers.
	@endcode
   */
	return 0;
}

static const struct c2_lay_enum_type_ops list_ops = {
	.leto_decode		= lay_list_enum_decode,
	.leto_encode		= lay_list_enum_encode,
};

const struct c2_lay_enum_type c2_lay_list_enum_type = {
	.let_ops		= &list_ops
};


/** @} end group list_enum */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
