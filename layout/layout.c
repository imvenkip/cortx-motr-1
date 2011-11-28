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
 *                  Trupti Patil <trupti_patil@xyratex.com>
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

void c2_lay_init(struct c2_lay *lay)
{
/**
	@code
	Invoke lay->l_ops->lo_init().
	@endcode
*/
}

void c2_lay_fini(struct c2_lay *lay)
{
/**
	@code
	Invoke lay->l_ops->lo_fini().
	@endcode
*/
}

/** Adds a reference to the layout. */
void c2_lay_get(struct c2_lay *lay)
{
/**
	@code
	Invoke lay->l_ops->lo_get().
	@endcode
*/
}

/** Releases a reference on the layout. */
void c2_lay_put(struct c2_lay *lay)
{
/**
	@code
	Invoke lay->l_ops->lo_put().
	@endcode
*/
}

/**
   Decode generic fields from a layout type and store them
   in the layout.
   This method builds an in-memory layout object from its
   representation stored in the data-base (including configuration
   information) or received over the network.
*/
int c2_lay_decode(const struct c2_bufvec_cursor *cur,
		     struct c2_lay **out)
{
	/**
	@code
	Read generic fields from the buffer like layout id, layout
	type id, enumeration type id, ref counter.

	Now call corresponding lto_decode() so as to continue
	decoding the layout type specific fields.
	@endcode
	*/

	return 0;

}

/**
   Store generic fields from a layout to the buffer.
*/
int c2_lay_encode(const struct c2_lay *l,
		     struct c2_bufvec_cursor *out)
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


int c2_lays_init(void)
{
/**
	@code
	Invoke lay->l_ops->lo_init() for all the registered layout types.
	@endcode
*/
	return 0;
}

void c2_lays_fini(void)
{
/**
	@code
	Invoke lay->l_ops->lo_fini() for all the registered layout types.
	@endcode
*/
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
