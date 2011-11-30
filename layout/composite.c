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

   Continues to build the in-memory layout object from its representation
   either 'stored in the Layout DB' or 'received over the network'.

   @param fromDB - This flag indicates if the in-memory layout object is
   being decoded 'from its representation stored in the Layout DB' or
   'from its representation received over the network'.
*/
static int composite_decode(bool fromDB, uint64_t lid,
			    const struct c2_bufvec_cursor *cur,
			    struct c2_lay **out)
{
   /**
	@code

	if (fromDB)
		C2_PRE(lid != 0);
	else
		C2_PRE(cur != NULL);

	Allocate new layout as an instance of c2_composite_layout that
	embeds c2_lay.

	if (fromDB) {
		struct c2_db_pair	pair;

		uint32_t recsize = sizeof(struct c2_ldb_rec);

		ret = ldb_layout_read(&lid, recsize, &pair)

		Set the cursor cur to point at the beginning of the key-val
		pair.
	}

	if (fromDB) {
		Read all the segments from comp_layout_ext_map, belonging to
		composite layout with layout id 'lid' and store them in the
		buffer pointed by cur.

		Set the cursor cur to point at the beginning of segments
		stored in it.
	}

	Parse the sub-layout information from the buffer (pointed by
	cur) and store it in the c2_composite_layout::cl_list_of_sub_layouts.

	@endcode
   */
	return 0;
}

/**
   Implementation of lto_encode() for composite layout type.
   Stores layout representation in the buffer.
*/
static int composite_encode(bool toDB, const struct c2_lay *l,
			    struct c2_bufvec_cursor *out)
{
   /**
	@code

	if (toDB) {
		uint64_t recSize = sizeof(struct c2_ldb_rec);

		ret = ldb_layout_write(recsize, out);
	}

	Store composite layout type specific fields like information
	about the sub-layouts, into the buffer.
	if (toDB) {
		Form records for the cob_lists table by using data from the
		buffer and insert those records into the cob_lists table.
	}


	@endcode
   */
	return 0;
}


static const struct c2_lay_type_ops composite_type_ops = {
	.lto_equal      = NULL,
	.lto_decode     = composite_decode,
	.lto_encode     = composite_encode,
};


/** @todo Define value for lt_id */
const struct c2_lay_type c2_composite_lay_type = {
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
