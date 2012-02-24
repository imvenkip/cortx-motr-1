/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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

/**
 * @addtogroup composite
 * @{
 */

#include "lib/memory.h"
#include "lib/vec.h"

#include "layout/layout_internal.h"
#include "layout/layout_db.h"       /* struct c2_ldb_rec */
#include "layout/composite.h"


struct composite_schema_data {
	/** Table to store extent maps for all the composite layouts. */
	struct c2_emap            csd_comp_layout_ext_map;;
};

/**
 * Prefix for comp_layout_ext_map table.
 */
struct layout_prefix {
	/**
	 * Layout id for the composite layout.
	 * Value is same as c2_layout::l_id.
	 */
	uint64_t                  lp_l_id;

	/** Filler since prefix is a 128 bit field.
	 *  Currently un-used.
	 */
	uint64_t                  lp_filler;
};

void c2_composite_init(struct c2_composite_layout *clay,
		       uint64_t id,
		       const struct c2_layout_type *type,
		       const struct c2_layout_ops *ops,
		       struct c2_tl *sub_layouts)
{
	c2_layout_init(&clay->cl_base, id, type, ops);

	/**
	   Intialize clay->cl_sub_layouts by using sub_layouts.
	   @todo Yet, need to explore this in detail.
	 */
}

void c2_composite_fini(struct c2_composite_layout *clay)
{
	/**
	   De-intialize clay->cl_sub_layouts.
	   @todo Yet, need to explore this in detail.
	 */
}

/**
 * Implementation of lto_register for COMPOSITE layout type.
 *
 * Intializes table specifically required for COMPOSITE layout type.
 */
static int composite_register(struct c2_ldb_schema *schema,
			      const struct c2_layout_type *lt)
{
   /**
	@code
	struct composite_schema_data *csd;

	C2_ALLOC_PTR(csd);

	Initialize csd->csd_comp_layout_ext_map table.

	schema->ls_type_data[lt->lt_id] = csd;
	@endcode
   */
	return 0;
}

/**
 * Implementation of lto_unregister for COMPOSITE layout type.
 *
 * Deintializes table specifically required for COMPOSITE layout type.
 */
static void composite_unregister(struct c2_ldb_schema *schema,
				 const struct c2_layout_type *lt)
{
   /**
	@code
	Deinitialize schema->ls_type_data[lt->lt_id]->csd_comp_layout_ext_map
	table.

	schema->ls_type_data[lt->lt_id] = NULL;
	@endcode
   */
}

/**
 * Implementation of lto_max_recsize() for COMPOSITE layout type.
 */
static uint32_t composite_max_recsize(struct c2_ldb_schema *schema)
{
	return 0;
}

/**
 * Implementation of lto_recsize() for COMPOSITE layout type.
 */
static uint32_t composite_recsize(struct c2_ldb_schema *schema,
				  struct c2_layout *l)
{
	return 0;
}


static const struct c2_layout_ops composite_ops;

/**
 * Implementation of lto_decode() for composite layout type.
 *
 * Continues to build the in-memory layout object from its representation
 * either 'stored in the Layout DB' or 'received over the network'.
 *
 * @param op - This enum parameter indicates what if a DB operation is to be
 * performed on the layout record and it could be LOOKUP if at all.
 * If it is NONE, then the layout is decoded from its representation received
 * over the network.
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
	/* Catch if the buffer is with insufficient size. */
	C2_PRE(!c2_bufvec_cursor_move(cur, 0));
	C2_PRE(op == C2_LXO_DB_LOOKUP || op == C2_LXO_DB_NONE);
	C2_PRE(tx != NULL);

	C2_ALLOC_PTR(cl);

	l = &cl->cl_base;
	l->l_ops = &composite_ops;

	*out = l;

   /**
	@code
	if (op == C2_LXO_DB_LOOKUP) {
		Read all the segments from the comp_layout_ext_map table,
		belonging to composite layout with layout id 'lid' and store
		them in the cl->cl_sub_layouts.

	} else {
		Parse the sub-layout information from the buffer pointed by
		cur and store it in cl->cl_sub_layouts.
	}
	@endcode
   */
	return 0;
}

/**
 * Implementation of lto_encode() for composite layout type.
 *
 * Continues to use the in-memory layout object and either 'stores it in the
 * Layout DB' or 'converts it to a buffer that can be passed on over the
 * network'.
 *
 * @param op - This enum parameter indicates what is the DB operation to be
 * performed on the layout record if at all and it could be one of
 * ADD/UPDATE/DELETE. If it is NONE, then the layout is stored in the buffer.
 */
static int composite_encode(struct c2_ldb_schema *schema,
			    const struct c2_layout *l,
			    enum c2_layout_xcode_op op,
			    struct c2_db_tx *tx,
			    struct c2_bufvec_cursor *oldrec_cur,
			    struct c2_bufvec_cursor *out)
{
	if ((op == C2_LXO_DB_ADD) || (op == C2_LXO_DB_UPDATE) ||
			       (op == C2_LXO_DB_DELETE)) {
		/**
		Form records for the cob_lists table by using data from the
		c2_layout object l and depending on the value of op,
		insert/update/delete those records to/from the cob_lists table.
		 */
	} else {
		/**
		Store composite layout type specific fields like information
		about the sub-layouts, into the buffer by referring it from
		c2_layout object l. If the buffer is found to be insufficient,
		return the error ENOBUFS.
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
	.lto_register    = composite_register,
	.lto_unregister  = composite_unregister,
	.lto_max_recsize = composite_max_recsize,
	.lto_recsize     = composite_recsize,
	.lto_decode      = composite_decode,
	.lto_encode      = composite_encode,
};


const struct c2_layout_type c2_composite_layout_type = {
	.lt_name  = "composite",
	.lt_id    = 1,
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
