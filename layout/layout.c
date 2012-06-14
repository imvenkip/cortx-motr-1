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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 *                  Trupti Patil <trupti_patil@xyratex.com>
 * Original creation date: 07/09/2010
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

/**
 * @addtogroup layout
 * @{
 *
 * @section layout-thread Layout Threading and Concurrency Model
 * - Arrays from the struct c2_layout_domain, storing registered layout types
 *   and registered enum types viz. ld_type[] and ld_enum[] are protected by
 *   using c2_layout_domain::ld_lock.
 * - Also, the list of layout objects stored in the struct c2_layout_domain
 *   viz. ld_layout_list is protected by using c2_layout_domain::ld_lock.
 * - Reference count is maintained for each of the layout types and enum types.
 *   This is to help verify that no layout type or enum type gets unregistered
 *   while any of the layout object or enum object is using it.
 * - Reference count is maintained for each of the layout object indicating
 *   how many users are associated with a particular layout object. This helps
 *   to confirm that no layout object gets deleted while any user is associated
 *   with it. It also helps to confirm that the layout object gets deleted
 *   while its last reference gets released.
 * - Various tables those are part of layout DB, directly or indirectly
 *   pointed by struct c2_layout_schema, are protected by using
 *   c2_layout_schema::ls_lock.
 * - The in-memory c2_layout object is protected by using c2_layout::l_lock.
 *
 * - c2_layout_domain::ld_lock is held during the following operations:
 *   - Registration and unregistration routines for various layout types and
 *     enum types.
 *   - While adding/deleting an entry to/from the layout list that happens
 *     through c2_layout__init() and c2_layout_put() respectively.
 * - c2_layout_schema::ls_lock is held during the following operations:
 *   - Part of the layout type and enum type registration and unregistration
 *     routines those deal with creating and deleting various DB tables.
 *   - c2_layout_lookup(), c2_layout_add(), c2_layout_update(),
 *     c2_layout_delete().
 *
 * - Note: Having two separate locks for domain data and schema data helps
 *   avoid serialising all the c2_layout_decode() and c2_layout_encode()
 *   operations. The only part of those APIs that is serialised through holding
 *   c2_layout_domain::ld_lock is during c2_layout__init() and
 *   c2_layout__enum_init() routines.
 *
 */

#include "lib/errno.h"
#include "lib/memory.h" /* C2_ALLOC_PTR() */
#include "lib/misc.h"   /* strlen(), C2_IN() */
#include "lib/vec.h"    /* c2_bufvec_cursor_step(), c2_bufvec_cursor_addr() */
#include "lib/bob.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_LAYOUT
#include "lib/trace.h"

#include "pool/pool.h" /* c2_pool_id_is_valid() */
#include "layout/layout_internal.h"
#include "layout/layout_db.h"
#include "layout/layout.h"

extern struct c2_layout_type c2_pdclust_layout_type;
extern struct c2_layout_enum_type c2_list_enum_type;
extern struct c2_layout_enum_type c2_linear_enum_type;

enum {
	LAYOUT_MAGIC = 0x4C41594F55544D41, /* LAYOUTMA */
	LIST_MAGIC   = 0x4C4953544D414749, /* LISTMAGI */
	HEAD_MAGIC   = 0x484541444D414749  /* HEADMAGI */
};

static const struct c2_bob_type layout_bob = {
	.bt_name         = "layout",
	.bt_magix_offset = offsetof(struct c2_layout, l_magic),
	.bt_magix        = LAYOUT_MAGIC,
	.bt_check        = NULL
};

C2_BOB_DEFINE(static, &layout_bob, c2_layout);

/** ADDB instrumentation for layout. */
static const struct c2_addb_ctx_type layout_addb_ctx_type = {
	.act_name = "layout"
};

const struct c2_addb_loc layout_addb_loc = {
	.al_name = "layout"
};

struct c2_addb_ctx layout_global_ctx = {
	.ac_type   = &layout_addb_ctx_type,
	/* todo What should this parent be set to? Something coming from FOP? */
	.ac_parent = NULL
};

C2_ADDB_EV_DEFINE(layout_decode_fail, "layout_decode_fail",
		  C2_ADDB_EVENT_LAYOUT_DECODE_FAIL, C2_ADDB_FUNC_CALL);
C2_ADDB_EV_DEFINE(layout_encode_fail, "layout_encode_fail",
		  C2_ADDB_EVENT_LAYOUT_ENCODE_FAIL, C2_ADDB_FUNC_CALL);

C2_ADDB_EV_DEFINE(layout_lookup_fail, "layout_lookup_fail",
		  C2_ADDB_EVENT_LAYOUT_LOOKUP_FAIL, C2_ADDB_FUNC_CALL);
C2_ADDB_EV_DEFINE(layout_add_fail, "layout_add_fail",
		  C2_ADDB_EVENT_LAYOUT_ADD_FAIL, C2_ADDB_FUNC_CALL);
C2_ADDB_EV_DEFINE(layout_update_fail, "layout_update_fail",
		  C2_ADDB_EVENT_LAYOUT_UPDATE_FAIL, C2_ADDB_FUNC_CALL);
C2_ADDB_EV_DEFINE(layout_delete_fail, "layout_delete_fail",
		  C2_ADDB_EVENT_LAYOUT_DELETE_FAIL, C2_ADDB_FUNC_CALL);

/** Layout list entry. */
struct llist_entry {
	/** Layout id. */
	uint64_t          lle_lid;

	/** Pointer to the layout object. */
	struct c2_layout *lle_l;

	struct c2_tlink   lle_linkage;
	uint64_t          lle_magic;
};

C2_TL_DESCR_DEFINE(layout_list, "layout-list", static,
		   struct llist_entry, lle_linkage,
		   lle_magic, LIST_MAGIC, HEAD_MAGIC);
C2_TL_DEFINE(layout_list, static, struct llist_entry);

static struct c2_bob_type llist_entry_bob;
C2_BOB_DEFINE(static, &llist_entry_bob, llist_entry);

bool domain_invariant(const struct c2_layout_domain *dom)
{
	return
		dom != NULL &&
		dom->ld_schema.ls_dbenv != NULL;
}

bool layout_invariant(const struct c2_layout *l)
{
	return
		l != NULL &&
		c2_layout_bob_check(l) &&
		l->l_id != LID_NONE &&
		l->l_type != NULL &&
		domain_invariant(l->l_dom) &&
		l->l_ref >= DEFAULT_REF_COUNT &&
		c2_pool_id_is_valid(l->l_pool_id) &&
		l->l_ops != NULL;
}

bool enum_invariant(const struct c2_layout_enum *le)
{
	return
		le != NULL &&
		le->le_l != NULL &&
		le->le_ops != NULL;
}

bool striped_layout_invariant(const struct c2_layout_striped *stl)
{
	return
		stl != NULL &&
		enum_invariant(stl->ls_enum) &&
		layout_invariant(&stl->ls_base);
}

static int rec_invariant(const struct c2_layout_rec *rec,
			 const struct c2_layout_domain *dom)
{
	return
		rec != NULL &&
		c2_layout__is_layout_type_valid(rec->lr_lt_id, dom) &&
		rec->lr_ref_count >= DEFAULT_REF_COUNT &&
		c2_pool_id_is_valid(rec->lr_pool_id);
}

bool c2_layout__is_layout_type_valid(uint32_t lt_id,
				     const struct c2_layout_domain *dom)
{
	C2_PRE(dom != 0);

	C2_PRE(domain_invariant(dom));

	if (!IS_IN_ARRAY(lt_id, dom->ld_type)) {
		C2_LOG("Invalid layout-type-id %lu", (unsigned long)lt_id);
		return false;
	}

	if (dom->ld_type[lt_id] == NULL) {
		C2_LOG("Unknown layout type, layout-type-id %lu",
		       (unsigned long)lt_id);
		return false;
	}

	C2_ASSERT(dom->ld_type[lt_id]->lt_ref_count >= DEFAULT_REF_COUNT);
	return true;
}

bool c2_layout__is_enum_type_valid(uint32_t let_id,
				   const struct c2_layout_domain *dom)
{
	C2_PRE(domain_invariant(dom));

	if (!IS_IN_ARRAY(let_id, dom->ld_enum)) {
		C2_LOG("Invalid enum-type-id %lu", (unsigned long)let_id);
		return false;
	}

	if (dom->ld_enum[let_id] == NULL) {
		C2_LOG("Unknown enum type, enum-type-id %lu",
		       (unsigned long)let_id);
		return false;
	}

	C2_ASSERT(dom->ld_enum[let_id]->let_ref_count >= DEFAULT_REF_COUNT);
	return true;
}

/** Adds a reference to the layout type. */
static void layout_type_get(struct c2_layout_domain *dom,
			    struct c2_layout_type *lt)
{
	C2_PRE(domain_invariant(dom));
	C2_PRE(lt != NULL);

	/*
	 * The DEFAULT_REF_COUNT while a layout type is registered, being 1,
	 * ensures that the layout can not be freed concurrently.
	 */

	c2_mutex_lock(&dom->ld_lock);
	C2_CNT_INC(lt->lt_ref_count);
	c2_mutex_unlock(&dom->ld_lock);
}

/** Releases a reference on the layout type. */
static void layout_type_put(struct c2_layout_domain *dom,
			    struct c2_layout_type *lt)
{
	C2_PRE(domain_invariant(dom));
	C2_PRE(lt != NULL);

	c2_mutex_lock(&dom->ld_lock);
	C2_CNT_DEC(lt->lt_ref_count);
	c2_mutex_unlock(&dom->ld_lock);
}

/** Adds a reference on the enum type. */
static void enum_type_get(struct c2_layout_domain *dom,
			  struct c2_layout_enum_type *let)
{
	C2_PRE(domain_invariant(dom));
	C2_PRE(let != NULL);

	/*
	 * The DEFAULT_REF_COUNT while an enum type is registered, being 1,
	 * ensures that the layout can not be freed concurrently.
	 */

	c2_mutex_lock(&dom->ld_lock);
	C2_CNT_INC(let->let_ref_count);
	c2_mutex_unlock(&dom->ld_lock);
}

/** Releases a reference on the enum type. */
static void enum_type_put(struct c2_layout_domain *dom,
			  struct c2_layout_enum_type *let)
{
	C2_PRE(domain_invariant(dom));
	C2_PRE(let != NULL);

	c2_mutex_lock(&dom->ld_lock);
	C2_CNT_DEC(let->let_ref_count);
	c2_mutex_unlock(&dom->ld_lock);
}

/**
 * Looks up for an entry from the layout list, with the specified layout id.
 */
static struct llist_entry *layout_list_lookup(struct c2_layout_domain *dom,
					      uint64_t lid)
{
	struct llist_entry *l_entry;

	C2_PRE(domain_invariant(dom));
	C2_PRE(lid != LID_NONE);

	c2_tl_for(layout_list, &dom->ld_layout_list, l_entry) {
		C2_ASSERT(llist_entry_bob_check(l_entry));
		if (l_entry->lle_lid == lid)
			break;
	} c2_tl_endfor;

	return l_entry;
}

/**
 * Adds an entry in the layout list, with the specified layout pointer and id.
 */
static int layout_list_add(struct c2_layout_domain *dom, struct c2_layout *l)
{
	struct llist_entry *l_entry;

	C2_ENTRY("dom %p, lid %llu", dom, (unsigned long long)l->l_id);
	C2_ALLOC_PTR(l_entry);
	if (l_entry == NULL) {
		c2_layout__log("layout_list_add", "C2_ALLOC_PTR() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &c2_addb_oom, &layout_global_ctx,
			       l->l_id, -ENOMEM);
		return -ENOMEM;
	}

	c2_mutex_lock(&dom->ld_lock);
	C2_ASSERT(layout_list_lookup(dom, l->l_id) == NULL);
	l_entry->lle_lid = l->l_id;
	l_entry->lle_l = l;
	layout_list_tlink_init(l_entry);
	llist_entry_bob_init(l_entry);
	layout_list_tlist_add(&dom->ld_layout_list, l_entry);
	c2_mutex_unlock(&dom->ld_lock);

	C2_LEAVE("lid %llu", (unsigned long long)l->l_id);
	return 0;
}

/** Initialises a layout, adds a reference on the respective layout type. */
int c2_layout__init(struct c2_layout_domain *dom,
		    struct c2_layout *l,
		    uint64_t lid, uint64_t pool_id,
		    struct c2_layout_type *type,
		    const struct c2_layout_ops *ops)
{
	int rc;

	C2_PRE(domain_invariant(dom));
	C2_PRE(l != NULL);
	C2_PRE(lid != LID_NONE);
	C2_PRE(c2_pool_id_is_valid(pool_id));
	C2_PRE(type != NULL);
	C2_PRE(c2_layout__is_layout_type_valid(type->lt_id, dom));
	C2_PRE(ops != NULL);

	C2_ENTRY("lid %llu, layout-type-id %lu", (unsigned long long)lid,
		 (unsigned long)type->lt_id);

	l->l_id      = lid;
	l->l_dom     = dom;
	l->l_ref     = DEFAULT_REF_COUNT;
	l->l_pool_id = pool_id;
	l->l_ops     = ops;
	l->l_type    = type;

	rc = layout_list_add(dom, l);
	if (rc == 0) {
		layout_type_get(dom, type);
		c2_mutex_init(&l->l_lock);
		c2_addb_ctx_init(&l->l_addb, &layout_addb_ctx_type,
				 &layout_global_ctx);
		c2_layout_bob_init(l);
		C2_POST(layout_invariant(l));
	}

	C2_LEAVE("lid %llu", (unsigned long long)lid);
	return rc;
}

/** Finalises a layout, releases a reference on the respective layout type. */
void c2_layout__fini(struct c2_layout *l)
{
	C2_PRE(layout_invariant(l));
	C2_PRE(layout_list_lookup(l->l_dom, l->l_id) == NULL);

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);

	c2_layout_bob_fini(l);
	c2_addb_ctx_fini(&l->l_addb);
	c2_mutex_fini(&l->l_lock);

	layout_type_put(l->l_dom, l->l_type);
	l->l_type = NULL;

	C2_LEAVE("lid %llu", (unsigned long long)l->l_id);
}

/**
 * Initialises a striped layout object, using provided enumeration object.
 * @pre The enumeration object e is already initialised by internally elevating
 * reference of the respective enum type.
 * @post Pointer to the c2_layout object is set back in the c2_layout_enum
 * object.
 */
int c2_layout__striped_init(struct c2_layout_domain *dom,
			    struct c2_layout_striped *str_l,
			    uint64_t lid, uint64_t pool_id,
			    struct c2_layout_type *type,
			    const struct c2_layout_ops *ops,
			    struct c2_layout_enum *e)

{
	int rc;

	C2_PRE(domain_invariant(dom));
	C2_PRE(str_l != NULL);
	C2_PRE(lid != LID_NONE);
	C2_PRE(c2_pool_id_is_valid(pool_id));
	C2_PRE(type != NULL);
	C2_PRE(ops != NULL);
	C2_PRE(e != NULL);

	C2_ENTRY("lid %llu, enum-type-id %lu", (unsigned long long)lid,
		 (unsigned long)e->le_type->let_id);

	rc = c2_layout__init(dom, &str_l->ls_base, lid, pool_id, type, ops);
	if (rc == 0) {
		str_l->ls_enum = e;
		str_l->ls_enum->le_l = &str_l->ls_base;

		/*
		 * enum_invariant() invoked internally from within
		 * striped_layout_invariant() verifies that
		 * str_l->ls_enum->le_l is set appropriately.
		 */
		C2_POST(striped_layout_invariant(str_l));
	}

	C2_LEAVE("lid %llu, rc %d", (unsigned long long)lid, rc);
	return rc;
}

/**
 * Initialises a striped layout object.
 * @post The enum object which is part of striped layout object, is finalised
 * as well.
 */
void c2_layout__striped_fini(struct c2_layout_striped *str_l)
{
	C2_PRE(striped_layout_invariant(str_l));

	C2_ENTRY("lid %llu", (unsigned long long)str_l->ls_base.l_id);

	str_l->ls_enum->le_ops->leo_fini(str_l->ls_enum);
	c2_layout__fini(&str_l->ls_base);

	C2_LEAVE("lid %llu", (unsigned long long)str_l->ls_base.l_id);
}

/**
 * Initialises an enumeration object, adds a reference on the respective
 * enum type.
 */
void c2_layout__enum_init(struct c2_layout_domain *dom,
			  struct c2_layout_enum *le,
			  struct c2_layout_enum_type *et,
			  const struct c2_layout_enum_ops *ops)
{
	C2_PRE(domain_invariant(dom));
	C2_PRE(le != NULL);
	C2_PRE(c2_layout__is_enum_type_valid(et->let_id, dom));
	C2_PRE(et != NULL);
	C2_PRE(ops != NULL);

	C2_ENTRY("Enum-type-id %lu", (unsigned long)et->let_id);

	/* le->le_l will be set through c2_layout__striped_init(). */
	le->le_l   = NULL;
	le->le_ops = ops;

	enum_type_get(dom, et);
	le->le_type = et;

	C2_LEAVE("Enum-type-id %lu", (unsigned long)et->let_id);
}

/**
 * Finalises an enum object, releases a reference on the respective enum
 * type.
 */
void c2_layout__enum_fini(struct c2_layout_enum *le)
{
	C2_PRE(enum_invariant(le));

	C2_ENTRY("Enum-type-id %lu", (unsigned long)le->le_type->let_id);

	enum_type_put(le->le_l->l_dom, le->le_type);
	le->le_type = NULL;

	C2_LEAVE();
}

/**
 * Compare layouts table keys.
 * This is a 3WAY comparison.
 */
static int l_key_cmp(struct c2_table *table,
		     const void *key0, const void *key1)
{
	const uint64_t *lid0 = key0;
	const uint64_t *lid1 = key1;

	return C2_3WAY(*lid0, *lid1);
}

/** table_ops for the layouts table. */
static const struct c2_table_ops layouts_table_ops = {
	.to = {
		[TO_KEY] = {
			.max_size = sizeof(struct c2_uint128)
		},
		[TO_REC] = {
			.max_size = ~0
		}
	},
	.key_cmp = l_key_cmp
};

/** Initialises the layout schema object - creates the layouts table. */
static int schema_init(struct c2_layout_schema *schema,
		       struct c2_dbenv *dbenv)
{
	int rc;

	C2_PRE(schema != NULL);
	C2_PRE(dbenv != NULL);

	C2_SET0(schema);

	schema->ls_dbenv = dbenv;

	rc = c2_table_init(&schema->ls_layouts, schema->ls_dbenv, "layouts",
			   DEFAULT_DB_FLAG, &layouts_table_ops);
	if (rc != 0) {
		c2_layout__log("c2_layout_schema_init",
			       "c2_table_init() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &c2_addb_func_fail, &layout_global_ctx,
			       LID_NONE, rc);
		schema->ls_dbenv = NULL;
	}

	c2_mutex_init(&schema->ls_lock);
	return rc;
}

/**
 * Finalises the layout schema.
 * @pre All the layout types and enum types should be unregistered.
 */
void schema_fini(struct c2_layout_schema *schema)
{
	c2_mutex_fini(&schema->ls_lock);
	c2_table_fini(&schema->ls_layouts);
	schema->ls_dbenv = NULL;
}

c2_bcount_t c2_layout__enum_max_recsize(struct c2_layout_domain *dom)
{
	uint32_t    i;
	c2_bcount_t e_recsize;
	c2_bcount_t max_recsize = 0;

	C2_PRE(dom != NULL);

	/*
	 * Iterate over all the enum types to find the maximum possible
	 * recsize.
	 */
        for (i = 0; i < ARRAY_SIZE(dom->ld_enum); ++i) {
		if (dom->ld_enum[i] == NULL)
			continue;
                e_recsize = dom->ld_enum[i]->let_ops->leto_max_recsize();
		max_recsize = max64u(max_recsize, e_recsize);
        }
	return max_recsize;
}

/**
 * Maximum possible size for a record in the layouts table (without
 * considering the data in the tables other than the layouts) is maintained in
 * c2_layout_schema::ls_max_recsize.
 * This function updates c2_layout_schema::ls_max_recsize, by re-calculating it.
 */
static void max_recsize_update(struct c2_layout_domain *dom)
{
	uint32_t    i;
	c2_bcount_t recsize;
	c2_bcount_t max_recsize = 0;

	C2_PRE(domain_invariant(dom));
	C2_PRE(c2_mutex_is_locked(&dom->ld_schema.ls_lock));

	/*
	 * Iterate over all the layout types to find the maximum possible
	 * recsize.
	 */
	for (i = 0; i < ARRAY_SIZE(dom->ld_type); ++i) {
		if (dom->ld_type[i] == NULL)
			continue;
		recsize = dom->ld_type[i]->lt_ops->lto_max_recsize(dom);
		max_recsize = max64u(max_recsize, recsize);
	}

	dom->ld_schema.ls_max_recsize = sizeof(struct c2_layout_rec) +
					max_recsize;
}

/**
 * This method adds an ADDB record indicating failure along with a short
 * error message string and the error code.
 */
static void addb_add(struct c2_addb_ctx *ctx,
		     const struct c2_addb_ev *ev,
		     const char *err_msg,
		     int rc)
{
	switch (ev->ae_id) {
	case C2_ADDB_EVENT_FUNC_FAIL:
		C2_ADDB_ADD(ctx, &layout_addb_loc, c2_addb_func_fail,
			    err_msg, rc);
		break;
	case C2_ADDB_EVENT_OOM:
		C2_ADDB_ADD(ctx, &layout_addb_loc, c2_addb_oom);
		break;
	case C2_ADDB_EVENT_LAYOUT_DECODE_FAIL:
		C2_ADDB_ADD(ctx, &layout_addb_loc, layout_decode_fail,
			    err_msg, rc);
		break;
	case C2_ADDB_EVENT_LAYOUT_ENCODE_FAIL:
		C2_ADDB_ADD(ctx, &layout_addb_loc, layout_encode_fail,
			    err_msg, rc);
		break;
	case C2_ADDB_EVENT_LAYOUT_LOOKUP_FAIL:
		C2_ADDB_ADD(ctx, &layout_addb_loc, layout_lookup_fail,
			    err_msg, rc);
		break;
	case C2_ADDB_EVENT_LAYOUT_ADD_FAIL:
		C2_ADDB_ADD(ctx, &layout_addb_loc, layout_add_fail,
			    err_msg, rc);
		break;
	case C2_ADDB_EVENT_LAYOUT_UPDATE_FAIL:
		C2_ADDB_ADD(ctx, &layout_addb_loc, layout_update_fail,
			    err_msg, rc);
		break;
	case C2_ADDB_EVENT_LAYOUT_DELETE_FAIL:
		C2_ADDB_ADD(ctx, &layout_addb_loc, layout_delete_fail,
			    err_msg, rc);
		break;
	default:
		C2_ASSERT(0);
	}
}

/**
 * This method performs the following operations:
 * 1) If value of the flag addb_record is true, then it invokes
 *    addb_add() to add an ADDB record.
 * 2) If value of the flag trace_record is true, then it adds a C2_LOG record
 *    (trace record), indicating failure, along with a short error message
 *    string and the error code.
 * 3) Note: For suceesss cases (indicated by rc == 0), c2_layout__log() is
 *    never invoked since:
 *    (i) ADDB records are not expected to be added in success cases.
 *    (ii) C2_LEAVE() or C2_LOG() are used directly to log the trace records,
 *         avoiding the function call overhead.
 *
 * @param addb_record Indicates if ADDB record is to be added.
 * @param trace_record Indicates if C2_LOG record is to be added.
 */
void c2_layout__log(const char *fn_name,
		    const char *err_msg,
		    bool addb_record,
		    bool trace_record,
		    const struct c2_addb_ev *ev,
		    struct c2_addb_ctx *ctx,
		    uint64_t lid,
		    int rc)
{
	C2_PRE(fn_name != NULL);
	C2_PRE(ev != NULL);
	C2_PRE(ctx != NULL);
	C2_PRE(rc != 0);
	C2_PRE(ergo(rc != 0, err_msg != NULL &&
			     C2_IN(ev->ae_id, (layout_decode_fail.ae_id,
					       layout_encode_fail.ae_id,
					       layout_lookup_fail.ae_id,
					       layout_add_fail.ae_id,
					       layout_update_fail.ae_id,
					       layout_delete_fail.ae_id,
					       c2_addb_func_fail.ae_id,
					       c2_addb_oom.ae_id))));

	/* ADDB record logging. */
	if (addb_record)
		addb_add(ctx, ev, err_msg, rc);

	/* Trace record logging. */
	if (trace_record)
		C2_LOG("%s(): lid %llu, %s, rc %d",
		       (const char *)fn_name, (unsigned long long)lid,
		       (const char *)err_msg, rc);
}

int c2_layouts_init(void)
{
	return 0;
}

void c2_layouts_fini(void)
{
}

/**
 * Initialises layout domain - Initialises arrays to hold the objects for
 * layout types and enum types and initialises the schema object.
 * @pre Caller should have performed c2_dbenv_init() on dbenv.
 */
int c2_layout_domain_init(struct c2_layout_domain *dom, struct c2_dbenv *dbenv)
{
	int rc;

	C2_PRE(dom != NULL);
	C2_PRE(dbenv != NULL);

	C2_SET0(dom);
	c2_mutex_init(&dom->ld_lock);
	layout_list_tlist_init(&dom->ld_layout_list);
	c2_bob_type_tlist_init(&llist_entry_bob, &layout_list_tl);

	rc = schema_init(&dom->ld_schema, dbenv);
	if (rc != 0)
		return rc;

	C2_POST(domain_invariant(dom));
	return rc;
}

/**
 * Finalises the layout domain.
 * @pre All the layout types and enum types should be unregistered.
 */
void c2_layout_domain_fini(struct c2_layout_domain *dom)
{
	C2_PRE(domain_invariant(dom));

	/*
	 * Verify that all the layout objects belonging to this domain have
	 * been finalised.
	 */
	C2_PRE(layout_list_tlist_is_empty(&dom->ld_layout_list));

	/* Verify that all the layout types are unregistered. */
	C2_PRE(c2_forall(i, ARRAY_SIZE(dom->ld_type),
	       dom->ld_type[i] == NULL));

	/* Verify that all the enum types are unregistered. */
	C2_PRE(c2_forall(i, ARRAY_SIZE(dom->ld_enum),
	       dom->ld_enum[i] == NULL));

	schema_fini(&dom->ld_schema);
	layout_list_tlist_fini(&dom->ld_layout_list);
	c2_mutex_fini(&dom->ld_lock);
}

/**
 * Registers all the available layout types and enum types.
 */
int c2_layout_register(struct c2_layout_domain *dom)
{
	int rc;

	C2_PRE(domain_invariant(dom));

	rc = c2_layout_type_register(dom, &c2_pdclust_layout_type);
	if (rc != 0)
		return rc;

	rc = c2_layout_enum_type_register(dom, &c2_list_enum_type);
	if (rc != 0) {
		c2_layout_type_unregister(dom, &c2_pdclust_layout_type);
		return rc;
	}

	rc = c2_layout_enum_type_register(dom, &c2_linear_enum_type);
	if (rc != 0) {
		c2_layout_type_unregister(dom, &c2_pdclust_layout_type);
		c2_layout_enum_type_unregister(dom, &c2_list_enum_type);
		return rc;
	}

	return rc;
}

void c2_layout_unregister(struct c2_layout_domain *dom)
{
	C2_PRE(domain_invariant(dom));

	c2_layout_enum_type_unregister(dom, &c2_list_enum_type);
	c2_layout_enum_type_unregister(dom, &c2_linear_enum_type);

	c2_layout_type_unregister(dom, &c2_pdclust_layout_type);
}

/**
 * Registers a new layout type with the layout types maintained by
 * c2_layout_domain::ld_type[] and initialises layout type specific tables,
 * if applicable.
 */
int c2_layout_type_register(struct c2_layout_domain *dom,
			    struct c2_layout_type *lt)
{
	int rc;

	C2_PRE(domain_invariant(dom));
	C2_PRE(lt != NULL);
	C2_PRE(IS_IN_ARRAY(lt->lt_id, dom->ld_type));
	C2_PRE(lt->lt_domain == NULL);

	C2_ENTRY("Layout-type-id %lu, domain %p",
		 (unsigned long)lt->lt_id, dom);

	c2_mutex_lock(&dom->ld_lock);

	C2_ASSERT(dom->ld_type[lt->lt_id] == NULL);
	C2_ASSERT(lt->lt_ops != NULL);

	dom->ld_type[lt->lt_id] = (struct c2_layout_type *)lt;

	/* Get the first reference on this layout type. */
	C2_ASSERT(lt->lt_ref_count == 0);
	C2_CNT_INC(lt->lt_ref_count);

	/* Allocate type specific schema data. */
	c2_mutex_lock(&dom->ld_schema.ls_lock);

	rc = lt->lt_ops->lto_register(dom, lt);
	if (rc != 0) {
		c2_layout__log("c2_layout_type_register",
			       "lto_register() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &c2_addb_func_fail, &layout_global_ctx,
			       LID_NONE, rc);
		C2_CNT_DEC(lt->lt_ref_count);
		dom->ld_type[lt->lt_id] = NULL;
	} else {
		max_recsize_update(dom);
		lt->lt_domain = dom;
	}

	c2_mutex_unlock(&dom->ld_schema.ls_lock);
	c2_mutex_unlock(&dom->ld_lock);

	C2_LEAVE("Layout-type-id %lu, rc %d", (unsigned long)lt->lt_id, rc);
	return rc;
}

/**
 * Unregisters a layout type from the layout types maintained by
 * c2_layout_domain::ld_type[] and finalises layout type specific tables,
 * if applicable.
 */
void c2_layout_type_unregister(struct c2_layout_domain *dom,
			       struct c2_layout_type *lt)
{
	C2_PRE(domain_invariant(dom));
	C2_PRE(lt != NULL);
	C2_PRE(dom->ld_type[lt->lt_id] == lt);
	C2_PRE(lt->lt_domain != NULL);

	C2_ENTRY("Layout-type-id %lu, lt_domain %p",
		 (unsigned long)lt->lt_id, lt->lt_domain);

	c2_mutex_lock(&dom->ld_lock);

	c2_mutex_lock(&dom->ld_schema.ls_lock);
	lt->lt_ops->lto_unregister(dom, lt);
	dom->ld_type[lt->lt_id] = NULL;
	max_recsize_update(dom);
	lt->lt_domain = NULL;
	c2_mutex_unlock(&dom->ld_schema.ls_lock);

	/* Release the last reference on this layout type. */
	C2_ASSERT(lt->lt_ref_count == DEFAULT_REF_COUNT);
	C2_CNT_DEC(lt->lt_ref_count);

	c2_mutex_unlock(&dom->ld_lock);

	C2_LEAVE("Layout-type-id %lu", (unsigned long)lt->lt_id);
}

/**
 * Registers a new enumeration type with the enumeration types
 * maintained by c2_layout_domain::ld_enum[] and initialises enum type specific
 * tables, if applicable.
 */
int c2_layout_enum_type_register(struct c2_layout_domain *dom,
				 struct c2_layout_enum_type *let)
{
	int rc;

	C2_PRE(domain_invariant(dom));
	C2_PRE(let != NULL);
	C2_PRE(IS_IN_ARRAY(let->let_id, dom->ld_enum));
	C2_PRE(let->let_domain == NULL);

	C2_ENTRY("Enum_type_id %lu, domain %p",
		 (unsigned long)let->let_id, dom);

	c2_mutex_lock(&dom->ld_lock);

	C2_ASSERT(dom->ld_enum[let->let_id] == NULL);
	C2_ASSERT(let->let_ops != NULL);

	dom->ld_enum[let->let_id] = (struct c2_layout_enum_type *)let;

	/* Get the first reference on this enum type. */
	C2_CNT_INC(let->let_ref_count);
	C2_ASSERT(let->let_ref_count == DEFAULT_REF_COUNT);

	/* Allocate enum type specific schema data. */
	c2_mutex_lock(&dom->ld_schema.ls_lock);

	rc = let->let_ops->leto_register(dom, let);
	if (rc != 0) {
		c2_layout__log("c2_layout_enum_type_register",
			       "leto_register() failed",
			       ADDB_RECORD_ADD, TRACE_RECORD_ADD,
			       &c2_addb_func_fail, &layout_global_ctx,
			       LID_NONE, rc);
		C2_CNT_DEC(let->let_ref_count);
		dom->ld_enum[let->let_id] = NULL;
	} else {
		max_recsize_update(dom);
		let->let_domain = dom;
	}

	c2_mutex_unlock(&dom->ld_schema.ls_lock);
	c2_mutex_unlock(&dom->ld_lock);

	C2_LEAVE("Enum_type_id %lu, rc %d", (unsigned long)let->let_id, rc);
	return rc;
}

/**
 * Unregisters an enumeration type from the enumeration types
 * maintained by c2_layout_domain::ld_enum[] and finalises enum type
 * specific tables, if applicable.
 */
void c2_layout_enum_type_unregister(struct c2_layout_domain *dom,
				    struct c2_layout_enum_type *let)
{
	C2_PRE(domain_invariant(dom));
	C2_PRE(let != NULL);
	C2_PRE(dom->ld_enum[let->let_id] == let);
	C2_PRE(let->let_domain != NULL);

	C2_ENTRY("Enum_type_id %lu, let_domain %p",
		 (unsigned long)let->let_id, let->let_domain);

	c2_mutex_lock(&dom->ld_lock);

	c2_mutex_lock(&dom->ld_schema.ls_lock);
	let->let_ops->leto_unregister(dom, let);
	dom->ld_enum[let->let_id] = NULL;
	max_recsize_update(dom);
	let->let_domain = NULL;
	c2_mutex_unlock(&dom->ld_schema.ls_lock);

	/* Release the last reference on this enum type. */
	C2_ASSERT(let->let_ref_count == DEFAULT_REF_COUNT);
	C2_CNT_DEC(let->let_ref_count);

	c2_mutex_unlock(&dom->ld_lock);

	C2_LEAVE("Enum_type_id %lu", (unsigned long)let->let_id);
}

/**
 * Finds a layout with the given identifier, from the list of layout objects
 * maintained in the layout domain.
 */
struct c2_layout *c2_layout_find(struct c2_layout_domain *dom, uint64_t lid)
{
	struct llist_entry *l_entry;
	struct c2_layout   *l;

	C2_PRE(domain_invariant(dom));
	C2_PRE(lid != LID_NONE);

	C2_ENTRY("lid %llu", (unsigned long long)lid);

	c2_mutex_lock(&dom->ld_schema.ls_lock);
	l_entry = layout_list_lookup(dom, lid);
	c2_mutex_unlock(&dom->ld_schema.ls_lock);

	l = l_entry == NULL ? NULL : l_entry->lle_l;
	C2_LEAVE("lid %llu, l_pointer %p", (unsigned long long)lid, l);
	return l;
}

/** Adds a reference to the layout. */
void c2_layout_get(struct c2_layout *l)
{
	C2_PRE(layout_invariant(l));
	C2_PRE(layout_list_lookup(l->l_dom, l->l_id) != NULL);
	/* layout_invariant() verifies that l->l_ref >= DEFAULT_REF_COUNT. */

	C2_ENTRY("lid %llu, ref_count %lu", (unsigned long long)l->l_id,
		 (unsigned long)l->l_ref);

	c2_mutex_lock(&l->l_lock);
	C2_CNT_INC(l->l_ref);
	c2_mutex_unlock(&l->l_lock);

	C2_LEAVE("lid %llu", (unsigned long long)l->l_id);
}

/**
 * Releases a reference on the layout. If it is the last reference being
 * released, then it removes the layout entry from the layout list
 * maintained in the layout domain and then finalises the layout along
 * with finalising its enumeration object, if applicable.
 */
void c2_layout_put(struct c2_layout *l)
{
	struct llist_entry *lentry;
	bool                killme;

	C2_PRE(layout_invariant(l));

	C2_ENTRY("lid %llu, ref_count %lu", (unsigned long long)l->l_id,
		 (unsigned long)l->l_ref);

	c2_mutex_lock(&l->l_dom->ld_lock);
	c2_mutex_lock(&l->l_lock);
	C2_CNT_DEC(l->l_ref);
	killme = l->l_ref == DEFAULT_REF_COUNT;
	if (killme) {
		lentry = layout_list_lookup(l->l_dom, l->l_id);
		C2_ASSERT(lentry != NULL);
		layout_list_tlist_del(lentry);
		llist_entry_bob_fini(lentry);
		layout_list_tlink_fini(lentry);
		c2_free(lentry);
	}

	c2_mutex_unlock(&l->l_lock);
	c2_mutex_unlock(&l->l_dom->ld_lock);

	if (killme)
		l->l_ops->lo_fini(l);
	C2_LEAVE();
}

/**
 * This method
 * - Either continues to build an in-memory layout object from its
 *   representation 'stored in the Layout DB'
 * - Or builds an in-memory layout object from its representation 'received
 *   through a buffer'.
 *
 * Two use cases of c2_layout_decode()
 * - Server decodes an on-disk layout record by reading it from the Layout
 *   DB, into an in-memory layout structure, using c2_layout_lookup() which
 *   internally calls c2_layout_decode().
 * - Client decodes a buffer received over the network, into an in-memory
 *   layout structure, using c2_layout_decode().
 *
 * @param cur Cursor pointing to a buffer containing serialised representation
 * of the layout. Regarding the size of the buffer:
 * - In case c2_layout_decode() is called through c2_layout_add(), then the
 *   buffer should be containing all the data that is read specifically from
 *   the layouts table. It means its size needs to be at the most the size
 *   returned by c2_layout_max_recsize().
 * - In case c2_layout_decode() is called by some other caller, then the
 *   buffer should be containing all the data belonging to the specific layout.
 *   It may include data that spans over tables other than layouts as well. It
 *   means its size may need to be even more than the one returned by
 *   c2_layout_max_recsize(). For example, in case of LIST enumeration type,
 *   the buffer needs to contain the data that is stored in the cob_lists table.
 *
 * @param op This enum parameter indicates what is the DB operation to be
 * performed on the layout record. It could be LOOKUP if at all a DB operation.
 * If it is BUFFER_OP, then the layout is decoded from its representation
 * received through the buffer.
 *
 * @post Layout object is built internally (along with enumeration object being
 * built if applicable). User is expected to add rererence/s to this layout
 * object while using it. Releasing the last reference will finalise the layout
 * object by freeing it.
 */
int c2_layout_decode(struct c2_layout_domain *dom,
		     uint64_t lid,
		     enum c2_layout_xcode_op op,
		     struct c2_db_tx *tx,
		     struct c2_bufvec_cursor *cur,
		     struct c2_layout **out)
{
	struct c2_layout_type *lt;
	struct c2_layout_rec  *rec;
	int                    rc;

	C2_PRE(domain_invariant(dom));
	C2_PRE(lid != LID_NONE);
	C2_PRE(layout_list_lookup(dom, lid) == NULL);
	C2_PRE(C2_IN(op, (C2_LXO_DB_LOOKUP, C2_LXO_BUFFER_OP)));
	C2_PRE(ergo(op == C2_LXO_DB_LOOKUP, tx != NULL));
	C2_PRE(cur != NULL);
	C2_PRE(c2_bufvec_cursor_step(cur) >= sizeof *rec);
	C2_PRE(out != NULL);

	C2_ENTRY("lid %llu", (unsigned long long)lid);

	rec = c2_bufvec_cursor_addr(cur);
	C2_ASSERT(rec_invariant(rec, dom));

	lt = dom->ld_type[rec->lr_lt_id];

	/* Move the cursor to point to the layout type specific payload. */
	c2_bufvec_cursor_move(cur, sizeof *rec);
	/*
	 * It is fine if any of the layout does not contain any data in
	 * rec->lr_data[], unless it is required by the specific layout type,
	 * which will be caught by the respective lto_decode() implementation.
	 * Hence, ignoring the return status of c2_bufvec_cursor_move() here.
	 */

	rc = lt->lt_ops->lto_decode(dom, lid, op, tx, rec->lr_pool_id,
				    cur, out);
	if (rc != 0) {
		c2_layout__log("c2_layout_decode", "lto_decode() failed",
			       op == C2_LXO_BUFFER_OP, TRACE_RECORD_ADD,
			       &layout_decode_fail, &layout_global_ctx,
			       lid, rc);
		goto out;
	}

	/* Following fields are set through c2_layout__init(). */
	C2_ASSERT((*out)->l_id == lid);
	C2_ASSERT((*out)->l_type == lt);
	C2_ASSERT((*out)->l_dom == dom);
	C2_ASSERT((*out)->l_pool_id == rec->lr_pool_id);

	/*
	 * l_ref is the only field that can get updated for a layout object
	 * or a layout record, once created.
	 */
	(*out)->l_ref = rec->lr_ref_count;

	C2_POST(layout_invariant(*out));
out:
	C2_LEAVE("lid %llu, rc %d", (unsigned long long)lid, rc);
	return rc;
}

/**
 * This method uses an in-memory layout object and
 * - Either adds/updates/deletes it to/from the Layout DB
 * - Or converts it to a buffer.
 *
 * Two use cases of c2_layout_encode()
 * - Server encodes an in-memory layout object into a buffer using
 *   c2_layout_encode(), so as to send it to the client.
 * - Server encodes an in-memory layout object using one of c2_layout_add(),
 *   c2_layout_update() or c2_layout_delete() and adds/updates/deletes
 *   it to or from the Layout DB.
 *
 * @param op This enum parameter indicates what is the DB operation to be
 * performed on the layout record if at all a DB operation which could be
 * one of ADD/UPDATE/DELETE. If it is BUFFER_OP, then the layout is stored
 * in the buffer provided by the caller.
 *
 * @param oldrec_cur Cursor pointing to a buffer to be used to read the
 * exisiting layout record from the layouts table. Applicable only in case of
 * layou update operation. In other cases, it is expected to be NULL.
 *
 * @param out Cursor pointing to a buffer. Regarding the size of the buffer:
 * - In case c2_layout_encode() is called through c2_layout_add()|
 *   c2_layout_update()|c2_layout_delete(), then the buffer size should be
 *   large enough to contain the data that is to be written specifically to
 *   the layouts table. It means it needs to be at the most the size returned
 *   by c2_layout_max_recsize().
 * - In case c2_layout_encode() is called by some other caller, then the
 *   buffer size should be large enough to contain all the data belonging to
 *   the specific layout. It means the size required may even be more than
 *   the one returned by c2_layout_max_recsize(). For example, in case of LIST
 *   enumeration type, some data goes into table other than layouts, viz.
 *   cob_lists table.
 *
 * @post
 * - If op is is either for ADD|UPDATE|DELETE, respective DB operation is
 *   continued.
 * - If op is BUFFER_OP, the buffer contains the serialised representation
 *   of the whole layout.
 */
int c2_layout_encode(struct c2_layout *l,
		     enum c2_layout_xcode_op op,
		     struct c2_db_tx *tx,
		     struct c2_bufvec_cursor *oldrec_cur,
		     struct c2_bufvec_cursor *out)
{
	struct c2_layout_rec   rec;
	struct c2_layout_rec  *oldrec;
	struct c2_layout_type *lt;
	c2_bcount_t            nbytes;
	int                    rc;

	C2_PRE(layout_invariant(l));
	C2_PRE(C2_IN(op, (C2_LXO_DB_ADD, C2_LXO_DB_UPDATE,
			  C2_LXO_DB_DELETE, C2_LXO_BUFFER_OP)));
	C2_PRE(ergo(op != C2_LXO_BUFFER_OP, tx != NULL));
	C2_PRE(ergo(op == C2_LXO_DB_UPDATE, oldrec_cur != NULL));
	C2_PRE(ergo(op == C2_LXO_DB_UPDATE,
	            c2_bufvec_cursor_step(oldrec_cur) >= sizeof *oldrec));
	C2_PRE(out != NULL);
	C2_PRE(c2_bufvec_cursor_step(out) >= sizeof rec);

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);

	c2_mutex_lock(&l->l_lock);

	rec.lr_lt_id     = l->l_type->lt_id;
	rec.lr_ref_count = l->l_ref;
	rec.lr_pool_id   = l->l_pool_id;

	C2_ASSERT(rec_invariant(&rec, l->l_dom));

	lt = l->l_dom->ld_type[l->l_type->lt_id];

	if (op == C2_LXO_DB_UPDATE) {
		/*
		 * Processing the oldrec_cur, to verify that nothing other than
		 * l_ref is being changed for this layout and then to make it
		 * point to the layout type specific payload.
		 */
		oldrec = c2_bufvec_cursor_addr(oldrec_cur);
		C2_ASSERT(rec_invariant(oldrec, l->l_dom));
		C2_ASSERT(oldrec->lr_lt_id == l->l_type->lt_id &&
			  oldrec->lr_pool_id == l->l_pool_id);
		c2_bufvec_cursor_move(oldrec_cur, sizeof *oldrec);
	}

	nbytes = c2_bufvec_cursor_copyto(out, &rec, sizeof rec);
	C2_ASSERT(nbytes == sizeof rec);

	rc = lt->lt_ops->lto_encode(l, op, tx, oldrec_cur, out);
	if (rc != 0)
		c2_layout__log("c2_layout_encode", "lto_encode() failed",
			       op == C2_LXO_BUFFER_OP, TRACE_RECORD_ADD,
			       &layout_encode_fail, &l->l_addb, l->l_id, rc);

	c2_mutex_unlock(&l->l_lock);
	C2_LEAVE("lid %llu, rc %d", (unsigned long long)l->l_id, rc);
	return rc;
}

/**
 * Returns maximum possible size for a record in the layouts table (without
 * considering the data in the tables other than layouts), from what is
 * maintained in the c2_layout_schema object.
 */
c2_bcount_t c2_layout_max_recsize(const struct c2_layout_domain *dom)
{
	C2_PRE(domain_invariant(dom));

	return dom->ld_schema.ls_max_recsize;
}

/** @} end group layout */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
