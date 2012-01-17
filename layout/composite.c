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

#include "lib/memory.h"
#include "lib/assert.h"
#include "lib/vec.h"

#include "layout/layout_internal.h"
#include "layout/layout_db.h"       /* struct c2_ldb_rec */
#include "layout/composite.h"

/**
   @addtogroup composite
   @{
*/

struct composite_schema_data {
	/** Table to store extent maps for all the composite layouts. */
	struct c2_emap            csd_comp_layout_ext_map;;
};

/**
   Implementation of lto_register for COMPOSITE layout type.

   Intializes table specifically required for COMPOSITE layout type.
*/
static int composite_register(struct c2_ldb_schema *schema,
			      const struct c2_layout_type *lt)
{
   /**
	@code
	struct composite_schema_data *csd;

	C2_ALLocate_PTR(csd);

	Initialize csd->csd_comp_layout_ext_map table.

	schema->ls_enum_data[lt->lt_id] = csd;
	@endcode
   */
	return 0;
}

/**
   Implementation of lto_unregister for COMPOSITE layout type.

   Deintializes table specifically required for COMPOSITE layout type.
*/
static int composite_unregister(struct c2_ldb_schema *schema,
				const struct c2_layout_type *lt)
{
   /**
	@code
	Deinitialize schema->ls_enum_data[lt->lt_id]->csd_comp_layout_ext_map
	table.

	schema->ls_enum_data[lt->lt_id] = NULL;
	@endcode
   */
	return 0;
}

static const struct c2_layout_ops composite_ops;

/**
   Implementation of lto_decode() for composite layout type.

   Continues to build the in-memory layout object from its representation
   either 'stored in the Layout DB' or 'received over the network'.

   @param op - This enum parameter indicates what if a DB operation is to be
   performed on the layout record and it could be LOOKUP if at all.
   If it is NONE, then the layout is decoded from its representation received
   over the network.
*/
static int composite_decode(struct c2_ldb_schema *schema, uint64_t lid,
			    struct c2_bufvec_cursor *cur,
			    enum c2_layout_xcode_op op,
			    struct c2_db_tx *tx,
			    struct c2_layout **out)
{
	struct c2_composite_layout   *cl;
	struct c2_layout             *l;

	C2_PRE(schema != NULL);
	C2_PRE(lid != LID_NONE);
	C2_PRE(cur != NULL);
	C2_PRE(op == C2_LXO_DB_LOOKUP || op == C2_LXO_DB_NONE);
	C2_PRE(tx != NULL);

	/**
	   There is no data expected in c2_ldb_rec::lr_data[] for the composite
	   layout type. Thus the buffer is expected to be at the end.
	 */
	C2_PRE(c2_bufvec_cursor_move(cur, 0));

	C2_ALLOC_PTR(cl);

	l = &cl->cl_base;
	l->l_ops = &composite_ops;

	/** @todo Check this with a test prog */
	*out = l;

   /**
	@code
	if (op == C2_LXO_DB_LOOKUP) {
		Read all the segments from comp_layout_ext_map, belonging to
		composite layout with layout id 'lid' and store them in the
		cl->cl_sub_layouts.
	} else {
		Parse the sub-layout information from the buffer pointed by
		cur and store it in cl->cl_sub_layouts.
	}

	@endcode
   */
	return 0;
}

/**
   Implementation of lto_encode() for composite layout type.

   Continues to use the in-memory layout object and either 'stores it in the
   Layout DB' or 'converts it to a buffer that can be passed on over the
   network'.

   @param op - This enum parameter indicates what is the DB operation to be
   performed on the layout record if at all and it could be one of
   ADD/UPDATE/DELETE. If it is NONE, then the layout is stored in the buffer.
*/
static int composite_encode(struct c2_ldb_schema *schema,
			    const struct c2_layout *l,
			    enum c2_layout_xcode_op op,
			    struct c2_db_tx *tx,
			    struct c2_bufvec_cursor *out)
{
	if ((op == C2_LXO_DB_ADD) || (op == C2_LXO_DB_UPDATE) ||
			       (op == C2_LXO_DB_DELETE)) {
		uint64_t recsize = sizeof(struct c2_ldb_rec);

		ldb_layout_write(schema, op, recsize, out, tx);
	}

	/**
	    If we are here through DB operation, then the buffer is at the
	    end by now since it was allocated with the max size possible
	    for a record in the layouts table. Add assert to verify that.
	 */

	if ((op == C2_LXO_DB_ADD) || (op == C2_LXO_DB_UPDATE) ||
			       (op == C2_LXO_DB_DELETE)) {
		/**
		Form records for the cob_lists table by using data from the
		buffer and depending on the value of op, insert/update/delete
		those records to/from the cob_lists table.
		 */
	} else {
		/**
		Store composite layout type specific fields like information
		about the sub-layouts, into the buffer.
		 */
	}

	return 0;
}

/** Implementation of lo_fini for composite layout type. */
static void composite_fini(struct c2_layout *lay)
{
}

static const struct c2_layout_ops composite_ops = {
	.lo_fini        = composite_fini
};

static const struct c2_layout_type_ops composite_type_ops = {
	.lto_register   = composite_register,
	.lto_unregister = composite_unregister,
	.lto_decode     = composite_decode,
	.lto_encode     = composite_encode,
};


const struct c2_layout_type c2_composite_layout_type = {
	.lt_name  = "composite",
	.lt_id    = 0x434F4D504F534954,	/* COMPOSIT */
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
