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

#include "fid/fid.h"  /* struct c2_fid */
#include "layout/list_enum.h"

/**
   @addtogroup list_enum

   A layout with list enumeration type contains list of component
   object identifiers in itself.
   @{
*/

struct list_schema_data {
	/** Table to store COB lists for all the layout with LIST enum type. */
	struct c2_table           lsd_cob_lists;
};

struct ldb_list_cob_entry {
	/** Index for the COB from the layout it is part of. */
	uint32_t                  llce_cob_index;

	/** COB identifier. */
	struct c2_fid             llce_cob_id;
};

enum {
	MAX_INLINE_COB_ENTRIES = 20
};

/**
   Structure used to store MAX_INLINE_COB_ENTRIES number of cob entries inline
   into the layouts table.
*/
struct ldb_list_cob_entries {
	/** Total number of COB Ids for the specific layout. */
	uint32_t                  llces_nr;

	/** Array for storing COB Ids. */
	struct ldb_list_cob_entry llces_cobs[MAX_INLINE_COB_ENTRIES];
};

/**
   Implementation of leto_register for LIST enumeration type.

   Initializes table specifically required for LIST enum type.
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
   Implementation of leto_unregister for LIST enumeration type.

   De-initializes table specifically required for LIST enum type.
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
   Implementation of leto_recsize() for list enumeration type.
   Returns record size for the part of the record required to store LIST enum
   details viz. list of COB identifiers upto MAX_INLINE_COB_ENTRIES.
*/
static uint64_t list_recsize(void)
{
	return sizeof(struct ldb_list_cob_entries);
}

/**
   Implementation of leto_rec_decode() for list enumeration type.
   Reads MAX_INLINE_COB_ENTRIES cob identifiers from the buffer into
   the c2_layout_list_enum object.
*/
static int list_rec_decode(const struct c2_bufvec_cursor *cur,
			   struct c2_layout_enum *e)
{
   /**
	@code
	Container_of(e) would give an object of the type c2_layout_list_enum,
	say le.
	Read the MAX_INLINE_COB_ENTRIES number of cob identifiers from the
	buffer (pointed by cur) into le->lle_list_of_cobs.
	@endcode
   */
	return 0;
}

/**
   Implementation of leto_rec_encode() for list enumeration type.
   Reads MAX_INLINE_COB_ENTRIES number of cob identifiers from
   the c2_layout_list_enum object into the buffer.
*/
static int list_rec_encode(const struct c2_layout_enum *e,
			   struct c2_bufvec_cursor *cur)
{
   /**
	@code
	Container_of(e) would give an object of the type c2_layout_list_enum,
	say le.
	Read the MAX_INLINE_COB_ENTRIES number of cob identifiers from
	le->lle_list_of_cobs, into the buffer (pointed by cur).
	@endcode
   */
	return 0;
}


/**
   Implementation of leto_decode() for list enumeration type.

   Continues to build the in-memory layout object from its representation
   either 'stored in the Layout DB' or 'received over the network'.

   @param op - This enum parameter indicates what if a DB operation is to be
   performed on the layout record and it could be LOOKUP if at all.
   If it is NONE, then the layout is decoded from its representation received
   over the network.
*/
static int list_decode(struct c2_ldb_schema *schema, uint64_t lid,
		       const struct c2_bufvec_cursor *cur,
		       enum c2_layout_xcode_op op,
		       struct c2_db_tx *tx,
		       struct c2_layout **out)
{
   /**
	@code
	if (op == C2_LXO_DB_LOOKUP) {
		C2_PRE(lid != 0);
	}
	C2_PRE(cur != NULL);

	Nothing to be done if ldb_list_cob_entries::llces_nr <=
	MAX_INLINE_COB_ENTRIES. Return from here if so.

	if (op == C2_LXO_DB_LOOKUP) {
		Read all the COB identifiers belonging to the layout with the
		layout id 'lid' and index greater than MAX_INLINE_COB_ENTRIES,
		from the cob_lists table and store those in the buffer pointed
		by cur.

		Set the cursor cur to point at the beginning of the list of COB
		identifiers.
	}

	Parse the cob identifiers list from the buffer (pointed by cur) and
	store it in the c2_layout_list_enum::lle_list_of_cobs.
	@endcode
   */
	return 0;
}

/**
   Implementation of leto_encode() for list enumeration type.

   Continues to use the in-memory layout object and either 'stores it in the
   Layout DB' or 'converts it to a buffer that can be passed on over the
   network'.

  @param op - This enum parameter indicates what is the DB operation to be
   performed on the layout record if at all and it could be one of
   ADD/UPDATE/DELETE. If it is NONE, then the layout is stored in the buffer.
*/
static int list_encode(struct c2_ldb_schema *schema,
		       const struct c2_layout *l,
		       enum c2_layout_xcode_op op,
		       struct c2_db_tx *tx,
		       struct c2_bufvec_cursor *out)
{
   /**
	@code
        Read the cob identifiers list from c2_layout_list_enum::lle_list_of_cobs
	and store it into the buffer.

	if ((op == C2_LXO_DB_ADD) || (op == C2_LXO_DB_UPDATE) ||
			       (op == C2_LXO_DB_DELETE)) {
		Depending upon the value of op, insert/update/delete cob
		entries beyond MAX_INLINE_COB_ENTRIES to/from the cob_lists
		table.

		(First MAX_INLINE_COB_ENTRIES number of entries are stored
		inline into the layouts table itself.)
	}

	@endcode
   */
	return 0;
}

/**
   Implementation of leo_nr for LIST enumeration.
   Rerurns number of objects in the enumeration.
   Argument fid is ignored here for LIST enumeration type.
*/
static uint32_t list_nr(const struct c2_layout_enum *le,
			struct c2_fid *gfid)
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
   Implementation of leo_get for LIST enumeration.
   Returns idx-th object from the enumeration.
   Argument fid is ignored here for LIST enumeration type.
*/
static void list_get(const struct c2_layout_enum *le,
		     uint32_t idx,
		     struct c2_fid *gfid,
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
	.leto_rec_decode     = list_rec_decode,
	.leto_rec_encode     = list_rec_encode,
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
