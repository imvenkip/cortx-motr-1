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

/* todo revise the c2_layout_schema related documentation. */
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
 *   - While increasing/decreasing references on the layout types and enum
 *     types through layout_type_get(), layout_type_put(), enum_type_get()
 *     and enum_type_put().
 * - c2_layout_schema::ls_lock is held during the following operations:
 *   - Part of the layout type and enum type registration and unregistration
 *     routines those deal with creating and deleting various DB tables.
 *   - c2_layout_lookup(), c2_layout_add(), c2_layout_update(),
 *     c2_layout_delete().
 *
 * - Note: Having two separate locks for domain data and schema data helps
 *   to keep the locking separate for the in-memory structures and for the
 *   on-disk DB tables. It helps avoid conditional locking in the routines
 *   c2_layout__init(), layout_type_get() and enum_type_get() which need to
 *   lock the in-memory domain structure. If we had only one lock, that
 *   would have been acquired in c2_layout_lookup() (along with being
 *   acquired in c2_layout_add(), c2_layout_update(), and c2_layout_delete()),
 *   and then c2_layout__init(), layout_type_get() and enum_type_get()
 *   would have required conditional locking - lock only when
 *   c2_layout_decode() is invoked by an external user and not when it is
 *   invoked by c2_layout_lookup().
 *   todo Revise the locking related dfocumentation.
 */

#include "lib/errno.h"
#include "lib/memory.h" /* C2_ALLOC_PTR() */
#include "lib/misc.h"   /* strlen(), C2_IN() */
#include "lib/vec.h"    /* c2_bufvec_cursor_step(), c2_bufvec_cursor_addr() */
#include "lib/bob.h"
#include "lib/finject.h"

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
	LAYOUT_MAGIC           = 0x4C41594F55544D41, /* LAYOUTMA */
	LAYOUT_ENUM_MAGIC      = 0x454E554D4D414749, /* ENUMMAGI */
	LAYOUT_LIST_HEAD_MAGIC = 0x4C484541444D4147  /* LHEADMAG */
};

static const struct c2_bob_type layout_bob = {
	.bt_name         = "layout",
	.bt_magix_offset = offsetof(struct c2_layout, l_magic),
	.bt_magix        = LAYOUT_MAGIC,
	.bt_check        = NULL
};
C2_BOB_DEFINE(static, &layout_bob, c2_layout);

static const struct c2_bob_type enum_bob = {
	.bt_name         = "enum",
	.bt_magix_offset = offsetof(struct c2_layout_enum, le_magic),
	.bt_magix        = LAYOUT_ENUM_MAGIC,
	.bt_check        = NULL
};
C2_BOB_DEFINE(static, &enum_bob, c2_layout_enum);

/** ADDB instrumentation for layout. */
static const struct c2_addb_ctx_type layout_addb_ctx_type = {
	.act_name = "layout"
};

const struct c2_addb_loc layout_addb_loc = {
	.al_name = "layout"
};

struct c2_addb_ctx layout_global_ctx = {
	.ac_type   = &layout_addb_ctx_type,
	.ac_parent = &c2_addb_global_ctx
};

C2_ADDB_EV_DEFINE(layout_decode_fail, "layout_decode_fail",
		  C2_ADDB_EVENT_LAYOUT_DECODE_FAIL, C2_ADDB_CALL);
C2_ADDB_EV_DEFINE(layout_encode_fail, "layout_encode_fail",
		  C2_ADDB_EVENT_LAYOUT_ENCODE_FAIL, C2_ADDB_CALL);

C2_ADDB_EV_DEFINE(layout_lookup_fail, "layout_lookup_fail",
		  C2_ADDB_EVENT_LAYOUT_LOOKUP_FAIL, C2_ADDB_CALL);
C2_ADDB_EV_DEFINE(layout_add_fail, "layout_add_fail",
		  C2_ADDB_EVENT_LAYOUT_ADD_FAIL, C2_ADDB_CALL);
C2_ADDB_EV_DEFINE(layout_update_fail, "layout_update_fail",
		  C2_ADDB_EVENT_LAYOUT_UPDATE_FAIL, C2_ADDB_CALL);
C2_ADDB_EV_DEFINE(layout_delete_fail, "layout_delete_fail",
		  C2_ADDB_EVENT_LAYOUT_DELETE_FAIL, C2_ADDB_CALL);

C2_TL_DESCR_DEFINE(layout, "layout-list", static,
		   struct c2_layout, l_list_linkage, l_magic,
		   LAYOUT_MAGIC, LAYOUT_LIST_HEAD_MAGIC);
C2_TL_DEFINE(layout, static, struct c2_layout);

bool c2_layout__domain_invariant(const struct c2_layout_domain *dom)
{
	return
		dom != NULL &&
		dom->ld_dbenv != NULL;
}

static bool layout_invariant_internal(const struct c2_layout *l)
{
	return
		c2_layout_bob_check(l) &&
		l->l_id != LID_NONE &&
		l->l_type != NULL &&
		l->l_dom->ld_type[l->l_type->lt_id] == l->l_type &&
		c2_layout__domain_invariant(l->l_dom) &&
		l->l_ops != NULL;
}

bool c2_layout__allocated_invariant(const struct c2_layout *l)
{
	return
		layout_invariant_internal(l) &&
		l->l_ref == 0;
}

bool c2_layout__invariant(const struct c2_layout *l)
{
	return
		layout_invariant_internal(l) &&
		l->l_ref >= 0;
}

bool c2_layout__enum_invariant(const struct c2_layout_enum *e)
{
	return
		c2_layout_enum_bob_check(e) &&
		e->le_type != NULL &&
		e->le_sl != NULL &&
		e->le_ops != NULL;
}

bool
c2_layout__striped_allocated_invariant(const struct c2_striped_layout *stl)
{
	return
		stl != NULL &&
		stl->sl_enum == NULL &&
		c2_layout__allocated_invariant(&stl->sl_base);
}

bool c2_layout__striped_invariant(const struct c2_striped_layout *stl)
{
	return
		stl != NULL &&
		c2_layout__enum_invariant(stl->sl_enum) &&
		c2_layout__invariant(&stl->sl_base);
}

/** Adds a reference to the layout type. */
static void layout_type_get(struct c2_layout_type *lt)
{
	C2_PRE(lt != NULL);

	c2_mutex_lock(&lt->lt_domain->ld_lock);
	C2_PRE(lt == lt->lt_domain->ld_type[lt->lt_id]);
	C2_CNT_INC(lt->lt_ref_count);
	c2_mutex_unlock(&lt->lt_domain->ld_lock);
}

/** Releases a reference on the layout type. */
static void layout_type_put(struct c2_layout_type *lt)
{
	C2_PRE(lt != NULL);

	c2_mutex_lock(&lt->lt_domain->ld_lock);
	C2_PRE(lt == lt->lt_domain->ld_type[lt->lt_id]);
	C2_CNT_DEC(lt->lt_ref_count);
	c2_mutex_unlock(&lt->lt_domain->ld_lock);
}

/** Adds a reference on the enum type. */
static void enum_type_get(struct c2_layout_enum_type *let)
{
	C2_PRE(let != NULL);

	c2_mutex_lock(&let->let_domain->ld_lock);
	C2_PRE(let == let->let_domain->ld_enum[let->let_id]);
	C2_CNT_INC(let->let_ref_count);
	c2_mutex_unlock(&let->let_domain->ld_lock);
}

/** Releases a reference on the enum type. */
static void enum_type_put(struct c2_layout_enum_type *let)
{
	C2_PRE(let != NULL);

	c2_mutex_lock(&let->let_domain->ld_lock);
	C2_PRE(let == let->let_domain->ld_enum[let->let_id]);
	C2_CNT_DEC(let->let_ref_count);
	c2_mutex_unlock(&let->let_domain->ld_lock);
}

/**
 * Adds an entry in the layout list, with the specified layout pointer and id.
 */
static void layout_list_add(struct c2_layout *l)
{
	C2_ENTRY("dom %p, lid %llu", l->l_dom, (unsigned long long)l->l_id);
	c2_mutex_lock(&l->l_dom->ld_lock);
	C2_PRE(c2_layout__list_lookup(l->l_dom, l->l_id, false) == NULL);
	layout_tlink_init_at(l, &l->l_dom->ld_layout_list);
	c2_mutex_unlock(&l->l_dom->ld_lock);
	C2_LEAVE("lid %llu", (unsigned long long)l->l_id);
}

/**
 * Looks up for an entry from the layout list, with the specified layout id.
 * If the entry is found, and if the argument ref_increment has its value
 * set to 'true' then it acquires an additional reference on the layout
 * object.
 * @pre c2_mutex_is_locked(&dom->ld_lock).
 * @param ref_increment Once layout with specified lid is found, an additional
 * reference is acquired on it if value of ref_increment is true.
 * @post ergo(l != NULL, c2_layout__invariant(l) && l->l_ref > 0));
 */
struct c2_layout *c2_layout__list_lookup(const struct c2_layout_domain *dom,
					 uint64_t lid,
					 bool ref_increment)
{
	struct c2_layout *l;

	C2_PRE(c2_mutex_is_locked(&dom->ld_lock));

	c2_tl_for(layout, &dom->ld_layout_list, l) {
		C2_ASSERT(c2_layout__invariant(l));
		if (l->l_id == lid)
			break;
	} c2_tl_endfor;

	if (l != NULL && ref_increment) {
		c2_mutex_lock(&l->l_lock);
		C2_CNT_INC(l->l_ref);
		c2_mutex_unlock(&l->l_lock);
	}
	return l;
}

/* Used for assertions. */
static struct c2_layout *list_lookup(struct c2_layout_domain *dom,
				     uint64_t lid)
{
	struct c2_layout *l;

	c2_mutex_lock(&dom->ld_lock);
	l = c2_layout__list_lookup(dom, lid, false);
	c2_mutex_unlock(&dom->ld_lock);
	return l;
}

/** Initialises a layout, adds a reference on the respective layout type. */
void c2_layout__init(struct c2_layout *l,
		     struct c2_layout_domain *dom,
		     uint64_t lid,
		     struct c2_layout_type *lt,
		     const struct c2_layout_ops *ops)
{
	C2_PRE(l != NULL);
	C2_PRE(c2_layout__domain_invariant(dom));
	C2_PRE(lid != LID_NONE);
	C2_PRE(lt != NULL);
	C2_PRE(lt->lt_domain == dom);
	C2_PRE(lt == dom->ld_type[lt->lt_id]);
	C2_PRE(ops != NULL);

	C2_ENTRY("lid %llu, layout-type-id %lu", (unsigned long long)lid,
		 (unsigned long)lt->lt_id);

	l->l_id   = lid;
	l->l_dom  = dom;
	l->l_ref  = 0;
	l->l_ops  = ops;
	l->l_type = lt;

	layout_type_get(lt);
	c2_mutex_init(&l->l_lock);
	c2_addb_ctx_init(&l->l_addb, &layout_addb_ctx_type, &layout_global_ctx);
	c2_layout_bob_init(l);

	C2_POST(c2_layout__allocated_invariant(l));
	C2_LEAVE("lid %llu", (unsigned long long)lid);
}

/**
 * @post c2_layout__list_lookup(l->l_dom, l->l_id, false) == l
 */
void c2_layout__populate(struct c2_layout *l, uint32_t ref_count)
{
	C2_PRE(c2_layout__allocated_invariant(l));
	C2_PRE(ref_count >= 0);

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);
	l->l_ref = ref_count;
	layout_list_add(l);
	C2_POST(c2_layout__invariant(l));
	C2_LEAVE("lid %llu", (unsigned long long)l->l_id);
}

void c2_layout__fini_internal(struct c2_layout *l)
{
	c2_addb_ctx_fini(&l->l_addb);
	c2_mutex_fini(&l->l_lock);
	layout_type_put(l->l_type);
	l->l_type = NULL;
	c2_layout_bob_fini(l);
}

void c2_layout__delete(struct c2_layout *l)
{
	C2_PRE(c2_layout__allocated_invariant(l));
	C2_PRE(list_lookup(l->l_dom, l->l_id) == NULL);
	C2_PRE(c2_mutex_is_not_locked(&l->l_lock));

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);
	c2_layout__fini_internal(l);
	C2_LEAVE();
}

/** Finalises a layout, releases a reference on the respective layout type. */
void c2_layout__fini(struct c2_layout *l)
{
	C2_PRE(c2_layout__invariant(l));
	C2_PRE(list_lookup(l->l_dom, l->l_id) == NULL);

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);
	layout_tlink_fini(l);
	c2_layout__fini_internal(l);
	C2_LEAVE();
}

void c2_layout__striped_init(struct c2_striped_layout *stl,
			     struct c2_layout_domain *dom,
			     uint64_t lid,
			     struct c2_layout_type *type,
			     const struct c2_layout_ops *ops)

{
	C2_PRE(stl != NULL);
	C2_PRE(c2_layout__domain_invariant(dom));
	C2_PRE(lid != LID_NONE);
	C2_PRE(type != NULL);
	C2_PRE(ops != NULL);

	C2_ENTRY("lid %llu", (unsigned long long)lid);
	c2_layout__init(&stl->sl_base, dom, lid, type, ops);
	/* stl->sl_enum will be set through c2_layout__striped_populate(). */
	stl->sl_enum = NULL;
	C2_POST(c2_layout__striped_allocated_invariant(stl));
	C2_LEAVE("lid %llu", (unsigned long long)lid);
}

/**
 * Initialises a striped layout object, using provided enumeration object.
 * @pre The enumeration object e is already initialised by internally elevating
 * reference of the respective enum type.
 * @post Pointer to the c2_layout object is set back in the c2_layout_enum
 * object.
 */
void c2_layout__striped_populate(struct c2_striped_layout *str_l,
				 struct c2_layout_enum *e,
				 uint32_t ref_count)

{
	C2_PRE(c2_layout__striped_allocated_invariant(str_l));
	C2_PRE(e != NULL); //todo enum_allocated_invariant

	C2_ENTRY("lid %llu, enum-type-id %lu",
		 (unsigned long long)str_l->sl_base.l_id,
		 (unsigned long)e->le_type->let_id);
	c2_layout__populate(&str_l->sl_base, ref_count);
	str_l->sl_enum = e;
	str_l->sl_enum->le_sl = str_l;

	/*
	 * c2_layout__enum_invariant() invoked internally from within
	 * c2_layout__striped_invariant() verifies that
	 * str_l->sl_base->le_sl is set appropriately.
	 */
	C2_POST(c2_layout__striped_invariant(str_l));
	//todo enum invariant
	C2_LEAVE("lid %llu", (unsigned long long)str_l->sl_base.l_id);
}

void c2_layout__striped_delete(struct c2_striped_layout *stl)
{
	C2_PRE(c2_layout__striped_allocated_invariant(stl));

	C2_ENTRY("lid %llu", (unsigned long long)stl->sl_base.l_id);
	c2_layout__delete(&stl->sl_base);
	C2_LEAVE("lid %llu", (unsigned long long)stl->sl_base.l_id);
}

/**
 * Finalises a striped layout object.
 * @post The enum object which is part of striped layout object, is finalised
 * as well.
 */
void c2_layout__striped_fini(struct c2_striped_layout *str_l)
{
	C2_PRE(c2_layout__striped_invariant(str_l));

	C2_ENTRY("lid %llu", (unsigned long long)str_l->sl_base.l_id);
	str_l->sl_enum->le_ops->leo_fini(str_l->sl_enum);
	c2_layout__fini(&str_l->sl_base);
	C2_LEAVE("lid %llu", (unsigned long long)str_l->sl_base.l_id);
}

/**
 * Initialises an enumeration object, adds a reference on the respective
 * enum type.
 */
void c2_layout__enum_init(struct c2_layout_domain *dom,
			  struct c2_layout_enum *le,
			  struct c2_layout_enum_type *let,
			  const struct c2_layout_enum_ops *ops)
{
	C2_PRE(c2_layout__domain_invariant(dom));
	C2_PRE(le != NULL);
	C2_PRE(let != NULL);
	C2_PRE(let->let_domain == dom);
	C2_PRE(let == dom->ld_enum[let->let_id]);
	C2_PRE(ops != NULL);

	C2_ENTRY("Enum-type-id %lu", (unsigned long)let->let_id);
	/* le->le_sl will be set through c2_layout__striped_populate(). */
	le->le_sl = NULL;
	le->le_ops = ops;
	enum_type_get(let);
	le->le_type = let;
	c2_layout_enum_bob_init(le);
	C2_LEAVE("Enum-type-id %lu", (unsigned long)let->let_id);
}

/**
 * Finalises an enum object, releases a reference on the respective enum
 * type.
 */
void c2_layout__enum_fini(struct c2_layout_enum *le)
{
	C2_PRE(c2_layout__enum_invariant(le));

	C2_ENTRY("Enum-type-id %lu", (unsigned long)le->le_type->let_id);
	enum_type_put(le->le_type);
	le->le_type = NULL;
	c2_layout_enum_bob_fini(le);
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

c2_bcount_t c2_layout__enum_max_recsize(struct c2_layout_domain *dom)
{
	c2_bcount_t e_recsize;
	c2_bcount_t max_recsize = 0;
	uint32_t    i;

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
 * c2_layout_domain::ld_max_recsize.
 * This function updates c2_layout_domain::ld_max_recsize, by re-calculating it.
 */
static void max_recsize_update(struct c2_layout_domain *dom)
{
	uint32_t    i;
	c2_bcount_t recsize;
	c2_bcount_t max_recsize = 0;

	C2_PRE(c2_layout__domain_invariant(dom));
	C2_PRE(c2_mutex_is_locked(&dom->ld_lock));

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
	dom->ld_max_recsize = sizeof(struct c2_layout_rec) + max_recsize;
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
		C2_ADDB_ADD(ctx, &layout_addb_loc, layout_decode_fail, rc);
		break;
	case C2_ADDB_EVENT_LAYOUT_ENCODE_FAIL:
		C2_ADDB_ADD(ctx, &layout_addb_loc, layout_encode_fail, rc);
		break;
	case C2_ADDB_EVENT_LAYOUT_LOOKUP_FAIL:
		C2_ADDB_ADD(ctx, &layout_addb_loc, layout_lookup_fail, rc);
		break;
	case C2_ADDB_EVENT_LAYOUT_ADD_FAIL:
		C2_ADDB_ADD(ctx, &layout_addb_loc, layout_add_fail, rc);
		break;
	case C2_ADDB_EVENT_LAYOUT_UPDATE_FAIL:
		C2_ADDB_ADD(ctx, &layout_addb_loc, layout_update_fail, rc);
		break;
	case C2_ADDB_EVENT_LAYOUT_DELETE_FAIL:
		C2_ADDB_ADD(ctx, &layout_addb_loc, layout_delete_fail, rc);
		break;
	default:
		C2_ASSERT(0);
	}
}

/**
 * This method performs the following operations:
 * 1) It invokes addb_add() to add an ADDB record.
 * 2) It adds a C2_LOG record (trace record), indicating failure, along with
 *    a short error message string and the error code.
 * 3) Note: For the suceesss cases (indicated by rc == 0), c2_layout__log() is
 *    never invoked since:
 *    (i) ADDB records are not expected to be added in success cases.
 *    (ii) C2_LEAVE() or C2_LOG() are used directly to log the trace records,
 *         avoiding the function call overhead.
 */
void c2_layout__log(const char *fn_name,
		    const char *err_msg,
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
	addb_add(ctx, ev, err_msg, rc);

	/* Trace record logging. */
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

int c2_layout_domain_init(struct c2_layout_domain *dom, struct c2_dbenv *dbenv)
{
	int rc;

	C2_PRE(dom != NULL);
	C2_PRE(dbenv != NULL);

	C2_SET0(dom);
	layout_tlist_init(&dom->ld_layout_list);
	dom->ld_dbenv = dbenv;
	rc = c2_table_init(&dom->ld_layouts, dom->ld_dbenv, "layouts",
			   DEFAULT_DB_FLAG, &layouts_table_ops);
	if (rc != 0) {
		c2_layout__log("c2_layout_domain_init",
			       "c2_table_init() failed",
			       &c2_addb_func_fail, &layout_global_ctx,
			       LID_NONE, rc);
		dom->ld_dbenv = NULL;
		return rc;
	}

	c2_mutex_init(&dom->ld_lock);
	C2_POST(c2_layout__domain_invariant(dom));
	return rc;
}

void c2_layout_domain_fini(struct c2_layout_domain *dom)
{
	C2_PRE(c2_layout__domain_invariant(dom));
	C2_PRE(c2_mutex_is_not_locked(&dom->ld_lock));
	/*
	 * Verify that all the layout objects belonging to this domain have
	 * been finalised.
	 */
	C2_PRE(layout_tlist_is_empty(&dom->ld_layout_list));

	/* Verify that all the layout types are unregistered. */
	C2_PRE(c2_forall(i, ARRAY_SIZE(dom->ld_type),
	       dom->ld_type[i] == NULL));

	/* Verify that all the enum types are unregistered. */
	C2_PRE(c2_forall(i, ARRAY_SIZE(dom->ld_enum),
	       dom->ld_enum[i] == NULL));

	c2_mutex_fini(&dom->ld_lock);
	c2_table_fini(&dom->ld_layouts);
	dom->ld_dbenv = NULL;
	layout_tlist_fini(&dom->ld_layout_list);
}

int c2_layout_all_types_register(struct c2_layout_domain *dom)
{
	int rc;

	C2_PRE(c2_layout__domain_invariant(dom));

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

void c2_layout_all_types_unregister(struct c2_layout_domain *dom)
{
	C2_PRE(c2_layout__domain_invariant(dom));

	c2_layout_enum_type_unregister(dom, &c2_list_enum_type);
	c2_layout_enum_type_unregister(dom, &c2_linear_enum_type);
	c2_layout_type_unregister(dom, &c2_pdclust_layout_type);
}

int c2_layout_type_register(struct c2_layout_domain *dom,
			    struct c2_layout_type *lt)
{
	int rc;

	C2_PRE(c2_layout__domain_invariant(dom));
	C2_PRE(lt != NULL);
	C2_PRE(IS_IN_ARRAY(lt->lt_id, dom->ld_type));
	C2_PRE(lt->lt_domain == NULL);
	C2_PRE(lt->lt_ref_count == 0);
	C2_PRE(lt->lt_ops != NULL);

	C2_ENTRY("Layout-type-id %lu, domain %p",
		 (unsigned long)lt->lt_id, dom);
	c2_mutex_lock(&dom->ld_lock);
	C2_PRE(dom->ld_type[lt->lt_id] == NULL);
	dom->ld_type[lt->lt_id] = lt;

	/* Allocate type specific schema data. */
	rc = lt->lt_ops->lto_register(dom, lt);
	if (rc == 0) {
		max_recsize_update(dom);
		lt->lt_domain = dom;
	} else {
		c2_layout__log("c2_layout_type_register",
			       "lto_register() failed",
			       &c2_addb_func_fail, &layout_global_ctx,
			       LID_NONE, rc);
		dom->ld_type[lt->lt_id] = NULL;
	}
	c2_mutex_unlock(&dom->ld_lock);
	C2_LEAVE("Layout-type-id %lu, rc %d", (unsigned long)lt->lt_id, rc);
	return rc;
}

void c2_layout_type_unregister(struct c2_layout_domain *dom,
			       struct c2_layout_type *lt)
{
	C2_PRE(c2_layout__domain_invariant(dom));
	C2_PRE(lt != NULL);
	C2_PRE(dom->ld_type[lt->lt_id] == lt); /* Registered layout type */
	C2_PRE(lt->lt_domain == dom);
	C2_PRE(lt->lt_ops != NULL);

	C2_ENTRY("Layout-type-id %lu, lt_domain %p",
		 (unsigned long)lt->lt_id, lt->lt_domain);
	c2_mutex_lock(&dom->ld_lock);
	C2_PRE(lt->lt_ref_count == 0);
	lt->lt_ops->lto_unregister(dom, lt);
	dom->ld_type[lt->lt_id] = NULL;
	max_recsize_update(dom);
	lt->lt_domain = NULL;
	c2_mutex_unlock(&dom->ld_lock);
	C2_LEAVE("Layout-type-id %lu", (unsigned long)lt->lt_id);
}

int c2_layout_enum_type_register(struct c2_layout_domain *dom,
				 struct c2_layout_enum_type *let)
{
	int rc;

	C2_PRE(c2_layout__domain_invariant(dom));
	C2_PRE(let != NULL);
	C2_PRE(IS_IN_ARRAY(let->let_id, dom->ld_enum));
	C2_PRE(let->let_domain == NULL);
	C2_PRE(let->let_ref_count == 0);
	C2_PRE(let->let_ops != NULL);

	C2_ENTRY("Enum_type_id %lu, domain %p",
		 (unsigned long)let->let_id, dom);
	c2_mutex_lock(&dom->ld_lock);
	C2_PRE(dom->ld_enum[let->let_id] == NULL);
	dom->ld_enum[let->let_id] = let;

	/* Allocate enum type specific schema data. */
	rc = let->let_ops->leto_register(dom, let);
	if (rc == 0) {
		max_recsize_update(dom);
		let->let_domain = dom;
	} else {
		c2_layout__log("c2_layout_enum_type_register",
			       "leto_register() failed",
			       &c2_addb_func_fail, &layout_global_ctx,
			       LID_NONE, rc);
		dom->ld_enum[let->let_id] = NULL;
	}
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
	C2_PRE(c2_layout__domain_invariant(dom));
	C2_PRE(let != NULL);
	C2_PRE(dom->ld_enum[let->let_id] == let); /* Registered enum type */
	C2_PRE(let->let_domain != NULL);

	C2_ENTRY("Enum_type_id %lu, let_domain %p",
		 (unsigned long)let->let_id, let->let_domain);
	c2_mutex_lock(&dom->ld_lock);
	C2_PRE(let->let_ref_count == 0);
	let->let_ops->leto_unregister(dom, let);
	dom->ld_enum[let->let_id] = NULL;
	max_recsize_update(dom);
	let->let_domain = NULL;
	c2_mutex_unlock(&dom->ld_lock);
	C2_LEAVE("Enum_type_id %lu", (unsigned long)let->let_id);
}

/* todo If c2_layout_lookup() first looks into the layout list, do we need
 * c2_layout_find() to exist ?
 */
struct c2_layout *c2_layout_find(struct c2_layout_domain *dom, uint64_t lid)
{
	struct c2_layout *l;

	C2_PRE(c2_layout__domain_invariant(dom));
	C2_PRE(lid != LID_NONE);

	C2_ENTRY("lid %llu", (unsigned long long)lid);
	c2_mutex_lock(&dom->ld_lock);
	l = c2_layout__list_lookup(dom, lid, true);
	c2_mutex_unlock(&dom->ld_lock);

	C2_POST(ergo(l != NULL, c2_layout__invariant(l) &&
				l->l_ref > 0));
	C2_LEAVE("lid %llu, l_pointer %p", (unsigned long long)lid, l);
	return l;
}

/** Adds a reference to the layout. */
void c2_layout_get(struct c2_layout *l)
{
	C2_PRE(c2_layout__invariant(l));

	C2_ENTRY("lid %llu, ref_count %lu", (unsigned long long)l->l_id,
		 (unsigned long)l->l_ref);
	c2_mutex_lock(&l->l_lock);
	C2_PRE(list_lookup(l->l_dom, l->l_id) == l);
	C2_CNT_INC(l->l_ref);
	c2_mutex_unlock(&l->l_lock);
	C2_LEAVE("lid %llu", (unsigned long long)l->l_id);
}

void c2_layout_put(struct c2_layout *l)
{
	bool killme;

	C2_PRE(c2_layout__invariant(l));

	C2_ENTRY("lid %llu, ref_count %lu", (unsigned long long)l->l_id,
		 (unsigned long)l->l_ref);
	c2_mutex_lock(&l->l_dom->ld_lock);
	c2_mutex_lock(&l->l_lock);
	C2_CNT_DEC(l->l_ref);
	killme = l->l_ref == 0;
	/* The layout should not be found anymore using c2_layout_find(). */
	if (killme)
		layout_tlist_del(l);

	c2_mutex_unlock(&l->l_lock);
	c2_mutex_unlock(&l->l_dom->ld_lock);

	if (killme)
		l->l_ops->lo_fini(l);
	C2_LEAVE();
}

int c2_layout_decode(struct c2_layout *l,
		     enum c2_layout_xcode_op op,
		     struct c2_db_tx *tx,
		     struct c2_bufvec_cursor *cur)
{
	struct c2_layout_type *lt;
	struct c2_layout_rec  *rec;
	int                    rc;

	C2_PRE(C2_IN(op, (C2_LXO_DB_LOOKUP, C2_LXO_BUFFER_OP)));
	C2_PRE(ergo(op == C2_LXO_DB_LOOKUP, tx != NULL));
	C2_PRE(cur != NULL);
	C2_PRE(c2_bufvec_cursor_step(cur) >= sizeof *rec);
	C2_PRE(c2_layout__allocated_invariant(l));
	C2_PRE(c2_mutex_is_locked(&l->l_lock));
	C2_PRE(list_lookup(l->l_dom, l->l_id) == NULL);

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);
	if (C2_FI_ENABLED("c2_l_decode_error_in_c2_l_lookup"))
		return -501;

	lt = l->l_type;
	C2_ASSERT(lt == l->l_dom->ld_type[lt->lt_id]);

	rec = c2_bufvec_cursor_addr(cur);
	C2_ASSERT(rec->lr_lt_id == lt->lt_id);

	/* Move the cursor to point to the layout type specific payload. */
	c2_bufvec_cursor_move(cur, sizeof *rec);
	/*
	 * It is fine if any of the layout does not contain any data in
	 * rec->lr_data[], unless it is required by the specific layout type,
	 * which will be caught by the respective lto_decode() implementation.
	 * Hence, ignoring the return status of c2_bufvec_cursor_move() here.
	 */

	rc = lt->lt_ops->lto_decode(l, op, tx, cur, rec->lr_ref_count);
	if (rc != 0) {
		c2_layout__log("c2_layout_decode", "lto_decode() failed",
			       &layout_decode_fail, &layout_global_ctx,
			       l->l_id, rc);
		goto out;
	}
	C2_POST(c2_layout__invariant(l));
	C2_POST(c2_mutex_is_locked(&l->l_lock));
	C2_POST(list_lookup(l->l_dom, l->l_id) == l);
out:
	C2_LEAVE("lid %llu, rc %d", (unsigned long long)l->l_id, rc);
	return rc;
}

int c2_layout_encode(struct c2_layout *l,
		     enum c2_layout_xcode_op op,
		     struct c2_db_tx *tx,
		     struct c2_bufvec_cursor *out)
{
	struct c2_layout_rec   rec;
	struct c2_layout_type *lt;
	c2_bcount_t            nbytes;
	int                    rc;

	C2_PRE(c2_layout__invariant(l));
	C2_PRE(C2_IN(op, (C2_LXO_DB_ADD, C2_LXO_DB_UPDATE,
			  C2_LXO_DB_DELETE, C2_LXO_BUFFER_OP)));
	C2_PRE(ergo(op != C2_LXO_BUFFER_OP, tx != NULL));
	C2_PRE(out != NULL);
	C2_PRE(c2_bufvec_cursor_step(out) >= sizeof rec);

	C2_ENTRY("lid %llu", (unsigned long long)l->l_id);
	lt = l->l_type;
	C2_ASSERT(lt == l->l_dom->ld_type[lt->lt_id]);

	rec.lr_lt_id     = l->l_type->lt_id;
	rec.lr_ref_count = l->l_ref;
	nbytes = c2_bufvec_cursor_copyto(out, &rec, sizeof rec);
	C2_ASSERT(nbytes == sizeof rec);

	rc = lt->lt_ops->lto_encode(l, op, tx, out);
	if (rc != 0)
		c2_layout__log("c2_layout_encode", "lto_encode() failed",
			       &layout_encode_fail, &l->l_addb, l->l_id, rc);

	C2_LEAVE("lid %llu, rc %d", (unsigned long long)l->l_id, rc);
	return rc;
}

c2_bcount_t c2_layout_max_recsize(const struct c2_layout_domain *dom)
{
	C2_PRE(c2_layout__domain_invariant(dom));
	return dom->ld_max_recsize;
}

struct c2_striped_layout *c2_layout_to_striped(const struct c2_layout *l)
{
	struct c2_striped_layout *stl;

	C2_PRE(c2_layout__invariant(l));
	stl = bob_of(l, struct c2_striped_layout, sl_base, &layout_bob);
	C2_ASSERT(c2_layout__striped_invariant(stl));
	return stl;
}

struct c2_layout_enum
*c2_striped_layout_to_enum(const struct c2_striped_layout *stl)
{
	C2_PRE(c2_layout__striped_invariant(stl));
	return stl->sl_enum;
}

struct c2_layout_enum *c2_layout_to_enum(const struct c2_layout *l)
{
	struct c2_striped_layout *stl;

	C2_PRE(l != NULL);
	stl = bob_of(l, struct c2_striped_layout, sl_base, &layout_bob);
	C2_ASSERT(c2_layout__striped_invariant(stl));
	return stl->sl_enum;
}

uint32_t c2_layout_enum_nr(const struct c2_layout_enum *e)
{
	C2_PRE(c2_layout__enum_invariant(e));
	return e->le_ops->leo_nr(e);
}

void c2_layout_enum_get(const struct c2_layout_enum *e,
			uint32_t idx,
			const struct c2_fid *gfid,
			struct c2_fid *out)
{
	C2_PRE(c2_layout__enum_invariant(e));
	e->le_ops->leo_get(e, idx, gfid, out);
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
