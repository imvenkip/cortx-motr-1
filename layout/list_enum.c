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
 * @addtogroup list_enum
 *
 * A layout with list enumeration type contains list of component
 * object identifiers in itself.
 * @{
 */

#include "lib/errno.h"
#include "lib/vec.h"
#include "lib/memory.h"
#include "fid/fid.h"  /* struct c2_fid */
#include "layout/layout_internal.h"
#include "layout/list_enum.h"

enum {
	MAX_INLINE_COB_ENTRIES = 20
};

struct ldb_list_cob_entry {
	/** Index for the COB from the layout it is part of. */
	uint32_t                  llce_cob_index;

	/** COB identifier. */
	struct c2_fid             llce_cob_id;
};

/**
 * Structure used to store cob entries inline into the layouts table - maximum
 * upto MAX_INLINE_COB_ENTRIES number of those.
 */
struct ldb_inline_cob_entries {
	/** Total number of COB Ids for the specific layout. */
	uint32_t                  llces_nr;

	/**
	 * Payload storing list of ldb_list_cob_entry, max upto
	 * MAX_INLINE_COB_ENTRIES number of those.
	 */
	char                      llces_cobs[0];
};

struct list_schema_data {
	/** Table to store COB lists for all the layouts with LIST enum type. */
	struct c2_table           lsd_cob_lists;
};

/**
 * cob_lists table.
 */
struct ldb_cob_lists_key {
	/** Layout id, value obtained from c2_layout::l_id. */
	uint64_t                  lclk_lid;

	/** Index for the COB from the layout it is part of. */
	uint32_t                  lclk_cob_index;
};

struct ldb_cob_lists_rec {
	/** COB identifier. */
	struct c2_fid             lclr_cob_id;
};

/**
 * Compare cob_lists table keys.
 * This is a 3WAY comparison.
 */
static int lcl_key_cmp(struct c2_table *table,
		       const void *key0,
		       const void *key1)
{
	return 0;
}

/**
 * table_ops for cob_lists table.
 */
static const struct c2_table_ops cob_lists_table_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct ldb_cob_lists_key)
		},
		[TO_REC] = {
			.max_size = sizeof(struct ldb_cob_lists_rec)
		}
	},
	.key_cmp = lcl_key_cmp
};


struct list_cob_entry {
	/** Layout id. */
	uint64_t        cle_lid;

	/** Index for the COB from the layout it is part of. */
	uint32_t        cle_cob_index;

	/** COB identifier. */
	struct c2_fid   cle_cob_id;

	struct c2_tlink cle_linkage;

	uint64_t        cle_magic;
};

/* @todo change the magic */
C2_TL_DESCR_DEFINE(cob_list, "cob-list", static, struct list_cob_entry,
		   cle_linkage, cle_magic, 0x1111, 0x2222);
C2_TL_DEFINE(cob_list, static, struct list_cob_entry);

static const struct c2_layout_enum_ops list_enum_ops;

int c2_list_enum_build(struct c2_layout_list_enum **out)
{
	struct c2_layout_list_enum *list_enum;

	C2_ALLOC_PTR(list_enum);
	if (list_enum == NULL)
		return -ENOMEM;

	c2_layout_enum_init(&list_enum->lle_base, &c2_list_enum_type,
			    &list_enum_ops);

	list_enum->lle_nr = 0;

	cob_list_tlist_init(&list_enum->lle_list_of_cobs);

	*out = list_enum;

	return 0;
}

int c2_list_enum_add(struct c2_layout_list_enum *le, uint64_t lid,
		     uint32_t idx, struct c2_fid *cob_id)
{
	struct list_cob_entry *cle;

	C2_ALLOC_PTR(cle);
	if (cle == NULL)
		return false;

	cle->cle_lid       = lid;
	cle->cle_cob_index = idx;
	cle->cle_cob_id    = *cob_id;

	/* @todo check that the idx does not already exist. */

	cob_list_tlink_init(cle);
	C2_ASSERT(!cob_list_tlink_is_in(cle));
	cob_list_tlist_add(&le->lle_list_of_cobs, cle);
	C2_ASSERT(cob_list_tlink_is_in(cle));

	le->lle_nr++;

	C2_ASSERT(cob_list_tlist_length(&le->lle_list_of_cobs) == le->lle_nr);

	return 0;
}

/* Something is wrong with the following.
int c2_list_enum_delete(struct c2_layout_list_enum *le, uint64_t lid,
			uint32_t idx, struct c2_fid *cob_id)
{
	struct list_cob_entry *cle;

	c2_tlist_for(&cob_list_tl, &le->lle_list_of_cobs, cle) {
		if (cle->cle_cob_index == idx)
			break;
	} c2_tlist_endfor;

	C2_ASSERT(cle->cle_lid == lid);
	C2_ASSERT(cle->cle_cob_id.f_container == cob_id->f_container);
	C2_ASSERT(cle->cle_cob_id.f_key == cob_id->f_key);

	c2_tlist_del(&cob_list_tl, cle);
	C2_ASSERT(!cob_list_tlink_is_in(cle));

	le->lle_nr--;

	C2_ASSERT(cob_list_tlist_length(&le->lle_list_of_cobs) == le->lle_nr);

	return 0;
}
*/

void c2_list_enum_fini(struct c2_layout_list_enum *list_enum)
{

	/**
	   De-initialize list_enum->lle_list_of_cob.
	   @todo Yet, need to explore this in detail.
	 */

	c2_layout_enum_fini(&list_enum->lle_base);
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
static void list_unregister(struct c2_ldb_schema *schema,
			    const struct c2_layout_enum_type *et)
{
   /**
	@code
	Deinitialize schema->ls_type_data[et->let_id]->lsd_cob_lists table.

	schema->ls_type_data[et->let_id] = NULL;;
	@endcode
   */
	//return 0;
}

/**
 * Implementation of leto_max_recsize() for list enumeration type.
 *
 * Returns maximum record size for the part of the layouts table record,
 * required to store LIST enum details.
 */
static uint32_t list_max_recsize(void)
{
	return sizeof(struct ldb_inline_cob_entries);
}

/**
 * Implementation of leto_recsize() for list enumeration type.
 *
 * Returns record size for the part of the layouts table record required to
 * store LIST enum details, for the specified layout.
 */
static uint32_t list_recsize(struct c2_layout_enum *e)
{
	struct c2_layout_list_enum  *list_enum;

	list_enum = container_of(e, struct c2_layout_list_enum, lle_base);

	if (list_enum->lle_nr >= MAX_INLINE_COB_ENTRIES)
		return sizeof(struct ldb_inline_cob_entries);
	else
		/* @todo Calculate the size required */
		return 0;
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
	uint32_t                       num_inline; /* No. of inline cobs */
	int                            i;

	C2_PRE(schema != NULL);
	C2_PRE(lid != LID_NONE);
	C2_PRE(cur != NULL);
	C2_PRE(op == C2_LXO_DB_LOOKUP || op == C2_LXO_DB_NONE);
	C2_PRE(ergo(op == C2_LXO_DB_LOOKUP, tx != NULL));

	C2_ALLOC_PTR(list_enum);
	if (list_enum == NULL)
		return -ENOMEM;

	*out = &list_enum->lle_base;

	inline_cobs = c2_bufvec_cursor_addr(cur);
	C2_ASSERT(inline_cobs != NULL);

	num_inline = inline_cobs->llces_nr >= MAX_INLINE_COB_ENTRIES ?
			MAX_INLINE_COB_ENTRIES : inline_cobs->llces_nr;

	for (i = 0; i < num_inline; ++i) {
		/* @todo Copy cob entry from inline_cobs->llces_cobs[] to
		 list_enum->lle_list_of_cobs */
	}

	if (inline_cobs->llces_nr <= MAX_INLINE_COB_ENTRIES)
		return 0;

	if (op == C2_LXO_DB_LOOKUP) {
		/* @todo
		Read all the COB identifiers belonging to the layout with the
		layout id 'lid' and index greater than MAX_INLINE_COB_ENTRIES,
		from the cob_lists table and store those in the
		c2_layout_list_enum::lle_list_of_cobs.
		*/
	} else {
		/* @todo
		c2_bufvec_cursor_move(cur,
			sizeof(struct ldb_inline_cob_entries)
			+ num_inline * sizeof(struct ldb_list_cob_entry));
		Parse the cob identifiers list from the buffer and store it in
		the c2_layout_list_enum::lle_list_of_cobs.
		*/
	}

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
	uint32_t                       num_inline; /* No. of inline cobs */
	struct list_cob_entry         *cob_entry;
	int                            i;
	struct ldb_list_cob_entry      ldb_cob_entry;
	c2_bcount_t                    num_bytes;

	C2_PRE(schema != NULL);
	C2_PRE(layout_invariant(l));
	C2_PRE(op == C2_LXO_DB_ADD || op == C2_LXO_DB_UPDATE ||
		op == C2_LXO_DB_DELETE || op == C2_LXO_DB_NONE);
	C2_PRE(ergo(op != C2_LXO_DB_NONE, tx != NULL));
	C2_PRE(out != NULL);

	stl = container_of(l, struct c2_layout_striped, ls_base);

	list_enum = container_of(stl->ls_enum, struct c2_layout_list_enum,
			lle_base);

	num_inline = list_enum->lle_nr >= MAX_INLINE_COB_ENTRIES ?
			MAX_INLINE_COB_ENTRIES : list_enum->lle_nr;

	C2_ALLOC_PTR(inline_cobs);
	if (inline_cobs == NULL)
		return -ENOMEM;

	inline_cobs->llces_nr = num_inline;

	/** @todo Copy all the cob entries from list_enum->lle_list_of_cobs
	 * to the buffer. */

	num_bytes = c2_bufvec_cursor_copyto(out, inline_cobs,
		sizeof(struct ldb_inline_cob_entries));
	C2_ASSERT(num_bytes == sizeof(struct ldb_inline_cob_entries));

	i = 0;
	c2_tlist_for(&cob_list_tl, &list_enum->lle_list_of_cobs, cob_entry) {
		ldb_cob_entry.llce_cob_index = cob_entry->cle_cob_index;
		ldb_cob_entry.llce_cob_id = cob_entry->cle_cob_id;

		num_bytes = c2_bufvec_cursor_copyto(out, &ldb_cob_entry,
					     sizeof(struct ldb_list_cob_entry));
		C2_ASSERT(num_bytes == sizeof(struct ldb_list_cob_entry));

		if (i == num_inline - 1)
			break;
	} c2_tlist_endfor;


	if ((op == C2_LXO_DB_ADD) || (op == C2_LXO_DB_UPDATE) ||
			(op == C2_LXO_DB_DELETE)) {
		/**
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
	.leto_max_recsize    = list_max_recsize,
	.leto_recsize        = list_recsize,
	.leto_decode         = list_decode,
	.leto_encode         = list_encode,
};

const struct c2_layout_enum_type c2_list_enum_type = {
	.let_name            = "list",
	.let_id              = 20, /* 0 */
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
