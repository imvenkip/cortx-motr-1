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

#include "lib/memory.h" /* C2_ALLOC_PTR() */

#include "layout/layout_internal.h"
#include "layout/composite.h"

struct composite_schema_data {
	/** Table to store extent maps for all the composite layouts. */
	struct c2_emap csd_comp_layout_ext_map;
};

/**
 * Prefix for comp_layout_ext_map table.
 */
struct layout_prefix {
	/**
	 * Layout id for the composite layout.
	 * Value is same as c2_layout::l_id.
	 */
	uint64_t lp_l_id;

	/**
	 * Filler since prefix is a 128 bit field.
	 * Currently un-used.
	 */
	uint64_t lp_filler;
};

/*
 * @post A composite type of layout object is created. It needs to be
 * finalised by the user, once done with the usage. It can be finalised
 * using the API c2_layout_fini().
 */
void c2_composite_build(struct c2_layout_domain *dom,
			uint64_t pool_id, uint64_t lid,
			struct c2_tl *sub_layouts,
			struct c2_composite_layout **out)
{
}

/** Implementation of lo_fini for composite layout type. */
static void composite_fini(struct c2_layout *l)
{
}

/**
 * Implementation of lto_register for COMPOSITE layout type.
 *
 * Initialises table specifically required for COMPOSITE layout type.
 */
static int composite_register(struct c2_layout_domain *dom,
			      const struct c2_layout_type *lt)
{
	/*
	@code
	struct composite_schema_data *csd;

	C2_ALLOC_PTR(csd);

	Initialise csd->csd_comp_layout_ext_map table.

	dom->ld_schema->ls_type_data[lt->lt_id] = csd;
	@endcode
	*/
	return 0;
}

/**
 * Implementation of lto_unregister for COMPOSITE layout type.
 *
 * Finalises table specifically required for COMPOSITE layout type.
 */
static void composite_unregister(struct c2_layout_domain *dom,
				 const struct c2_layout_type *lt)
{
	/*
	@code
	Finalise
	dom->ld_schema->ls_type_data[lt->lt_id]->csd_comp_layout_ext_map
	table.

	dom->ld_schema->ls_type_data[lt->lt_id] = NULL;
	@endcode
	*/
}

/**
 * Implementation of lto_max_recsize() for COMPOSITE layout type.
 */
static c2_bcount_t composite_max_recsize(struct c2_layout_domain *dom)
{
	return 0;
}

/**
 * Implementation of lto_recsize() for COMPOSITE layout type.
 */
static c2_bcount_t composite_recsize(const struct c2_layout *l)
{
	return 0;
}


static const struct c2_layout_ops composite_ops;

/**
 * Implementation of lto_decode() for composite layout type.
 *
 * Continues to build the in-memory layout object from its representation
 * either 'stored in the Layout DB' or 'received through the buffer'.
 *
 * @param op This enum parameter indicates what if a DB operation is to be
 * performed on the layout record and it could be LOOKUP if at all.
 * If it is BUFFER_OP, then the layout is decoded from its representation
 * received through the buffer.
 */
static int composite_decode(struct c2_layout_domain *dom,
			    uint64_t lid,
			    enum c2_layout_xcode_op op,
			    struct c2_db_tx *tx,
			    uint64_t pool_id,
			    struct c2_bufvec_cursor *cur,
			    struct c2_layout **out)
{
	/*
	@code
	struct c2_composite_layout *cl;

	C2_PRE(C2_IN(op, (C2_LXO_DB_LOOKUP, C2_LXO_BUFFER_OP));

	C2_ALLOC_PTR(cl);

	c2_layout__init(dom, &cl->cl_base, lid, pool_id,
			&c2_composite_layout_type, &composite_ops);

	if (op == C2_LXO_DB_LOOKUP) {
		Read all the segments from the comp_layout_ext_map table,
		belonging to composite layout with layout id 'lid' and store
		them in the cl->cl_sub_layouts.

	} else {
		Parse the sub-layout information from the buffer pointed by
		cur and store it in cl->cl_sub_layouts.
	}

	*out = &cl->cl_base;
	@endcode
	*/

	return 0;
}

/**
 * Implementation of lto_encode() for composite layout type.
 *
 * Continues to use the in-memory layout object and either 'stores it in the
 * Layout DB' or 'converts it to a buffer'.
 *
 * @param op This enum parameter indicates what is the DB operation to be
 * performed on the layout record if at all and it could be one of
 * ADD/UPDATE/DELETE. If it is BUFFER_OP, then the layout is stored in the
 * buffer.
 */
static int composite_encode(struct c2_layout *l,
			    enum c2_layout_xcode_op op,
			    struct c2_db_tx *tx,
			    struct c2_bufvec_cursor *oldrec_cur,
			    struct c2_bufvec_cursor *out)
{
	/*
	@code

	C2_PRE(C2_IN(op, (C2_LXO_DB_ADD, C2_LXO_DB_UPDATE,
			  C2_LXO_DB_DELETE, C2_LXO_BUFFER_OP)));

	if ((op == C2_LXO_DB_ADD) || (op == C2_LXO_DB_UPDATE) ||
            (op == C2_LXO_DB_DELETE)) {
		Form records for the cob_lists table by using data from the
		c2_layout object l and depending on the value of op,
		insert/update/delete those records to/from the cob_lists table.
	} else {
		Store composite layout type specific fields like information
		about the sub-layouts, into the buffer by referring it from
		c2_layout object l.
	}

	@endcode
	*/

	return 0;
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
