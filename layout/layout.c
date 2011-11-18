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
 * Original creation date: 07/09/2010
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "layout/layout.h"

/**
   @addtogroup layout
   @{
 */

void c2_layout_init(struct c2_layout *lay)
{
}

void c2_layout_fini(struct c2_layout *lay)
{
}

int c2_layouts_init(void)
{
	return 0;
}

void c2_layouts_fini(void)
{
}

/** 
   Decode generic fields from a layout type and store them
   in the layouti.
   This methos builds an in-memory layout object from its
   representation stored in the data-base (including configuration
   information) or received over the network.
*/
int c2_layout_decode(const struct c2_bufvec_cursor *cur,
		     struct c2_layout **out)
{
	/**
	@code
	Read generic fields from the buffer like layout id, layout
	type id, enumeration type id, ref counter, enumeration description.

	Now that the layout type id is known, create an instance of the 
	layout-type specific data-type that embeds the c2_layout.
 
	Set c2_layout::l_ops.

	Now call corresponding lto_decode() so as to continue
	decoding the layout type specific fields.
	@endcode
	*/

	return 0;

}

/**
   Store generic fields from a layout to the buffer.
*/   
int c2_layout_encode(const struct c2_layout *l,
		     struct c2_bufvec_cursor *cur_out)
{
	/**
	@code
	Read generic fields from the layout and store them in the buffer.

	Now call corresponding lt_decode for the specific layout type so as
	to continue encoding the layout type specific fields.
	@endcode
	*/

	return 0;
}

/** @} end group layout */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
