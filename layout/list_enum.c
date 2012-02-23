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
#include "lib/trace.h"
#include "fid/fid.h"                /* struct c2_fid */
#include "layout/layout_internal.h"
#include "layout/list_enum.h"
#include "layout/layout_db.h"       /* struct c2_ldb_schema */

enum {
	/**
	 * Maximum limit on the number of COB entries those can be stored
	 * inline into the layouts table, while rest of those are stored into
	 * the cob_lists table.
	 */
	LDB_MAX_INLINE_COB_ENTRIES = 20
};

struct ldb_cob_entry {
	/** Index for the COB from the layout it is part of. */
	uint32_t                  llce_cob_index;

	/** COB identifier. */
	struct c2_fid             llce_cob_id;
};

/**
 * Structure used to store cob entries inline into the layouts table - maximum
 * upto LDB_MAX_INLINE_COB_ENTRIES number of those.
 */
struct ldb_cob_entries_header {
	/** Total number of COB Ids for the specific layout. */
	uint32_t                  llces_nr;

	/**
	 * Payload storing list of ldb_cob_entry, max upto
	 * LDB_MAX_INLINE_COB_ENTRIES number of those.
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
	const struct ldb_cob_lists_key *k0 = key0;
	const struct ldb_cob_lists_key *k1 = key1;

	return C2_3WAY(k0->lclk_lid, k1->lclk_lid) ?:
                C2_3WAY(k0->lclk_cob_index, k1->lclk_cob_index);
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


static const struct c2_layout_enum_ops list_enum_ops;

int c2_list_enum_build(uint64_t lid, uint32_t nr,
		       struct c2_layout_list_enum **out)
{
	struct c2_layout_list_enum *list_enum;

	C2_PRE(lid != LID_NONE);
	C2_PRE(out != NULL);

	C2_ALLOC_PTR(list_enum);
	if (list_enum == NULL)
		return -ENOMEM;

	c2_layout_enum_init(&list_enum->lle_base, &c2_list_enum_type,
			    &list_enum_ops);

	list_enum->lle_lid = lid;
	list_enum->lle_nr = nr;

	C2_ALLOC_ARR(list_enum->lle_list_of_cobs, nr);
	if (list_enum == NULL)
		return -ENOMEM;

	*out = list_enum;

	return 0;
}

void c2_list_enum_fini(struct c2_layout_list_enum *list_enum)
{
	c2_free(list_enum->lle_list_of_cobs);

	c2_layout_enum_fini(&list_enum->lle_base);
}

int c2_list_enum_add(struct c2_layout_list_enum *le,
		     uint32_t idx, struct c2_fid *cob_id)
{
	C2_PRE(idx < le->lle_nr);
	C2_PRE(le->lle_list_of_cobs[idx].f_container == 0 &&
	       le->lle_list_of_cobs[idx].f_key == 0);

	if(idx >= le->lle_nr || le->lle_list_of_cobs[idx].f_container != 0 ||
	   le->lle_list_of_cobs[idx].f_key != 0)
		return -EINVAL;

	le->lle_list_of_cobs[idx] = *cob_id;

	return 0;
}

/**
 * Implementation of leto_register for LIST enumeration type.
 *
 * Initializes table specifically required for LIST enum type.
 */
static int list_register(struct c2_ldb_schema *schema,
			 const struct c2_layout_enum_type *et)
{
	struct list_schema_data  *lsd;
	int                       rc;

	C2_PRE(schema != NULL);
	C2_PRE(et != NULL);

	C2_ALLOC_PTR(lsd);
	if (lsd == NULL)
		return -ENOMEM;

	rc = c2_table_init(&lsd->lsd_cob_lists, schema->dbenv, "cob_lists", 0,
			   &cob_lists_table_ops);
	C2_ASSERT(rc == 0);

	schema->ls_type_data[et->let_id] = lsd;

	return rc;
}

/**
 * Implementation of leto_unregister for LIST enumeration type.
 *
 * De-initializes table specifically required for LIST enum type.
 */
static void list_unregister(struct c2_ldb_schema *schema,
			    const struct c2_layout_enum_type *et)
{
	struct list_schema_data  *lsd;

	C2_PRE(schema != NULL);
	C2_PRE(et != NULL);

	lsd = schema->ls_type_data[et->let_id];

	c2_table_fini(&lsd->lsd_cob_lists);

	schema->ls_type_data[et->let_id] = NULL;
}

/**
 * Implementation of leto_max_recsize() for list enumeration type.
 *
 * Returns maximum record size for the part of the layouts table record,
 * required to store LIST enum details.
 */
static uint32_t list_max_recsize(void)
{
	return sizeof(struct ldb_cob_entries_header) +
		LDB_MAX_INLINE_COB_ENTRIES * sizeof(struct ldb_cob_entry);
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

	if (list_enum->lle_nr <= LDB_MAX_INLINE_COB_ENTRIES)
		return sizeof(struct ldb_cob_entries_header) +
			list_enum->lle_nr * sizeof(struct ldb_cob_entry);
	else
		return sizeof(struct ldb_cob_entries_header) +
			LDB_MAX_INLINE_COB_ENTRIES * sizeof(struct
							    ldb_cob_entry);
}

/* todo Check how should the cob entry be passed to this fn. */
static int ldb_cob_list_read(struct c2_ldb_schema *schema,
			     enum c2_layout_xcode_op op,
			     uint64_t lid, uint32_t idx,
			     struct c2_fid *cob_id,
			     struct c2_db_tx *tx)
{
	struct list_schema_data  *lsd;
	struct ldb_cob_lists_key  key;
	struct ldb_cob_lists_rec  rec;
	struct c2_db_pair         pair;
	int                       rc;

	lsd = schema->ls_type_data[c2_list_enum_type.let_id];

	key.lclk_lid       = lid;
	key.lclk_cob_index = idx;

	c2_db_pair_setup(&pair, &lsd->lsd_cob_lists,
			 &key, sizeof key,
			 &rec, sizeof rec);

	rc = c2_table_lookup(tx, &pair);
	C2_ASSERT(rc == 0);
	if (rc != 0)
		return rc;

	/* todo tempo */
	C2_ASSERT(rec.lclr_cob_id.f_container != 0);
	*cob_id = rec.lclr_cob_id;

	return 0;
}


/**
 * Implementation of leto_decode() for list enumeration type.
 *
 * Reads LDB_MAX_INLINE_COB_ENTRIES cob identifiers from the buffer into
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
	struct ldb_cob_entries_header *ldb_ce_header;
	struct ldb_cob_entry          *ldb_ce;
	uint32_t                       num_inline; /* No. of inline cobs */
	struct c2_fid                  cob_fid;
	uint32_t                       i;
	int                            rc;

	C2_PRE(schema != NULL);
	C2_PRE(lid != LID_NONE);
	C2_PRE(cur != NULL);
	C2_PRE(op == C2_LXO_DB_LOOKUP || op == C2_LXO_DB_NONE);
	C2_PRE(ergo(op == C2_LXO_DB_LOOKUP, tx != NULL));

	//C2_LOG("In list_decode(), cur %p \n", cur);

	ldb_ce_header = c2_bufvec_cursor_addr(cur);
	C2_ASSERT(ldb_ce_header != NULL);
	c2_bufvec_cursor_move(cur, sizeof *ldb_ce_header);

	rc = c2_list_enum_build(lid, ldb_ce_header->llces_nr, &list_enum);
	C2_ASSERT(rc == 0);
	if (list_enum == NULL)
		return -ENOMEM;

	*out = &list_enum->lle_base;


	num_inline = ldb_ce_header->llces_nr >= LDB_MAX_INLINE_COB_ENTRIES ?
			LDB_MAX_INLINE_COB_ENTRIES : ldb_ce_header->llces_nr;

	for (i = 0; i < ldb_ce_header->llces_nr; ++i) {
		if (i == 0) {
			//C2_LOG("list_decode(): Start processing "
			       //"inline cob entries.\n");
		} else if (i == num_inline) {
			//C2_LOG("list_decode(): Start processing "
			       //"non-inline cob entries.\n");
		}

		if (i < num_inline || op == C2_LXO_DB_NONE) {
			ldb_ce = c2_bufvec_cursor_addr(cur);
			c2_bufvec_cursor_move(cur, sizeof *ldb_ce);
			cob_fid = ldb_ce->llce_cob_id;
			C2_ASSERT(ldb_ce != NULL);
			C2_ASSERT(ldb_ce->llce_cob_index <=
				  ldb_ce_header->llces_nr);

			rc = c2_list_enum_add(list_enum,
					      ldb_ce->llce_cob_index,
					      &ldb_ce->llce_cob_id);
			C2_ASSERT(rc == 0);
			if (rc != 0)
				return rc;
		} else {
			/* todo Is it acceptable if any of the cob entry is not
			 * found in between from the cob_list table? If yes,
			 * following assert needs to be removed.
			 */
			rc = ldb_cob_list_read(schema, op, lid, i,
					       &cob_fid, tx);
			C2_ASSERT(rc == 0);
			if (rc != 0)
				return rc;

			rc = c2_list_enum_add(list_enum, i, &cob_fid);
			C2_ASSERT(rc == 0);
			if (rc != 0)
				return rc;
		}


		/* todo Verify elsewhere that it is fine if order is not
		 * followed.*/
	}

	return 0;
}

/* todo Check how should the cob entry be passed to this fn. */
int ldb_cob_list_write(struct c2_ldb_schema *schema,
		       enum c2_layout_xcode_op op,
		       uint64_t lid,
		       struct ldb_cob_entry *ldb_ce,
		       struct c2_db_tx *tx)
{
	struct list_schema_data  *lsd;
	struct ldb_cob_lists_key  key;
	struct ldb_cob_lists_rec  rec;
	struct c2_db_pair         pair;

	lsd = schema->ls_type_data[c2_list_enum_type.let_id];

	key.lclk_lid       = lid;
	key.lclk_cob_index = ldb_ce->llce_cob_index;
	rec.lclr_cob_id    = ldb_ce->llce_cob_id;

	c2_db_pair_setup(&pair, &lsd->lsd_cob_lists,
			 &key, sizeof key,
			 &rec, sizeof rec);

	if (op == C2_LXO_DB_ADD) {
		c2_table_insert(tx, &pair);
	} else if (op == C2_LXO_DB_UPDATE) {
		c2_table_update(tx, &pair);
	} else if (op == C2_LXO_DB_DELETE) {
		c2_table_delete(tx, &pair);
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
 * ADD/UPDATE/DELETE. If it is NONE, then the layout is converted into the
 * buffer.
 */
static int list_encode(struct c2_ldb_schema *schema,
		       const struct c2_layout *l,
		       enum c2_layout_xcode_op op,
		       struct c2_db_tx *tx,
		       struct c2_bufvec_cursor *oldrec_cur,
		       struct c2_bufvec_cursor *out)
{
	struct c2_layout_striped      *stl;
	struct c2_layout_list_enum    *list_enum;
	uint32_t                       num_inline; /* No. of inline cobs */
	struct ldb_cob_entries_header *ldb_ce_header;
	struct ldb_cob_entries_header *ldb_ce_oldheader;
	struct ldb_cob_entry           ldb_ce;
	struct ldb_cob_entry          *ldb_ce_old;
	c2_bcount_t                    nbytes_copied;
	int                            i;
	int                            rc = 0;

	C2_PRE(schema != NULL);
	C2_PRE(layout_invariant(l));
	C2_PRE(op == C2_LXO_DB_ADD || op == C2_LXO_DB_UPDATE ||
		op == C2_LXO_DB_DELETE || op == C2_LXO_DB_NONE);
	C2_PRE(ergo(op != C2_LXO_DB_NONE, tx != NULL));
	C2_PRE(ergo(op == C2_LXO_DB_UPDATE, oldrec_cur != NULL));
	C2_PRE(out != NULL);

	//C2_LOG("In list_encode(), l %p \n", l);

	stl = container_of(l, struct c2_layout_striped, ls_base);
	list_enum = container_of(stl->ls_enum, struct c2_layout_list_enum,
				 lle_base);

	num_inline = list_enum->lle_nr >= LDB_MAX_INLINE_COB_ENTRIES ?
			LDB_MAX_INLINE_COB_ENTRIES : list_enum->lle_nr;

	if (op == C2_LXO_DB_UPDATE) {
		ldb_ce_oldheader = c2_bufvec_cursor_addr(oldrec_cur);
		C2_ASSERT(ldb_ce_oldheader != NULL);

		if (ldb_ce_oldheader->llces_nr != list_enum->lle_nr)
			return -EINVAL;

		c2_bufvec_cursor_move(oldrec_cur, sizeof *ldb_ce_oldheader);

		for (i = 0; i < num_inline; ++i) {
			ldb_ce_old = c2_bufvec_cursor_addr(oldrec_cur);

			if (ldb_ce_old->llce_cob_index != i)
				return -EINVAL;
			if (ldb_ce_old->llce_cob_id.f_container !=
			    list_enum->lle_list_of_cobs[i].f_container ||
			    ldb_ce_old->llce_cob_id.f_key !=
			    list_enum->lle_list_of_cobs[i].f_key)
				return -EINVAL;

			c2_bufvec_cursor_move(oldrec_cur, sizeof *ldb_ce_old);
		}

		/*
		 * The auxiliary table viz. cob_lists is not to be modified
		 * for an update operation. Hence, return from here.
		 */
		return 0;
	}

	C2_ALLOC_PTR(ldb_ce_header);
	if (ldb_ce_header == NULL)
		return -ENOMEM;

	ldb_ce_header->llces_nr = list_enum->lle_nr;

	nbytes_copied = c2_bufvec_cursor_copyto(out, ldb_ce_header,
						sizeof *ldb_ce_header);
	C2_ASSERT(nbytes_copied == sizeof *ldb_ce_header);

	for(i = 0; i < list_enum->lle_nr; ++i) {
		ldb_ce.llce_cob_index = i;
		ldb_ce.llce_cob_id = list_enum->lle_list_of_cobs[i];
		if (i < num_inline || op == C2_LXO_DB_NONE) {
			if (i == 0) {
				C2_LOG("list_encode(): Start processing "
				       "inline cob entries.\n");
			}
			if (i == num_inline) {
				C2_LOG("list_encode(): Start processing "
				       "non-inline cob entries.\n");
			}

			nbytes_copied = c2_bufvec_cursor_copyto(out, &ldb_ce,
							sizeof ldb_ce);
			C2_ASSERT(nbytes_copied == sizeof ldb_ce);
		}
		else {
			/* Write non-inline cob entries to the
			 * cob_lists table. */
			C2_LOG("list_encode(): Writing to cob_lists table: "
			       "i %u, ldb_ce.llce_cob_index %llu/n",
			       i, (unsigned long long)ldb_ce.llce_cob_index);
			rc = ldb_cob_list_write(schema, op, l->l_id,
						&ldb_ce, tx);
		}
	}

	return rc;
}

/**
 * Implementation of leo_nr for LIST enumeration.
 * Returns number of objects in the enumeration.
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
	.let_id              = 0,
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
