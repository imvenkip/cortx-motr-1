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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 07/15/2010
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
		          struct c2_layout **l_out)
{
   /**
	@code
	Allocate new layout as an instance of c2_composite_layout that
	embeds c2_layout.

	Read composite layout type specific fields from the buffer.
	Based on the layout-enumeration type, call respective leto_decode(). 
   	@endcode
   */
	return 0;
}

/** 
   Implementation of lto_encode() for composite layout type.
   Stores layout representation in the buffer.
*/
static int composite_encode(const struct c2_layout *l,
		          struct c2_bufvec_cursor *cur_out)
{
   /**
   @code
	Store composite layout type specific fields like N, K into the buffer.

	Based on the layout-enumeration type, call respective leto_encode(). 
   @endcode
   */

	return 0;
}


**
   Implementation of lto_rec_add for COMPOSITE layout type.
   Adds the layout entry into the layout_entries table.
   Adds the relevant extent map into the comp_layout_ext_map table.
*/
int composite_rec_add(const struct c2_bufvec_cursor *cur,
		struct c2_layout_schema *l_schema,
		struct c2_db_tx *tx)
{
   /**
	@code
	Adds a layout entry into the layout_entries table.
	Adds the relevant extent map into the comp_layout_ext_map table.
	@endcode
   */
}

/**
   Implementation of lto_rec_delete for COMPOSITE layout type.
*/
int composite_rec_delete(const struct c2_bufvec_cursor *cur,
		struct c2_layout_schema *l_schema,
		struct c2_db_tx *tx)
{
   /**
	@code
	Deletes the layout entry from the table layout_entries.
	Deletes the relevant extent map from the comp_layout_ext_map table.
	@endcode	
   */
}

/**
   Implementation of lto_rec_update for COMPOSITE layout type.
*/
int composite_rec_update(const struct c2_bufvec_cursor *l_rec,
		struct c2_layout_schema *l_schema,
		struct c2_db_tx *tx)
{
   /**
	@code
	Updates the layout entry in the layout_entries table.
	Updates the relevant extent map in the comp_layout_ext_map table.
	@endcode
   */
}

/**
   Implementation of lto_rec_lookup for COMPOSITE layout type.
*/
int composite_lookup(const struct c2_layout_id l_id,
		struct c2_layout_schema *l_schema,
		struct c2_db_tx *tx,
		struct c2_bufvec_cursor *cur)
{
   /**
	@code
	Obtains the layout entry from the layout_entries table.
	Obtains the relevant extent map from the comp_layout_ext_map table.
	@endcode
   */
}


static const struct c2_layout_type_ops composite_type_ops = {
	.lt_equal  = composite_equal,
	.lt_decode = composite_decode,
	.lt_encode = composite_encode
};

const struct c2_layout_type c2_composite_layout_type = {
	.lt_name  = "composite",
	.lt_ops   = &composite_type_ops 
};

/** @} end of group composite */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
