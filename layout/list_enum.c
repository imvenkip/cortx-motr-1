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
#include "lib/misc.h" /* c2_forall() */
#include "lib/bob.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_LAYOUT
#include "lib/trace.h"

#include "fid/fid.h"
#include "layout/layout_internal.h"
#include "layout/list_enum.h"

extern const struct c2_addb_loc layout_addb_loc;
extern struct c2_addb_ctx layout_global_ctx;

enum {
	LIST_ENUM_MAGIC = 0x4C495354454E554DULL /* LISTENUM */
};

static const struct c2_bob_type list_enum_bob = {
	.bt_name         = "list_enum",
	.bt_magix_offset = offsetof(struct c2_layout_list_enum, lle_magic),
	.bt_magix        = LIST_ENUM_MAGIC,
	.bt_check        = NULL
};

C2_BOB_DEFINE(static, &list_enum_bob, c2_layout_list_enum);

struct list_schema_data {
	/** Table to store COB lists for all the layouts with LIST enum type. */
	struct c2_table  lsd_cob_lists;
};

/** cob_lists table. */
struct cob_lists_key {
	/** Layout id, value obtained from c2_layout::l_id. */
	uint64_t  clk_lid;

	/** Index for the COB from the layout it is part of. */
	uint32_t  clk_cob_index;
};

struct cob_lists_rec {
	/** COB identifier. */
	struct c2_fid  clr_cob_id;
};

/**
 * Compare cob_lists table keys.
 * This is a 3WAY comparison.
 */
static int lcl_key_cmp(struct c2_table *table,
		       const void *key0, const void *key1)
{
	const struct cob_lists_key *k0 = key0;
	const struct cob_lists_key *k1 = key1;

	return C2_3WAY(k0->clk_lid, k1->clk_lid) ?:
                C2_3WAY(k0->clk_cob_index, k1->clk_cob_index);
}

/** table_ops for cob_lists table. */
static const struct c2_table_ops cob_lists_table_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct cob_lists_key)
		},
		[TO_REC] = {
			.max_size = sizeof(struct cob_lists_rec)
		}
	},
	.key_cmp = lcl_key_cmp
};

/**
 * list_enum_invariant() can not be invoked until an enumeration object
 * is associated with some layout object. Hence this separation.
 */
static bool list_enum_invariant_internal(const struct c2_layout_list_enum *le)
{
	return
		le != NULL &&
		c2_layout_list_enum_bob_check(le) &&
		le->lle_nr != NR_NONE &&
		le->lle_list_of_cobs != NULL &&
		c2_forall(i, le->lle_nr,
			  c2_fid_is_valid(&le->lle_list_of_cobs[i]));
}

static bool list_enum_invariant(const struct c2_layout_list_enum *le)
{
	return
		list_enum_invariant_internal(le) &&
		enum_invariant(&le->lle_base);
}

static const struct c2_layout_enum_ops list_enum_ops;

/**
 * Build list enumeration object.
 * @note Enum object need not be finalised explicitly by the user. It is
 * finalised internally through c2_layout__striped_fini().
 */
int c2_list_enum_build(struct c2_layout_domain *dom,
		       struct c2_fid *cob_list, uint32_t nr,
		       struct c2_layout_list_enum **out)
{
	struct c2_layout_list_enum *list_enum;
	uint32_t                    i;
	int                         rc;

	C2_PRE(domain_invariant(dom));
	C2_PRE(cob_list != NULL);
	C2_PRE(nr != NR_NONE);
	C2_PRE(out != NULL);

	C2_ENTRY();

	C2_ALLOC_PTR(list_enum);
	if (list_enum == NULL) {
		rc = -ENOMEM;
		c2_layout__log("c2_list_enum_build", "C2_ALLOC_PTR() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &c2_addb_oom, &layout_global_ctx, LID_NONE, rc);
		goto out;
	}

	c2_layout__enum_init(dom, &list_enum->lle_base, &c2_list_enum_type,
			     &list_enum_ops);
	list_enum->lle_nr = nr;

	C2_ALLOC_ARR(list_enum->lle_list_of_cobs, nr);
	if (list_enum == NULL) {
		rc = -ENOMEM;
		c2_layout__log("c2_list_enum_build", "C2_ALLOC_ARR() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &c2_addb_oom, &layout_global_ctx, LID_NONE, rc);
		c2_layout__enum_fini(&list_enum->lle_base);
		c2_free(list_enum);
		goto out;
	}

	for (i = 0; i < nr; ++i) {
		C2_ASSERT(c2_fid_is_valid(&cob_list[i]));
		list_enum->lle_list_of_cobs[i] = cob_list[i];
	}

	c2_layout_list_enum_bob_init(list_enum);

	*out = list_enum;
	C2_POST(list_enum_invariant_internal(list_enum));
	rc = 0;
out:
	C2_LEAVE("rc %d", rc);
	return rc;
}

/**
 * Finalise list enumeration object.
 * @note This interface is expected to be used only in cases where layout
 * build operation fails and the user (for example c2t1fs) needs to get rid of
 * the enumeration object created prior to attempting the layout build
 * operation. In the other regular cases, enumeration object is finalised
 * internally through c2_layout__striped_fini().
 */
void c2_list_enum_fini(struct c2_layout_list_enum *e)
{
	C2_PRE(list_enum_invariant_internal(e));
	C2_PRE(e->lle_base.le_l == NULL);

	e->lle_base.le_ops->leo_fini(&e->lle_base);
}

/**
 * Implementation of leo_fini for LIST enumeration type.
 * Invoked internally by c2_layout__striped_fini().
 */
void list_fini(struct c2_layout_enum *e)
{
	struct c2_layout_list_enum *list_enum;

	C2_PRE(e != NULL);

	C2_ENTRY("lid %llu, enum_pointer %p",
		 (unsigned long long)e->le_l->l_id, e);

	list_enum = container_of(e, struct c2_layout_list_enum, lle_base);
	C2_ASSERT(list_enum_invariant(list_enum));

	c2_layout_list_enum_bob_fini(list_enum);
	c2_free(list_enum->lle_list_of_cobs);
	c2_layout__enum_fini(&list_enum->lle_base);
	c2_free(list_enum);

	C2_LEAVE();
}

/**
 * Implementation of leto_register for LIST enumeration type.
 *
 * Initialises table specifically required for LIST enum type.
 */
static int list_register(struct c2_layout_domain *dom,
			 const struct c2_layout_enum_type *et)
{
	struct list_schema_data *lsd;
	int                      rc;

	C2_PRE(domain_invariant(dom));
	C2_PRE(et != NULL);
	C2_PRE(IS_IN_ARRAY(et->let_id, dom->ld_enum));
	C2_PRE(dom->ld_schema.ls_type_data[et->let_id] == NULL);

	C2_ENTRY("Enum_type_id %lu", (unsigned long)et->let_id);

	C2_ALLOC_PTR(lsd);
	if (lsd == NULL) {
		rc = -ENOMEM;
		c2_layout__log("list_register", "C2_ALLOC_PTR() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &c2_addb_oom, &layout_global_ctx, LID_NONE, rc);
		goto out;
	}

	rc = c2_table_init(&lsd->lsd_cob_lists, dom->ld_schema.ls_dbenv,
			   "cob_lists", DEFAULT_DB_FLAG, &cob_lists_table_ops);
	if (rc != 0) {
		c2_layout__log("list_register", "c2_table_init() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &c2_addb_func_fail, &layout_global_ctx,
			       LID_NONE, rc);
		c2_free(lsd);
		goto out;
	}

	dom->ld_schema.ls_type_data[et->let_id] = lsd;
out:
	C2_LEAVE("Enum_type_id %lu, rc %d", (unsigned long)et->let_id, rc);
	return rc;
}

/**
 * Implementation of leto_unregister for LIST enumeration type.
 *
 * Finalises table specifically required for LIST enum type.
 */
static void list_unregister(struct c2_layout_domain *dom,
			    const struct c2_layout_enum_type *et)
{
	struct list_schema_data *lsd;

	C2_PRE(domain_invariant(dom));
	C2_PRE(et != NULL);

	C2_ENTRY("Enum_type_id %lu", (unsigned long)et->let_id);

	lsd = dom->ld_schema.ls_type_data[et->let_id];
	c2_table_fini(&lsd->lsd_cob_lists);
	dom->ld_schema.ls_type_data[et->let_id] = NULL;
	c2_free(lsd);

	C2_LEAVE("Enum_type_id %lu", (unsigned long)et->let_id);
}

/**
 * Implementation of leto_max_recsize() for list enumeration type.
 *
 * Returns maximum record size for the part of the layouts table record,
 * required to store LIST enum details.
 */
static c2_bcount_t list_max_recsize(void)
{
	return sizeof(struct cob_entries_header) +
		LDB_MAX_INLINE_COB_ENTRIES * sizeof(struct c2_fid);
}

/**
 * Implementation of leto_recsize() for list enumeration type.
 *
 * Returns record size for the part of the layouts table record required to
 * store LIST enum details, for the specified enumeration object.
 */
static c2_bcount_t list_recsize(struct c2_layout_enum *e)
{
	struct c2_layout_list_enum *list_enum;

	C2_PRE(e != NULL);

	list_enum = container_of(e, struct c2_layout_list_enum, lle_base);
	C2_ASSERT(list_enum_invariant(list_enum));

	if (list_enum->lle_nr < LDB_MAX_INLINE_COB_ENTRIES)
		return sizeof(struct cob_entries_header) +
			list_enum->lle_nr * sizeof(struct c2_fid);
	else
		return sizeof(struct cob_entries_header) +
			LDB_MAX_INLINE_COB_ENTRIES * sizeof(struct c2_fid);
}

static int noninline_cob_list_read(struct c2_layout_schema *schema,
				   struct c2_db_tx *tx,
				   uint64_t lid,
				   uint32_t idx_start,
				   uint32_t idx_end,
				   struct c2_fid *cob_list)
{
	struct list_schema_data *lsd;
	struct cob_lists_key     key;
	struct cob_lists_rec     rec;
	struct c2_db_pair        pair;
	struct c2_db_cursor      cursor;
	uint32_t                 i;
	int                      rc;

	C2_PRE(schema != NULL);
	C2_PRE(tx != NULL);
	C2_PRE(idx_end > idx_start);
	C2_PRE(cob_list != NULL);

	C2_ENTRY("lid %llu, idx_start %lu, idx_end %lu",
		 (unsigned long long)lid, (unsigned long)idx_start,
		 (unsigned long)idx_end);

	lsd = schema->ls_type_data[c2_list_enum_type.let_id];
	C2_ASSERT(lsd != NULL);

	rc = c2_db_cursor_init(&cursor, &lsd->lsd_cob_lists, tx, 0);
	if (rc != 0) {
		c2_layout__log("noninline_cob_list_read",
			       "c2_db_cursor_init() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &c2_addb_func_fail, &layout_global_ctx,
			       lid, rc);
		goto out;
	}

	key.clk_lid       = lid;
	key.clk_cob_index = idx_start;
	c2_db_pair_setup(&pair, &lsd->lsd_cob_lists,
			 &key, sizeof key, &rec, sizeof rec);
	for (i = idx_start; i < idx_end; ++i) {
		if (i == idx_start)
			rc = c2_db_cursor_get(&cursor, &pair);
		else
			rc = c2_db_cursor_next(&cursor, &pair);
		if (rc != 0) {
			c2_layout__log("noninline_cob_list_read",
				       "c2_db_cursor_get() failed",
				       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
				       &c2_addb_func_fail, &layout_global_ctx,
				       lid, rc);
			goto out;
		}
		C2_ASSERT(key.clk_lid == lid);
		C2_ASSERT(key.clk_cob_index == i);
		C2_ASSERT(c2_fid_is_valid(&rec.clr_cob_id));
		cob_list[i] = rec.clr_cob_id;
	}

out:
	c2_db_pair_fini(&pair);
	c2_db_cursor_fini(&cursor);
	C2_LEAVE("rc %d", rc);
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
 * If it is BUFFER_OP, then the layout is decoded from its representation
 * received through the buffer.
 */
static int list_decode(struct c2_layout_domain *dom,
		       uint64_t lid,
		       enum c2_layout_xcode_op op,
		       struct c2_db_tx *tx,
		       struct c2_bufvec_cursor *cur,
		       struct c2_layout_enum **out)
{
	struct c2_layout_list_enum *list_enum = NULL;
	struct cob_entries_header  *ce_header;
	uint32_t                    num_inline; /* Number of inline cobs */
	struct c2_fid              *cob_id;
	struct c2_fid              *cob_list;
	uint32_t                    i;
	int                         rc;

	C2_PRE(domain_invariant(dom));
	C2_PRE(lid != LID_NONE);
	C2_PRE(C2_IN(op, (C2_LXO_DB_LOOKUP, C2_LXO_BUFFER_OP)));
	C2_PRE(ergo(op == C2_LXO_DB_LOOKUP, tx != NULL));
	C2_PRE(cur != NULL);
	C2_PRE(c2_bufvec_cursor_step(cur) >= sizeof *ce_header);
	C2_PRE(out != NULL);

	ce_header = c2_bufvec_cursor_addr(cur);

	C2_ENTRY("lid %llu, nr %lu", (unsigned long long)lid,
		 (unsigned long)ce_header->ces_nr);

	c2_bufvec_cursor_move(cur, sizeof *ce_header);

	C2_ALLOC_ARR(cob_list, ce_header->ces_nr);
	if (cob_list == NULL) {
		rc = -ENOMEM;
		c2_layout__log("list_decode", "C2_ALLOC_ARR() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &c2_addb_oom, &layout_global_ctx, lid, rc);
		goto out;
	}

	num_inline = min_check(ce_header->ces_nr,
			       (uint32_t)LDB_MAX_INLINE_COB_ENTRIES);

	C2_ASSERT(ergo(op == C2_LXO_BUFFER_OP,
		       c2_bufvec_cursor_step(cur) >=
				ce_header->ces_nr * sizeof *cob_id));
	C2_ASSERT(ergo(op == C2_LXO_DB_LOOKUP,
		       c2_bufvec_cursor_step(cur) >=
				num_inline * sizeof *cob_id));

	for (i = 0; i < ce_header->ces_nr; ++i) {
		if (i < num_inline || op == C2_LXO_BUFFER_OP) {
			if (i == 0)
				C2_LOG("lid %llu, nr %lu, Start reading "
				       "inline entries from the buffer",
				       (unsigned long long)lid,
				       (unsigned long)ce_header->ces_nr);
			if (i == num_inline)
				C2_LOG("lid %llu, nr %lu, Start reading "
				       "noninline entries from the buffer",
				       (unsigned long long)lid,
				       (unsigned long)ce_header->ces_nr);
			cob_id = c2_bufvec_cursor_addr(cur);
			c2_bufvec_cursor_move(cur, sizeof *cob_id);
			C2_ASSERT(cob_id != NULL);
			cob_list[i] = *cob_id;
		} else
			/*
			 * When op == C2_LXO_DB_LOOKUP, noninline entries
			 * are to read from the DB.
			 */
			break;
	}

	if (op == C2_LXO_DB_LOOKUP && ce_header->ces_nr > num_inline) {
		C2_LOG("lid %llu, nr %lu, Start reading noninline entries "
		       "from the DB", (unsigned long long)lid,
		       (unsigned long)ce_header->ces_nr);
		rc = noninline_cob_list_read(&dom->ld_schema, tx, lid, i,
					     ce_header->ces_nr, cob_list);
		if (rc != 0) {
			C2_LOG("noninline_cob_list_read() failed");
			goto out;
		}
	}

	rc = c2_list_enum_build(dom, cob_list, ce_header->ces_nr, &list_enum);
	if (rc != 0) {
		c2_layout__log("list_decode", "c2_list_enum_build() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &c2_addb_func_fail, &layout_global_ctx,
			       lid, rc);
		goto out;
	}

	*out = &list_enum->lle_base;
	C2_POST(list_enum_invariant_internal(list_enum));
out:
	c2_free(cob_list);
	C2_LEAVE("lid %llu, rc %d", (unsigned long long)lid, rc);
	return rc;
}

static int noninline_cob_list_write(const struct c2_layout_schema *schema,
				    struct c2_db_tx *tx,
				    enum c2_layout_xcode_op op,
				    uint64_t lid,
				    uint32_t idx_start,
				    uint32_t idx_end,
				    struct c2_fid *cob_list)
{
	struct list_schema_data *lsd;
	struct cob_lists_key     key;
	struct cob_lists_rec     rec;
	struct c2_db_pair        pair;
	struct c2_db_cursor      cursor;
	uint32_t                 i;
	int                      rc = 0;

	C2_PRE(schema != NULL);
	C2_PRE(tx != NULL);
	C2_PRE(C2_IN(op, (C2_LXO_DB_ADD, C2_LXO_DB_DELETE)));
	C2_PRE(idx_end > idx_start);
	C2_PRE(cob_list != NULL);

	C2_ENTRY("lid %llu, idx_start %lu, idx_end %lu",
		 (unsigned long long)lid, (unsigned long)idx_start,
		 (unsigned long)idx_end);

	lsd = schema->ls_type_data[c2_list_enum_type.let_id];
	C2_ASSERT(lsd != NULL);

	rc = c2_db_cursor_init(&cursor, &lsd->lsd_cob_lists, tx,
			       C2_DB_CURSOR_RMW);
	if (rc != 0) {
		c2_layout__log("noninline_cob_list_write",
			       "c2_db_cursor_init() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &c2_addb_func_fail, &layout_global_ctx,
			       lid, rc);
		goto out;
	}

	key.clk_lid = lid;
	for (i = idx_start; i < idx_end; ++i) {
		C2_ASSERT(c2_fid_is_valid(&cob_list[i]));
		key.clk_cob_index = i;
		c2_db_pair_setup(&pair, &lsd->lsd_cob_lists,
				 &key, sizeof key, &rec, sizeof rec);

		if (op == C2_LXO_DB_ADD) {
			rec.clr_cob_id = cob_list[i];
			rc = c2_db_cursor_add(&cursor, &pair);
			if (rc != 0) {
				c2_layout__log("noninline_cob_list_write",
					       "c2_db_cursor_add() failed",
					       ADDB_RECORD_ADD,
					       TRACE_RECORD_ADD,
					       &c2_addb_func_fail,
					       &layout_global_ctx, //todo check
					       lid, rc);
				goto out;
			}
		} else if (op == C2_LXO_DB_DELETE) {
			if (i == idx_start)
				rc = c2_db_cursor_get(&cursor, &pair);
			else
				rc = c2_db_cursor_next(&cursor, &pair);
			if (rc != 0) {
				c2_layout__log("noninline_cob_list_write",
					       "c2_db_cursor_get() failed",
					       ADDB_RECORD_ADD,
					       TRACE_RECORD_ADD,
					       &c2_addb_func_fail,
					       &layout_global_ctx,
					       lid, rc);
				goto out;
			}
			C2_ASSERT(c2_fid_eq(&rec.clr_cob_id, &cob_list[i]));
			rc = c2_db_cursor_del(&cursor);
			if (rc != 0) {
				c2_layout__log("noninline_cob_list_write",
					       "c2_db_cursor_del() failed",
					       ADDB_RECORD_ADD,
					       TRACE_RECORD_ADD,
					       &c2_addb_func_fail,
					       &layout_global_ctx,
					       lid, rc);
				goto out;
			}
		}
	}

out:
	c2_db_pair_fini(&pair);
	c2_db_cursor_fini(&cursor);
	C2_LEAVE("rc %d", rc);
	return rc;
}

/**
 * Implementation of leto_encode() for list enumeration type.
 *
 * Continues to use the in-memory layout object and either 'stores it in the
 * Layout DB' or 'converts it to a buffer'.

 * @param op This enum parameter indicates what is the DB operation to be
 * performed on the layout record if at all and it could be one of
 * ADD/UPDATE/DELETE. If it is BUFFER_OP, then the layout is converted into a
 * buffer.
 */
static int list_encode(const struct c2_layout_enum *le,
		       enum c2_layout_xcode_op op,
		       struct c2_db_tx *tx,
		       struct c2_bufvec_cursor *oldrec_cur,
		       struct c2_bufvec_cursor *out)
{
	struct c2_layout_list_enum *list_enum;
	uint32_t                    num_inline; /* Number of inline cobs */
	struct cob_entries_header   ce_header;
	struct cob_entries_header  *ce_oldheader;
	struct c2_fid              *cob_id_old;
	c2_bcount_t                 nbytes;
	uint64_t                    lid;
	uint32_t                    i;
	int                         rc = 0;

	C2_PRE(le != NULL);
	C2_PRE(C2_IN(op, (C2_LXO_DB_ADD, C2_LXO_DB_UPDATE,
			  C2_LXO_DB_DELETE, C2_LXO_BUFFER_OP)));
	C2_PRE(ergo(op != C2_LXO_BUFFER_OP, tx != NULL));
	C2_PRE(ergo(op == C2_LXO_DB_UPDATE, oldrec_cur != NULL));
	C2_PRE(ergo(op == C2_LXO_DB_UPDATE,
		    c2_bufvec_cursor_step(oldrec_cur) >= sizeof *ce_oldheader));
	C2_PRE(out != NULL);
	C2_PRE(c2_bufvec_cursor_step(out) >= sizeof ce_header);

	list_enum = container_of(le, struct c2_layout_list_enum, lle_base);
	C2_ASSERT(list_enum_invariant(list_enum));

	lid = le->le_l->l_id;
	C2_ENTRY("lid %llu, nr %lu", (unsigned long long)lid,
		 (unsigned long)list_enum->lle_nr);

	num_inline = min_check(list_enum->lle_nr,
			       (uint32_t)LDB_MAX_INLINE_COB_ENTRIES);

	if (op == C2_LXO_DB_UPDATE) {
		/*
		 * Processing the oldrec_cur, to verify that no enumeration
		 * specific data is being changed for this layout.
		 */
		ce_oldheader = c2_bufvec_cursor_addr(oldrec_cur);
		C2_ASSERT(ce_oldheader->ces_nr == list_enum->lle_nr);

		c2_bufvec_cursor_move(oldrec_cur, sizeof *ce_oldheader);
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

	ce_header.ces_nr = list_enum->lle_nr;
	nbytes = c2_bufvec_cursor_copyto(out, &ce_header,
					 sizeof ce_header);
	C2_ASSERT(nbytes == sizeof ce_header);

	C2_ASSERT(ergo(op == C2_LXO_BUFFER_OP,
		       c2_bufvec_cursor_step(out) >=
				list_enum->lle_nr *
				sizeof list_enum->lle_list_of_cobs[i]));
	C2_ASSERT(ergo(op == C2_LXO_DB_LOOKUP,
		       c2_bufvec_cursor_step(out) >=
				num_inline *
				sizeof list_enum->lle_list_of_cobs[i]));

	for (i = 0; i < list_enum->lle_nr; ++i) {
		if (i < num_inline || op == C2_LXO_BUFFER_OP) {
			if (i == 0)
				C2_LOG("lid %llu, nr %lu, Start accepting "
				       "inline entries into the buffer",
				       (unsigned long long)lid,
				       (unsigned long)list_enum->lle_nr);
			if (i == num_inline)
				C2_LOG("lid %llu, nr %lu, Start accepting "
				       "noninline entries into the buffer",
				       (unsigned long long)lid,
				       (unsigned long)list_enum->lle_nr);
			nbytes = c2_bufvec_cursor_copyto(out,
					&list_enum->lle_list_of_cobs[i],
					sizeof list_enum->lle_list_of_cobs[i]);
			C2_ASSERT(nbytes ==
				  sizeof list_enum->lle_list_of_cobs[i]);
		} else if (op == C2_LXO_DB_UPDATE) {
			/*
			 * The auxiliary table viz. cob_lists is not to be
			 * modified for an update operation.
			 */
			rc = 0;
			break;
		} else
			/*
			 * When op == C2_LXO_DB_ADD or C2_LXO_DB_DELETE,
			 * noninline entries are to be written to the DB.
			 */
			break;
	}
	/*
	 * At the end of the above for loop, rc remains 0, in the following
	 * cases (with rc being initialised to 0 at the beginning of this
	 * function).
	 */
	C2_ASSERT(ergo(num_inline == list_enum->lle_nr ||
		       op == C2_LXO_BUFFER_OP ||
		       op == C2_LXO_DB_UPDATE, rc == 0));

	if ((op == C2_LXO_DB_ADD || op == C2_LXO_DB_DELETE) &&
	    list_enum->lle_nr > num_inline) {
		C2_LOG("lid %llu, nr %lu, Start writing noninline entries "
		       "to the DB", (unsigned long long)lid,
		       (unsigned long)list_enum->lle_nr);
		rc = noninline_cob_list_write(&le->le_l->l_dom->ld_schema,
					      tx, op, le->le_l->l_id,
					      i, list_enum->lle_nr,
					      list_enum->lle_list_of_cobs);
		C2_LOG("noninline_cob_list_write() failed");
	}

	C2_LEAVE("lid %llu, rc %d", (unsigned long long)lid, rc);
	return rc;
}

/**
 * Implementation of leo_nr for LIST enumeration.
 * Returns number of objects in the enumeration.
 * Argument fid is ignored here for LIST enumeration type.
 */
static uint32_t list_nr(const struct c2_layout_enum *le)
{
	struct c2_layout_list_enum *list_enum;

	C2_PRE(le != NULL);

	C2_ENTRY("lid %llu, enum_pointer %p",
		 (unsigned long long)le->le_l->l_id, le);
	list_enum = container_of(le, struct c2_layout_list_enum, lle_base);
	C2_ASSERT(list_enum_invariant(list_enum));
	C2_LEAVE("lid %llu, enum_pointer %p, nr %lu",
		 (unsigned long long)le->le_l->l_id, le,
		 (unsigned long)list_enum->lle_nr);

	return list_enum->lle_nr;
}

/**
 * Implementation of leo_get for LIST enumeration.
 * Returns idx-th object from the enumeration.
 * Argument fid is ignored here for LIST enumeration type.
 */
static void list_get(const struct c2_layout_enum *le, uint32_t idx,
		     const struct c2_fid *gfid, struct c2_fid *out)
{
	struct c2_layout_list_enum *list_enum;

	C2_PRE(le != NULL);
	/* gfid is ignored for the list enumeration type. */
	C2_PRE(out != NULL);

	C2_ENTRY("lid %llu, enum_pointer %p",
		 (unsigned long long)le->le_l->l_id, le);
	list_enum = container_of(le, struct c2_layout_list_enum, lle_base);
	C2_ASSERT(list_enum_invariant(list_enum));
	C2_ASSERT(idx < list_enum->lle_nr);
	C2_ASSERT(c2_fid_is_valid(&list_enum->lle_list_of_cobs[idx]));
	C2_LEAVE("lid %llu, enum_pointer %p, fid_pointer %p",
		 (unsigned long long)le->le_l->l_id, le, out);

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

struct c2_layout_enum_type c2_list_enum_type = {
	.let_name         = "list",
	.let_id           = 0,
	.let_ref_count    = 0,
	.let_domain       = NULL,
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
