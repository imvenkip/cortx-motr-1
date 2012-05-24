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
 */

#include "lib/errno.h"
#include "lib/misc.h"  /* strlen() */
#include "lib/vec.h"   /* c2_bufvec_cursor_step(), c2_bufvec_cursor_addr() */
#include "lib/bob.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_LAYOUT
#include "lib/trace.h"

#include "pool/pool.h" /* c2_pool_id_is_valid() */
#include "layout/layout_internal.h"
#include "layout/layout_db.h"
#include "layout/layout.h"

extern const struct c2_layout_type c2_pdclust_layout_type;
extern const struct c2_layout_enum_type c2_list_enum_type;
extern const struct c2_layout_enum_type c2_linear_enum_type;

enum {
	LAYOUT_MAGIC = 0x4C41594F55544D41 /* LAYOUTMA */
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

static int layout_rec_invariant(const struct c2_layout_rec *rec,
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

	if (dom->ld_type[lt_id] == NULL ||
		dom->ld_type_ref_count[lt_id] < DEFAULT_REF_COUNT) {
		C2_LOG("Unknown layout type, layout-type-id %lu",
		       (unsigned long)lt_id);
		return false;
	}

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

	if (dom->ld_enum[let_id] == NULL ||
		dom->ld_enum_ref_count[let_id] < DEFAULT_REF_COUNT) {
		C2_LOG("Unknown enum type, enum-type-id %lu",
		       (unsigned long)let_id);
		return false;
	}

	return true;
}

/** Adds a reference to the layout type. */
static void layout_type_get(struct c2_layout_domain *dom,
			    const struct c2_layout_type *lt)
{
	C2_PRE(domain_invariant(dom));
	C2_PRE(lt != NULL);

	/*
	 * The DEFAULT_REF_COUNT while a layout type is registered, being 1,
	 * ensures that the layout can not be freed concurrently.
	 */

	c2_mutex_lock(&dom->ld_lock);

	C2_CNT_INC(dom->ld_type_ref_count[lt->lt_id]);

	c2_mutex_unlock(&dom->ld_lock);
}

/** Releases a reference on the layout type. */
static void layout_type_put(struct c2_layout_domain *dom,
			    const struct c2_layout_type *lt)
{
	C2_PRE(domain_invariant(dom));
	C2_PRE(lt != NULL);

	c2_mutex_lock(&dom->ld_lock);

	C2_CNT_DEC(dom->ld_type_ref_count[lt->lt_id]);

	c2_mutex_unlock(&dom->ld_lock);
}

/** Adds a reference on the enum type. */
static void enum_type_get(struct c2_layout_domain *dom,
			  const struct c2_layout_enum_type *let)
{
	C2_PRE(domain_invariant(dom));
	C2_PRE(let != NULL);

	/*
	 * The DEFAULT_REF_COUNT while an enum type is registered, being 1,
	 * ensures that the layout can not be freed concurrently.
	 */

	c2_mutex_lock(&dom->ld_lock);

	C2_CNT_INC(dom->ld_enum_ref_count[let->let_id]);

	c2_mutex_unlock(&dom->ld_lock);
}

/** Releases a reference on the enum type. */
static void enum_type_put(struct c2_layout_domain *dom,
			  const struct c2_layout_enum_type *let)
{
	C2_PRE(domain_invariant(dom));
	C2_PRE(let != NULL);

	c2_mutex_lock(&dom->ld_lock);

	C2_CNT_DEC(dom->ld_enum_ref_count[let->let_id]);

	c2_mutex_unlock(&dom->ld_lock);
}

/** Initialises a layout, adds a reference on the respective layout type. */
void c2_layout__init(struct c2_layout_domain *dom,
		     struct c2_layout *l,
		     uint64_t lid, uint64_t pool_id,
		     const struct c2_layout_type *type,
		     const struct c2_layout_ops *ops)
{
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

	layout_type_get(dom, type);
	l->l_type    = type;

	c2_mutex_init(&l->l_lock);
	c2_addb_ctx_init(&l->l_addb, &layout_addb_ctx_type,
			 &layout_global_ctx);

	c2_layout_bob_init(l);

	C2_POST(layout_invariant(l));
	C2_LEAVE("lid %llu", (unsigned long long)lid);
}

/** Finalises a layout, releases a reference on the respective layout type. */
void c2_layout__fini(struct c2_layout *l)
{
	C2_PRE(layout_invariant(l));

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
 * @post Pointer to the c2_layout object is set back in the c2_layout_enum
 * object.
 */
void c2_layout__striped_init(struct c2_layout_domain *dom,
			     struct c2_layout_striped *str_l,
			     struct c2_layout_enum *e,
			     uint64_t lid, uint64_t pool_id,
			     const struct c2_layout_type *type,
			     const struct c2_layout_ops *ops)

{
	C2_PRE(domain_invariant(dom));
	C2_PRE(str_l != NULL);
	C2_PRE(e != NULL);
	C2_PRE(c2_pool_id_is_valid(pool_id));
	C2_PRE(lid != LID_NONE);
	C2_PRE(type != NULL);
	C2_PRE(ops != NULL);

	C2_ENTRY("lid %llu, enum-type-id %lu", (unsigned long long)lid,
		 (unsigned long)e->le_type->let_id);

	c2_layout__init(dom, &str_l->ls_base, lid, pool_id, type, ops);
	str_l->ls_enum = e;

	str_l->ls_enum->le_l = &str_l->ls_base;

	/*
	 * enum_invariant() invoked internally from within
	 * striped_layout_invariant() verifies that str_l->ls_enum->le_l is
	 * set appropriately.
	 */
	C2_POST(striped_layout_invariant(str_l));

	C2_LEAVE("lid %llu", (unsigned long long)lid);
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
			  const struct c2_layout_enum_type *et,
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
		layout_log("c2_layout_schema_init", "c2_table_init() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG, &c2_addb_func_fail,
			   &layout_global_ctx, LID_NONE, rc);

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
	 * Iterate over all the layout types to find maximum possible recsize.
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
 * This method adds an ADDB message indicating failure along with a short
 * error message string and the error code.
 */
static void layout_addb_add(struct c2_addb_ctx *ctx,
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
 * 1) If value of the flag if_addb_msg is true, then it invokes
 *    layout_addb_add() to add an ADDB record.
 * 2) If value of the flag if_trace_msg is true, then it adds a C2_LOG message
 *    (trace message), indicating failure, along with a short error message
 *    string and the error code.
 * 3) Note: For suceesss cases (indicated by rc == 0), layout_log() is never
 *    invoked since:
 *    (i) ADDB records are not expected to be added in success cases.
 *    (ii) C2_LEAVE() or C2_LOG() are used firectly to log the trace messages,
 *         avoiding the function call overhead.
 *
 * @param addb_msg Indicates if ADDB message is to be printed.
 * @param trace_msg Indicates if C2_LOG message is to be printed.
 */
void layout_log(const char *fn_name,
		const char *err_msg,
		bool addb_msg,
		bool trace_msg,
		const struct c2_addb_ev *ev,
		struct c2_addb_ctx *ctx,
		uint64_t lid,
		int rc)
{
	C2_PRE(fn_name != NULL);
	C2_PRE(ev != NULL);
	C2_PRE(ctx != NULL);
	C2_PRE(ergo(trace_msg, addb_msg));
	C2_PRE(rc != 0);
	C2_PRE(ergo(rc != 0, err_msg != NULL &&
			     (ev->ae_id == layout_decode_fail.ae_id ||
			      ev->ae_id == layout_encode_fail.ae_id ||
			      ev->ae_id == layout_lookup_fail.ae_id ||
			      ev->ae_id == layout_add_fail.ae_id ||
			      ev->ae_id == layout_update_fail.ae_id ||
			      ev->ae_id == layout_delete_fail.ae_id ||
			      ev->ae_id == c2_addb_func_fail.ae_id ||
			      ev->ae_id == c2_addb_oom.ae_id)));


	/* ADDB record logging. */
	if (addb_msg)
		layout_addb_add(ctx, ev, err_msg, rc);

	/* Trace message logging. */
	if (trace_msg)
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
 * layout types and enum types and initializes the schema object.
 * @pre Caller should have performed c2_dbenv_init() on dbenv.
 */
int c2_layout_domain_init(struct c2_layout_domain *dom, struct c2_dbenv *dbenv)
{
	int rc;

	C2_PRE(dom != NULL);
	C2_PRE(dbenv != NULL);

	C2_SET0(dom);

	c2_mutex_init(&dom->ld_lock);

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
	uint32_t i;

	C2_PRE(domain_invariant(dom));

	/* Verify that all the layout types are unregistered. */
	for (i = 0; i < ARRAY_SIZE(dom->ld_type); ++i)
		C2_PRE(dom->ld_type[i] == NULL);

	/* Verify that all the enum types are unregistered. */
	for (i = 0; i < ARRAY_SIZE(dom->ld_enum); ++i)
		C2_PRE(dom->ld_enum[i] == NULL);

	schema_fini(&dom->ld_schema);

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
	if (rc != 0)
		return rc;

	rc = c2_layout_enum_type_register(dom, &c2_linear_enum_type);
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
			    const struct c2_layout_type *lt)
{
	int rc;

	C2_PRE(domain_invariant(dom));
	C2_PRE(lt != NULL);
	C2_PRE(IS_IN_ARRAY(lt->lt_id, dom->ld_type));

	C2_ENTRY("Layout-type-id %lu", (unsigned long)lt->lt_id);

	c2_mutex_lock(&dom->ld_lock);

	C2_ASSERT(dom->ld_type[lt->lt_id] == NULL);
	C2_ASSERT(lt->lt_ops != NULL);

	dom->ld_type[lt->lt_id] = (struct c2_layout_type *)lt;

	/* Get the first reference on this layout type. */
	C2_ASSERT(dom->ld_type_ref_count[lt->lt_id] == 0);
	C2_CNT_INC(dom->ld_type_ref_count[lt->lt_id]);

	/* Allocate type specific schema data. */
	c2_mutex_lock(&dom->ld_schema.ls_lock);

	rc = lt->lt_ops->lto_register(dom, lt);
	if (rc != 0)
		layout_log("c2_layout_type_register", "lto_register() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG, &c2_addb_func_fail,
			   &layout_global_ctx, LID_NONE, rc);

	max_recsize_update(dom);

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
			       const struct c2_layout_type *lt)
{
	C2_PRE(domain_invariant(dom));
	C2_PRE(lt != NULL);
	C2_PRE(dom->ld_type[lt->lt_id] == lt);

	C2_ENTRY("Layout-type-id %lu", (unsigned long)lt->lt_id);

	c2_mutex_lock(&dom->ld_lock);

	c2_mutex_lock(&dom->ld_schema.ls_lock);
	lt->lt_ops->lto_unregister(dom, lt);
	dom->ld_type[lt->lt_id] = NULL;
	max_recsize_update(dom);
	c2_mutex_unlock(&dom->ld_schema.ls_lock);

	/* Release the last reference on this layout type. */
	C2_ASSERT(dom->ld_type_ref_count[lt->lt_id] == 1);
	C2_CNT_DEC(dom->ld_type_ref_count[lt->lt_id]);

	c2_mutex_unlock(&dom->ld_lock);

	C2_LEAVE("Layout-type-id %lu", (unsigned long)lt->lt_id);
}

/**
 * Registers a new enumeration type with the enumeration types
 * maintained by c2_layout_domain::ld_enum[] and initialises enum type specific
 * tables, if applicable.
 */
int c2_layout_enum_type_register(struct c2_layout_domain *dom,
				 const struct c2_layout_enum_type *let)
{
	int rc;

	C2_PRE(domain_invariant(dom));
	C2_PRE(let != NULL);
	C2_PRE(IS_IN_ARRAY(let->let_id, dom->ld_enum));

	C2_ENTRY("Enum_type_id %lu", (unsigned long)let->let_id);

	c2_mutex_lock(&dom->ld_lock);

	C2_ASSERT(dom->ld_enum[let->let_id] == NULL);
	C2_ASSERT(let->let_ops != NULL);

	dom->ld_enum[let->let_id] = (struct c2_layout_enum_type *)let;

	/* Get the first reference on this enum type. */
	C2_CNT_INC(dom->ld_enum_ref_count[let->let_id]);
	C2_ASSERT(dom->ld_enum_ref_count[let->let_id] == DEFAULT_REF_COUNT);

	/* Allocate enum type specific schema data. */
	c2_mutex_lock(&dom->ld_schema.ls_lock);

	rc = let->let_ops->leto_register(dom, let);
	if (rc != 0)
		layout_log("c2_layout_enum_type_register",
			   "leto_register() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG, &c2_addb_func_fail,
			   &layout_global_ctx, LID_NONE, rc);

	max_recsize_update(dom);

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
				    const struct c2_layout_enum_type *let)
{
	C2_PRE(domain_invariant(dom));
	C2_PRE(let != NULL);
	C2_PRE(dom->ld_enum[let->let_id] == let);

	C2_ENTRY("Enum_type_id %lu", (unsigned long)let->let_id);

	c2_mutex_lock(&dom->ld_lock);

	c2_mutex_lock(&dom->ld_schema.ls_lock);
	let->let_ops->leto_unregister(dom, let);
	dom->ld_enum[let->let_id] = NULL;
	max_recsize_update(dom);
	c2_mutex_unlock(&dom->ld_schema.ls_lock);

	/* Release the last reference on this enum type. */
	C2_ASSERT(dom->ld_enum_ref_count[let->let_id] == DEFAULT_REF_COUNT);
	C2_CNT_DEC(dom->ld_enum_ref_count[let->let_id]);

	c2_mutex_unlock(&dom->ld_lock);

	C2_LEAVE("Enum_type_id %lu", (unsigned long)let->let_id);
}

/* Finalise layout and its internal enum object if applicable. */
void c2_layout_fini(struct c2_layout *l)
{
	C2_PRE(layout_invariant(l));

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);
	l->l_ops->lo_fini(l);
	C2_LEAVE();
}

/** Adds a reference to the layout. */
void c2_layout_get(struct c2_layout *l)
{
	C2_PRE(layout_invariant(l));

	/*
	 * The DEFAULT_REF_COUNT being 1, ensures that the layout can not
	 * be freed concurrently.
	 */

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);

	c2_mutex_lock(&l->l_lock);
	C2_CNT_INC(l->l_ref);
	c2_mutex_unlock(&l->l_lock);

	C2_LEAVE("lid %llu", (unsigned long long)l->l_id);
}

/** Releases a reference on the layout. */
void c2_layout_put(struct c2_layout *l)
{
	C2_PRE(layout_invariant(l));

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);

	c2_mutex_lock(&l->l_lock);
	C2_CNT_DEC(l->l_ref);
	c2_mutex_unlock(&l->l_lock);

	/* The layout may be freed at this point. Hence not printig the lid. */
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
 * built if applicable). Hence, user needs to finalise the layout object when
 * done with the use. It can be accomplished by using the API c2_layout_fini().
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
	C2_PRE(op == C2_LXO_DB_LOOKUP || op == C2_LXO_BUFFER_OP);
	C2_PRE(ergo(op == C2_LXO_DB_LOOKUP, tx != NULL));
	C2_PRE(cur != NULL);
	C2_PRE(c2_bufvec_cursor_step(cur) >= sizeof *rec);
	C2_PRE(out != NULL);

	C2_ENTRY("lid %llu", (unsigned long long)lid);

	rec = c2_bufvec_cursor_addr(cur);
	C2_ASSERT(layout_rec_invariant(rec, dom));

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
		layout_log("c2_layout_decode", "lto_decode() failed",
			   op == C2_LXO_BUFFER_OP, PRINT_TRACE_MSG,
			   &layout_decode_fail, &layout_global_ctx, lid, rc);
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
	C2_PRE(op == C2_LXO_DB_ADD || op == C2_LXO_DB_UPDATE ||
	       op == C2_LXO_DB_DELETE || op == C2_LXO_BUFFER_OP);
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

	C2_ASSERT(layout_rec_invariant(&rec, l->l_dom));

	lt = l->l_dom->ld_type[l->l_type->lt_id];

	if (op == C2_LXO_DB_UPDATE) {
		/*
		 * Processing the oldrec_cur, to verify that the layout
		 * type and pool id have not changed and then to make it
		 * point to the layout type specific payload.
		 */
		oldrec = c2_bufvec_cursor_addr(oldrec_cur);
		C2_ASSERT(layout_rec_invariant(oldrec, l->l_dom));
		C2_ASSERT(oldrec->lr_lt_id == l->l_type->lt_id &&
			  oldrec->lr_pool_id == l->l_pool_id);
		c2_bufvec_cursor_move(oldrec_cur, sizeof *oldrec);
	}

	nbytes = c2_bufvec_cursor_copyto(out, &rec, sizeof rec);
	C2_ASSERT(nbytes == sizeof rec);

	rc = lt->lt_ops->lto_encode(l, op, tx, oldrec_cur, out);
	if (rc != 0) {
		layout_log("c2_layout_encode", "lto_encode() failed",
			   PRINT_ADDB_MSG, PRINT_TRACE_MSG,
			   &layout_encode_fail, &l->l_addb, l->l_id, rc);
		goto out;
	}

out:
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
