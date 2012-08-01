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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

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
#include "lib/finject.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_LAYOUT
#include "lib/trace.h"

#include "fid/fid.h"  /* c2_fid_is_valid() */
#include "layout/layout_internal.h"
#include "layout/list_enum.h"

extern const struct c2_addb_loc layout_addb_loc;
extern struct c2_addb_ctx layout_global_ctx;

enum {
	LIST_ENUM_MAGIC = 0x4C495354454E554DULL /* LISTENUM */
};

static const struct c2_bob_type list_bob = {
	.bt_name         = "list_enum",
	.bt_magix_offset = offsetof(struct c2_layout_list_enum, lle_magic),
	.bt_magix        = LIST_ENUM_MAGIC,
	.bt_check        = NULL
};

C2_BOB_DEFINE(static, &list_bob, c2_layout_list_enum);

struct list_schema_data {
	/** Table to store COB lists for all the layouts with LIST enum type. */
	struct c2_table  lsd_cob_lists;
};

/**
 * cob_lists table.
 *
 * @note This structure needs to be maintained as 8 bytes aligned.
 */
struct cob_lists_key {
	/** Layout id, value obtained from c2_layout::l_id. */
	uint64_t  clk_lid;

	/** Index for the COB from the layout it is part of. */
	uint32_t  clk_cob_index;

	/** Padding to make the structure 8 bytes aligned. */
	uint32_t  clk_pad;
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

static bool list_allocated_invariant(const struct c2_layout_list_enum *le)
{
	return
		c2_layout_list_enum_bob_check(le) &&
		le->lle_nr == 0 &&
		le->lle_list_of_cobs == NULL;
}

static bool list_invariant(const struct c2_layout_list_enum *le)
{
	return
		c2_layout_list_enum_bob_check(le) &&
		le->lle_nr != 0 &&
		le->lle_list_of_cobs != NULL &&
		c2_forall(i, le->lle_nr,
			  c2_fid_is_valid(&le->lle_list_of_cobs[i])) &&
		c2_layout__enum_invariant(&le->lle_base);
}

static const struct c2_layout_enum_ops list_enum_ops;

/** Implementation of leto_allocate for LIST enumeration type. */
static int list_allocate(struct c2_layout_domain *dom,
			 struct c2_layout_enum **out)
{
	struct c2_layout_list_enum *list_enum;

	C2_PRE(c2_layout__domain_invariant(dom));
	C2_PRE(out != NULL);

	C2_ENTRY();

	if (C2_FI_ENABLED("mem_err")) { list_enum = NULL; goto err1_injected; }
	C2_ALLOC_PTR(list_enum);
err1_injected:
	if (list_enum == NULL) {
		c2_layout__log("list_allocate", "C2_ALLOC_PTR() failed",
			       &c2_addb_oom, &layout_global_ctx, LID_NONE,
			       -ENOMEM);
		return -ENOMEM;
	}
	c2_layout__enum_init(dom, &list_enum->lle_base,
			     &c2_list_enum_type, &list_enum_ops);
	c2_layout_list_enum_bob_init(list_enum);
	C2_POST(list_allocated_invariant(list_enum));
	*out = &list_enum->lle_base;
	C2_LEAVE("list enum pointer %p", list_enum);
	return 0;
}

/** Implementation of leo_delete for LIST enumeration type. */
static void list_delete(struct c2_layout_enum *e)
{
	struct c2_layout_list_enum *list_enum;

	list_enum = bob_of(e, struct c2_layout_list_enum,
			   lle_base, &list_bob);
	C2_PRE(list_allocated_invariant(list_enum));

	C2_ENTRY("enum_pointer %p", e);
	c2_layout_list_enum_bob_fini(list_enum);
	c2_layout__enum_fini(&list_enum->lle_base);
	c2_free(list_enum);
	C2_LEAVE();
}

/* Populates the allocated list enum object using the supplied arguemnts. */
static int list_populate(struct c2_layout_list_enum *list_enum,
			 struct c2_fid *cob_list, uint32_t nr)
{
	C2_PRE(list_allocated_invariant(list_enum));
	C2_PRE(cob_list != NULL);

	if (nr == 0) {
		C2_LOG("list_enum %p, Invalid attributes (nr = 0), rc %d",
		       list_enum, -EINVAL);
		return -EINVAL; //todo EPROTO
	}
	list_enum->lle_nr = nr;
	list_enum->lle_list_of_cobs = cob_list;
	C2_POST(list_invariant(list_enum));
	return 0;
}

int c2_list_enum_build(struct c2_layout_domain *dom,
		       struct c2_fid *cob_list, uint32_t nr,
		       struct c2_layout_list_enum **out)
{
	struct c2_layout_enum      *e;
	struct c2_layout_list_enum *list_enum;
	uint32_t                    i;
	int                         rc;

	C2_PRE(out != NULL);

	C2_ENTRY("domain %p", dom);
	if (C2_FI_ENABLED("fid_invalid_err")) { goto err1_injected; }
	for (i = 0; i < nr; ++i) {
		if (!c2_fid_is_valid(&cob_list[i])) {
err1_injected:
			c2_layout__log("c2_list_enum_build", "fid invalid",
				       &c2_addb_func_fail, &layout_global_ctx,
				       LID_NONE, -EPROTO);
			return -EPROTO;
		}
	}

	rc = list_allocate(dom, &e);
	if (rc == 0) {
		list_enum = bob_of(e, struct c2_layout_list_enum,
				   lle_base, &list_bob);
		rc = list_populate(list_enum, cob_list, nr);
		if (rc == 0)
			*out = list_enum;
		else
			list_delete(e);
	}
	C2_POST(ergo(rc == 0, list_invariant(*out)));
	C2_LEAVE("domain %p, rc %d", dom, rc);
	return rc;
}

static struct c2_layout_list_enum
*enum_to_list_enum(const struct c2_layout_enum *e)
{
	struct c2_layout_list_enum *list_enum;

	list_enum = bob_of(e, struct c2_layout_list_enum,
			   lle_base, &list_bob);
	C2_ASSERT(list_invariant(list_enum));
	return list_enum;
}

/** Implementation of leo_fini for LIST enumeration type. */
static void list_fini(struct c2_layout_enum *e)
{
	struct c2_layout_list_enum *list_enum;
	uint64_t                    lid;

	C2_PRE(c2_layout__enum_invariant(e));

	lid = e->le_sl_is_set ? e->le_sl->sl_base.l_id : 0;
	C2_ENTRY("lid %llu, enum_pointer %p", (unsigned long long)lid, e);
	list_enum = enum_to_list_enum(e);
	c2_layout_list_enum_bob_fini(list_enum);
	c2_layout__enum_fini(&list_enum->lle_base);
	c2_free(list_enum->lle_list_of_cobs);
	c2_free(list_enum);
	C2_LEAVE();
}

/** Implementation of leto_register for LIST enumeration type. */
static int list_register(struct c2_layout_domain *dom,
			 const struct c2_layout_enum_type *et)
{
	struct list_schema_data *lsd;
	int                      rc;

	C2_PRE(c2_layout__domain_invariant(dom));
	C2_PRE(et != NULL);
	C2_PRE(IS_IN_ARRAY(et->let_id, dom->ld_enum));
	C2_PRE(dom->ld_type_data[et->let_id] == NULL);

	C2_ENTRY("Enum_type_id %lu", (unsigned long)et->let_id);
	C2_ALLOC_PTR(lsd);
	if (lsd == NULL) {
		c2_layout__log("list_register", "C2_ALLOC_PTR() failed",
			       &c2_addb_oom, &layout_global_ctx, LID_NONE,
			       -ENOMEM);
		return -ENOMEM;
	}
	rc = c2_table_init(&lsd->lsd_cob_lists, dom->ld_dbenv,
			   "cob_lists", DEFAULT_DB_FLAG, &cob_lists_table_ops);
	if (rc == 0)
		dom->ld_type_data[et->let_id] = lsd;
	else {
		c2_layout__log("list_register", "c2_table_init() failed",
			       &c2_addb_func_fail, &layout_global_ctx,
			       LID_NONE, rc);
		c2_free(lsd);
	}
	C2_LEAVE("Enum_type_id %lu, rc %d", (unsigned long)et->let_id, rc);
	return rc;
}

/** Implementation of leto_unregister for LIST enumeration type. */
static void list_unregister(struct c2_layout_domain *dom,
			    const struct c2_layout_enum_type *et)
{
	struct list_schema_data *lsd;

	C2_PRE(c2_layout__domain_invariant(dom));
	C2_PRE(et != NULL);

	C2_ENTRY("Enum_type_id %lu", (unsigned long)et->let_id);
	lsd = dom->ld_type_data[et->let_id];
	c2_table_fini(&lsd->lsd_cob_lists);
	dom->ld_type_data[et->let_id] = NULL;
	c2_free(lsd);
	C2_LEAVE("Enum_type_id %lu", (unsigned long)et->let_id);
}

/** Implementation of leto_max_recsize() for LIST enumeration type. */
static c2_bcount_t list_max_recsize(void)
{
	return sizeof(struct cob_entries_header) +
		LDB_MAX_INLINE_COB_ENTRIES * sizeof(struct c2_fid);
}

static int noninline_read(struct c2_fid *cob_list,
			  struct c2_striped_layout *stl,
			  struct c2_db_tx *tx,
			  uint32_t idx_start,
			  uint32_t idx_end)
{
	struct list_schema_data *lsd;
	struct cob_lists_key     key;
	struct cob_lists_rec     rec;
	struct c2_db_pair        pair;
	struct c2_db_cursor      cursor;
	uint32_t                 i;
	int                      rc;

	C2_ENTRY("lid %llu, idx_start %lu, idx_end %lu",
		 (unsigned long long)stl->sl_base.l_id,
		 (unsigned long)idx_start,
		 (unsigned long)idx_end);
	lsd = stl->sl_base.l_dom->ld_type_data[c2_list_enum_type.let_id];
	C2_ASSERT(lsd != NULL);

	rc = c2_db_cursor_init(&cursor, &lsd->lsd_cob_lists, tx, 0);
	if (rc != 0) {
		c2_layout__log("noninline_read",
			       "c2_db_cursor_init() failed",
			       &c2_addb_func_fail, &stl->sl_base.l_addb,
			       stl->sl_base.l_id, rc);
		return rc;
	}

	key.clk_lid       = stl->sl_base.l_id;
	key.clk_cob_index = idx_start;
	c2_db_pair_setup(&pair, &lsd->lsd_cob_lists,
			 &key, sizeof key, &rec, sizeof rec);
	for (i = idx_start; i < idx_end; ++i) {
		if (i == idx_start)
			rc = c2_db_cursor_get(&cursor, &pair);
		else
			rc = c2_db_cursor_next(&cursor, &pair);
		if (rc != 0) {
			c2_layout__log("noninline_read",
				       "c2_db_cursor_get() failed",
				       &c2_addb_func_fail,
				       &stl->sl_base.l_addb,
				       key.clk_lid, rc);
			goto out;
		}
		if (!c2_fid_is_valid(&rec.clr_cob_id)) {
			rc = -EPROTO;
			c2_layout__log("noninline_read",
				       "fid invalid",
				       &c2_addb_func_fail,
				       &stl->sl_base.l_addb,
				       key.clk_lid, rc);
			goto out;
		}
		cob_list[i] = rec.clr_cob_id;
	}
out:
	c2_db_pair_fini(&pair);
	c2_db_cursor_fini(&cursor);
	C2_LEAVE("lid %llu, rc %d", (unsigned long long)stl->sl_base.l_id, rc);
	return rc;
}

/** Implementation of leo_decode() for LIST enumeration type. */
static int list_decode(struct c2_layout_enum *e,
		       struct c2_bufvec_cursor *cur,
		       enum c2_layout_xcode_op op,
		       struct c2_db_tx *tx,
		       struct c2_striped_layout *stl)
{
	uint64_t                    lid;
	struct c2_layout_list_enum *list_enum;
	struct cob_entries_header  *ce_header;
	/* Number of cobs to be read from the buffer. */
	uint32_t                    num_inline;
	struct c2_fid              *cob_id;
	struct c2_fid              *cob_list;
	uint32_t                    i;
	int                         rc;

	C2_PRE(e != NULL);
	C2_PRE(cur != NULL);
	C2_PRE(c2_bufvec_cursor_step(cur) >= sizeof *ce_header);
	C2_PRE(C2_IN(op, (C2_LXO_DB_LOOKUP, C2_LXO_BUFFER_OP)));
	C2_PRE(ergo(op == C2_LXO_DB_LOOKUP, tx != NULL));
	C2_PRE(c2_layout__striped_allocated_invariant(stl));

	lid = stl->sl_base.l_id;
	ce_header = c2_bufvec_cursor_addr(cur);
	c2_bufvec_cursor_move(cur, sizeof *ce_header);
	C2_ENTRY("lid %llu, nr %lu", (unsigned long long)lid,
		 (unsigned long)ce_header->ces_nr);
	list_enum = bob_of(e, struct c2_layout_list_enum,
			   lle_base, &list_bob);
	C2_ASSERT(list_allocated_invariant(list_enum));

	C2_ALLOC_ARR(cob_list, ce_header->ces_nr);
	if (cob_list == NULL) {
		rc = -ENOMEM;
		c2_layout__log("list_decode", "C2_ALLOC_ARR() failed",
			       &c2_addb_oom, &stl->sl_base.l_addb, lid, rc);
		goto out;
	}
	rc = 0;
	num_inline = op == C2_LXO_BUFFER_OP ? ce_header->ces_nr :
		min_check(ce_header->ces_nr,
			  (uint32_t)LDB_MAX_INLINE_COB_ENTRIES);
	C2_ASSERT(c2_bufvec_cursor_step(cur) >= num_inline * sizeof *cob_id);

	C2_LOG("lid %llu, nr %lu, Start reading inline entries",
	       (unsigned long long)lid, (unsigned long)ce_header->ces_nr);
	for (i = 0; i < num_inline; ++i) {
		cob_id = c2_bufvec_cursor_addr(cur);
		c2_bufvec_cursor_move(cur, sizeof *cob_id);
		if (!c2_fid_is_valid(cob_id)) {
			rc = -EPROTO;
			C2_LOG("fid invalid, i %lu", (unsigned long)i);
			goto out;
		}
		cob_list[i] = *cob_id;
	}

	if (ce_header->ces_nr > num_inline) {
		C2_ASSERT(op == C2_LXO_DB_LOOKUP);
		C2_LOG("lid %llu, nr %lu, Start reading noninline entries",
		       (unsigned long long)lid,
		       (unsigned long)ce_header->ces_nr);
		rc = noninline_read(cob_list, stl, tx, i, ce_header->ces_nr);
		if (rc != 0) {
			C2_LOG("noninline_read() failed");
			goto out;
		}
	}

	if (C2_FI_ENABLED("list_attr_err")) { ce_header->ces_nr = 0; }
	rc = list_populate(list_enum, cob_list, ce_header->ces_nr);
	if (rc != 0)
		C2_LOG("list_populate() failed");
out:
	if (rc != 0)
		c2_free(cob_list);
	C2_POST(ergo(rc == 0, list_invariant(list_enum)));
	C2_POST(ergo(rc != 0, list_allocated_invariant(list_enum)));
	C2_LEAVE("lid %llu, rc %d", (unsigned long long)lid, rc);
	return rc;
}

static int noninline_write(const struct c2_layout_enum *e,
			   struct c2_db_tx *tx,
			   enum c2_layout_xcode_op op,
			   uint32_t idx_start)
{
	struct c2_layout_list_enum *list_enum;
	struct c2_fid              *cob_list;
	struct list_schema_data    *lsd;
	struct c2_db_cursor         cursor;
	struct cob_lists_key        key;
	struct cob_lists_rec        rec;
	struct c2_db_pair           pair;
	uint32_t                    i;
	int                         rc;

	C2_PRE(C2_IN(op, (C2_LXO_DB_ADD, C2_LXO_DB_DELETE)));

	list_enum = enum_to_list_enum(e);
	C2_ENTRY("lid %llu, idx_start %lu, idx_end %lu",
		 (unsigned long long)e->le_sl->sl_base.l_id,
		 (unsigned long)idx_start,
		 (unsigned long)list_enum->lle_nr);
	cob_list = list_enum->lle_list_of_cobs;
	lsd = e->le_sl->sl_base.l_dom->ld_type_data[c2_list_enum_type.let_id];
	C2_ASSERT(lsd != NULL);

	rc = c2_db_cursor_init(&cursor, &lsd->lsd_cob_lists, tx,
			       C2_DB_CURSOR_RMW);
	if (rc != 0) {
		c2_layout__log("noninline_write",
			       "c2_db_cursor_init() failed",
			       &c2_addb_func_fail, &e->le_sl->sl_base.l_addb,
			       (unsigned long long)e->le_sl->sl_base.l_id, rc);
		return rc;
	}

	key.clk_lid = e->le_sl->sl_base.l_id;
	for (i = idx_start; i < list_enum->lle_nr; ++i) {
		C2_ASSERT(c2_fid_is_valid(&cob_list[i]));
		key.clk_cob_index = i;
		c2_db_pair_setup(&pair, &lsd->lsd_cob_lists,
				 &key, sizeof key, &rec, sizeof rec);

		if (op == C2_LXO_DB_ADD) {
			rec.clr_cob_id = cob_list[i];
			rc = c2_db_cursor_add(&cursor, &pair);
			if (rc != 0) {
				c2_layout__log("noninline_write",
					       "c2_db_cursor_add() failed",
					       &c2_addb_func_fail,
					       &e->le_sl->sl_base.l_addb,
					       key.clk_lid, rc);
				goto out;
			}
		} else if (op == C2_LXO_DB_DELETE) {
			if (i == idx_start)
				rc = c2_db_cursor_get(&cursor, &pair);
			else
				rc = c2_db_cursor_next(&cursor, &pair);
			if (rc != 0) {
				c2_layout__log("noninline_write",
					       "c2_db_cursor_get() failed",
					       &c2_addb_func_fail,
					       &e->le_sl->sl_base.l_addb,
					       key.clk_lid, rc);
				goto out;
			}
			C2_ASSERT(c2_fid_eq(&rec.clr_cob_id, &cob_list[i]));
			rc = c2_db_cursor_del(&cursor);
			if (rc != 0) {
				c2_layout__log("noninline_write",
					       "c2_db_cursor_del() failed",
					       &c2_addb_func_fail,
					       &e->le_sl->sl_base.l_addb,
					       key.clk_lid, rc);
				goto out;
			}
		}
	}
out:
	c2_db_pair_fini(&pair);
	c2_db_cursor_fini(&cursor);
	C2_LEAVE("lid %llu, rc %d",
		 (unsigned long long)e->le_sl->sl_base.l_id, rc);
	return rc;
}

/** Implementation of leo_encode() for LIST enumeration type. */
static int list_encode(const struct c2_layout_enum *e,
		       enum c2_layout_xcode_op op,
		       struct c2_db_tx *tx,
		       struct c2_bufvec_cursor *out)
{
	struct c2_layout_list_enum *list_enum;
	/* Number of cobs to be written to the buffer. */
	uint32_t                    num_inline;
	struct cob_entries_header   ce_header;
	c2_bcount_t                 nbytes;
	uint64_t                    lid;
	uint32_t                    i;
	int                         rc;

	C2_PRE(e != NULL);
	C2_PRE(C2_IN(op, (C2_LXO_DB_ADD, C2_LXO_DB_UPDATE,
			  C2_LXO_DB_DELETE, C2_LXO_BUFFER_OP)));
	C2_PRE(ergo(op != C2_LXO_BUFFER_OP, tx != NULL));
	C2_PRE(out != NULL);
	C2_PRE(c2_bufvec_cursor_step(out) >= sizeof ce_header);

	list_enum = enum_to_list_enum(e);
	lid = e->le_sl->sl_base.l_id;
	C2_ENTRY("lid %llu, nr %lu", (unsigned long long)lid,
		 (unsigned long)list_enum->lle_nr);

	ce_header.ces_nr = list_enum->lle_nr;
	nbytes = c2_bufvec_cursor_copyto(out, &ce_header, sizeof ce_header);
	C2_ASSERT(nbytes == sizeof ce_header);

	num_inline = op == C2_LXO_BUFFER_OP ? list_enum->lle_nr :
		min_check(list_enum->lle_nr,
			  (uint32_t)LDB_MAX_INLINE_COB_ENTRIES);

	C2_ASSERT(c2_bufvec_cursor_step(out) >= num_inline *
					sizeof list_enum->lle_list_of_cobs[0]);

	C2_LOG("lid %llu, nr %lu, Start accepting inline entries",
	       (unsigned long long)lid, (unsigned long)list_enum->lle_nr);
	for (i = 0; i < num_inline; ++i) {
		nbytes = c2_bufvec_cursor_copyto(out,
					&list_enum->lle_list_of_cobs[i],
					sizeof list_enum->lle_list_of_cobs[i]);
		C2_ASSERT(nbytes == sizeof list_enum->lle_list_of_cobs[i]);
	}

	rc = 0;
	/*
	 * The auxiliary table viz. cob_lists is not to be modified for an
	 * update operation.
	 */
	if (list_enum->lle_nr > num_inline && op != C2_LXO_DB_UPDATE) {
		C2_ASSERT(op == C2_LXO_DB_ADD || op == C2_LXO_DB_DELETE);
		C2_LOG("lid %llu, nr %lu, Start writing noninline entries",
		       (unsigned long long)lid,
		       (unsigned long)list_enum->lle_nr);
		rc = noninline_write(e, tx, op, i);
		if (rc != 0)
			C2_LOG("noninline_write() failed");
	}

	C2_LEAVE("lid %llu, rc %d", (unsigned long long)lid, rc);
	return rc;
}

/** Implementation of leo_nr for LIST enumeration. */
static uint32_t list_nr(const struct c2_layout_enum *e)
{
	struct c2_layout_list_enum *list_enum;

	C2_PRE(e != NULL);

	C2_ENTRY("lid %llu, enum_pointer %p",
		 (unsigned long long)e->le_sl->sl_base.l_id, e);
	list_enum = enum_to_list_enum(e);
	C2_LEAVE("lid %llu, enum_pointer %p, nr %lu",
		 (unsigned long long)e->le_sl->sl_base.l_id, e,
		 (unsigned long)list_enum->lle_nr);
	return list_enum->lle_nr;
}

/** Implementation of leo_get for LIST enumeration. */
static void list_get(const struct c2_layout_enum *e, uint32_t idx,
		     const struct c2_fid *gfid, struct c2_fid *out)
{
	struct c2_layout_list_enum *list_enum;

	C2_PRE(e != NULL);
	/* gfid is ignored for the list enumeration type. */
	C2_PRE(out != NULL);

	C2_ENTRY("lid %llu, enum_pointer %p",
		 (unsigned long long)e->le_sl->sl_base.l_id, e);
	list_enum = enum_to_list_enum(e);
	C2_PRE(idx < list_enum->lle_nr);
	C2_ASSERT(c2_fid_is_valid(&list_enum->lle_list_of_cobs[idx]));
	*out = list_enum->lle_list_of_cobs[idx];
	C2_LEAVE("lid %llu, enum_pointer %p, fid_pointer %p",
		 (unsigned long long)e->le_sl->sl_base.l_id, e, out);
}

/** Implementation of leo_recsize() for list enumeration type. */
static c2_bcount_t list_recsize(struct c2_layout_enum *e)
{
	struct c2_layout_list_enum *list_enum;

	C2_PRE(e != NULL);

	list_enum = enum_to_list_enum(e);
	return sizeof(struct cob_entries_header) +
		min_check((uint32_t)LDB_MAX_INLINE_COB_ENTRIES,
			  list_enum->lle_nr) *
		sizeof(struct c2_fid);
}

static const struct c2_layout_enum_ops list_enum_ops = {
	.leo_nr      = list_nr,
	.leo_get     = list_get,
	.leo_recsize = list_recsize,
	.leo_fini    = list_fini,
	.leo_delete  = list_delete,
	.leo_decode  = list_decode,
	.leo_encode  = list_encode
};

static const struct c2_layout_enum_type_ops list_type_ops = {
	.leto_register    = list_register,
	.leto_unregister  = list_unregister,
	.leto_max_recsize = list_max_recsize,
	.leto_allocate    = list_allocate
};

struct c2_layout_enum_type c2_list_enum_type = {
	.let_name      = "list",
	.let_id        = 0,
	.let_ref_count = 0,
	.let_domain    = NULL,
	.let_ops       = &list_type_ops
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
