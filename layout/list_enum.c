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

#include "layout/list_enum.h""

/**
   @addtogroup list_enum

   A layout with list enumeration type contains list of component
   object identifiers in itself.
   @{
*/

/**
   Implementation of leto_encode() for list enumeration type.
*/
static int layout_list_enum_encode(const struct c2_layout *l,
                                     c2_bufvec_curser *cur)
{
   /**
	@code
        Read list enumeration type specific fields like list of
	component object identifiers.
	@endcode
   */
}

/**
   Implementation of leto_decode() for list enumeration type.
*/
static int layout_list_enum_decode(const struct c2_layout *l,
				   c2_bufvec_cursor *cur))
{
}

/**
   Implementation of leto_rec_add for list enumeration type.
*/
int list_rec_add(const struct c2_bufvec_cursor *cur,
		 struct c2_layout_schema *l_schema,
		 struct c2_db_tx *tx)
{
   /**
	@code
	Add list of cob ids to the cob_lists table.
	@endcode
   */
}

/**
   Implementation of lto_rec_delete for list enumeration type.
*/
int list_rec_delete(const struct c2_bufvec_cursor *cur,
		    struct c2_layout_schema *l_schema,
		    struct c2_db_tx *tx)
{
   /**
	@code
	Delete relevant cob id list from the cob_lists
	table.
	@endcode
   */
}

/**
   Implementation of leto_rec_update for list enumeration type.
*/
int list_rec_update(const struct c2_bufvec_cursor *cur,
		    struct c2_layout_schema *l_schema,
		    struct c2_db_tx *tx)
{
   /**
   @code
	Update the relevant list of cob ids in the
	cob_lists table.
   @endcode
   */
}

/**
   Implementation of leto_rec_lookup for list enumeration type.
*/
int list_rec_lookup(const struct c2_layout_id l_id,
		    struct c2_layout_schema *l_schema,
		    struct c2_db_tx *tx,
		    struct c2_bufvec_cursor *cur)
{
   /**
   @code
	Obtain the relevant list of cob ids from the
	cob_lists table.
   @endcode
   */
}

static const struct c2_layout_enum_type_ops list_ops = {
	.leto_decode = layout_type_list_decode,
	.leto_encode = layout_type_list_encode
};

const struct c2_layout_enum_type c2_layout_list_enum_type = {
	.let_ops = &list_ops
};

/** @} end of group list_enum */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
