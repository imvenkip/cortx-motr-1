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
 * Original creation date: 12/21/2011
 */

#include "lib/ut.h"
#include "lib/ub.h"
#include "lib/memory.h"
#include "lib/misc.h"                    /* C2_SET0 */
#include "lib/bitstring.h"
#include "lib/vec.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_LAYOUT
#include "lib/trace.h"                   /* C2_LOG */

#ifdef __KERNEL__
# include "c2t1fs/linux_kernel/c2t1fs.h" /* c2t1fs_globals */
#endif

#include "lib/finject.h"

#include "pool/pool.h"                   /* c2_pool_init(), c2_pool_fini() */
#include "fid/fid.h"                     /* c2_fid_set() */
#include "layout/layout.h"
#include "layout/layout_internal.h"      /* LDB_MAX_INLINE_COB_ENTRIES, *_ERR */
#include "layout/layout_db.h"
#include "layout/pdclust.h"
#include "layout/list_enum.h"
#include "layout/linear_enum.h"
#include "layout/ut/ldemo_internal.c"    /* layout_demo() */

static struct c2_dbenv         dbenv;
static const char              db_name[] = "ut-layout";
static struct c2_layout_domain domain;
static struct c2_pool          pool;
enum c2_addb_ev_level          orig_addb_level;
static int                     rc;

enum {
	DBFLAGS                  = 0,    /* Flag used for dbenv and tx init */
	LIST_ENUM_ID             = 0x4C495354, /* "LIST" */
	LINEAR_ENUM_ID           = 0x4C494E45, /* "LINE" */
	ADDITIONAL_BYTES_NONE    = 0,    /* For buffer initialisation */
	ADDITIONAL_BYTES_DEFAULT = 2048, /* For buffer initialisation */
	INLINE_NOT_APPLICABLE    = 0,    /* For list enumeration */
	LESS_THAN_INLINE         = 1,    /* For list enumeration */
	EXACT_INLINE             = 2,    /* For list enumeration */
	MORE_THAN_INLINE         = 3,    /* For list enumeration */
	EXISTING_TEST            = true, /* Add a layout to the DB */
	DUPLICATE_TEST           = true, /* Try to re-add a layout */
	FAILURE_TEST             = true, /* Failure injected */
	LAYOUT_DESTROY           = true, /* Delete layout object */
	UNIT_SIZE                = 4096  /* For pdclust layout type */
};

extern struct c2_layout_type c2_pdclust_layout_type;
extern struct c2_layout_enum_type c2_list_enum_type;
extern struct c2_layout_enum_type c2_linear_enum_type;

static int test_init(void)
{
	/*
	 * Note: In test_init() and test_fini(), need to use C2_ASSERT()
	 * as against C2_UT_ASSERT().
	 */

	/*
	 * Store the original addb level before changing it and change it to
	 * AEL_WARN.
	 * Note: This is a provision to avoid recompiling the whole ADDB module,
	 * when interested in ADDB messages only for the LAYOUT module.
	 * Just changing the level to AEL_NONE here and recompiling the LAYOUT
	 * module serves the purpose in that case.
	 */
	orig_addb_level = c2_addb_choose_default_level_console(AEL_WARN);

#ifndef __KERNEL__
	c2_ut_db_reset(db_name);
#endif

	rc = c2_dbenv_init(&dbenv, db_name, DBFLAGS);
	C2_ASSERT(rc == 0);

	/* Initialise the domain. */
	rc = c2_layout_domain_init(&domain, &dbenv);
	C2_ASSERT(rc == 0);

#ifdef __KERNEL__
	/*
	 * A layout type can be registered with only one domain at a time.
	 * As a part of the kernel UT, all the available layout types and enum
	 * types have been registered with the domain
	 * "c2t1fs_globals.g_layout_dom".
	 * (This happpens during the module load operation, by performing
	 * c2_layout_standard_types_register(&c2t1fs_globals.g_layout_dom)
	 * through c2t1fs_init()). Hence, performing
	 * c2_layout_standard_types_unregister(&c2t1fs_globals.g_layout_dom)
	 * here to temporarily unregister all the available layout types and
	 * enum types from the domain "c2t1fs_globals.g_layout_dom". Those will
	 * be registered back in test_fini().
	 */
	c2_layout_standard_types_unregister(&c2t1fs_globals.g_layout_dom);
#endif

	/* Register all the standard layout types and enum types. */
	rc = c2_layout_standard_types_register(&domain);
	C2_ASSERT(rc == 0);

	return rc;
}

static int test_fini(void)
{
	c2_layout_standard_types_unregister(&domain);

#ifdef __KERNEL__
	c2_layout_standard_types_register(&c2t1fs_globals.g_layout_dom);
#endif

	c2_layout_domain_fini(&domain);
	c2_dbenv_fini(&dbenv);

	/* Restore the original addb level. */
	c2_addb_choose_default_level_console(orig_addb_level);

	return 0;
}

static void test_domain_init_fini(void)
{
	const char              t_db_name[] = "t1-layout";
	struct c2_layout_domain t_domain;
	struct c2_dbenv         t_dbenv;

	C2_ENTRY();

	rc = c2_dbenv_init(&t_dbenv, t_db_name, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	/* Initialise the domain. */
	rc = c2_layout_domain_init(&t_domain, &t_dbenv);
	C2_UT_ASSERT(rc == 0);

	/* Finalise the domain. */
	c2_layout_domain_fini(&t_domain);

	/* Should be able to initialise the domain again after finalising it. */
	rc = c2_layout_domain_init(&t_domain, &t_dbenv);
	C2_UT_ASSERT(rc == 0);

	/* Finalise the domain. */
	c2_layout_domain_fini(&t_domain);

	c2_dbenv_fini(&t_dbenv);

	C2_LEAVE();
}

static void test_domain_init_fini_failure(void)
{
	const char              t_db_name[] = "t2-layout";
	struct c2_layout_domain t_domain;
	struct c2_dbenv         t_dbenv;

	C2_ENTRY();
	rc = c2_dbenv_init(&t_dbenv, t_db_name, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	c2_fi_enable_once("c2_layout_domain_init", "table_init_err");
	rc = c2_layout_domain_init(&t_domain, &t_dbenv);
	C2_UT_ASSERT(rc == L_TABLE_INIT_ERR);

	c2_dbenv_fini(&t_dbenv);
	C2_LEAVE();
}

static int t_register(struct c2_layout_domain *dom,
		      const struct c2_layout_type *lt)
{
	return 0;
}

static void t_unregister(struct c2_layout_domain *dom,
			 const struct c2_layout_type *lt)
{
}

static c2_bcount_t t_max_recsize(struct c2_layout_domain *dom)
{
	return 0;
}

static const struct c2_layout_type_ops test_layout_type_ops = {
	.lto_register    = t_register,
	.lto_unregister  = t_unregister,
	.lto_max_recsize = t_max_recsize
};

struct c2_layout_type test_layout_type = {
	.lt_name     = "test",
	.lt_id       = 2,
	.lt_domain   = NULL,
	.lt_ops      = &test_layout_type_ops
};

static void test_type_reg_unreg(void)
{
	C2_ENTRY();

	/* Register a layout type. */
	rc = c2_layout_type_register(&domain, &test_layout_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(domain.ld_type[test_layout_type.lt_id] ==
		     &test_layout_type);

	/* Unregister it. */
	c2_layout_type_unregister(&domain, &test_layout_type);
	C2_UT_ASSERT(domain.ld_type[test_layout_type.lt_id] == NULL);

	C2_LEAVE();
}

static int t_enum_register(struct c2_layout_domain *dom,
			   const struct c2_layout_enum_type *et)
{
	return 0;
}

static void t_enum_unregister(struct c2_layout_domain *dom,
			      const struct c2_layout_enum_type *et)
{
}

static c2_bcount_t t_enum_max_recsize(void)
{
	return 0;
}

static const struct c2_layout_enum_type_ops test_enum_ops = {
	.leto_register    = t_enum_register,
	.leto_unregister  = t_enum_unregister,
	.leto_max_recsize = t_enum_max_recsize
};

struct c2_layout_enum_type test_enum_type = {
	.let_name = "test",
	.let_id   = 2,
	.let_ops  = &test_enum_ops
};

static void test_etype_reg_unreg(void)
{
	C2_ENTRY();

	/* Register a layout enum type. */
	rc = c2_layout_enum_type_register(&domain, &test_enum_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(domain.ld_enum[test_enum_type.let_id] ==
		     &test_enum_type);

	/* Unregister it. */
	c2_layout_enum_type_unregister(&domain, &test_enum_type);
	C2_UT_ASSERT(domain.ld_enum[test_enum_type.let_id] == NULL);

	C2_LEAVE();
}

static void test_reg_unreg(void)
{
	const char              t_db_name[] = "t3-layout";
	struct c2_dbenv         t_dbenv;
	struct c2_layout_domain t_domain;

	C2_ENTRY();

	/*
	 * A layout type can be registered with only one domain at a time.
	 * Hence, unregister all the available layout types and enum types from
	 * the domain "domain", which are registered through test_init().
	 * This also covers the test of registering with one domain,
	 * unregistering from that domain and then registering with another
	 * domain.
	 */
	c2_layout_standard_types_unregister(&domain);

	rc = c2_dbenv_init(&t_dbenv, t_db_name, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	/* Initialise the domain. */
	rc = c2_layout_domain_init(&t_domain, &t_dbenv);
	C2_UT_ASSERT(rc == 0);

	/* Register all the available layout types and enum types. */
	rc = c2_layout_standard_types_register(&t_domain);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(t_domain.ld_enum[c2_list_enum_type.let_id] ==
		     &c2_list_enum_type);
	C2_UT_ASSERT(t_domain.ld_enum[c2_linear_enum_type.let_id] ==
		     &c2_linear_enum_type);
	C2_UT_ASSERT(t_domain.ld_type[c2_pdclust_layout_type.lt_id] ==
		     &c2_pdclust_layout_type);

	/* Unregister all the registered layout and enum types. */
	c2_layout_standard_types_unregister(&t_domain);
	C2_UT_ASSERT(t_domain.ld_enum[c2_list_enum_type.let_id] == NULL);
	C2_UT_ASSERT(t_domain.ld_enum[c2_linear_enum_type.let_id] == NULL);
	C2_UT_ASSERT(t_domain.ld_type[c2_pdclust_layout_type.lt_id] == NULL);

	/*
	 * Should be able to register all the available layout types and enum
	 * types, again after unregistering those.
	 */
	rc = c2_layout_standard_types_register(&t_domain);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(t_domain.ld_enum[c2_list_enum_type.let_id] ==
		     &c2_list_enum_type);
	C2_UT_ASSERT(t_domain.ld_enum[c2_linear_enum_type.let_id] ==
		     &c2_linear_enum_type);
	C2_UT_ASSERT(t_domain.ld_type[c2_pdclust_layout_type.lt_id] ==
		     &c2_pdclust_layout_type);

	/* Unregister all the registered layout and enum types. */
	c2_layout_standard_types_unregister(&t_domain);
	C2_UT_ASSERT(t_domain.ld_enum[c2_list_enum_type.let_id] == NULL);
	C2_UT_ASSERT(t_domain.ld_enum[c2_linear_enum_type.let_id] == NULL);
	C2_UT_ASSERT(t_domain.ld_type[c2_pdclust_layout_type.lt_id] == NULL);

	/* Finalise the domain. */
	c2_layout_domain_fini(&t_domain);

	c2_dbenv_fini(&t_dbenv);

	/*
	 * Register back all the available layout types and enum types with
	 * the domain "domain", to undo the change done at the beginning of
	 * this function.
	 */
	rc = c2_layout_standard_types_register(&domain);
	C2_ASSERT(rc == 0);

	C2_LEAVE();
}

static void test_reg_unreg_failure(void)
{
	const char              t_db_name[] = "t4-layout";
	struct c2_dbenv         t_dbenv;
	struct c2_layout_domain t_domain;

	C2_ENTRY();

	/*
	 * A layout type can be registered with only one domain at a time.
	 * Hence, unregister all the available layout types and enum types from
	 * the domain "domain", which are registered through test_init().
	 * This also covers the test of registering with one domain,
	 * unregistering from that domain and then registering with another
	 * domain.
	 */
	c2_layout_standard_types_unregister(&domain);

	rc = c2_dbenv_init(&t_dbenv, t_db_name, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	/* Initialise the domain. */
	rc = c2_layout_domain_init(&t_domain, &t_dbenv);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Try to register all the standard layout types and enum types by
	 * injecting errors.
	 */
	c2_fi_enable_once("c2_layout_type_register", "lto_reg_err");
	rc = c2_layout_type_register(&t_domain, &c2_pdclust_layout_type);
	C2_UT_ASSERT(rc == LTO_REG_ERR);

	c2_fi_enable_once("c2_layout_enum_type_register", "leto_reg_err");
	rc = c2_layout_enum_type_register(&t_domain, &c2_list_enum_type);
	C2_UT_ASSERT(rc == LETO_REG_ERR);

	c2_fi_enable_once("c2_layout_enum_type_register", "leto_reg_err");
	rc = c2_layout_enum_type_register(&t_domain, &c2_linear_enum_type);
	C2_UT_ASSERT(rc == LETO_REG_ERR);

	c2_fi_enable_once("list_register", "mem_err");
	rc = c2_layout_enum_type_register(&t_domain, &c2_list_enum_type);
	C2_UT_ASSERT(rc == -ENOMEM);

	c2_fi_enable_once("list_register", "table_init_err");
	rc = c2_layout_enum_type_register(&t_domain, &c2_list_enum_type);
	C2_UT_ASSERT(rc == -EEXIST);

	/*
	 * Now cover all the error cases from
	 * c2_layout_standard_types_register().
	 */
	c2_fi_enable_once("c2_layout_type_register", "lto_reg_err");
	rc = c2_layout_standard_types_register(&t_domain);
	C2_UT_ASSERT(rc == LTO_REG_ERR);

	c2_fi_enable_once("c2_layout_enum_type_register", "leto_reg_err");
	rc = c2_layout_standard_types_register(&t_domain);
	C2_UT_ASSERT(rc == LETO_REG_ERR);

	c2_fi_enable_off_n_on_m("c2_layout_enum_type_register", "leto_reg_err",
				1, 1);
	rc = c2_layout_standard_types_register(&t_domain);
	C2_UT_ASSERT(rc == LETO_REG_ERR);
	c2_fi_disable("c2_layout_enum_type_register", "leto_reg_err");

	c2_layout_domain_fini(&t_domain);
	c2_dbenv_fini(&t_dbenv);

	/*
	 * Register back all the available layout types and enum types with
	 * the domain "domain", to undo the change done at the beginning of
	 * this function.
	 */
	rc = c2_layout_standard_types_register(&domain);
	C2_ASSERT(rc == 0);

	C2_LEAVE();
}

static struct c2_layout *list_lookup(uint64_t lid)
{
	struct c2_layout *l;

	c2_mutex_lock(&domain.ld_lock);
	l = c2_layout__list_lookup(&domain, lid, false);
	c2_mutex_unlock(&domain.ld_lock);
	return l;
}

/*
 * Builds a layout object with PDCLUST layout type and using the provided
 * enumeration object.
 */
static int pdclust_l_build(uint64_t lid, uint32_t N, uint32_t K, uint32_t P,
			   struct c2_uint128 *seed,
			   struct c2_layout_enum *le,
			   struct c2_pdclust_layout **pl,
			   bool failure_test)
{
	struct c2_layout_type  *lt;
	struct c2_pdclust_attr  attr;

	lt = &c2_pdclust_layout_type;

	C2_UT_ASSERT(le != NULL);
	C2_UT_ASSERT(pl != NULL);

	attr.pa_N         = N;
	attr.pa_K         = K;
	attr.pa_P         = P;
	attr.pa_unit_size = UNIT_SIZE;
	attr.pa_seed      = *seed;

	if (C2_FI_ENABLED("attr_err")) { attr.pa_P = 1; }
	rc = c2_pdclust_build(&domain, lid, &attr, le, pl);
	if (failure_test)
		C2_UT_ASSERT(rc == -ENOMEM || rc == -EPROTO);
	else {
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(list_lookup(lid) == &(*pl)->pl_base.sl_base);
		C2_UT_ASSERT((*pl)->pl_base.sl_base.l_ref == 1);
		C2_UT_ASSERT((*pl)->pl_base.sl_base.l_user_count == 0);
	}

	return rc;
}

/*
 * Builds a layout object with PDCLUST layout type, by first building an
 * enumeration object with the specified enumeration type.
 */
static int pdclust_layout_build(uint32_t enum_id,
				uint64_t lid,
				uint32_t N, uint32_t K, uint32_t P,
				struct c2_uint128 *seed,
				uint32_t A, uint32_t B,
				struct c2_pdclust_layout **pl,
				struct c2_layout_list_enum **list_enum,
				struct c2_layout_linear_enum **lin_enum,
				bool failure_test)
{
	struct c2_fid                *cob_list = NULL; /* to keep gcc happy. */
	int                           i;
	struct c2_layout_enum        *e;
	struct c2_layout_linear_attr  lin_attr;
	struct c2_layout             *l_from_pl;
	struct c2_layout_enum        *e_from_layout;

	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);
	C2_UT_ASSERT(pl != NULL);

	/* Build an enumeration object with the specified enum type. */
	if (enum_id == LIST_ENUM_ID) {
		C2_ALLOC_ARR(cob_list, P);
		C2_UT_ASSERT(cob_list != NULL);

		for (i = 0; i < P; ++i)
			c2_fid_set(&cob_list[i], i * 100 + 1, i + 1);

		if (C2_FI_ENABLED("list_attr_err")) { P = 0; }
		rc = c2_list_enum_build(&domain, cob_list, P, list_enum);
		C2_UT_ASSERT(rc == 0 || rc == -ENOMEM || rc == -EPROTO);

		e = &(*list_enum)->lle_base;

	} else { /* LINEAR_ENUM_ID */
		lin_attr.lla_nr = P;
		lin_attr.lla_A  = A;
		lin_attr.lla_B  = B;
		if (C2_FI_ENABLED("lin_attr_err")) { lin_attr.lla_nr = 0; }
		rc = c2_linear_enum_build(&domain, &lin_attr, lin_enum);
		C2_UT_ASSERT(rc == 0 || rc == -ENOMEM || rc == -EPROTO);

		e = &(*lin_enum)->lle_base;
	}
	if (rc != 0) {
		C2_UT_ASSERT(failure_test);
		if (enum_id == LIST_ENUM_ID)
			c2_free(cob_list);
		return rc;
	}

	/*
	 * Build a layout object with PDCLUST layout type and using the
	 * enumeration object built earlier here.
	 */
	rc = pdclust_l_build(lid, N, K, P, seed, e, pl, failure_test);
	if (failure_test) {
		C2_UT_ASSERT(rc == -ENOMEM || rc == -EPROTO);
		c2_layout_enum_fini(e);
		return rc;
	}
	else {
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(list_lookup(lid) == &(*pl)->pl_base.sl_base);
	}

	/* Verify c2_pdl_to_layout(). */
	l_from_pl = c2_pdl_to_layout(*pl);
	C2_UT_ASSERT(l_from_pl == &(*pl)->pl_base.sl_base);

	/* Verify c2_layout_to_enum(). */
	e_from_layout = c2_layout_to_enum(l_from_pl);
	C2_UT_ASSERT(e_from_layout == e);
	return rc;
}

/* Verifies generic part of the layout object. */
static void l_verify(struct c2_layout *l, uint64_t lid)
{
	C2_UT_ASSERT(l->l_id == lid);
	C2_UT_ASSERT(l->l_ref >= 0);
	C2_UT_ASSERT(l->l_ops != NULL);
}

/*
 * Verifies generic part of the layout object and the PDCLUST layout type
 * specific part of it.
 */
static void pdclust_l_verify(struct c2_pdclust_layout *pl,
			     uint64_t lid,
			     uint32_t N, uint32_t K, uint32_t P,
			     struct c2_uint128 *seed)
{
	/* Verify generic part of the layout object. */
	l_verify(&pl->pl_base.sl_base, lid);

	/* Verify PDCLUST layout type specific part of the layout object. */
	C2_UT_ASSERT(pl->pl_attr.pa_N == N);
	C2_UT_ASSERT(pl->pl_attr.pa_K == K);
	C2_UT_ASSERT(pl->pl_attr.pa_P == P);
	C2_UT_ASSERT(pl->pl_attr.pa_unit_size == UNIT_SIZE);
	C2_UT_ASSERT(c2_uint128_eq(&pl->pl_attr.pa_seed, seed));
}

/* Verifies the layout object against the various input arguments. */
static void pdclust_layout_verify(uint32_t enum_id,
				  struct c2_layout *l, uint64_t lid,
				  uint32_t N, uint32_t K, uint32_t P,
				  struct c2_uint128 *seed,
				  uint32_t A, uint32_t B)
{
	struct c2_pdclust_layout     *pl;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	int                           i;
	struct c2_fid                 cob_id;

	C2_UT_ASSERT(l != NULL);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);
	C2_UT_ASSERT(l->l_type == &c2_pdclust_layout_type);

	pl = container_of(l, struct c2_pdclust_layout, pl_base.sl_base);

	/*
	 * Verify generic and PDCLUST layout type specific parts of the
	 * layout object.
	 */
	pdclust_l_verify(pl, lid, N, K, P, seed);

	/* Verify enum type specific part of the layout object. */
	C2_UT_ASSERT(pl->pl_base.sl_enum != NULL);

	if (enum_id == LIST_ENUM_ID) {
		list_enum = container_of(pl->pl_base.sl_enum,
					 struct c2_layout_list_enum, lle_base);
		for(i = 0; i < list_enum->lle_nr; ++i) {
			c2_fid_set(&cob_id, i * 100 + 1, i + 1);
			C2_UT_ASSERT(c2_fid_eq(&cob_id,
					      &list_enum->lle_list_of_cobs[i]));
		}
		C2_UT_ASSERT(list_enum->lle_nr == P);
	} else {
		lin_enum = container_of(pl->pl_base.sl_enum,
					struct c2_layout_linear_enum, lle_base);
		C2_UT_ASSERT(lin_enum->lle_attr.lla_nr == P);
		C2_UT_ASSERT(lin_enum->lle_attr.lla_A == A);
		C2_UT_ASSERT(lin_enum->lle_attr.lla_B == B);
	}
}

static void NKP_assign_and_pool_init(uint32_t enum_id,
				     uint32_t inline_test,
				     uint32_t list_nr_less,
				     uint32_t list_nr_more,
				     uint32_t linear_nr,
				     uint32_t *N, uint32_t *K, uint32_t *P)
{
	C2_UT_ASSERT(ergo(enum_id == LIST_ENUM_ID,
			  list_nr_less < LDB_MAX_INLINE_COB_ENTRIES &&
			  list_nr_more > LDB_MAX_INLINE_COB_ENTRIES));

	if (enum_id == LIST_ENUM_ID) {
		switch (inline_test) {
		case LESS_THAN_INLINE:
			*P = list_nr_less;
			break;
		case EXACT_INLINE:
			*P = LDB_MAX_INLINE_COB_ENTRIES;
			break;
		case MORE_THAN_INLINE:
			*P = list_nr_more;
			break;
		default:
			C2_ASSERT(0);
		}
	} else {
		*P = linear_nr;
	}

	if (*P <= 20)
		*K = 1;
	else if (*P <= 50)
		*K = 2;
	else if (*P <= 200)
		*K = 6;
	else if (*P <= 500)
		*K = 12;
	else if (*P <= 1000)
		*K = 100;
	else
		*K = 200;

	if (*P <= 20)
		*N = *P - (2 * (*K));
	else if (*P <= 100)
		*N = *P - (2 * (*K)) - 10;
	else if (*P <= 1000)
		*N = *P - (2 * (*K)) - 12;
	else
		*N = *P - (2 * (*K)) - 100;

	/* Initialise the pool. */
	rc = c2_pool_init(&pool, *P);
	C2_ASSERT(rc == 0);
}

/*
 * Tests the APIs supported for enumeration object build, layout object build
 * and layout dstruction that happens using c2_layout_put(). Verifies that the
 * newly build layout object is added to the list of layout objects maintained
 * in the domain object and that c2_layout_find() returns the same object.
 */
static int test_build_pdclust(uint32_t enum_id, uint64_t lid,
			      uint32_t inline_test,
			      bool failure_test)
{
	struct c2_uint128             seed;
	uint32_t                      N;
	uint32_t                      K;
	uint32_t                      P;
	struct c2_pdclust_layout     *pl;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	struct c2_layout             *l;

	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	c2_uint128_init(&seed, "buildpdclustlayo");

	NKP_assign_and_pool_init(enum_id,
				 inline_test, 9, 109, 12000,
				 &N, &K, &P);

	rc = pdclust_layout_build(enum_id, lid,
				  N, K, P, &seed,
				  10, 20,
				  &pl, &list_enum, &lin_enum,
				  failure_test);
	if (failure_test)
		C2_UT_ASSERT(rc == -ENOMEM || rc == -EPROTO);
	else {
		C2_UT_ASSERT(rc == 0);
		/*
		 * Verify that c2_layout_find() returns the same object by
		 * reading it from the memory.
		 */
		l = c2_layout_find(&domain, lid);
		C2_UT_ASSERT(l == &pl->pl_base.sl_base);

		/* Verify the layout object built earlier here. */
		pdclust_layout_verify(enum_id, &pl->pl_base.sl_base, lid,
				      N, K, P, &seed,
				      10, 20);
		/*
		 * Delete the layout object by reducing the reference that
		 * c2_layout_find() has acquired on it.
		 */
		c2_layout_put(&pl->pl_base.sl_base);

		/*
		 * Delete the layout object by reducing the last reference.
		 */
		c2_layout_put(&pl->pl_base.sl_base);
		C2_UT_ASSERT(list_lookup(lid) == NULL);
	}

	c2_pool_fini(&pool);
	return rc;
}

/*
 * Tests the APIs supported for enumeration object build, layout object build
 * and layout dstruction that happens using c2_layout_put().
 */
static void test_build(void)
{
	uint64_t lid;

	/*
	 * Build a layout object with PDCLUST layout type, LIST enum type
	 * with a few inline entries only and then destroy it.
	 */
	lid = 1001;
	rc = test_build_pdclust(LIST_ENUM_ID, lid, LESS_THAN_INLINE,
				!FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout object with PDCLUST layout type, LIST enum type
	 * with a number of inline entries exactly equal to
	 * LDB_MAX_INLINE_COB_ENTRIES and then destroy it.
	 */
	lid = 1002;
	rc = test_build_pdclust(LIST_ENUM_ID, lid, EXACT_INLINE,
				!FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout object with PDCLUST layout type, LIST enum type
	 * including noninline entries and then destroy it.
	 */
	lid = 1003;
	rc = test_build_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE,
				!FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout object with PDCLUST layout type and LINEAR enum
	 * type and then destroy it.
	 */
	lid = 1004;
	rc = test_build_pdclust(LINEAR_ENUM_ID, lid, INLINE_NOT_APPLICABLE,
				!FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);
}

static void test_build_failure(void)
{
	uint64_t lid;

	/*
	 * Simulate memory allocation failure in pdclust_allocate() that is
	 * in the path of c2_pdclust_build().
	 */
	lid = 1005;
	c2_fi_enable_once("pdclust_allocate", "mem_err");
	rc = test_build_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE,
				FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOMEM);

	/*
	 * Simulate memory allocation failure in pdclust_allocate() that is
	 * in the path of c2_pdclust_build().
	 */
	lid = 1006;
	c2_fi_enable_once("pdclust_allocate", "mem_err");
	rc = test_build_pdclust(LINEAR_ENUM_ID, lid, INLINE_NOT_APPLICABLE,
				FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOMEM);

	/*
	 * Simulate invalid attributes error in pdclust_populate() that is
	 * in the path of c2_pdclust_build().
	 */
	lid = 1007;
	c2_fi_enable_once("pdclust_l_build", "attr_err");
	rc = test_build_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE,
				FAILURE_TEST);
	C2_UT_ASSERT(rc == -EPROTO);

	/*
	 * Simulate memory allocation failure in linear_allocate() that is
	 * in the path of c2_pdclust_build().
	 */
	lid = 1008;
	c2_fi_enable_once("list_allocate", "mem_err");
	rc = test_build_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE,
				FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOMEM);

	/*
	 * Simulate memory allocation failure in linear_allocate() that is
	 * in the path of c2_pdclust_build().
	 */
	lid = 1009;
	c2_fi_enable_once("linear_allocate", "mem_err");
	rc = test_build_pdclust(LINEAR_ENUM_ID, lid, INLINE_NOT_APPLICABLE,
				FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOMEM);

	/* Simulate attributes invalid error in c2_list_enum_build(). */
	lid = 1010;
	c2_fi_enable_once("pdclust_layout_build", "list_attr_err");
	rc = test_build_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE,
				FAILURE_TEST);
	C2_UT_ASSERT(rc == -EPROTO);

	/* Simulate attributes invalid error in c2_linear_enum_build(). */
	lid = 1011;
	c2_fi_enable_once("pdclust_layout_build", "lin_attr_err");
	rc = test_build_pdclust(LINEAR_ENUM_ID, lid, INLINE_NOT_APPLICABLE,
				FAILURE_TEST);
	C2_UT_ASSERT(rc == -EPROTO);

	/* Simulate fid invalid error in c2_list_enum_build(). */
	lid = 1012;
	c2_fi_enable_once("c2_list_enum_build", "fid_invalid_err");
	rc = test_build_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE,
				FAILURE_TEST);
	C2_UT_ASSERT(rc == -EPROTO);
}


/* Builds part of the buffer representing generic part of the layout object. */
static void buf_build(uint32_t lt_id, struct c2_bufvec_cursor *dcur)
{
	struct c2_layout_rec rec;
	c2_bcount_t          nbytes_copied;

	rec.lr_lt_id      = lt_id;
	rec.lr_user_count = 0;

	nbytes_copied = c2_bufvec_cursor_copyto(dcur, &rec, sizeof rec);
	C2_UT_ASSERT(nbytes_copied == sizeof rec);
}

/*
 * Builds part of the buffer representing generic and PDCLUST layout type
 * specific parts of the layout object.
 */
static void pdclust_buf_build(uint32_t let_id, uint64_t lid,
			      uint32_t N, uint32_t K, uint32_t P,
			      struct c2_uint128 *seed,
			      struct c2_bufvec_cursor *dcur)
{
	struct c2_layout_pdclust_rec pl_rec;
	c2_bcount_t                  nbytes_copied;

	buf_build(c2_pdclust_layout_type.lt_id, dcur);

	pl_rec.pr_let_id            = let_id;
	pl_rec.pr_attr.pa_N         = N;
	pl_rec.pr_attr.pa_K         = K;
	pl_rec.pr_attr.pa_P         = P;
	pl_rec.pr_attr.pa_unit_size = UNIT_SIZE;
	pl_rec.pr_attr.pa_seed      = *seed;

	nbytes_copied = c2_bufvec_cursor_copyto(dcur, &pl_rec, sizeof pl_rec);
	C2_UT_ASSERT(nbytes_copied == sizeof pl_rec);
}

/* Builds a buffer containing serialised representation of a layout object. */
static int pdclust_layout_buf_build(uint32_t enum_id, uint64_t lid,
				    uint32_t N, uint32_t K, uint32_t P,
				    struct c2_uint128 *seed,
				    uint32_t A, uint32_t B,
				    struct c2_bufvec_cursor *dcur)
{
	uint32_t                     let_id;
	c2_bcount_t                  nbytes_copied;
	struct cob_entries_header    ce_header;
	struct c2_fid                cob_id;
	uint32_t                     i;
	struct c2_layout_linear_attr lin_rec;

	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);
	C2_UT_ASSERT(dcur != NULL);

	/*
	 * Build part of the buffer representing generic and the PDCLUST layout
	 * type specific parts of the layout object.
	 */
	let_id = enum_id == LIST_ENUM_ID ? c2_list_enum_type.let_id :
					   c2_linear_enum_type.let_id;
	pdclust_buf_build(let_id, lid, N, K, P, seed, dcur);

	/*
	 * Build part of the buffer representing enum type specific part of
	 * the layout object.
	 */
	if (enum_id == LIST_ENUM_ID) {
		ce_header.ces_nr = P;
		nbytes_copied = c2_bufvec_cursor_copyto(dcur, &ce_header,
							sizeof ce_header);
		C2_UT_ASSERT(nbytes_copied == sizeof ce_header);

		for (i = 0; i < ce_header.ces_nr; ++i) {
			c2_fid_set(&cob_id, i * 100 + 1, i + 1);
			nbytes_copied = c2_bufvec_cursor_copyto(dcur, &cob_id,
								sizeof cob_id);
			C2_UT_ASSERT(nbytes_copied == sizeof cob_id);
		}
	} else {
		lin_rec.lla_nr = P;
		lin_rec.lla_A  = A;
		lin_rec.lla_B  = B;

		nbytes_copied = c2_bufvec_cursor_copyto(dcur, &lin_rec,
							sizeof lin_rec);
		C2_UT_ASSERT(nbytes_copied == sizeof lin_rec);
	}

	return 0;
}

/*
 * Allocates area with size returned by c2_layout_max_recsize() and with
 * additional_bytes required if any.
 * For example, additional_bytes are required for LIST enumeration type, and
 * specifically when directly invoking 'c2_layout_encode() or
 * c2_layout_decode()' (and not while invoking Layout DB APIs like
 * c2_layout_add() etc).
 */
static void allocate_area(void **area,
			  c2_bcount_t additional_bytes,
			  c2_bcount_t *num_bytes)
{
	C2_UT_ASSERT(area != NULL);

	*num_bytes = c2_layout_max_recsize(&domain) + additional_bytes;

	*area = c2_alloc(*num_bytes);
	C2_UT_ASSERT(*area != NULL);
}

/* Tests the API c2_layout_decode() for PDCLUST layout type. */
static int test_decode_pdclust(uint32_t enum_id, uint64_t lid,
			       uint32_t inline_test,
			       bool failure_test)
{
	void                    *area;
	c2_bcount_t              num_bytes;
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;
	struct c2_layout        *l;
	struct c2_uint128        seed;
	uint32_t                 N;
	uint32_t                 K;
	uint32_t                 P;
	struct c2_layout_type   *lt;

	C2_ENTRY();
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	c2_uint128_init(&seed, "decodepdclustlay");

	/* Build a layout buffer. */
	if (enum_id == LIST_ENUM_ID)
		allocate_area(&area, ADDITIONAL_BYTES_DEFAULT, &num_bytes);
	else
		allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	NKP_assign_and_pool_init(enum_id,
				 inline_test, 5, 125, 1500,
				 &N, &K, &P);

	rc = pdclust_layout_buf_build(enum_id, lid,
				      N, K, P, &seed,
				      777, 888, &cur);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);

	lt = &c2_pdclust_layout_type;
	rc = lt->lt_ops->lto_allocate(&domain, lid, &l);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(c2_layout__allocated_invariant(l));

	/* Decode the layout buffer into a layout object. */
	rc = c2_layout_decode(l, &cur, C2_LXO_BUFFER_OP, NULL);
	if (failure_test)
		C2_UT_ASSERT(rc == -ENOMEM || rc == -EPROTO);
	else {
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(list_lookup(lid) == l);

		/* Verify the layout object built by c2_layout_decode(). */
		pdclust_layout_verify(enum_id, l, lid,
				      N, K, P, &seed,
				      777, 888);
	}

	/* Destroy the layout object. */
	if (failure_test)
		l->l_ops->lo_delete(l);
	else {
		/* Unlock the layout, locked by lto_allocate() */
		c2_mutex_unlock(&l->l_lock);
		c2_layout_put(l);
	}
	C2_UT_ASSERT(list_lookup(lid) == NULL);

	c2_free(area);
	c2_pool_fini(&pool);
	C2_LEAVE();
	return rc;
}

/* Tests the API c2_layout_decode(). */
static void test_decode(void)
{
	uint64_t lid;

	/*
	 * Decode a layout object with PDCLUST layout type, LIST enum type
	 * with a few inline entries only.
	 */
	lid = 2001;
	rc = test_decode_pdclust(LIST_ENUM_ID, lid, LESS_THAN_INLINE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Decode a layout object with PDCLUST layout type, LIST enum type
	 * with a number of inline entries exactly equal to
	 * LDB_MAX_INLINE_COB_ENTRIES and then destroy it.
	 */
	lid = 2002;
	rc = test_decode_pdclust(LIST_ENUM_ID, lid, EXACT_INLINE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Decode a layout object with PDCLUST layout type, LIST enum type
	 * including noninline entries and then destroy it.
	 */
	lid = 2003;
	rc = test_decode_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Decode a layout object with PDCLUST layout type and LINEAR enum
	 * type.
	 */
	lid = 2004;
	rc = test_decode_pdclust(LINEAR_ENUM_ID, lid, INLINE_NOT_APPLICABLE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);
}

static void test_decode_failure(void)
{
	uint64_t lid;

	/* Simulate invalid attributes error in c2_layout_decode(). */
	lid = 2005;
	c2_fi_enable_once("c2_layout_decode", "attr_err");
	rc = test_decode_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -EPROTO);

	/* Simulate invalid attributes error in pdclust_decode(). */
	lid = 2006;
	c2_fi_enable_once("pdclust_decode", "attr_err1");
	rc = test_decode_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -EPROTO);

	/* Simulate invalid attributes error in pdclust_decode(). */
	lid = 2007;
	c2_fi_enable_once("pdclust_decode", "attr_err2");
	rc = test_decode_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -EPROTO);

	/* Simulate invalid attributes error in list_populate(). */
	lid = 2008;
	c2_fi_enable_once("list_decode", "attr_err");
	rc = test_decode_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -EPROTO);

	/* Simulate invalid attributes error in linear_populate(). */
	lid = 2009;
	c2_fi_enable_once("linear_decode", "attr_err");
	rc = test_decode_pdclust(LINEAR_ENUM_ID, lid, INLINE_NOT_APPLICABLE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -EPROTO);

	/* Simulate memory allocation failure in list_decode(). */
	lid = 2010;
	c2_fi_enable_once("list_decode", "mem_err");
	rc = test_decode_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOMEM);

	/* Simulate fid invalid error in list_decode(). */
	lid = 2011;
	c2_fi_enable_once("list_decode", "fid_invalid_err");
	rc = test_decode_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -EPROTO);

	/* Simulate leto_allocate() failure in pdclust_decode(). */
	lid = 2012;
	c2_fi_enable_once("linear_allocate", "mem_err");
	rc = test_decode_pdclust(LINEAR_ENUM_ID, lid, INLINE_NOT_APPLICABLE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOMEM);

	/* Simulate pdclust_populate() failure in pdclust_decode(). */
	lid = 2013;
	c2_fi_enable_once("pdclust_decode", "attr_err3");
	rc = test_decode_pdclust(LINEAR_ENUM_ID, lid, INLINE_NOT_APPLICABLE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -EPROTO);
}

/*
 * Verifies part of the layout buffer representing generic part of the layout
 * object.
 */
static void lbuf_verify(struct c2_bufvec_cursor *cur, uint32_t *lt_id)
{
	struct c2_layout_rec *rec;

	C2_UT_ASSERT(c2_bufvec_cursor_step(cur) >= sizeof *rec);

	rec = c2_bufvec_cursor_addr(cur);
	C2_UT_ASSERT(rec != NULL);

	*lt_id = rec->lr_lt_id;

	C2_UT_ASSERT(rec->lr_user_count == 0);

	c2_bufvec_cursor_move(cur, sizeof *rec);
}

/*
 * Verifies part of the layout buffer representing PDCLUST layout type specific
 * part of the layout object.
 */
static void pdclust_lbuf_verify(uint32_t N, uint32_t K, uint32_t P,
				struct c2_uint128 *seed,
				struct c2_bufvec_cursor *cur,
				uint32_t *let_id)
{
	struct c2_layout_pdclust_rec *pl_rec;

	C2_UT_ASSERT(c2_bufvec_cursor_step(cur) >= sizeof *pl_rec);

	pl_rec = c2_bufvec_cursor_addr(cur);

	C2_UT_ASSERT(pl_rec->pr_attr.pa_N == N);
	C2_UT_ASSERT(pl_rec->pr_attr.pa_K == K);
	C2_UT_ASSERT(pl_rec->pr_attr.pa_P == P);
	C2_UT_ASSERT(c2_uint128_eq(&pl_rec->pr_attr.pa_seed, seed));
	C2_UT_ASSERT(pl_rec->pr_attr.pa_unit_size == UNIT_SIZE);

	*let_id = pl_rec->pr_let_id;
	c2_bufvec_cursor_move(cur, sizeof *pl_rec);
}

/* Verifies layout buffer against the various input arguments. */
static void pdclust_layout_buf_verify(uint32_t enum_id, uint64_t lid,
				      uint32_t N, uint32_t K, uint32_t P,
				      struct c2_uint128 *seed,
				      uint32_t A, uint32_t B,
				      struct c2_bufvec_cursor *cur)
{
	uint32_t                      lt_id;
	uint32_t                      let_id;
	uint32_t                      i;
	struct cob_entries_header    *ce_header;
	struct c2_fid                *cob_id_from_layout;
	struct c2_fid                 cob_id_calculated;
	struct c2_layout_linear_attr *lin_attr;

	C2_UT_ASSERT(cur != NULL);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	/* Verify generic part of the layout buffer. */
	lbuf_verify(cur, &lt_id);
	C2_UT_ASSERT(lt_id == c2_pdclust_layout_type.lt_id);

	/* Verify PDCLUST layout type specific part of the layout buffer. */
	pdclust_lbuf_verify(N, K, P, seed, cur, &let_id);

	/* Verify enum type specific part of the layout buffer. */
	if (enum_id == LIST_ENUM_ID) {
		C2_UT_ASSERT(let_id == c2_list_enum_type.let_id);

		C2_UT_ASSERT(c2_bufvec_cursor_step(cur) >= sizeof *ce_header);

		ce_header = c2_bufvec_cursor_addr(cur);
		C2_UT_ASSERT(ce_header != NULL);
		c2_bufvec_cursor_move(cur, sizeof *ce_header);

		C2_UT_ASSERT(ce_header->ces_nr == P);
		C2_UT_ASSERT(c2_bufvec_cursor_step(cur) >=
			     ce_header->ces_nr * sizeof *cob_id_from_layout);

		for (i = 0; i < ce_header->ces_nr; ++i) {
			cob_id_from_layout = c2_bufvec_cursor_addr(cur);
			C2_UT_ASSERT(cob_id_from_layout != NULL);

			c2_fid_set(&cob_id_calculated, i * 100 + 1, i + 1);
			C2_UT_ASSERT(c2_fid_eq(cob_id_from_layout,
					       &cob_id_calculated));

			c2_bufvec_cursor_move(cur, sizeof *cob_id_from_layout);
		}
	} else {
		C2_UT_ASSERT(let_id == c2_linear_enum_type.let_id);

		C2_UT_ASSERT(c2_bufvec_cursor_step(cur) >= sizeof *lin_attr);

		lin_attr = c2_bufvec_cursor_addr(cur);
		C2_UT_ASSERT(lin_attr->lla_nr == P);
		C2_UT_ASSERT(lin_attr->lla_A == A);
		C2_UT_ASSERT(lin_attr->lla_B == B);
	}
}

/* Tests the API c2_layout_encode() for PDCLUST layout type. */
static int test_encode_pdclust(uint32_t enum_id, uint64_t lid,
			       uint32_t inline_test,
			       bool failure_test)
{
	struct c2_pdclust_layout     *pl;
	void                         *area;
	c2_bcount_t                   num_bytes;
	struct c2_bufvec              bv;
	struct c2_bufvec_cursor       cur;
	struct c2_uint128             seed;
	uint32_t                      N;
	uint32_t                      K;
	uint32_t                      P;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;

	C2_ENTRY("lid %llu", (unsigned long long)lid);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	c2_uint128_init(&seed, "encodepdclustlay");

	/* Build a layout object. */
	if (enum_id == LIST_ENUM_ID)
		allocate_area(&area, ADDITIONAL_BYTES_DEFAULT, &num_bytes);
	else
		allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	NKP_assign_and_pool_init(enum_id,
				 inline_test, 10, 120, 120,
				 &N, &K, &P);

	rc = pdclust_layout_build(enum_id, lid,
				  N, K, P, &seed,
				  11, 21,
				  &pl, &list_enum, &lin_enum,
				  !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* Encode the layout object into a layout buffer. */
	c2_mutex_lock(&pl->pl_base.sl_base.l_lock);
	rc  = c2_layout_encode(&pl->pl_base.sl_base, C2_LXO_BUFFER_OP,
			       NULL, &cur);
	c2_mutex_unlock(&pl->pl_base.sl_base.l_lock);
	if (failure_test)
		C2_UT_ASSERT(rc == LO_ENCODE_ERR);
	else
		C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);

	/* Verify the layout buffer produced by c2_layout_encode(). */
	if (!failure_test)
		pdclust_layout_buf_verify(enum_id, lid,
					  N, K, P, &seed,
					  11, 21, &cur);

	/* Delete the layout object. */
	c2_layout_put(&pl->pl_base.sl_base);
	C2_UT_ASSERT(list_lookup(lid) == NULL);
	c2_free(area);

	c2_pool_fini(&pool);
	C2_LEAVE();
	return rc;
}

/* Tests the API c2_layout_encode(). */
static void test_encode(void)
{
	uint64_t lid;
	int      rc;

	/*
	 * Encode for PDCLUST layout type and LIST enumeration type,
	 * with a few inline entries only.
	 */
	lid = 3001;
	rc = test_encode_pdclust(LIST_ENUM_ID, lid, LESS_THAN_INLINE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Encode for PDCLUST layout type and LIST enumeration type,
	 * with a number of inline entries exactly equal to
	 * LDB_MAX_INLINE_COB_ENTRIES.
	 */
	lid = 3002;
	rc = test_encode_pdclust(LIST_ENUM_ID, lid, EXACT_INLINE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Encode for PDCLUST layout type and LIST enumeration type,
	 * including noninline entries and then destroy it.
	 */
	lid = 3003;
	rc = test_encode_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* Encode for PDCLUST layout type and LINEAR enumeration type. */
	lid = 3004;
	rc = test_encode_pdclust(LINEAR_ENUM_ID, lid, INLINE_NOT_APPLICABLE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);
}

static void test_encode_failure(void)
{
	uint64_t lid;
	int      rc;

	/* Simulate c2_layout_encode() failure. */
	lid = 3005;
	c2_fi_enable_once("c2_layout_encode", "lo_encode_err");
	rc = test_encode_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == LO_ENCODE_ERR);

	/* Simulate c2_layout_encode() failure. */
	lid = 3006;
	c2_fi_enable_once("c2_layout_encode", "lo_encode_err");
	rc = test_encode_pdclust(LINEAR_ENUM_ID, lid, INLINE_NOT_APPLICABLE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == LO_ENCODE_ERR);
}


/* Compares generic part of the layout buffers. */
static void lbuf_compare(struct c2_bufvec_cursor *cur1,
			 struct c2_bufvec_cursor *cur2)
{
	struct c2_layout_rec *rec1;
	struct c2_layout_rec *rec2;

	C2_UT_ASSERT(c2_bufvec_cursor_step(cur1) >= sizeof *rec2);
	C2_UT_ASSERT(c2_bufvec_cursor_step(cur2) >= sizeof *rec2);

	rec1 = c2_bufvec_cursor_addr(cur1);
	rec2 = c2_bufvec_cursor_addr(cur2);

	C2_UT_ASSERT(rec1->lr_lt_id == rec2->lr_lt_id);
	C2_UT_ASSERT(rec1->lr_user_count == rec2->lr_user_count);

	c2_bufvec_cursor_move(cur1, sizeof *rec1);
	c2_bufvec_cursor_move(cur2, sizeof *rec2);
}

/* Compares PDCLUST layout type specific part of the layout buffers. */
static void pdclust_lbuf_compare(struct c2_bufvec_cursor *cur1,
				 struct c2_bufvec_cursor *cur2)
{
	struct c2_layout_pdclust_rec *pl_rec1;
	struct c2_layout_pdclust_rec *pl_rec2;

	C2_UT_ASSERT(c2_bufvec_cursor_step(cur1) >= sizeof *pl_rec1);
	C2_UT_ASSERT(c2_bufvec_cursor_step(cur2) >= sizeof *pl_rec2);

	pl_rec1 = c2_bufvec_cursor_addr(cur1);
	pl_rec2 = c2_bufvec_cursor_addr(cur2);

	C2_UT_ASSERT(pl_rec1->pr_attr.pa_N == pl_rec2->pr_attr.pa_N);
	C2_UT_ASSERT(pl_rec1->pr_attr.pa_K == pl_rec2->pr_attr.pa_K);
	C2_UT_ASSERT(pl_rec1->pr_attr.pa_P == pl_rec2->pr_attr.pa_P);
	C2_UT_ASSERT(c2_uint128_eq(&pl_rec1->pr_attr.pa_seed,
				   &pl_rec2->pr_attr.pa_seed));
	C2_UT_ASSERT(pl_rec1->pr_attr.pa_unit_size ==
		     pl_rec2->pr_attr.pa_unit_size);

	c2_bufvec_cursor_move(cur1, sizeof *pl_rec1);
	c2_bufvec_cursor_move(cur2, sizeof *pl_rec2);
}

/* Compares two layout buffers provided as input arguments. */
static void pdclust_layout_buf_compare(uint32_t enum_id,
				       struct c2_bufvec_cursor *cur1,
				       struct c2_bufvec_cursor *cur2)
{
	struct cob_entries_header    *ce_header1;
	struct cob_entries_header    *ce_header2;
	struct c2_fid                *cob_id1;
	struct c2_fid                *cob_id2;
	struct c2_layout_linear_attr *lin_attr1;
	struct c2_layout_linear_attr *lin_attr2;
	uint32_t                      i;

	C2_UT_ASSERT(cur1 != NULL);
	C2_UT_ASSERT(cur2 != NULL);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	/* Compare generic part of the layout buffers. */
	lbuf_compare(cur1, cur2);

	/* Compare PDCLUST layout type specific part of the layout buffers. */
	pdclust_lbuf_compare(cur1, cur2);

	/* Compare enumeration type specific part of the layout buffers. */
	if (enum_id == LIST_ENUM_ID) {
		C2_UT_ASSERT(c2_bufvec_cursor_step(cur1) >= sizeof *ce_header1);
		C2_UT_ASSERT(c2_bufvec_cursor_step(cur2) >= sizeof *ce_header2);

		ce_header1 = c2_bufvec_cursor_addr(cur1);
		ce_header2 = c2_bufvec_cursor_addr(cur2);

		c2_bufvec_cursor_move(cur1, sizeof *ce_header1);
		c2_bufvec_cursor_move(cur2, sizeof *ce_header2);

		C2_UT_ASSERT(ce_header1->ces_nr == ce_header2->ces_nr);

		C2_UT_ASSERT(c2_bufvec_cursor_step(cur1) >=
			     ce_header1->ces_nr * sizeof *cob_id1);
		C2_UT_ASSERT(c2_bufvec_cursor_step(cur2) >=
			     ce_header2->ces_nr * sizeof *cob_id2);

		for (i = 0; i < ce_header1->ces_nr; ++i) {
			cob_id1 = c2_bufvec_cursor_addr(cur1);
			cob_id2 = c2_bufvec_cursor_addr(cur2);

			C2_UT_ASSERT(c2_fid_eq(cob_id1, cob_id2));

			c2_bufvec_cursor_move(cur1, sizeof *cob_id1);
			c2_bufvec_cursor_move(cur2, sizeof *cob_id2);
		}
	} else {
		C2_UT_ASSERT(c2_bufvec_cursor_step(cur1) >= sizeof *lin_attr1);
		C2_UT_ASSERT(c2_bufvec_cursor_step(cur2) >= sizeof *lin_attr2);

		lin_attr1 = c2_bufvec_cursor_addr(cur1);
		lin_attr2 = c2_bufvec_cursor_addr(cur2);

		C2_UT_ASSERT(lin_attr1->lla_nr == lin_attr2->lla_nr);
		C2_UT_ASSERT(lin_attr1->lla_A == lin_attr2->lla_A);
		C2_UT_ASSERT(lin_attr1->lla_B == lin_attr2->lla_B);
	}
}

/*
 * Tests the API sequence c2_layout_decode() followed by c2_layout_encode(),
 * for the PDCLUST layout type.
 */
static int test_decode_encode_pdclust(uint32_t enum_id, uint64_t lid,
				      uint32_t inline_test)
{
	void                    *area1;
	struct c2_bufvec         bv1;
	struct c2_bufvec_cursor  cur1;
	void                    *area2;
	struct c2_bufvec         bv2;
	struct c2_bufvec_cursor  cur2;
	c2_bcount_t              num_bytes;
	uint32_t                 N;
	uint32_t                 K;
	uint32_t                 P;
	struct c2_uint128        seed;
	struct c2_layout        *l;
	struct c2_layout_type   *lt;

	C2_ENTRY();
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	c2_uint128_init(&seed, "decodeencodepdcl");

	/* Build a layout buffer. */
	if (enum_id == LIST_ENUM_ID)
		allocate_area(&area1, ADDITIONAL_BYTES_DEFAULT, &num_bytes);
	else
		allocate_area(&area1, ADDITIONAL_BYTES_NONE, &num_bytes);

	bv1 = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area1, &num_bytes);
	c2_bufvec_cursor_init(&cur1, &bv1);

	NKP_assign_and_pool_init(enum_id,
				 inline_test, 3, 103, 1510,
				 &N, &K, &P);

	rc = pdclust_layout_buf_build(LINEAR_ENUM_ID, lid,
				      N, K, P, &seed,
				      777, 888, &cur1);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur1, &bv1);

	lt = &c2_pdclust_layout_type;
	rc = lt->lt_ops->lto_allocate(&domain, lid, &l);
	C2_ASSERT(c2_layout__allocated_invariant(l));

	/* Decode the layout buffer into a layout object. */
	rc = c2_layout_decode(l, &cur1, C2_LXO_BUFFER_OP, NULL);
	C2_UT_ASSERT(rc == 0);

	/* Unlock the layout, locked by lto_allocate() */
	c2_mutex_unlock(&l->l_lock);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur1, &bv1);

	/*
	 * Encode the layout object produced by c2_layout_decode() into
	 * another layout buffer.
	 */
	if (enum_id == LIST_ENUM_ID)
		allocate_area(&area2, ADDITIONAL_BYTES_DEFAULT, &num_bytes);
	else
		allocate_area(&area2, ADDITIONAL_BYTES_NONE, &num_bytes);

	bv2 = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area2, &num_bytes);
	c2_bufvec_cursor_init(&cur2, &bv2);

	c2_mutex_lock(&l->l_lock);
	rc = c2_layout_encode(l, C2_LXO_BUFFER_OP, NULL, &cur2);
	c2_mutex_unlock(&l->l_lock);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur2, &bv2);

	/*
	 * Compare the two layout buffers - one created earlier here and
	 * the one that is produced by c2_layout_encode().
	 */
	pdclust_layout_buf_compare(enum_id, &cur1, &cur2);

	/* Destroy the layout. */
	c2_layout_put(l);
	C2_UT_ASSERT(list_lookup(lid) == NULL);

	c2_free(area1);
	c2_free(area2);
	c2_pool_fini(&pool);
	C2_LEAVE();
	return rc;
}

/* Tests the API sequence c2_layout_decode() followed by c2_layout_encode(). */
static void test_decode_encode(void)
{
	uint64_t lid;
	int      rc;

	/*
	 * Build a layout buffer representing a layout with PDCLUST layout type
	 * and LIST enum type, with a few inline entries only.
	 * Decode it into a layout object. Then encode that layout object again
	 * into another layout buffer.
	 * Then, compare the original layout buffer with the encoded layout
	 * buffer.
	 */
	lid = 4001;
	rc = test_decode_encode_pdclust(LIST_ENUM_ID, lid, LESS_THAN_INLINE);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout buffer representing a layout with PDCLUST layout type
	 * and LIST enum type, with a number of inline entries exactly equal to
	 * LDB_MAX_INLINE_COB_ENTRIES.
	 * Decode it into a layout object. Then encode that layout object again
	 * into another layout buffer.
	 * Then, compare the original layout buffer with the encoded layout
	 * buffer.
	 */
	lid = 4002;
	rc = test_decode_encode_pdclust(LIST_ENUM_ID, lid, EXACT_INLINE);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout buffer representing a layout with PDCLUST layout type
	 * and LIST enum type including noninline entries.
	 * Decode it into a layout object. Then encode that layout object again
	 * into another layout buffer.
	 * Then, compare the original layout buffer with the encoded layout
	 * buffer.
	 */
	lid = 4003;
	rc = test_decode_encode_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout buffer representing a layout with PDCLUST layout type
	 * and LINEAR enum type.
	 * Decode it into a layout object. Then encode that layout object again
	 * into another layout buffer.
	 * Then, compare the original layout buffer with the encoded layout
	 * buffer.
	 */
	lid = 4004;
	rc = test_decode_encode_pdclust(LINEAR_ENUM_ID, lid,
					INLINE_NOT_APPLICABLE);
	C2_UT_ASSERT(rc == 0);
}

/*
 * Compares two layout objects with PDCLUST layout type, provided as input
 * arguments.
 */
static void pdclust_layout_compare(uint32_t enum_id,
				   const struct c2_layout *l1,
				   const struct c2_layout *l2,
				   bool l2_ref_elevated)
{
	struct c2_pdclust_layout     *pl1;
	struct c2_pdclust_layout     *pl2;
	struct c2_layout_list_enum   *list_e1;
	struct c2_layout_list_enum   *list_e2;
	struct c2_layout_linear_enum *lin_e1;
	struct c2_layout_linear_enum *lin_e2;
	uint32_t                      i;

	C2_UT_ASSERT(l1 != NULL && l2 != NULL);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	/* Compare generic part of the layout objects. */
	C2_UT_ASSERT(l1->l_id == l2->l_id);
	C2_UT_ASSERT(l1->l_type == l2->l_type);
	C2_UT_ASSERT(l1->l_dom == l2->l_dom);
	if (l2_ref_elevated)
		C2_UT_ASSERT(l1->l_ref == l2->l_ref - 1);
	else
		C2_UT_ASSERT(l1->l_ref == l2->l_ref);
	C2_UT_ASSERT(l1->l_user_count == l2->l_user_count);
	C2_UT_ASSERT(l1->l_ops == l2->l_ops);

	/* Compare PDCLUST layout type specific part of the layout objects. */
	pl1 = container_of(l1, struct c2_pdclust_layout, pl_base.sl_base);
	pl2 = container_of(l2, struct c2_pdclust_layout, pl_base.sl_base);

	C2_UT_ASSERT(pl1->pl_attr.pa_N == pl2->pl_attr.pa_N);
	C2_UT_ASSERT(pl1->pl_attr.pa_K == pl2->pl_attr.pa_K);
	C2_UT_ASSERT(pl1->pl_attr.pa_P == pl2->pl_attr.pa_P);
	C2_UT_ASSERT(c2_uint128_eq(&pl1->pl_attr.pa_seed,
				   &pl2->pl_attr.pa_seed));

	/* Compare enumeration specific part of the layout objects. */
	C2_UT_ASSERT(pl1->pl_base.sl_enum->le_type ==
		     pl2->pl_base.sl_enum->le_type);
	C2_UT_ASSERT(pl1->pl_base.sl_enum->le_sl == &pl1->pl_base);
	C2_UT_ASSERT(pl1->pl_base.sl_enum->le_sl->sl_base.l_id ==
		     pl2->pl_base.sl_enum->le_sl->sl_base.l_id);
	C2_UT_ASSERT(pl1->pl_base.sl_enum->le_ops ==
		     pl2->pl_base.sl_enum->le_ops);

	/* Compare enumeration type specific part of the layout objects. */
	if (enum_id == LIST_ENUM_ID) {
		list_e1 = container_of(pl1->pl_base.sl_enum,
				       struct c2_layout_list_enum, lle_base);
		list_e2 = container_of(pl2->pl_base.sl_enum,
				       struct c2_layout_list_enum, lle_base);

		C2_UT_ASSERT(list_e1->lle_nr == list_e2->lle_nr);

		for (i = 0; i < list_e1->lle_nr; ++i)
			C2_UT_ASSERT(c2_fid_eq(&list_e1->lle_list_of_cobs[i],
					       &list_e2->lle_list_of_cobs[i]));
	} else { /* LINEAR_ENUM_ID */
		lin_e1 = container_of(pl1->pl_base.sl_enum,
				      struct c2_layout_linear_enum, lle_base);
		lin_e2 = container_of(pl2->pl_base.sl_enum,
				      struct c2_layout_linear_enum, lle_base);

		C2_UT_ASSERT(lin_e1->lle_attr.lla_nr ==
			     lin_e2->lle_attr.lla_nr);
		C2_UT_ASSERT(lin_e1->lle_attr.lla_A == lin_e2->lle_attr.lla_A);
		C2_UT_ASSERT(lin_e1->lle_attr.lla_B == lin_e2->lle_attr.lla_B);
	}
}

/* Copies contents of one layout object to the other. */
static void pdclust_layout_copy(uint32_t enum_id,
				const struct c2_layout *l_src,
				struct c2_layout **l_dest)
{
	struct c2_pdclust_layout     *pl_src;
	struct c2_pdclust_layout     *pl_dest;
	struct c2_layout_list_enum   *list_src;
	struct c2_layout_list_enum   *list_dest;
	struct c2_layout_linear_enum *lin_src;
	struct c2_layout_linear_enum *lin_dest;
	uint32_t                      i;

	C2_UT_ASSERT(l_src != NULL && l_dest != NULL);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	pl_src = container_of(l_src, struct c2_pdclust_layout, pl_base.sl_base);
	pl_dest = c2_alloc(sizeof *pl_src);
	C2_UT_ASSERT(pl_dest != NULL);
	*l_dest = &pl_dest->pl_base.sl_base;

	/* Copy generic part of the layout object. */
	(*l_dest)->l_id         = l_src->l_id;
	(*l_dest)->l_type       = l_src->l_type;
	(*l_dest)->l_dom        = l_src->l_dom;
	(*l_dest)->l_ref        = l_src->l_ref;
	(*l_dest)->l_user_count = l_src->l_user_count;
	(*l_dest)->l_ops        = l_src->l_ops;

	/* Copy PDCLUST layout type specific part of the layout objects. */
	pl_dest->pl_attr = pl_src->pl_attr;

	/* Copy enumeration type specific part of the layout objects. */
	if (enum_id == LIST_ENUM_ID) {
		list_src = container_of(pl_src->pl_base.sl_enum,
					struct c2_layout_list_enum, lle_base);
		list_dest = c2_alloc(sizeof *list_src);
		C2_UT_ASSERT(list_src != NULL);

		list_dest->lle_nr = list_src->lle_nr;
		C2_ALLOC_ARR(list_dest->lle_list_of_cobs, list_dest->lle_nr);

		for (i = 0; i < list_src->lle_nr; ++i)
			list_dest->lle_list_of_cobs[i] =
					       list_src->lle_list_of_cobs[i];

		pl_dest->pl_base.sl_enum = &list_dest->lle_base;
	} else { /* LINEAR_ENUM_ID */
		lin_src = container_of(pl_src->pl_base.sl_enum,
				       struct c2_layout_linear_enum, lle_base);
		lin_dest = c2_alloc(sizeof *lin_src);
		C2_UT_ASSERT(lin_src != NULL);

		lin_dest->lle_attr = lin_src->lle_attr;
		pl_dest->pl_base.sl_enum = &lin_dest->lle_base;
	}

	/* Copy enumeration specific part of the layout objects. */
	pl_dest->pl_base.sl_enum->le_type = pl_src->pl_base.sl_enum->le_type;
	pl_dest->pl_base.sl_enum->le_ops = pl_src->pl_base.sl_enum->le_ops;
	pl_dest->pl_base.sl_enum->le_sl = &pl_dest->pl_base;

	pdclust_layout_compare(enum_id, &pl_src->pl_base.sl_base,
			       &pl_dest->pl_base.sl_base, false);
}

static void pdclust_layout_copy_delete(uint32_t enum_id, struct c2_layout *l)
{
	struct c2_pdclust_layout     *pl;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;

	C2_UT_ASSERT(l != NULL);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	pl = container_of(l, struct c2_pdclust_layout, pl_base.sl_base);
	if (enum_id == LIST_ENUM_ID) {
		list_enum = container_of(pl->pl_base.sl_enum,
					struct c2_layout_list_enum, lle_base);
		c2_free(list_enum->lle_list_of_cobs);
		c2_free(list_enum);
	} else { /* LINEAR_ENUM_ID */
		lin_enum = container_of(pl->pl_base.sl_enum,
				        struct c2_layout_linear_enum, lle_base);
		c2_free(lin_enum);
	}

	c2_free(pl);
}

/*
 * Tests the API sequence c2_layout_encode() followed by c2_layout_decode(),
 * for the PDCLUST layout type.
 */
static int test_encode_decode_pdclust(uint32_t enum_id, uint64_t lid,
				      uint32_t inline_test)
{
	struct c2_pdclust_layout     *pl;
	void                         *area;
	c2_bcount_t                   num_bytes;
	struct c2_bufvec              bv;
	struct c2_bufvec_cursor       cur;
	struct c2_uint128             seed;
	uint32_t                      N;
	uint32_t                      K;
	uint32_t                      P;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	struct c2_layout             *l;
	struct c2_layout             *l_copy;
	struct c2_layout_type        *lt;

	C2_ENTRY("lid %llu", (unsigned long long)lid);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	c2_uint128_init(&seed, "encodedecodepdcl");

	/* Build a layout object. */
	if (enum_id == LIST_ENUM_ID)
		allocate_area(&area, ADDITIONAL_BYTES_DEFAULT, &num_bytes);
	else
		allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	NKP_assign_and_pool_init(enum_id,
				 inline_test, 13, 113, 1130,
				 &N, &K, &P);

	rc = pdclust_layout_build(enum_id, lid,
				  N, K, P, &seed,
				  10, 20,
				  &pl, &list_enum, &lin_enum,
				  !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	pdclust_layout_copy(enum_id, &pl->pl_base.sl_base, &l_copy);
	C2_UT_ASSERT(l_copy != NULL);

	/* Encode the layout object into a layout buffer. */
	c2_mutex_lock(&pl->pl_base.sl_base.l_lock);
	rc = c2_layout_encode(&pl->pl_base.sl_base, C2_LXO_BUFFER_OP,
			      NULL, &cur);
	c2_mutex_unlock(&pl->pl_base.sl_base.l_lock);
	C2_UT_ASSERT(rc == 0);

	/* Destroy the layout. */
	c2_layout_put(&pl->pl_base.sl_base);
	C2_UT_ASSERT(list_lookup(lid) == NULL);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);

	lt = &c2_pdclust_layout_type;
	rc = lt->lt_ops->lto_allocate(&domain, lid, &l);
	C2_ASSERT(c2_layout__allocated_invariant(l));

	/*
	 * Decode the layout buffer produced by c2_layout_encode() into another
	 * layout object.
	 */
	rc = c2_layout_decode(l, &cur, C2_LXO_BUFFER_OP, NULL);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Comapre the two layout objects - one created earlier here and the
	 * one that is produced by c2_layout_decode().
	 */
	pdclust_layout_compare(enum_id, l_copy, l, false);
	pdclust_layout_copy_delete(enum_id, l_copy);

	/* Unlock the layout, locked by lto_allocate() */
	c2_mutex_unlock(&l->l_lock);

	/* Destroy the layout. */
	c2_layout_put(l);
	C2_UT_ASSERT(list_lookup(lid) == NULL);

	c2_free(area);
	c2_pool_fini(&pool);
	C2_LEAVE();
	return rc;
}

/* Tests the API sequence c2_layout_encode() followed by c2_layout_decode(). */
static void test_encode_decode(void)
{
	uint64_t lid;
	int      rc;

	/*
	 * Build a layout object with PDCLUST layout type and LIST enum type,
	 * with a few inline entries only.
	 * Encode it into a layout buffer. Then decode that layout buffer again
	 * into another layout object.
	 * Then, compare the original layout object with the decoded layout
	 * object.
	 */
	lid = 5001;
	rc = test_encode_decode_pdclust(LIST_ENUM_ID, lid, LESS_THAN_INLINE);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout object with PDCLUST layout type and LIST enum type,
	 * with a number of inline entries exactly equal to
	 * LDB_MAX_INLINE_COB_ENTRIES.
	 * Encode it into a layout buffer. Then decode that layout buffer again
	 * into another layout object.
	 * Then, compare the original layout object with the decoded layout
	 * object.
	 */
	lid = 5002;
	rc = test_encode_decode_pdclust(LIST_ENUM_ID, lid, EXACT_INLINE);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout object with PDCLUST layout type and LIST enum type
	 * including noninline entries.
	 * Encode it into a layout buffer. Then decode that layout buffer again
	 * into another layout object.
	 * Then, compare the original layout object with the decoded layout
	 * object.
	 */
	lid = 5003;
	rc = test_encode_decode_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout object with PDCLUST layout type and LINEAR enum type.
	 * Encode it into a layout buffer. Then decode that layout buffer again
	 * into a another layout object.
	 * Now, compare the original layout object with the decoded layout
	 * object.
	 */
	lid = 5004;
	rc = test_encode_decode_pdclust(LINEAR_ENUM_ID, lid,
					INLINE_NOT_APPLICABLE);
	C2_UT_ASSERT(rc == 0);
}

/*
 * Tests the API c2_layout_get() and c2_layout_put(), for the PDCLUST layout
 * type.
 */
static int test_ref_get_put_pdclust(uint32_t enum_id, uint64_t lid)
{
	struct c2_pdclust_layout     *pl;
	struct c2_uint128             seed;
	uint32_t                      N;
	uint32_t                      K;
	uint32_t                      P;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	uint32_t                      i;

	C2_ENTRY("lid %llu", (unsigned long long)lid);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	c2_uint128_init(&seed, "refgetputpdclust");

	/* Build a layout object. */
	NKP_assign_and_pool_init(enum_id, MORE_THAN_INLINE,
				 10, 1212, 1212,
				 &N, &K, &P);

	rc = pdclust_layout_build(LIST_ENUM_ID, lid,
				  N, K, P, &seed,
				  10, 20,
				  &pl, &list_enum, &lin_enum,
				  !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* Verify that the ref count is set to 1. */
	C2_UT_ASSERT(pl->pl_base.sl_base.l_ref == 1);

	/* Add multiple references on the layout object. */
	for (i = 0; i < 123; ++i)
		c2_layout_get(&pl->pl_base.sl_base);
	C2_UT_ASSERT(pl->pl_base.sl_base.l_ref ==
		     1 + 123);

	/* Release multiple references on the layout object. */
	for (i = 0; i < 123; ++i)
		c2_layout_put(&pl->pl_base.sl_base);
	C2_UT_ASSERT(pl->pl_base.sl_base.l_ref == 1);

	/* Release the last reference so as to delete the layout. */
	c2_layout_put(&pl->pl_base.sl_base);
	C2_UT_ASSERT(list_lookup(lid) == NULL);

	C2_LEAVE();
	return rc;
}

/* Tests the APIs c2_layout_get() and c2_layout_put(). */
static void test_ref_get_put(void)
{
	uint64_t lid;
	int      rc;

	/*
	 * Reference get and put operations for PDCLUST layout type and LIST
	 * enumeration type.
	 */
	lid = 6001;
	rc = test_ref_get_put_pdclust(LIST_ENUM_ID, lid);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Reference get and put operations for PDCLUST layout type and LINEAR
	 * enumeration type.
	 */
	lid = 6002;
	rc = test_ref_get_put_pdclust(LINEAR_ENUM_ID, lid);
	C2_UT_ASSERT(rc == 0);
}

/* Verifies the enum operations pointed by leo_nr and leo_get. */
static void enum_op_verify(uint32_t enum_id, uint64_t lid,
			   uint32_t nr, struct c2_layout *l)
{
	struct c2_striped_layout     *stl;
	struct c2_layout_enum        *e;
	struct c2_layout_linear_enum *lin_enum;
	struct c2_fid                 fid_calculated;
	struct c2_fid                 fid_from_layout;
	struct c2_fid                 gfid;
	int                           i;

	C2_UT_ASSERT(l != NULL);

	stl = c2_layout_to_striped(l);
	e = c2_striped_layout_to_enum(stl);
	C2_UT_ASSERT(c2_layout_enum_nr(e) == nr);

	if (enum_id == LIST_ENUM_ID) {
		for(i = 0; i < nr; ++i) {
			c2_fid_set(&fid_calculated, i * 100 + 1, i + 1);
			c2_layout_enum_get(e, i, NULL, &fid_from_layout);
			C2_UT_ASSERT(c2_fid_eq(&fid_calculated,
					       &fid_from_layout));
		}
	} else {
		/* Set gfid to some dummy value. */
		c2_fid_set(&gfid, 0, 999);
		lin_enum = container_of(e, struct c2_layout_linear_enum,
					lle_base);
		for(i = 0; i < nr; ++i) {
			c2_fid_set(&fid_calculated,
				   lin_enum->lle_attr.lla_A +
				   i * lin_enum->lle_attr.lla_B,
				   gfid.f_key);
			c2_layout_enum_get(e, i, &gfid, &fid_from_layout);
			C2_UT_ASSERT(c2_fid_eq(&fid_calculated,
					       &fid_from_layout));
		}
	}
}

/*
 * Tests the enum operations pointed by leo_nr and leo_get, for the PDCLUST
 * layout type.
 */
static int test_enum_ops_pdclust(uint32_t enum_id, uint64_t lid,
				 uint32_t inline_test)
{
	struct c2_uint128             seed;
	uint32_t                      N;
	uint32_t                      K;
	uint32_t                      P;
	struct c2_pdclust_layout     *pl;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;

	C2_ENTRY();
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	/* Build a layout object. */
	c2_uint128_init(&seed, "enumopspdclustla");

	NKP_assign_and_pool_init(enum_id,
				 inline_test, 14, 1014, 1014,
				 &N, &K, &P);

	rc = pdclust_layout_build(enum_id, lid,
				  N, K, P, &seed,
				  777, 888,
				  &pl, &list_enum, &lin_enum,
				  !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* Verify enum operations. */
	enum_op_verify(enum_id, lid, P, &pl->pl_base.sl_base);

	/* Destroy the layout object. */
	c2_layout_put(&pl->pl_base.sl_base);
	C2_UT_ASSERT(list_lookup(lid) == NULL);

	c2_pool_fini(&pool);
	C2_LEAVE();
	return rc;
}

/* Tests the enum operations pointed by leo_nr and leo_get. */
static void test_enum_operations(void)
{
	uint64_t lid;

	/*
	 * Decode a layout with PDCLUST layout type, LIST enum type and
	 * with a few inline entries only.
	 * And then verify its enum ops.
	 */
	lid = 7001;
	rc = test_enum_ops_pdclust(LIST_ENUM_ID, lid, LESS_THAN_INLINE);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Decode a layout with PDCLUST layout type, LIST enum type and
	 * with a number of inline entries exactly equal to
	 * LDB_MAX_INLINE_COB_ENTRIES.
	 * And then verify its enum ops.
	 */
	lid = 7002;
	rc = test_enum_ops_pdclust(LIST_ENUM_ID, lid, EXACT_INLINE);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Decode a layout with PDCLUST layout type and LIST enum type
	 * including noninline entries.
	 * And then verify its enum ops.
	 */
	lid = 7003;
	rc = test_enum_ops_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Decode a layout with PDCLUST layout type and LINEAR enum type.
	 * And then verify its enum ops.
	 */
	lid = 7004;
	rc = test_enum_ops_pdclust(LINEAR_ENUM_ID, lid, INLINE_NOT_APPLICABLE);
	C2_UT_ASSERT(rc == 0);
}

/* Tests the API c2_layout_max_recsize(). */
static void test_max_recsize(void)
{
	const char              t_db_name[] = "t5-layout";
	struct c2_dbenv         t_dbenv;
	struct c2_layout_domain t_domain;
	c2_bcount_t             max_size_from_api;
	c2_bcount_t             max_size_calculated;

	C2_ENTRY();

	/*
	 * A layout type can be registered with only one domain at a time.
	 * Hence, unregister all the available layout types and enum types from
	 * the domain "domain", which are registered through test_init().
	 */
	c2_layout_standard_types_unregister(&domain);

	rc = c2_dbenv_init(&t_dbenv, t_db_name, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	/* Initialise the domain. */
	rc = c2_layout_domain_init(&t_domain, &t_dbenv);
	C2_UT_ASSERT(rc == 0);

	/* Register pdclust layout type and verify c2_layout_max_recsize(). */
	rc = c2_layout_type_register(&t_domain, &c2_pdclust_layout_type);
	C2_UT_ASSERT(rc == 0);

	max_size_from_api = c2_layout_max_recsize(&t_domain);

	max_size_calculated = sizeof(struct c2_layout_rec) +
			      sizeof(struct c2_layout_pdclust_rec);

	C2_UT_ASSERT(max_size_from_api == max_size_calculated);

	/* Register linear enum type and verify c2_layout_max_recsize(). */
	rc = c2_layout_enum_type_register(&t_domain, &c2_linear_enum_type);
	C2_UT_ASSERT(rc == 0);

	max_size_from_api = c2_layout_max_recsize(&t_domain);

	max_size_calculated = sizeof(struct c2_layout_rec) +
			      sizeof(struct c2_layout_pdclust_rec) +
			      sizeof(struct c2_layout_linear_attr);

	C2_UT_ASSERT(max_size_from_api == max_size_calculated);

	/* Register list enum type and verify c2_layout_max_recsize(). */
	rc = c2_layout_enum_type_register(&t_domain, &c2_list_enum_type);
	C2_UT_ASSERT(rc == 0);

	max_size_from_api = c2_layout_max_recsize(&t_domain);

	max_size_calculated = sizeof(struct c2_layout_rec) +
			      sizeof(struct c2_layout_pdclust_rec) +
			      sizeof(struct cob_entries_header) +
			      LDB_MAX_INLINE_COB_ENTRIES *
			      sizeof(struct c2_fid);;

	C2_UT_ASSERT(max_size_from_api == max_size_calculated);

	/* Unregister list enum type and verify c2_layout_max_recsize(). */
	c2_layout_enum_type_unregister(&t_domain, &c2_list_enum_type);

	max_size_from_api = c2_layout_max_recsize(&t_domain);

	max_size_calculated = sizeof(struct c2_layout_rec) +
			      sizeof(struct c2_layout_pdclust_rec) +
			      sizeof(struct c2_layout_linear_attr);

	C2_UT_ASSERT(max_size_from_api == max_size_calculated);

	/* Unregister linear enum type and verify c2_layout_max_recsize(). */
	c2_layout_enum_type_unregister(&t_domain, &c2_linear_enum_type);

	max_size_from_api = c2_layout_max_recsize(&t_domain);

	max_size_calculated = sizeof(struct c2_layout_rec) +
			      sizeof(struct c2_layout_pdclust_rec);

	C2_UT_ASSERT(max_size_from_api == max_size_calculated);

	/* Unregister pdclust layout type and verify c2_layout_max_recsize(). */
	c2_layout_type_unregister(&t_domain, &c2_pdclust_layout_type);

	max_size_from_api = c2_layout_max_recsize(&t_domain);

	max_size_calculated = sizeof(struct c2_layout_rec);

	C2_UT_ASSERT(max_size_from_api == max_size_calculated);

	/* Finalise the domain. */
	c2_layout_domain_fini(&t_domain);

	c2_dbenv_fini(&t_dbenv);

	/*
	 * Register back all the available layout types and enum types with
	 * the domain "domain", to undo the change done at the beginning of
	 * this function.
	 */
	rc = c2_layout_standard_types_register(&domain);
	C2_ASSERT(rc == 0);

	C2_LEAVE();
}

/*
 * Calculates the recsize by considering the sizes of the internal data
 * structures and their values, as applicable. Then verifies that the recsize
 * provided as an argument matches the calcualted one.
 */
static void pdclust_recsize_verify(uint32_t enum_id,
				   struct c2_layout *l,
				   c2_bcount_t recsize_to_verify)
{
	struct c2_pdclust_layout    *pl;
	struct c2_layout_list_enum  *list_enum;
	c2_bcount_t                  recsize;

	C2_UT_ASSERT(l != NULL);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);
	C2_UT_ASSERT(l->l_type == &c2_pdclust_layout_type);

	pl = container_of(l, struct c2_pdclust_layout, pl_base.sl_base);

	/* Account for the enum type specific recsize. */
	if (enum_id == LIST_ENUM_ID) {
		list_enum = container_of(pl->pl_base.sl_enum,
					 struct c2_layout_list_enum, lle_base);
		if (list_enum->lle_nr < LDB_MAX_INLINE_COB_ENTRIES)
			recsize = sizeof(struct cob_entries_header) +
				  list_enum->lle_nr * sizeof(struct c2_fid);
		else
			recsize = sizeof(struct cob_entries_header) +
				  LDB_MAX_INLINE_COB_ENTRIES *
				  sizeof(struct c2_fid);
	} else
		recsize = sizeof(struct c2_layout_linear_attr);

	/*
	 * Account for the recsize for the generic part of the layout object
	 * and for the PDCLUST layout type specific part of it.
	 */
	recsize = sizeof(struct c2_layout_rec) +
		  sizeof(struct c2_layout_pdclust_rec) + recsize;

	/* Compare the two sizes. */
	C2_UT_ASSERT(recsize == recsize_to_verify);
}

/* Tests the function lo_recsize(), for the PDCLUST layout type. */
static int test_recsize_pdclust(uint32_t enum_id, uint64_t lid,
				uint32_t inline_test)
{
	struct c2_pdclust_layout     *pl;
	struct c2_layout             *l;
	struct c2_uint128             seed;
	uint32_t                      N;
	uint32_t                      K;
	uint32_t                      P;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	c2_bcount_t                   recsize;

	C2_ENTRY("lid %llu", (unsigned long long)lid);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	c2_uint128_init(&seed, "recsizepdclustla");

	/* Build a layout object. */
	NKP_assign_and_pool_init(enum_id,
				 inline_test, 1, 1200, 1111,
				 &N, &K, &P);

	rc = pdclust_layout_build(enum_id, lid,
				  N, K, P, &seed,
				  10, 20,
				  &pl, &list_enum, &lin_enum,
				  !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* Obtain the recsize by using the internal function lo_recsize(). */
	l = &pl->pl_base.sl_base;
	recsize = l->l_ops->lo_recsize(l);

	/* Verify the recsize returned by lo_recsize(). */
	pdclust_recsize_verify(enum_id, &pl->pl_base.sl_base, recsize);

	/* Destroy the layout object. */
	c2_layout_put(l);
	C2_UT_ASSERT(list_lookup(lid) == NULL);

	c2_pool_fini(&pool);
	C2_LEAVE();
	return rc;
}

/* Tests the function lo_recsize(). */
static void test_recsize(void)
{
	uint64_t lid;
	int      rc;

	/*
	 * lo_recsize() for PDCLUST layout type and LIST enumeration type,
	 * with a few inline entries only.
	 */
	lid = 8001;
	rc = test_recsize_pdclust(LIST_ENUM_ID, lid, LESS_THAN_INLINE);
	C2_UT_ASSERT(rc == 0);

	/*
	 * lo_recsize() for PDCLUST layout type and LIST enumeration type,
	 * with a number of inline entries exactly equal to
	 * LDB_MAX_INLINE_COB_ENTRIES
	 */
	lid = 8002;
	rc = test_recsize_pdclust(LIST_ENUM_ID, lid, EXACT_INLINE);
	C2_UT_ASSERT(rc == 0);

	/*
	 * lo_recsize() for PDCLUST layout type and LIST enumeration type
	 * including noninline entries.
	 */
	lid = 8003;
	rc = test_recsize_pdclust(LIST_ENUM_ID, lid, MORE_THAN_INLINE);
	C2_UT_ASSERT(rc == 0);

	/* lo_recsize() for PDCLUST layout type and LINEAR enumeration type. */
	lid = 8004;
	rc = test_recsize_pdclust(LINEAR_ENUM_ID, lid, INLINE_NOT_APPLICABLE);
	C2_UT_ASSERT(rc == 0);
}

/* Tests the APIs supported for c2_pdclust_instance object. */
static int test_pdclust_instance_obj(uint32_t enum_id, uint64_t lid,
				     bool inline_test, bool failure_test)
{
	struct c2_uint128             seed;
	uint32_t                      N;
	uint32_t                      K;
	uint32_t                      P;
	struct c2_layout             *l;
	struct c2_pdclust_layout     *pl;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	struct c2_pdclust_instance   *pi;
	struct c2_fid                 gfid;
	struct c2_layout_instance    *li;

	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	c2_uint128_init(&seed, "buildpdclustlayo");

	NKP_assign_and_pool_init(enum_id,
				 inline_test, 14, 30, 30,
				 &N, &K, &P);

	rc = pdclust_layout_build(enum_id, lid,
				  N, K, P, &seed,
				  10, 20,
				  &pl, &list_enum, &lin_enum,
				  !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* Verify some pdclust APIs. */
	C2_UT_ASSERT(c2_pdclust_N(pl) == N);
	C2_UT_ASSERT(c2_pdclust_K(pl) == K);
	C2_UT_ASSERT(c2_pdclust_P(pl) == P);
	C2_UT_ASSERT(c2_pdclust_unit_size(pl) == UNIT_SIZE);

	C2_UT_ASSERT(c2_pdclust_unit_classify(pl, N - 1) == C2_PUT_DATA);
	C2_UT_ASSERT(c2_pdclust_unit_classify(pl, N) == C2_PUT_PARITY);
	C2_UT_ASSERT(c2_pdclust_unit_classify(pl, N + 2 * K ) == C2_PUT_SPARE);

	/* Build pdclust instance. */
	c2_fid_set(&gfid, 0, 999);
	l  = c2_pdl_to_layout(pl);
	C2_UT_ASSERT(l->l_ref == 1);
	rc = c2_layout_instance_build(l, &gfid, &li);
	if (failure_test) {
		C2_UT_ASSERT(rc == -ENOMEM || rc == -EPROTO);
	}
	else {
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(l->l_ref == 2);
		pi = c2_layout_instance_to_pdi(li);
		layout_demo(pi, pl, 1, 1, false);

		/* Verify c2_layout_instance_to_pdi(). */
		li = &pi->pi_base;
		C2_UT_ASSERT(c2_layout_instance_to_pdi(li) == pi);

		/* Verify c2_layout_instance_to_enum */
		if (enum_id == LIST_ENUM_ID)
			C2_UT_ASSERT(c2_layout_instance_to_enum(li) ==
				     &list_enum->lle_base);
		else
			C2_UT_ASSERT(c2_layout_instance_to_enum(li) ==
				     &lin_enum->lle_base);
		/*
		 * Delete the pdclust instance object. It destroys the layout
		 * object as well since there has been only one reference
		 * acquired on that layout.
		 */
		c2_layout_instance_fini(&pi->pi_base);
		C2_UT_ASSERT(l->l_ref == 1);
	}

	c2_layout_put(c2_pdl_to_layout(pl));
	C2_UT_ASSERT(list_lookup(lid) == NULL);
	c2_pool_fini(&pool);
	return rc;
}

/*
 * Tests the APIs supported for c2_pdclust_instance object, for various enum
 * types.
 */
static void test_pdclust_instance(void)
{
	uint64_t lid;

	/*
	 * Build a layout object with PDCLUST layout type, LIST enum type
	 * with a few inline entries only and then destroy it.
	 */
	lid = 9001;
	rc = test_pdclust_instance_obj(LIST_ENUM_ID, lid, LESS_THAN_INLINE,
				       !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout object with PDCLUST layout type, LIST enum type
	 * with a number of inline entries exactly equal to
	 * LDB_MAX_INLINE_COB_ENTRIES and then destroy it.
	 */
	lid = 9002;
	rc = test_pdclust_instance_obj(LIST_ENUM_ID, lid, EXACT_INLINE,
				       !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout object with PDCLUST layout type, LIST enum type
	 * including noninline entries and then destroy it.
	 */
	lid = 9003;
	rc = test_pdclust_instance_obj(LIST_ENUM_ID, lid, MORE_THAN_INLINE,
				       !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout object with PDCLUST layout type and LINEAR enum
	 * type and then destroy it.
	 */
	lid = 9004;
	rc = test_pdclust_instance_obj(LINEAR_ENUM_ID, lid,
				       INLINE_NOT_APPLICABLE,
				       !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);
}

static void test_pdclust_instance_failure(void)
{
	uint64_t lid;

	/* Simulate memory allocation error in c2_pdclust_instance_build(). */
	lid = 9005;
	c2_fi_enable_once("pdclust_instance_build", "mem_err1");
	rc = test_pdclust_instance_obj(LIST_ENUM_ID, lid, LESS_THAN_INLINE,
				       FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOMEM);

	/* Simulate memory allocation error in c2_pdclust_instance_build(). */
	lid = 9006;
	c2_fi_enable_once("pdclust_instance_build", "mem_err2");
	rc = test_pdclust_instance_obj(LINEAR_ENUM_ID, lid,
				      INLINE_NOT_APPLICABLE,
				       FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOMEM);

	/*
	 * Simulate c2_parity_math_init() error in
	 * c2_pdclust_instance_build().
	 */
	lid = 9007;
	c2_fi_enable_once("pdclust_instance_build", "parity_math_err");
	rc = test_pdclust_instance_obj(LINEAR_ENUM_ID, lid,
				      INLINE_NOT_APPLICABLE,
				       FAILURE_TEST);
	C2_UT_ASSERT(rc == -EPROTO);
}

#ifndef __KERNEL__
/*
 * Sets (or resets) the pair using the area pointer and the layout id provided
 * as arguments.
 */
static void pair_set(struct c2_db_pair *pair, uint64_t *lid,
		       void *area, c2_bcount_t num_bytes)
{
	pair->dp_key.db_buf.b_addr = lid;
	pair->dp_key.db_buf.b_nob  = sizeof *lid;
	pair->dp_rec.db_buf.b_addr = area;
	pair->dp_rec.db_buf.b_nob  = num_bytes;
}

static int test_add_pdclust(uint32_t enum_id, uint64_t lid,
			    uint32_t inline_test,
			    bool layout_destroy, struct c2_layout **l_obj,
			    bool duplicate_test,
			    bool failure_test);

/* Tests the API c2_layout_lookup(), for the PDCLUST layout type. */
static int test_lookup_pdclust(uint32_t enum_id, uint64_t lid,
			       bool existing_test,
			       uint32_t inline_test,
			       bool failure_test)
{
	c2_bcount_t        num_bytes;
	void              *area;
	struct c2_layout  *l1;
	struct c2_layout  *l1_copy;
	struct c2_layout  *l2;
	struct c2_layout  *l3;
	struct c2_db_pair  pair;
	struct c2_db_tx    tx;
	int                rc_tmp;

	C2_ENTRY();
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);
	C2_UT_ASSERT(ergo(!existing_test, failure_test));

	/*
	 * If existing_test is true, then first add a layout object to the
	 * DB.
	 */
	if (existing_test) {
		rc = test_add_pdclust(enum_id, lid,
				      inline_test,
				      !LAYOUT_DESTROY, &l1,
				      !DUPLICATE_TEST,
				      !FAILURE_TEST);
		C2_UT_ASSERT(rc == 0);
		if (!failure_test)
			pdclust_layout_copy(enum_id, l1, &l1_copy);

		/*
		 * Lookup for the layout object to verify that the same object
		 * is returned from the memory, not requiring a lookup from the
		 * DB.
		 */
		rc = c2_layout_lookup(&domain, lid, &c2_pdclust_layout_type,
				      &tx, &pair, &l2);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(l2 == l1);

		/*
		 * Release the reference acquired by c2_layout_lookup().
		 */
		c2_layout_put(l1);

		/*
		 * Destroy the layout object.
		 */
		c2_layout_put(l1);
	}

	C2_UT_ASSERT(list_lookup(lid) == NULL);
	C2_UT_ASSERT(c2_layout_find(&domain, lid) == NULL);

	/* Lookup for the layout object from the DB. */
	allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_set(&pair, &lid, area, num_bytes);

	rc = c2_layout_lookup(&domain, lid, &c2_pdclust_layout_type,
			      &tx, &pair, &l3);
	if (failure_test)
		C2_UT_ASSERT(rc == -ENOENT || rc == -ENOMEM || rc == -EPROTO ||
			     rc == LO_DECODE_ERR);
	else
		C2_UT_ASSERT(rc == 0);

	rc_tmp = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc_tmp == 0);

	if (existing_test && !failure_test) {
		C2_UT_ASSERT(list_lookup(lid) == l3);
		pdclust_layout_compare(enum_id, l1_copy, l3, false);
		pdclust_layout_copy_delete(enum_id, l1_copy);

		/* Destroy the layout object. */
		c2_layout_put(l3);
		C2_UT_ASSERT(list_lookup(lid) == NULL);
	}
	c2_free(area);
	C2_LEAVE();
	return rc;
}

struct ghost_data {
	uint64_t                 lid;
	struct c2_layout        *l;
	struct c2_bufvec_cursor *cur;
};

static bool ghost_create(void *d)
{
	struct ghost_data     *data = d;
	struct c2_layout_type *lt;

	C2_ENTRY();
	C2_UT_ASSERT(list_lookup(data->lid) == NULL);

	lt = &c2_pdclust_layout_type;
	rc = lt->lt_ops->lto_allocate(&domain, data->lid, &data->l);
	C2_UT_ASSERT(rc == 0);

	/* Decode the layout buffer stored in the ghost_data. */
	rc = c2_layout_decode(data->l, data->cur, C2_LXO_BUFFER_OP, NULL);
	C2_UT_ASSERT(rc == 0);

	/* Unlock the layout, locked by lto_allocate() */
	c2_mutex_unlock(&data->l->l_lock);

	C2_LEAVE();
	return rc;
}

static int test_lookup_with_ghost_creation(uint32_t enum_id, uint64_t lid,
					   uint32_t inline_test)
{
	struct c2_layout        *l1;
	struct c2_layout        *l1_copy;
	struct ghost_data        g_data;
	void                    *area_for_encode;
	c2_bcount_t              num_bytes_for_encode;
	struct c2_bufvec         bv_for_encode;
	struct c2_bufvec_cursor  cur_for_encode;
	struct c2_layout        *l_from_DB;
	c2_bcount_t              num_bytes_for_lookup;
	void                    *area_for_lookup;
	struct c2_db_pair        pair;
	struct c2_db_tx          tx;
	int                      rc_tmp;

	C2_ENTRY();
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	/* Create one layout object and add it to the LDB. */
	rc = test_add_pdclust(enum_id, lid,
			      inline_test,
			      !LAYOUT_DESTROY, &l1,
			      !DUPLICATE_TEST,
			      !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);
	pdclust_layout_copy(enum_id, l1, &l1_copy);

        /*
	 * Encode the layout object and store its encoded representation into
	 * the ghost_data (g_data.cur) so that the ghost can be created at a
	 * later point.
	 */
	if (enum_id == LIST_ENUM_ID)
		allocate_area(&area_for_encode, ADDITIONAL_BYTES_DEFAULT,
			      &num_bytes_for_encode);
	else
		allocate_area(&area_for_encode, ADDITIONAL_BYTES_NONE,
			      &num_bytes_for_encode);
	bv_for_encode = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area_for_encode,
						&num_bytes_for_encode);
	c2_bufvec_cursor_init(&cur_for_encode, &bv_for_encode);
	c2_mutex_lock(&l1->l_lock);
	rc = c2_layout_encode(l1, C2_LXO_BUFFER_OP, NULL, &cur_for_encode);
	c2_mutex_unlock(&l1->l_lock);
	C2_UT_ASSERT(rc == 0);
	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur_for_encode, &bv_for_encode);
	g_data.cur = &cur_for_encode;
	g_data.lid = lid;

	/*
	 * Destroy the layout object, so that the next c2_layout_lookup() does
	 * not return right away with the layout object read from memory and
	 * instead goes to the LDB to read it.
	 */
	c2_layout_put(l1);
	C2_UT_ASSERT(list_lookup(lid) == NULL);

	/*
	 * Lookup for the layout object from the LDB, using c2_layout_lookup().
	 * But while this c2_layout_lookup() is in progress, ghost_create()
	 * will create another in-memory layout object using c2_layout_decode()
	 * performed on the serialised representation of the same layout
	 * created above and stored in g_data.cur.
	 */
	if (enum_id == LIST_ENUM_ID)
		allocate_area(&area_for_lookup, ADDITIONAL_BYTES_DEFAULT,
			      &num_bytes_for_lookup);
	else
		allocate_area(&area_for_lookup, ADDITIONAL_BYTES_NONE,
			      &num_bytes_for_lookup);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_set(&pair, &lid, area_for_lookup, num_bytes_for_lookup);
	c2_fi_enable_func("c2_layout_lookup", "ghost_creation",
			  ghost_create, &g_data);
	rc = c2_layout_lookup(&domain, lid, &c2_pdclust_layout_type,
			      &tx, &pair, &l_from_DB);
	C2_UT_ASSERT(rc == 0);
	c2_layout_put(l_from_DB);
	c2_fi_disable("c2_layout_lookup", "ghost_creation");
	rc_tmp = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc_tmp == 0);

	C2_UT_ASSERT(l_from_DB == g_data.l);
	C2_UT_ASSERT(list_lookup(lid) == l_from_DB);
	pdclust_layout_compare(enum_id, l1_copy, l_from_DB, false);
	pdclust_layout_copy_delete(enum_id, l1_copy);

	/* Destroy the layout object. */
	c2_layout_put(l_from_DB);
	C2_UT_ASSERT(list_lookup(lid) == NULL);

	c2_free(area_for_encode);
	c2_free(area_for_lookup);
	C2_LEAVE();
	return rc;
}

/* Tests the API c2_layout_lookup(). */
static void test_lookup(void)
{
	uint64_t lid;

	/*
	 * Lookup for a layout object with LIST enum type, that does not
	 * exist in the DB.
	 */
	lid = 10001;
	rc = test_lookup_pdclust(LIST_ENUM_ID, lid,
				 !EXISTING_TEST,
				 MORE_THAN_INLINE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOENT);

	/*
	 * Add a layout object with PDCLUST layout type, LIST enum type and
	 * with a few inline entries only. Then perform lookup for it.
	 */
	lid = 10002;
	rc = test_lookup_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 LESS_THAN_INLINE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Add a layout object with PDCLUST layout type, LIST enum type and
	 * with a number of inline entries exactly equal to
	 * LDB_MAX_INLINE_COB_ENTRIES. Then perform lookup for it.
	 */
	lid = 10003;
	rc = test_lookup_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 EXACT_INLINE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Add a layout object with PDCLUST layout type, LIST enum type
	 * including noninline entries.
	 * Then perform lookup for it.
	 */
	lid = 10004;
	rc = test_lookup_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 MORE_THAN_INLINE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Now that a few entries are added into the DB, once again, lookup
	 * for a layout object that does not exist in the DB.
	 */
	lid = 10005;
	rc = test_lookup_pdclust(LIST_ENUM_ID, lid,
				 !EXISTING_TEST,
				 MORE_THAN_INLINE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOENT);

	/*
	 * Add a layout object with PDCLUST layout type and LINEAR enum type.
	 * Then perform lookup for it.
	 */
	lid = 10006;
	rc = test_lookup_pdclust(LINEAR_ENUM_ID, lid,
				 EXISTING_TEST,
				 INLINE_NOT_APPLICABLE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Simulate that another layout object with the same layout id is
	 * created while the first layout object is being allocated by
	 * c2_layout_lookup() with having the domain lock released.
	 */
	lid = 10007;
	rc = test_lookup_with_ghost_creation(LINEAR_ENUM_ID, lid,
					     INLINE_NOT_APPLICABLE);
	C2_UT_ASSERT(rc == 0);
}

/* Tests the API c2_layout_lookup(). */
static void test_lookup_failure(void)
{
	uint64_t           lid;
	struct c2_db_tx    tx;
	struct c2_db_pair  pair;
	struct c2_layout  *l;

	C2_ENTRY();

	/*
	 * Lookup for a layout object with LIST enum type, that does not
	 * exist in the DB.
	 */
	lid = 10008;
	rc = test_lookup_pdclust(LIST_ENUM_ID, lid,
				 !EXISTING_TEST,
				 MORE_THAN_INLINE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOENT);

	/*
	 * Lookup for a layout object with LINEAR enum type, that does not
	 * exist in the DB.
	 */
	lid = 10009;
	rc = test_lookup_pdclust(LINEAR_ENUM_ID, lid,
				 !EXISTING_TEST,
				 INLINE_NOT_APPLICABLE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOENT);

	/* Simulate pdclust_allocate() failure in c2_layout_lookup(). */
	lid = 10010;
	c2_fi_enable_off_n_on_m("pdclust_allocate", "mem_err", 1, 1);
	rc = test_lookup_pdclust(LINEAR_ENUM_ID, lid,
				 EXISTING_TEST,
				 INLINE_NOT_APPLICABLE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOMEM);
	c2_fi_disable("pdclust_allocate", "mem_err");

	/* Simulate c2_layout_decode() failure in c2_layout_lookup(). */
	lid = 10011;
	c2_fi_enable_once("c2_layout_decode", "lo_decode_err");
	rc = test_lookup_pdclust(LINEAR_ENUM_ID, lid,
				 EXISTING_TEST,
				 INLINE_NOT_APPLICABLE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == LO_DECODE_ERR);

	/* Furnish c2_layout_lookup() with unregistered layout type. */
	struct c2_layout_type test_layout_type = {
		.lt_name     = "test",
		.lt_id       = 1,
		.lt_domain   = NULL,
		.lt_ops      = NULL
	};
	lid = 10012;
	rc = c2_layout_lookup(&domain, lid, &test_layout_type, &tx, &pair, &l);
	C2_UT_ASSERT(rc == -EPROTO);

	/*
	 * Simulate cursor init error in noninline_read() that is in the path
	 * of list_decode() that is in the path of c2_layout_decode().
	 */
	lid = 10013;
	c2_fi_enable_once("noninline_read", "cursor_init_err");
	rc = test_lookup_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 MORE_THAN_INLINE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOENT);

	/*
	 * Simulate cursor get error in noninline_read() that is in the path
	 * of list_decode() that is in the path of c2_layout_decode().
	 */
	lid = 10014;
	c2_fi_enable_once("noninline_read", "cursor_get_err");
	rc = test_lookup_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 MORE_THAN_INLINE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOMEM);

	/*
	 * Simulate invalid fid error in noninline_read() that is in the path
	 * of list_decode() that is in the path of c2_layout_decode().
	 */
	lid = 10015;
	c2_fi_enable_once("noninline_read", "invalid_fid_err");
	rc = test_lookup_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 MORE_THAN_INLINE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -EPROTO);

	C2_LEAVE();
}

/* Tests the API c2_layout_add(), for the PDCLUST layout type. */
static int test_add_pdclust(uint32_t enum_id, uint64_t lid,
			    uint32_t inline_test,
			    bool layout_destroy, struct c2_layout **l_obj,
			    bool duplicate_test,
			    bool failure_test)
{
	c2_bcount_t                   num_bytes;
	uint32_t                      N;
	uint32_t                      K;
	uint32_t                      P;
	void                         *area;
	struct c2_pdclust_layout     *pl;
	struct c2_db_pair             pair;
	struct c2_db_tx               tx;
	struct c2_uint128             seed;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	int                           rc_tmp;

	C2_ENTRY("lid %llu", (unsigned long long)lid);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);
	C2_UT_ASSERT(ergo(layout_destroy, l_obj == NULL));
	C2_UT_ASSERT(ergo(!layout_destroy, l_obj != NULL));
	C2_UT_ASSERT(ergo(duplicate_test, !failure_test));

	c2_uint128_init(&seed, "addpdclustlayout");

	allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	/* Build a layout object. */
	NKP_assign_and_pool_init(enum_id,
				 inline_test, 7, 1900, 1900,
				 &N, &K, &P);

	rc = pdclust_layout_build(enum_id, lid,
				  N, K, P, &seed,
				  100, 200,
				  &pl, &list_enum, &lin_enum,
				  !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* Add the layout object to the DB. */
	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_set(&pair, &lid, area, num_bytes);

	rc = c2_layout_add(&pl->pl_base.sl_base, &tx, &pair);
	if (failure_test)
		C2_UT_ASSERT(rc == -ENOENT || rc == LO_ENCODE_ERR);
	else
		C2_UT_ASSERT(rc == 0);

	rc_tmp = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc_tmp == 0);

	C2_UT_ASSERT(list_lookup(lid) == &pl->pl_base.sl_base);

	/*
	 * If duplicate_test is true, again try to add the same layout object
	 * to the DB, to verify that it results into EEXIST error.
	 */
	if (duplicate_test) {
		rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
		C2_UT_ASSERT(rc == 0);

		pair_set(&pair, &lid, area, num_bytes);

		rc = c2_layout_add(&pl->pl_base.sl_base, &tx, &pair);
		C2_UT_ASSERT(rc == -EEXIST);

		rc_tmp = c2_db_tx_commit(&tx);
		C2_UT_ASSERT(rc_tmp == 0);
	}

	if (layout_destroy) {
		c2_layout_put(&pl->pl_base.sl_base);
		C2_UT_ASSERT(list_lookup(lid) == NULL);
	}
	else
		*l_obj = &pl->pl_base.sl_base;

	c2_free(area);
	c2_pool_fini(&pool);
	C2_LEAVE("lid %llu", (unsigned long long)lid);
	return rc;
}

/* Tests the API c2_layout_add(). */
static void test_add(void)
{
	uint64_t lid;

	/*
	 * Add a layout object with PDCLUST layout type, LIST enum type and
	 * with a few inline entries only.
	 */
	lid = 11001;
	rc = test_add_pdclust(LIST_ENUM_ID, lid,
			      LESS_THAN_INLINE,
			      LAYOUT_DESTROY, NULL,
			      !DUPLICATE_TEST,
			      !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Add a layout object with PDCLUST layout type, LIST enum type and
	 * with a number of inline entries exactly equal to
	 * LDB_MAX_INLINE_COB_ENTRIES.
	 */
	lid = 11002;
	rc = test_add_pdclust(LIST_ENUM_ID, lid,
			      EXACT_INLINE,
			      LAYOUT_DESTROY, NULL,
			      !DUPLICATE_TEST,
			      !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Add a layout object with PDCLUST layout type and LIST enum type
	 * including noninline entries.
	 */
	lid = 11003;
	rc = test_add_pdclust(LIST_ENUM_ID, lid,
			      MORE_THAN_INLINE,
			      LAYOUT_DESTROY, NULL,
			      !DUPLICATE_TEST,
			      !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* Add a layout object with PDCLUST layout type and LINEAR enum type. */
	lid = 11004;
	rc = test_add_pdclust(LINEAR_ENUM_ID, lid,
			      INLINE_NOT_APPLICABLE,
			      LAYOUT_DESTROY, NULL,
			      !DUPLICATE_TEST,
			      !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);
}

static void test_add_failure(void)
{
	uint64_t lid;

	/* Simulate c2_layout_encode() failure in c2_layout_add(). */
	lid = 11005;
	c2_fi_enable_once("c2_layout_encode", "lo_encode_err");
	rc = test_add_pdclust(LIST_ENUM_ID, lid,
			      MORE_THAN_INLINE,
			      LAYOUT_DESTROY, NULL,
			      !DUPLICATE_TEST,
			      FAILURE_TEST);
	C2_UT_ASSERT(rc == LO_ENCODE_ERR);

	/*
	 * Simulate the error that entry already exists in the layout DB with
	 * the dame layout id.
	 */
	lid = 11006;
	rc = test_add_pdclust(LINEAR_ENUM_ID, lid,
			      INLINE_NOT_APPLICABLE,
			      LAYOUT_DESTROY, NULL,
			      DUPLICATE_TEST,
			      !FAILURE_TEST);
	C2_UT_ASSERT(rc == -EEXIST);

	/*
	 * Simulate cursor init failure in noninline_write() that is in the
	 * path of list_encode() which is in the path of c2_layout_encode().
	 */
	lid = 11007;
	c2_fi_enable_once("noninline_write", "cursor_init_err");
	rc = test_add_pdclust(LIST_ENUM_ID, lid,
			      MORE_THAN_INLINE,
			      LAYOUT_DESTROY, NULL,
			      !DUPLICATE_TEST,
			      FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOENT);

	/*
	 * Simulate cursor add failure in noninline_write() that is in the
	 * path of list_encode() which is in the path of c2_layout_encode().
	 */
	lid = 11008;
	c2_fi_enable_once("noninline_write", "cursor_add_err");
	rc = test_add_pdclust(LIST_ENUM_ID, lid,
			      MORE_THAN_INLINE,
			      LAYOUT_DESTROY, NULL,
			      !DUPLICATE_TEST,
			      FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOENT);

}

/* Tests the API c2_layout_update(), for the PDCLUST layout type. */
static int test_update_pdclust(uint32_t enum_id, uint64_t lid,
			       bool existing_test,
			       uint32_t inline_test,
			       bool failure_test)
{
	c2_bcount_t                   num_bytes;
	void                         *area;
	struct c2_db_pair             pair;
	struct c2_db_tx               tx;
	struct c2_layout             *l1;
	struct c2_layout             *l1_copy;
	struct c2_layout             *l2;
	uint32_t                      i;
	struct c2_uint128             seed;
	uint32_t                      N;
	uint32_t                      K;
	uint32_t                      P;
	struct c2_pdclust_layout     *pl;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	int                           rc_tmp;

	C2_ENTRY();
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	NKP_assign_and_pool_init(enum_id,
				 inline_test, 13, 123, 1230,
				 &N, &K, &P);

	if (existing_test) {
		/* Add a layout object to the DB. */
		rc = test_add_pdclust(enum_id, lid,
				      inline_test,
				      !LAYOUT_DESTROY, &l1,
				      !DUPLICATE_TEST,
				      !FAILURE_TEST);
		C2_UT_ASSERT(rc == 0);
	} else {
		/* Build a layout object. */
		c2_uint128_init(&seed, "updatepdclustlay");

		rc = pdclust_layout_build(enum_id, lid,
					  N, K, P, &seed,
					  10, 20,
					  &pl, &list_enum, &lin_enum,
					  !FAILURE_TEST);
		C2_UT_ASSERT(rc == 0);
		l1 = &pl->pl_base.sl_base;
	}

	/* Verify the original user count is as expected. */
	C2_UT_ASSERT(l1->l_user_count == 0);

	/* Update the in-memory layout object - update its user count. */
	for (i = 0; i < 100; ++i)
		c2_layout_user_count_inc(l1);
	C2_UT_ASSERT(l1->l_user_count == 100);

	/* Update the layout object in the DB. */
	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_set(&pair, &lid, area, num_bytes);

	rc = c2_layout_update(l1, &tx, &pair);
	if (failure_test)
		C2_UT_ASSERT(rc == LO_ENCODE_ERR || rc == L_TABLE_UPDATE_ERR);
	else
		C2_UT_ASSERT(rc == 0);
	/*
	 * Even a non-existing record can be written to the database using
	 * the database update operation.
	 */
	if (existing_test && !failure_test)
		pdclust_layout_copy(enum_id, l1, &l1_copy);

	rc_tmp = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc_tmp == 0);

	/*
	 * Update the in-memory layout object - update its user count. This is
	 * to verify the functioning of c2_layout_user_count_dec().
	 */
	for (i = 0; i < 50; ++i)
		c2_layout_user_count_dec(l1);
	C2_UT_ASSERT(l1->l_user_count == 50);

	/* Delete the in-memory layout. */
	c2_layout_put(l1);
	C2_UT_ASSERT(list_lookup(lid) == NULL);

	if (existing_test && !failure_test) {
		/*
		 * Lookup for the layout object from the DB to verify that its
		 * reference count is indeed updated.
		 */
		rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
		C2_UT_ASSERT(rc == 0);

		pair_set(&pair, &lid, area, num_bytes);

		rc = c2_layout_lookup(&domain, lid, &c2_pdclust_layout_type,
				      &tx, &pair, &l2);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(l2->l_user_count == 100);
		C2_UT_ASSERT(l2->l_ref == 1);

		rc = c2_db_tx_commit(&tx);
		C2_UT_ASSERT(rc == 0);

		/*
		 * Compare the two layouts - one created earlier here and the
		 * one that is looked up from the DB.
		 */
		pdclust_layout_compare(enum_id, l1_copy, l2, false);
		pdclust_layout_copy_delete(enum_id, l1_copy);

		/* Delete the in-memory layout. */
		c2_layout_put(l2);
		C2_UT_ASSERT(list_lookup(lid) == NULL);
	}

	c2_free(area);
	c2_pool_fini(&pool);
	C2_LEAVE();
	return rc;
}

/* Tests the API c2_layout_update(). */
static void test_update(void)
{
	uint64_t lid;

	/*
	 * Try to update a layout object with PDCLUST layout type and LIST enum
	 * type, that does not exist in the DB to verify that the operation
	 * fails with the error ENOENT.
	 */
	lid = 12001;
	rc = test_update_pdclust(LIST_ENUM_ID, lid,
				 !EXISTING_TEST,
				 MORE_THAN_INLINE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Update a layout object with PDCLUST layout type, LIST enum type and
	 * with a few inline entries only.
	 */
	lid = 12002;
	rc = test_update_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 LESS_THAN_INLINE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Update a layout object with PDCLUST layout type, LIST enum type and
	 * with number of inline entries exactly equal to
	 * LDB_MAX_INLINE_COB_ENTRIES.
	 */
	lid = 12003;
	rc = test_update_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 EXACT_INLINE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Update a layout object with PDCLUST layout type and LIST enum
	 * type including noninline entries.
	 */
	lid = 12004;
	rc = test_update_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 MORE_THAN_INLINE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Update a layout object with PDCLUST layout type and LINEAR enum
	 * type.
	 */
	lid = 12005;
	rc = test_update_pdclust(LINEAR_ENUM_ID, lid,
				 EXISTING_TEST,
				 INLINE_NOT_APPLICABLE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);
}

static void test_update_failure(void)
{
	uint64_t lid;

	/* Simulate c2_layout_encode() failure in c2_layout_update(). */
	lid = 12006;
	c2_fi_enable_off_n_on_m("c2_layout_encode", "lo_encode_err", 1, 1);
	rc = test_update_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 MORE_THAN_INLINE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == LO_ENCODE_ERR);
	c2_fi_disable("c2_layout_encode", "lo_encode_err");

	/* Simulate c2_table_update() failure in c2_layout_update(). */
	lid = 12007;
	c2_fi_enable_once("c2_layout_update", "table_update_err");
	rc = test_update_pdclust(LINEAR_ENUM_ID, lid,
				 EXISTING_TEST,
				 INLINE_NOT_APPLICABLE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == L_TABLE_UPDATE_ERR);
}

/* Tests the API c2_layout_delete(), for the PDCLUST layout type. */
static int test_delete_pdclust(uint32_t enum_id, uint64_t lid,
			       bool existing_test,
			       uint32_t inline_test,
			       uint32_t failure_test)
{
	c2_bcount_t                   num_bytes;
	void                         *area;
	struct c2_db_pair             pair;
	struct c2_db_tx               tx;
	struct c2_layout             *l;
	struct c2_uint128             seed;
	uint32_t                      N;
	uint32_t                      K;
	uint32_t                      P;
	struct c2_pdclust_layout     *pl;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	int                           rc_tmp;

	C2_ENTRY();
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	NKP_assign_and_pool_init(enum_id,
				 inline_test, 12, 122, 1220,
				 &N, &K, &P);
	if (existing_test) {
		/* Add a layout object to the DB. */
		rc = test_add_pdclust(enum_id, lid,
				      inline_test,
				      !LAYOUT_DESTROY, &l,
				      !DUPLICATE_TEST,
				      !FAILURE_TEST);
		C2_UT_ASSERT(rc == 0);
	} else {
		/* Build a layout object. */
		c2_uint128_init(&seed, "deletepdclustlay");

		rc = pdclust_layout_build(enum_id, lid,
					  N, K, P, &seed,
					  10, 20,
					  &pl, &list_enum, &lin_enum,
					  !FAILURE_TEST);
		C2_UT_ASSERT(rc == 0);
		l = &pl->pl_base.sl_base;
	}

	if (C2_FI_ENABLED("nonzero_user_count_err"))
		c2_layout_user_count_inc(l);

	/* Delete the layout object from the DB. */
	pair_set(&pair, &lid, area, num_bytes);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	rc = c2_layout_delete(l, &tx, &pair);
	if (failure_test)
		C2_UT_ASSERT(rc == -ENOENT || rc == -ENOMEM ||
			     rc == -EPROTO || rc == LO_ENCODE_ERR);
	else
		C2_UT_ASSERT(rc == 0);

	rc_tmp = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc_tmp == 0);

	/* Destroy the layout object. */
	c2_layout_put(l);
	C2_UT_ASSERT(list_lookup(lid) == NULL);

	if (!failure_test) {
		/*
		 * Lookup for the layout object from the DB, to verify that it
		 * does not exist there and that the lookup results into
		 * ENOENT error.
		 */
		rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
		C2_UT_ASSERT(rc == 0);

		pair_set(&pair, &lid, area, num_bytes);

		rc_tmp = c2_layout_lookup(&domain, lid, &c2_pdclust_layout_type,
					  &tx, &pair, &l);
		C2_UT_ASSERT(rc_tmp == -ENOENT);

		rc_tmp = c2_db_tx_commit(&tx);
		C2_UT_ASSERT(rc_tmp == 0);
	}

	c2_free(area);
	c2_pool_fini(&pool);
	C2_LEAVE();
	return rc;
}

/* Tests the API c2_layout_delete(). */
static void test_delete(void)
{
	uint64_t lid;

	/*
	 * Delete a layout object with PDCLUST layout type, LIST enum type and
	 * with a few inline entries only.
	 */
	lid = 13001;
	rc = test_delete_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 LESS_THAN_INLINE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Delete a layout object with PDCLUST layout type, LIST enum type and
	 * with a number of inline entries exactly equal to
	 * LDB_MAX_INLINE_COB_ENTRIES.
	 */
	lid = 13002;
	rc = test_delete_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 EXACT_INLINE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Delete a layout object with PDCLUST layout type and LIST enum
	 * type including noninline entries.
	 */
	lid = 13003;
	rc = test_delete_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 MORE_THAN_INLINE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Delete a layout object with PDCLUST layout type and LINEAR enum
	 * type.
	 */
	lid = 13004;
	rc = test_delete_pdclust(LINEAR_ENUM_ID, lid,
				 EXISTING_TEST,
				 INLINE_NOT_APPLICABLE,
				 !FAILURE_TEST);
	C2_UT_ASSERT(rc == 0);
}

static void test_delete_failure(void)
{
	uint64_t lid;

	/* Simulate c2_layout_encode() failure in c2_layout_delete(). */
	lid = 13005;
	c2_fi_enable_off_n_on_m("c2_layout_encode", "lo_encode_err", 1, 1);
	rc = test_delete_pdclust(LINEAR_ENUM_ID, lid,
				 EXISTING_TEST,
				 INLINE_NOT_APPLICABLE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == LO_ENCODE_ERR);
	c2_fi_disable("c2_layout_encode", "lo_encode_err");

	/*
	 * Try to delete a layout object with PDCLUST layout type and LINEAR
	 * enum type, that does not exist in the DB, to verify that it results
	 * into the error ENOENT.
	 */
	lid = 13006;
	rc = test_delete_pdclust(LINEAR_ENUM_ID, lid,
				 !EXISTING_TEST,
				 INLINE_NOT_APPLICABLE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOENT);

	/*
	 * Simulate cursor get failure in noninline_write() that is in the
	 * path of list_encode() which is in the path of c2_layout_encode().
	 */
	lid = 13007;
	c2_fi_enable_once("noninline_write", "cursor_get_err");
	rc = test_delete_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 MORE_THAN_INLINE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOENT);

	/*
	 * Simulate cursor delete failure in noninline_write() that is in the
	 * path of list_encode() which is in the path of c2_layout_encode().
	 */
	lid = 13008;
	c2_fi_enable_once("noninline_write", "cursor_del_err");
	rc = test_delete_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 MORE_THAN_INLINE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -ENOMEM);

	/*
	 * Try to delete a layout with PDCLUST layout type and LINEAR
	 * enum type, that has non-zero user count, to verify that it results
	 * into the error -EINVAL.
	 */
	lid = 13009;
	c2_fi_enable_once("test_delete_pdclust", "nonzero_user_count_err");
	rc = test_delete_pdclust(LINEAR_ENUM_ID, lid,
				 EXISTING_TEST,
				 INLINE_NOT_APPLICABLE,
				 FAILURE_TEST);
	C2_UT_ASSERT(rc == -EPROTO);
}

#endif /* __KERNEL__ */

const struct c2_test_suite layout_ut = {
	.ts_name  = "layout-ut",
	.ts_init  = test_init,
	.ts_fini  = test_fini,
	.ts_tests = {
		{ "layout-domain-init-fini", test_domain_init_fini },
		{ "layout-domain-init-fini-failure",
				test_domain_init_fini_failure },
		{ "layout-type-register-unregister", test_type_reg_unreg },
		{ "layout-etype-register-unregister", test_etype_reg_unreg },
		{ "layout-register-unregister", test_reg_unreg },
		{ "layout-register-unregister-failure",
					test_reg_unreg_failure },
		{ "layout-build", test_build },
		{ "layout-build-failure", test_build_failure },
		{ "layout-decode", test_decode },
		{ "layout-decode-failure", test_decode_failure },
		{ "layout-encode", test_encode },
		{ "layout-encode-failure", test_encode_failure },
		{ "layout-decode-encode", test_decode_encode },
		{ "layout-encode-decode", test_encode_decode },
		{ "layout-ref-get-put", test_ref_get_put },
		{ "layout-enum-ops", test_enum_operations },
		{ "layout-max-recsize", test_max_recsize },
		{ "layout-recsize", test_recsize },
		{ "layout-pdclust-instance", test_pdclust_instance },
		{ "layout-pdclust-instance-failure",
					test_pdclust_instance_failure },
#ifndef __KERNEL__
		{ "layout-lookup", test_lookup },
		{ "layout-lookup-failure", test_lookup_failure },
		{ "layout-add", test_add },
		{ "layout-add-failure", test_add_failure },
		{ "layout-update", test_update },
		{ "layout-update-failure", test_update_failure },
		{ "layout-delete", test_delete },
		{ "layout-delete-failure", test_delete_failure },
#endif
		{ NULL, NULL }
	}
};
C2_EXPORTED(layout_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
