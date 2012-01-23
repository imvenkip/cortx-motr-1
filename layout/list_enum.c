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

#include "lib/vec.h"
#include "lib/memory.h"
#include "fid/fid.h"  /* struct c2_fid */
#include "layout/layout_internal.h"
#include "layout/list_enum.h"

/**
 * @addtogroup list_enum
 *
 * A layout with list enumeration type contains list of component
 * object identifiers in itself.
 * @{
 */

void c2_layout_list_enum_init(struct c2_layout_list_enum *list_enum,
			      struct c2_tl *list_of_cobs,
			      struct c2_layout *l,
			      struct c2_layout_enum_type *lt,
			      struct c2_layout_enum_ops *ops)
{
	c2_layout_enum_init(&list_enum->lle_base, l, lt, ops);

	/**
	   Intialize list_enum->lle_list_of_cobs by using list_of_cobs.
	   @todo Yet, need to explore this in detail.
	 */
}

void c2_layout_list_enum_fini(struct c2_layout_list_enum *list_enum)
{

	/**
	   De-intialize list_enum->lle_list_of_cob.
	   @todo Yet, need to explore this in detail.
	 */
}

/**
 * Implementation of leto_register for LIST enumeration type.
 *
 * Initializes table specifically required for LIST enum type.
 */
static int list_register(struct c2_ldb_schema *schema,
			 const struct c2_layout_enum_type *et)
{
   /**
	@code
	struct list_schema_data  *lsd;

	C2_ALLOCATE_PTR(lsd);

	Initialize lsd->lsd_cob_lists table.

	schema->ls_type_data[et->let_id] = lsd;
	@endcode
   */
	return 0;
}

/**
 * Implementation of leto_unregister for LIST enumeration type.
 *
 * De-initializes table specifically required for LIST enum type.
 */
static int list_unregister(struct c2_ldb_schema *schema,
			   const struct c2_layout_enum_type *et)
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
 * Implementation of leto_recsize() for list enumeration type.
 *
 * Returns record size for the part of the layouts table record required to
 * store LIST enum details.
 */
static uint32_t list_recsize(void)
{
	return sizeof(struct ldb_inline_cob_entries);
}

/**
 * Implementation of leto_decode() for list enumeration type.
 *
 * Reads MAX_INLINE_COB_ENTRIES cob identifiers from the buffer into
 * the c2_layout_list_enum object. Then reads further cob identifiers either
 * from the DB or from the buffer, as applicable.
 *
 * @param op - This enum parameter indicates what if a DB operation is to be
 * performed on the layout record and it could be LOOKUP if at all.
 * If it is NONE, then the layout is decoded from its representation received
 * over the network.
 */
static int list_decode(struct c2_ldb_schema *schema, uint64_t lid,
		       struct c2_bufvec_cursor *cur,
		       enum c2_layout_xcode_op op,
		       struct c2_db_tx *tx,
		       struct c2_layout_enum **out)
{
	struct c2_layout_list_enum    *list_enum;
	struct ldb_inline_cob_entries *inline_cobs;

	inline_cobs = c2_bufvec_cursor_addr(cur);

	C2_ALLOC_PTR(list_enum);

	/* Copy cob entries from inline_cobs to list_enum. */

	*out = &list_enum->lle_base;

   /**
	@code

	Nothing to be done if inline_cobs->llces_nr <= MAX_INLINE_COB_ENTRIES.
	Return from here if so.

	if (op == C2_LXO_DB_LOOKUP) {
		Read all the COB identifiers belonging to the layout with the
		layout id 'lid' and index greater than MAX_INLINE_COB_ENTRIES,
		from the cob_lists table and store those in the
		c2_layout_list_enum::lle_list_of_cobs.

		The buffer is now expected to be at the end.
		C2_ASSERT(c2_bufvec_cursor_move(cur, 0));
	} else {
		Parse the cob identifiers list from the buffer and store it in
		the c2_layout_list_enum::lle_list_of_cobs.
	}

	@endcode
   */
	return 0;
}

/**
 * Implementation of leto_encode() for list enumeration type.
 *
 * Continues to use the in-memory layout object and either 'stores it in the
 * Layout DB' or 'converts it to a buffer that can be passed on over the
 * network'.

 * @param op - This enum parameter indicates what is the DB operation to be
 * performed on the layout record if at all and it could be one of
 * ADD/UPDATE/DELETE. If it is NONE, then the layout is stored in the buffer.
 */
static int list_encode(struct c2_ldb_schema *schema,
		       const struct c2_layout *l,
		       enum c2_layout_xcode_op op,
		       struct c2_db_tx *tx,
		       struct c2_bufvec_cursor *out)
{
	struct c2_layout_striped      *stl;
	struct c2_layout_list_enum    *list_enum;
	struct ldb_inline_cob_entries *inline_cobs;

	stl = container_of(l, struct c2_layout_striped, ls_base);

	list_enum = container_of(stl->ls_enum, struct c2_layout_list_enum,
			lle_base);

	/** Read the MAX_INLINE_COB_ENTRIES number of cob identifiers from
	    list_enum->lle_list_of_cobs, into inline_cobs. Temporarily,
	    assigning NULL to inline_cobs to avoid the uninitialization error.
	 */
	inline_cobs = NULL;

	c2_bufvec_cursor_copyto(out, inline_cobs,
				sizeof(struct ldb_inline_cob_entries));

	/**
	    If we are here through DB operation, then the buffer is at the
	    end by now since it was allocated with the max size possible
	    for a record in the layouts table. Add assert to verify that.
	 */


	if ((op == C2_LXO_DB_ADD) || (op == C2_LXO_DB_UPDATE) ||
			       (op == C2_LXO_DB_DELETE)) {
		/**
		Thus the buffer is expected to be at the end, at this point.
		C2_ASSERT(c2_bufvec_cursor_move(out, 0));

		Depending upon the value of op, insert/update/delete cob
		entries beyond MAX_INLINE_COB_ENTRIES to/from the cob_lists
		table.

		(First MAX_INLINE_COB_ENTRIES number of entries are stored
		inline into the layouts table itself.)
		*/
	} else {

		/**
		Read the cob identifiers list beyond the index
		MAX_INLINE_COB_ENTRIES from
		c2_layout_list_enum::lle_list_of_cobs and store it into the
		buffer. If the buffer is found to be insufficient, return the
		error ENOBUFS.
		*/
	}

	return 0;
}

/**
 * Implementation of leo_nr for LIST enumeration.
 * Rerurns number of objects in the enumeration.
 * Argument fid is ignored here for LIST enumeration type.
 */
static uint32_t list_nr(const struct c2_layout_enum *le)
{
   /**
	@code
	c2_layout_list_enum::lle_list_of_cobs has list of COB Ids in it.
	Provide count of entries stored in
	c2_layout_list_enum::lle_list_of_cobs. (Probably c2_tlist_length() can
	be used for this purpose.)
	@endcode
   */
	return 0;
}

/**
 * Implementation of leo_get for LIST enumeration.
 * Returns idx-th object from the enumeration.
 * Argument fid is ignored here for LIST enumeration type.
 */
static void list_get(const struct c2_layout_enum *le,
		     uint32_t idx,
		     const struct c2_fid *gfid,
		     struct c2_fid *out)
{
   /**
	@code
	Provide idx-th object from c2_layout_list_enum::lle_list_of_cobs.
	@endcode
   */
}

static const struct c2_layout_enum_ops list_enum_ops = {
	.leo_nr              = list_nr,
	.leo_get             = list_get
};

static const struct c2_layout_enum_type_ops list_type_ops = {
	.leto_register       = list_register,
	.leto_unregister     = list_unregister,
	.leto_recsize        = list_recsize,
	.leto_decode         = list_decode,
	.leto_encode         = list_encode,
};

const struct c2_layout_enum_type c2_list_enum_type = {
	.let_name            = "list",
	.let_id              = 0x4C495354454E554D, /* LISTENUM */
	.let_ops             = &list_type_ops
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
