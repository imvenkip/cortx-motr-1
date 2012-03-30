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
#include "lib/bob.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_LAYOUT
#include "lib/trace.h"

#include "fid/fid.h"
#include "layout/layout_internal.h"
#include "layout/layout_db.h"       /* struct c2_ldb_schema */
#include "layout/list_enum.h"

extern bool layout_invariant(const struct c2_layout *l);
extern const struct c2_addb_loc layout_addb_loc;
extern struct c2_addb_ctx layout_global_ctx;

enum {
	/**
	 * Maximum limit on the number of COB entries those can be stored
	 * inline into the layouts table, while rest of those are stored into
	 * the cob_lists table.
	 */
	LDB_MAX_INLINE_COB_ENTRIES = 20,
	LIST_ENUM_MAGIC            = 0x4C495354454E554DULL, /* LISTENUM */
	LIST_NR_NONE               = 0
};

static const struct c2_bob_type list_enum_bob = {
	.bt_name         = "list_enum",
	.bt_magix_offset = offsetof(struct c2_layout_list_enum, lle_magic),
	.bt_magix        = LIST_ENUM_MAGIC,
	.bt_check        = NULL
};

C2_BOB_DEFINE(static, &list_enum_bob, c2_layout_list_enum);

/**
 * Structure used to store cob entries inline into the layouts table - maximum
 * upto LDB_MAX_INLINE_COB_ENTRIES number of those.
 */
struct ldb_cob_entries_header {
	/** Total number of COB Ids for the specific layout. */
	uint32_t         llces_nr;

	/**
	 * Payload storing list of cob ids (struct c2_fid), max upto
	 * LDB_MAX_INLINE_COB_ENTRIES number of those.
	 */
	char             llces_cobs[0];
};

struct list_schema_data {
	/** Table to store COB lists for all the layouts with LIST enum type. */
	struct c2_table  lsd_cob_lists;
};

/**
 * cob_lists table.
 */
struct ldb_cob_lists_key {
	/** Layout id, value obtained from c2_layout::l_id. */
	uint64_t         lclk_lid;

	/** Index for the COB from the layout it is part of. */
	uint32_t         lclk_cob_index;
};

struct ldb_cob_lists_rec {
	/** COB identifier. */
	struct c2_fid    lclr_cob_id;
};

/**
 * Compare cob_lists table keys.
 * This is a 3WAY comparison.
 */
static int lcl_key_cmp(struct c2_table *table,
		       const void *key0, const void *key1)
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

extern bool enum_invariant(const struct c2_layout_enum *le, uint64_t lid);

bool c2_list_enum_invariant(const struct c2_layout_list_enum *list_enum,
			    uint64_t lid)
{
	return list_enum != NULL && list_enum->lle_nr != LIST_NR_NONE &&
		list_enum->lle_list_of_cobs != NULL &&
		c2_layout_list_enum_bob_check(list_enum) &&
		enum_invariant(&list_enum->lle_base, lid);
}

static const struct c2_layout_enum_ops list_enum_ops;

/**
 * Build list enumeration object.
 * @note Enum object need not be finalized explicitly by the user. It is
 * finalized internally through the layout finalization routine to be invoked
 * as l->l_ops->lo_fini().
 */
int c2_list_enum_build(uint64_t lid, struct c2_fid *cob_list, uint32_t nr,
		       struct c2_layout_list_enum **out)
{
	struct c2_layout_list_enum *list_enum;
	uint32_t                    i;
	int                         rc;

	C2_PRE(lid != LID_NONE);
	C2_PRE(cob_list != NULL);
	C2_PRE(nr != LIST_NR_NONE);
	C2_PRE(out != NULL && *out == NULL);

	C2_ENTRY("BUILD, lid %llu", (unsigned long long)lid);

	C2_ALLOC_PTR(list_enum);
	if (list_enum == NULL) {
		rc = -ENOMEM;
		layout_log("c2_list_enum_build", "C2_ALLOC_PTR() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   c2_addb_oom.ae_id,
			   &layout_global_ctx, LID_APPLICABLE, lid, rc);
		goto out;
	}

	rc = c2_layout_enum_init(&list_enum->lle_base, lid,
				 &c2_list_enum_type, &list_enum_ops);
	if (rc != 0) {
		layout_log("c2_list_enum_build", "c2_layout_enum_init() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   c2_addb_func_fail.ae_id,
			   &layout_global_ctx, LID_APPLICABLE, lid, rc);
		goto out;
	}

	list_enum->lle_nr = nr;

	C2_ALLOC_ARR(list_enum->lle_list_of_cobs, nr);
	if (list_enum == NULL) {
		rc = -ENOMEM;
		layout_log("c2_list_enum_build", "C2_ALLOC_ARR() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   c2_addb_oom.ae_id,
			   &layout_global_ctx, LID_APPLICABLE, lid, rc);
		goto out;
	}

	for (i = 0; i < nr; ++i) {
		C2_ASSERT(c2_fid_is_valid(&cob_list[i]));
		list_enum->lle_list_of_cobs[i] = cob_list[i];
	}

	c2_layout_list_enum_bob_init(list_enum);

	*out = list_enum;
	C2_POST(c2_list_enum_invariant(list_enum, lid));

out:
	if (rc != 0 && list_enum != NULL) {
		c2_free(list_enum->lle_list_of_cobs);
		c2_layout_enum_fini(&list_enum->lle_base);
		c2_free(list_enum);
	}

	C2_LEAVE("lid %llu, rc %d", (unsigned long long)lid, rc);
	return rc;
}

/**
 * Implementation of leo_fini for LIST enumeration type.
 * Invoked internally by l->l_ops->lo_fini().
 */
void list_fini(struct c2_layout_enum *e, uint64_t lid)
{
	struct c2_layout_list_enum *list_enum;

	C2_PRE(e != NULL);

	C2_ENTRY("DESTROY");

	list_enum = container_of(e, struct c2_layout_list_enum, lle_base);

	C2_ASSERT(c2_list_enum_invariant(list_enum, lid));

	c2_layout_list_enum_bob_fini(list_enum);
	c2_free(list_enum->lle_list_of_cobs);
	c2_layout_enum_fini(&list_enum->lle_base);
	c2_free(list_enum);

	C2_LEAVE();
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
	C2_PRE(IS_IN_ARRAY(et->let_id, schema->ls_enum));
	C2_PRE(schema->ls_type_data[et->let_id] == NULL);

	C2_ENTRY("Enum_type_id %lu", (unsigned long)et->let_id);

	C2_ALLOC_PTR(lsd);
	if (lsd == NULL) {
		rc = -ENOMEM;
		layout_log("list_register", "C2_ALLOC_PTR() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   c2_addb_oom.ae_id,
			   &layout_global_ctx, !LID_APPLICABLE, LID_NONE, rc);
		goto out;
	}

	rc = c2_table_init(&lsd->lsd_cob_lists, schema->ls_dbenv,
			   "cob_lists", DEFAULT_DB_FLAG, &cob_lists_table_ops);
	if (rc != 0) {
		layout_log("list_register", "c2_table_init() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   c2_addb_func_fail.ae_id,
			   &layout_global_ctx, !LID_APPLICABLE, LID_NONE, rc);
		goto out;
	}

	schema->ls_type_data[et->let_id] = lsd;

out:
	if (rc != 0)
		c2_free(lsd);

	C2_LEAVE("Enum_type_id %lu, rc %d", (unsigned long)et->let_id, rc);
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
	struct list_schema_data *lsd;

	C2_PRE(schema != NULL);
	C2_PRE(et != NULL);

	C2_ENTRY("Enum_type_id %lu", (unsigned long)et->let_id);

	lsd = schema->ls_type_data[et->let_id];

	c2_table_fini(&lsd->lsd_cob_lists);

	schema->ls_type_data[et->let_id] = NULL;

	c2_free(lsd);

	C2_LEAVE("Enum_type_id %lu", (unsigned long)et->let_id);
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
		LDB_MAX_INLINE_COB_ENTRIES * sizeof(struct c2_fid);
}

/**
 * Implementation of leto_recsize() for list enumeration type.
 *
 * Returns record size for the part of the layouts table record required to
 * store LIST enum details, for the specified enumeration object.
 */
static uint32_t list_recsize(struct c2_layout_enum *e, uint64_t lid)
{
	struct c2_layout_list_enum *list_enum;

	C2_PRE(e != NULL);

	list_enum = container_of(e, struct c2_layout_list_enum, lle_base);

	C2_ASSERT(c2_list_enum_invariant(list_enum, lid));

	if (list_enum->lle_nr < LDB_MAX_INLINE_COB_ENTRIES)
		return sizeof(struct ldb_cob_entries_header) +
			list_enum->lle_nr * sizeof(struct c2_fid);
	else
		return sizeof(struct ldb_cob_entries_header) +
			LDB_MAX_INLINE_COB_ENTRIES * sizeof(struct c2_fid);
}

static int ldb_cob_list_read(struct c2_ldb_schema *schema,
			     enum c2_layout_xcode_op op, uint64_t lid,
			     uint32_t idx, struct c2_fid *cob_id,
			     struct c2_db_tx *tx)
{
	struct list_schema_data  *lsd;
	struct ldb_cob_lists_key  key;
	struct ldb_cob_lists_rec  rec;
	struct c2_db_pair         pair;
	int                       rc = 0;

	C2_PRE(op == C2_LXO_DB_LOOKUP);

	lsd = schema->ls_type_data[c2_list_enum_type.let_id];

	key.lclk_lid       = lid;
	key.lclk_cob_index = idx;

	c2_db_pair_setup(&pair, &lsd->lsd_cob_lists,
			 &key, sizeof key, &rec, sizeof rec);

	rc = c2_table_lookup(tx, &pair);
	if (rc != 0) {
		layout_log("ldb_cob_list_read", "c2_table_lookup() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   c2_addb_func_fail.ae_id,
			   &layout_global_ctx, LID_APPLICABLE, lid, rc);
		goto out;
	}

	C2_ASSERT(c2_fid_is_valid(&rec.lclr_cob_id));

	*cob_id = rec.lclr_cob_id;

out:
	c2_db_pair_fini(&pair);
	return rc;
}

/**
 * Implementation of leto_decode() for list enumeration type.
 *
 * Reads LDB_MAX_INLINE_COB_ENTRIES cob identifiers from the buffer into
 * the c2_layout_list_enum object. Then reads further cob identifiers either
 * from the DB or from the buffer, as applicable.
 *
 * @param op This enum parameter indicates what if a DB operation is to be
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
	struct c2_layout_list_enum    *list_enum = NULL;
	struct ldb_cob_entries_header *ldb_ce_header;
	uint32_t                       num_inline; /* No. of inline cobs */
	struct c2_fid                 *cob_id;
	struct c2_fid                 *cob_list;
	uint32_t                       i;
	int                            rc;

	C2_PRE(schema != NULL);
	C2_PRE(lid != LID_NONE);
	C2_PRE(cur != NULL);
	C2_PRE(op == C2_LXO_DB_LOOKUP || op == C2_LXO_DB_NONE);
	C2_PRE(ergo(op == C2_LXO_DB_LOOKUP, tx != NULL));
	C2_PRE(c2_bufvec_cursor_step(cur) >= sizeof *ldb_ce_header);

	C2_ENTRY("lid %llu", (unsigned long long)lid);

	ldb_ce_header = c2_bufvec_cursor_addr(cur);

	c2_bufvec_cursor_move(cur, sizeof *ldb_ce_header);

	C2_ALLOC_ARR(cob_list, ldb_ce_header->llces_nr);
	if (cob_list == NULL) {
		rc = -ENOMEM;
		layout_log("list_decode", "C2_ALLOC_ARR() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   c2_addb_oom.ae_id,
			   &layout_global_ctx, LID_APPLICABLE, lid, rc);
		goto out;
	}

	num_inline = ldb_ce_header->llces_nr >= LDB_MAX_INLINE_COB_ENTRIES ?
			LDB_MAX_INLINE_COB_ENTRIES : ldb_ce_header->llces_nr;

	C2_ASSERT(ergo(op == C2_LXO_DB_NONE,
		       c2_bufvec_cursor_step(cur) >=
				ldb_ce_header->llces_nr * sizeof *cob_id));
	C2_ASSERT(ergo(op == C2_LXO_DB_LOOKUP,
		       c2_bufvec_cursor_step(cur) >=
				num_inline * sizeof *cob_id));

	for (i = 0; i < ldb_ce_header->llces_nr; ++i) {
		if (i < num_inline || op == C2_LXO_DB_NONE) {
			if (i == 0)
				C2_LOG("list_decode(): Start reading "
				       "inline cob entries.");

			cob_id = c2_bufvec_cursor_addr(cur);
			c2_bufvec_cursor_move(cur, sizeof *cob_id);
			C2_ASSERT(cob_id != NULL);
			cob_list[i] = *cob_id;
		} else {
			if (i == num_inline)
				C2_LOG("list_decode(): Start reading from "
				       "cob_lists table.");

			rc = ldb_cob_list_read(schema, op, lid, i,
					       &cob_list[i], tx);
			if (rc != 0) {
				C2_LOG("list_decode(): lid %llu, "
				       "ldb_cob_list_read() failed, rc %d",
				       (unsigned long long)lid, rc);
				goto out;
			}
		}
	}

	rc = c2_list_enum_build(lid, cob_list, ldb_ce_header->llces_nr,
			        &list_enum);
	if (rc != 0) {
		layout_log("list_decode", "c2_list_enum_build() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   c2_addb_func_fail.ae_id,
			   &layout_global_ctx, LID_APPLICABLE, lid, rc);
		goto out;
	}

	*out = &list_enum->lle_base;
	C2_POST(c2_list_enum_invariant(list_enum, lid));

out:
	c2_free(cob_list);

	C2_LEAVE("lid %llu, rc %d", (unsigned long long)lid, rc);
	return rc;
}

/* todo this fn should accept lid instead of l ? */
int ldb_cob_list_write(struct c2_ldb_schema *schema,
		       enum c2_layout_xcode_op op, struct c2_layout *l,
		       uint32_t idx, struct c2_fid *cob_id,
		       struct c2_db_tx *tx)
{
	struct list_schema_data  *lsd;
	struct ldb_cob_lists_key  key;
	struct ldb_cob_lists_rec  rec;
	struct c2_db_pair         pair;
	int                       rc;

	C2_PRE(schema != NULL);
	C2_PRE(op == C2_LXO_DB_ADD || op == C2_LXO_DB_DELETE);
	C2_PRE(l != NULL);
	C2_PRE(cob_id != NULL);
	C2_PRE(tx != NULL);

	lsd = schema->ls_type_data[c2_list_enum_type.let_id];
	C2_ASSERT(lsd != NULL);

	key.lclk_lid       = l->l_id;
	key.lclk_cob_index = idx;
	rec.lclr_cob_id    = *cob_id;

	c2_db_pair_setup(&pair, &lsd->lsd_cob_lists,
			 &key, sizeof key, &rec, sizeof rec);

	if (op == C2_LXO_DB_ADD) {
		rc = c2_table_insert(tx, &pair);
		if (rc != 0) {
			layout_log("ldb_cob_list_write",
				   "c2_table_insert() failed",
				   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
				   c2_addb_func_fail.ae_id,
				   &layout_global_ctx,
				   LID_APPLICABLE, l->l_id, rc);
		}
	} else if (op == C2_LXO_DB_DELETE) {
		rc = c2_table_delete(tx, &pair);
		if (rc != 0) {
			layout_log("ldb_cob_list_write",
				   "c2_table_delete() failed",
				   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
				   c2_addb_func_fail.ae_id,
				   &layout_global_ctx,
				   LID_APPLICABLE, l->l_id, rc);
		}
	}

	c2_db_pair_fini(&pair);
	return rc;
}

/**
 * Implementation of leto_encode() for list enumeration type.
 *
 * Continues to use the in-memory layout object and either 'stores it in the
 * Layout DB' or 'converts it to a buffer that can be passed on over the
 * network'.

 * @param op This enum parameter indicates what is the DB operation to be
 * performed on the layout record if at all and it could be one of
 * ADD/UPDATE/DELETE. If it is NONE, then the layout is converted into the
 * buffer.
 */
static int list_encode(struct c2_ldb_schema *schema,
		       struct c2_layout *l,
		       enum c2_layout_xcode_op op,
		       struct c2_db_tx *tx,
		       struct c2_bufvec_cursor *oldrec_cur,
		       struct c2_bufvec_cursor *out)
{
	struct c2_layout_striped      *stl;
	struct c2_layout_list_enum    *list_enum;
	uint32_t                       num_inline; /* No. of inline cobs */
	struct ldb_cob_entries_header  ldb_ce_header;
	struct ldb_cob_entries_header *ldb_ce_oldheader;
	struct c2_fid                 *cob_id_old;
	c2_bcount_t                    nbytes;
	int                            i;
	int                            rc = 0;

	C2_PRE(schema != NULL);
	C2_PRE(layout_invariant(l));
	C2_PRE(op == C2_LXO_DB_ADD || op == C2_LXO_DB_UPDATE ||
	       op == C2_LXO_DB_DELETE || op == C2_LXO_DB_NONE);
	C2_PRE(ergo(op != C2_LXO_DB_NONE, tx != NULL));
	C2_PRE(ergo(op == C2_LXO_DB_UPDATE, oldrec_cur != NULL));
	C2_PRE(out != NULL);
	C2_PRE(c2_bufvec_cursor_step(out) >= sizeof ldb_ce_header);
	C2_PRE(ergo(op == C2_LXO_DB_UPDATE,
		    c2_bufvec_cursor_step(oldrec_cur) >=
				sizeof *ldb_ce_oldheader));

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);

	stl = container_of(l, struct c2_layout_striped, ls_base);
	list_enum = container_of(stl->ls_enum, struct c2_layout_list_enum,
				 lle_base);
	C2_ASSERT(c2_list_enum_invariant(list_enum, l->l_id));

	num_inline = list_enum->lle_nr >= LDB_MAX_INLINE_COB_ENTRIES ?
			LDB_MAX_INLINE_COB_ENTRIES : list_enum->lle_nr;

	if (op == C2_LXO_DB_UPDATE) {
		ldb_ce_oldheader = c2_bufvec_cursor_addr(oldrec_cur);

		C2_ASSERT(ldb_ce_oldheader->llces_nr == list_enum->lle_nr);

		c2_bufvec_cursor_move(oldrec_cur, sizeof *ldb_ce_oldheader);

		C2_ASSERT(c2_bufvec_cursor_step(oldrec_cur) >=
			  num_inline * sizeof *cob_id_old);

		for (i = 0; i < num_inline; ++i) {
			cob_id_old = c2_bufvec_cursor_addr(oldrec_cur);

			C2_ASSERT(c2_fid_eq(cob_id_old,
					    &list_enum->lle_list_of_cobs[i]));

			c2_bufvec_cursor_move(oldrec_cur,
					sizeof list_enum->lle_list_of_cobs[i]);
		}
	}

	ldb_ce_header.llces_nr = list_enum->lle_nr;

	nbytes = c2_bufvec_cursor_copyto(out, &ldb_ce_header,
					 sizeof ldb_ce_header);
	C2_ASSERT(nbytes == sizeof ldb_ce_header);

	C2_ASSERT(ergo(op == C2_LXO_DB_NONE,
		       c2_bufvec_cursor_step(out) >=
				list_enum->lle_nr *
				sizeof list_enum->lle_list_of_cobs[i]));

	C2_ASSERT(ergo(op == C2_LXO_DB_LOOKUP,
		       c2_bufvec_cursor_step(out) >=
				num_inline *
				sizeof list_enum->lle_list_of_cobs[i]));

	for(i = 0; i < list_enum->lle_nr; ++i) {
		if (i < num_inline || op == C2_LXO_DB_NONE) {
			if (i == 0)
				C2_LOG("list_encode(): lid %llu, Start "
				       "accepting inline cob entries.",
				       (unsigned long long)l->l_id);

			nbytes = c2_bufvec_cursor_copyto(out,
					&list_enum->lle_list_of_cobs[i],
					sizeof list_enum->lle_list_of_cobs[i]);
			C2_ASSERT(nbytes ==
				  sizeof list_enum->lle_list_of_cobs[i]);
		}
		else if (op == C2_LXO_DB_UPDATE) {
			/*
			 * The auxiliary table viz. cob_lists is not to be
			 * modified for an update operation. Hence, need to
			 * return from here.
			 */
			rc = 0;
			goto out;
		} else {
			/*
			 * Write non-inline cob entries to the cob_lists
			 * table.
			 */
			C2_ASSERT(op == C2_LXO_DB_ADD ||
				  op == C2_LXO_DB_DELETE);
			if (i == num_inline)
				C2_LOG("list_encode(): lid %llu, Start writing"
				       " to the cob_lists table.",
				       (unsigned long long)l->l_id);

			rc = ldb_cob_list_write(schema, op, l, i,
						&list_enum->lle_list_of_cobs[i],
						tx);
			if (rc != 0) {
				C2_LOG("list_encode(): lid %llu, "
				       "ldb_cob_list_write() failed, rc %d",
				       (unsigned long long)l->l_id, rc);
				goto out;
			}
		}
	}
out:
	C2_LEAVE("lid %llu, rc %d", (unsigned long long)l->l_id, rc);
	return rc;
}

/**
 * Implementation of leo_nr for LIST enumeration.
 * Returns number of objects in the enumeration.
 * Argument fid is ignored here for LIST enumeration type.
 */
static uint32_t list_nr(const struct c2_layout_enum *le, uint64_t lid)
{
	struct c2_layout_list_enum *list_enum;

	C2_PRE(le != NULL);
	C2_PRE(lid != LID_NONE);

	list_enum = container_of(le, struct c2_layout_list_enum, lle_base);
	C2_ASSERT(c2_list_enum_invariant(list_enum, lid));

	return list_enum->lle_nr;
}

/**
 * Implementation of leo_get for LIST enumeration.
 * Returns idx-th object from the enumeration.
 * Argument fid is ignored here for LIST enumeration type.
 */
static void list_get(const struct c2_layout_enum *le, uint64_t lid,
		     uint32_t idx, const struct c2_fid *gfid,
		     struct c2_fid *out)
{
	struct c2_layout_list_enum *list_enum;

	C2_PRE(le != NULL);
	C2_PRE(lid != LID_NONE);
	/* gfid is ignored for the list enumeration type. */
	C2_PRE(out != NULL);

	list_enum = container_of(le, struct c2_layout_list_enum, lle_base);
	C2_ASSERT(c2_list_enum_invariant(list_enum, lid));

	C2_ASSERT(idx < list_enum->lle_nr);

	C2_ASSERT(c2_fid_is_valid(&list_enum->lle_list_of_cobs[idx]));

	*out = list_enum->lle_list_of_cobs[idx];
}

static const struct c2_layout_enum_ops list_enum_ops = {
	.leo_nr           = list_nr,
	.leo_get          = list_get,
	.leo_fini         = list_fini
};

static const struct c2_layout_enum_type_ops list_type_ops = {
	.leto_register    = list_register,
	.leto_unregister  = list_unregister,
	.leto_max_recsize = list_max_recsize,
	.leto_recsize     = list_recsize,
	.leto_decode      = list_decode,
	.leto_encode      = list_encode,
};

const struct c2_layout_enum_type c2_list_enum_type = {
	.let_name         = "list",
	.let_id           = 0,
	.let_ops          = &list_type_ops
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
