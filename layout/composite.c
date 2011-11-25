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

#include "layout/composite.h"

/**
   @addtogroup composite
   @{
*/

/**
   Implementation of lto_decode() for composite layout type.
   Continues to decode layout representation stored in the buffer and
   to create the layout.
*/
static int composite_decode(const struct c2_bufvec_cursor *cur,
			    struct c2_layout **out)
{
   /**
	@code
	Allocate new layout as an instance of c2_composite_layout that
	embeds c2_layout.

	Read composite layout type specific fields from the buffer
	like information of sub-layouts and store it in the c2_layout
	structure.
	@endcode
   */
	return 0;
}

/**
   Implementation of lto_encode() for composite layout type.
   Stores layout representation in the buffer.
*/
static int composite_encode(const struct c2_layout *l,
			    struct c2_bufvec_cursor *out)
{
   /**
	@code
	Store composite layout type specific fields like information
	about the sub-layouts, into the buffer.
	@endcode
   */
	return 0;
}


/**
   Implementation of lto_rec_add for COMPOSITE layout type.
   Adds the layout entry into the layouts table.
   Adds the relevant extent map into the comp_layout_ext_map table.
*/
int composite_rec_add(const struct c2_bufvec_cursor *cur,
		      struct c2_layout_schema *schema,
		      struct c2_db_tx *tx)
{
   /**
	@code
	Form c2_db_pair by using the data from the buffer.
	Add a layout entry into the layouts table.
	Add the relevant extent map into the comp_layout_ext_map table.

	If any of the segments uses a composite layout again, add the
        extent map for that composite layout as well.
	Note: This means calling composite_rec_add() recursively.
	@endcode
   */
	return 0;
}

/**
   Implementation of lto_rec_delete for COMPOSITE layout type.
*/
int composite_rec_delete(const struct c2_bufvec_cursor *cur,
		struct c2_layout_schema *schema,
		struct c2_db_tx *tx)
{
   /**
	@code
	Form c2_db_pair by using the data from the buffer.
	Delete the layout entry from the table layouts.
	Delete the relevant extent map from the comp_layout_ext_map table,
	if an only if its reference count is 0.

	If any of the segments uses a composite layout again, delete the
        extent map for that composite layout as well.
	Note: This means calling composite_rec_delete() recursively.
	@endcode
   */
	return 0;
}

/**
   Implementation of lto_rec_update for COMPOSITE layout type.
*/
int composite_rec_update(const struct c2_bufvec_cursor *cur,
		struct c2_layout_schema *schema,
		struct c2_db_tx *tx)
{
   /**
	@code
	Form c2_db_pair by using the data from the buffer.
	Update the layout entry in the layouts table.
	Update the relevant extent map in the comp_layout_ext_map table.

	If any of the segments uses a composite layout again, update the
        extent map for that composite layout as well.
	Note: This means calling composite_rec_update() recursively.
	@endcode
   */
	return 0;
}

/**
   Implementation of lto_rec_lookup for COMPOSITE layout type.
*/
int composite_rec_lookup(const struct c2_layout_id *id,
		struct c2_layout_schema *schema,
		struct c2_db_tx *tx,
		struct c2_bufvec_cursor *cur)
{
   /**
	@code
	Form c2_db_pair by using the data from the buffer.
	Obtain the layout entry from the layouts table.
	Obtain the relevant extent map from the comp_layout_ext_map table.

	If any of the segments uses a composite layout again, obtain the
        extent map for that composite layout as well.
	Note: This means calling composite_rec_lookup() recursively.
	@endcode
   */
	return 0;
}


static const struct c2_layout_type_ops composite_type_ops = {
	.lto_equal      = NULL,
	.lto_decode     = composite_decode,
	.lto_encode     = composite_encode,
	.lto_rec_add    = composite_rec_add,
	.lto_rec_delete = composite_rec_delete,
	.lto_rec_update = composite_rec_update,
	.lto_rec_lookup = composite_rec_lookup,
};


/** @todo Define value for lt_id */
const struct c2_layout_type c2_composite_layout_type = {
	.lt_name  = "composite",
	.lt_id    = 1234,
	.lt_ops   = &composite_type_ops
};

/** @} end group composite */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
