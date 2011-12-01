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
   Implementation of leto_register for LIST enumeration type.

   Initializes table specifically required for LIST enum type.
*/
int list_register(struct c2_ldb_schema *schema,
		  const struct c2_lay_enum_type *et)
{
   /**
	@code
	struct list_schema_data *lsd;

	C2_ALLOCATE_PTR(lsd);

	Initialize lsd->lsd_cob_lists table.

	schema->ls_type_data[et->let_id] = lsd;
	@endcode
   */
	return 0;
}

/**
   Implementation of leto_unregister for LIST enumeration type.

   De-initializes table specifically required for LIST enum type.
*/
int list_unregister(struct c2_ldb_schema *schema,
		    const struct c2_lay_enum_type *et)
{
   /**
	@code
	Deinitialize schema->ls_type_data[et->let_id]->lsd_cob_lists table.

	schema->ls_type_data[et->let_id] = NULL;;
	@endcode
   */
	return 0;
}


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

/**
   Enumerate the COB identifiers for a layout with LIST enum type.
*/
int list_enumerate(const struct c2_lay_enum *le)
{
   /**
	@code
	The layout is le->le_lptr->l_id.
	Use c2_ldb_rec_lookup() to read the layout with that layout id.
	This will result into a list of COB identifiers stored in
	c2_lay_list_enum::lle_list_of_cobs.
	(c2_lay_list_enum is container of c2_lay_enum in this case or c2_lay).)

	@endcode
   */
	return 0;
}

/**
   Implementation of leo_nr for LIST enumeration.
   Rerurns number of objects in the enumeration.
*/
uint32_t list_nr(const struct c2_lay_enum *le)
{
   /**
	@code
	list_enumerate(le);
	And provide number of objects for that layout enumeration.
	@endcode
   */
	return 0;
}

/**
   Implementation of leo_get for LIST enumeration.
   Rerurns idx-th object from the enumeration.
*/
void list_get(const struct c2_lay_enum *le,
	      uint32_t idx,
	      struct c2_fid *out)
{
   /**
	@code
	list_enumerate(le);
	And provide idx-th object from that layout enumeration.
	@endcode
   */
}


static const struct c2_lay_enum_type_ops list_ops = {
	.leto_register		= list_register,
	.leto_unregister	= list_unregister,
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
