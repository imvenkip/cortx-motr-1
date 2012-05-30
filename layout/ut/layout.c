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
#include "lib/misc.h" /* C2_SET0 */
#include "lib/bitstring.h"
#include "lib/vec.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_LAYOUT
#include "lib/trace.h" /* C2_LOG */

#include "fid/fid.h" /* c2_fid_set() */
#include "pool/pool.h" /* c2_pool_init() */
#include "layout/layout.h"
#include "layout/layout_internal.h" /* DEFAULT_REF_COUNT */
#include "layout/layout_db.h"
#include "layout/pdclust.h"
#include "layout/list_enum.h"
#include "layout/linear_enum.h"

#ifndef __KERNEL__
# include "layout/layout_db.c" /* recsize_get() */
#endif

static struct c2_dbenv         dbenv;
static const char              db_name[] = "ut-layout";
static struct c2_layout_domain domain;
static struct c2_pool          pool;
enum c2_addb_ev_level          orig_addb_level;
static int                     rc;

enum {
	DBFLAGS                  = 0,
	DEFAULT_POOL_ID          = 1,
	POOL_WIDTH               = 200,
	LIST_ENUM_ID             = 0x4C495354, /* "LIST" */
	LINEAR_ENUM_ID           = 0x4C494E45, /* "LINE" */
	A_NONE                   = 0, /* Invalid value for attribute A */
	B_NONE                   = 0, /* Invalid value for attribute B */
	ADDITIONAL_BYTES_NONE    = 0,
	ADDITIONAL_BYTES_DEFAULT = 2048,
	ONLY_INLINE_TEST         = true,
	EXISTING_TEST            = true,
	DUPLICATE_TEST           = true,
	LAYOUT_DESTROY           = true
};

extern const struct c2_layout_type c2_pdclust_layout_type;
extern const struct c2_layout_enum_type c2_list_enum_type;
extern const struct c2_layout_enum_type c2_linear_enum_type;

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
	 * when interested in ADDB messages only for LAYOUT module.
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

	/* Register all the available layout types and enum types. */
	rc = c2_layout_register(&domain);
	C2_ASSERT(rc == 0);

	/* Initialise the pool. */
	rc = c2_pool_init(&pool, DEFAULT_POOL_ID, POOL_WIDTH);
	C2_ASSERT(rc == 0);

	return rc;
}

static int test_fini(void)
{
	c2_pool_fini(&pool);

	c2_layout_unregister(&domain);

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
	.lto_max_recsize = t_max_recsize,
	.lto_recsize     = NULL,
	.lto_decode      = NULL,
	.lto_encode      = NULL
};

const struct c2_layout_type test_layout_type = {
	.lt_name     = "test",
	.lt_id       = 2,
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
	.leto_max_recsize = t_enum_max_recsize,
	.leto_recsize     = NULL,
	.leto_decode      = NULL,
	.leto_encode      = NULL
};

const struct c2_layout_enum_type test_enum_type = {
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
	const char              t_db_name[] = "t2-layout";
	struct c2_dbenv         t_dbenv;
	struct c2_layout_domain t_domain;

	C2_ENTRY();

	rc = c2_dbenv_init(&t_dbenv, t_db_name, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	/* Initialise the domain. */
	rc = c2_layout_domain_init(&t_domain, &t_dbenv);
	C2_UT_ASSERT(rc == 0);

	/* Register all the available layout types and enum types. */
	rc = c2_layout_register(&t_domain);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(t_domain.ld_enum[c2_list_enum_type.let_id] ==
		     &c2_list_enum_type);
	C2_UT_ASSERT(t_domain.ld_enum[c2_linear_enum_type.let_id] ==
		     &c2_linear_enum_type);
	C2_UT_ASSERT(t_domain.ld_type[c2_pdclust_layout_type.lt_id] ==
		     &c2_pdclust_layout_type);

	/* Unregister all the registered layout and enum types. */
	c2_layout_unregister(&t_domain);
	C2_UT_ASSERT(t_domain.ld_enum[c2_list_enum_type.let_id] == NULL);
	C2_UT_ASSERT(t_domain.ld_enum[c2_linear_enum_type.let_id] == NULL);
	C2_UT_ASSERT(t_domain.ld_type[c2_pdclust_layout_type.lt_id] == NULL);

	/*
	 * Should be able to register all the available layout types and enum
	 * types, again after unregistering those.
	 */
	rc = c2_layout_register(&t_domain);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(t_domain.ld_enum[c2_list_enum_type.let_id] ==
		     &c2_list_enum_type);
	C2_UT_ASSERT(t_domain.ld_enum[c2_linear_enum_type.let_id] ==
		     &c2_linear_enum_type);
	C2_UT_ASSERT(t_domain.ld_type[c2_pdclust_layout_type.lt_id] ==
		     &c2_pdclust_layout_type);

	/* Unregister all the registered layout and enum types. */
	c2_layout_unregister(&t_domain);
	C2_UT_ASSERT(t_domain.ld_enum[c2_list_enum_type.let_id] == NULL);
	C2_UT_ASSERT(t_domain.ld_enum[c2_linear_enum_type.let_id] == NULL);
	C2_UT_ASSERT(t_domain.ld_type[c2_pdclust_layout_type.lt_id] == NULL);

	/* Finalise the domain. */
	c2_layout_domain_fini(&t_domain);

	c2_dbenv_fini(&t_dbenv);

	C2_LEAVE();
}

/*
 * Builds a layout object with PDCLUST layout type and using the provided
 * enumeration object.
 */
static int pdclust_l_build(uint64_t lid, uint32_t N, uint32_t K,
			   uint64_t unitsize, struct c2_uint128 *seed,
			   struct c2_layout_enum *le,
			   struct c2_pdclust_layout **pl)
{
	struct c2_pool *pool;

	C2_UT_ASSERT(le != NULL);
	C2_UT_ASSERT(pl != NULL);

	rc = c2_pool_lookup(DEFAULT_POOL_ID, &pool);
	C2_UT_ASSERT(rc == 0);

	rc = c2_pdclust_build(&domain, pool, lid, N, K, unitsize, seed, le, pl);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(c2_layout_find(&domain, lid) == &(*pl)->pl_base.ls_base);

	return rc;
}

/*
 * Builds a layout object with PDCLUST layout type, by first building an
 * enumeration object with the specified enumeration type.
 */
static int pdclust_layout_build(uint32_t enum_id,
				uint64_t lid,
				uint32_t N, uint32_t K,
				uint64_t unitsize, struct c2_uint128 *seed,
				uint32_t nr,
				uint32_t A, uint32_t B, /* For linear enum.*/
				struct c2_pdclust_layout **pl,
				struct c2_layout_list_enum **list_enum,
				struct c2_layout_linear_enum **lin_enum)
{
	struct c2_fid         *cob_list;
	int                    i;
	struct c2_layout_enum *e;

	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);
	C2_UT_ASSERT(pl != NULL);

	/* Build an enumeration object with the specified enum type. */
	if (enum_id == LIST_ENUM_ID) {
		C2_UT_ASSERT(A == A_NONE && B == B_NONE && lin_enum == NULL);
		C2_UT_ASSERT(list_enum != NULL);

		C2_ALLOC_ARR(cob_list, nr);
		C2_UT_ASSERT(cob_list != NULL);

		for (i = 0; i < nr; ++i)
			c2_fid_set(&cob_list[i], i * 100 + 1, i + 1);

		rc = c2_list_enum_build(&domain, cob_list, nr, list_enum);
		C2_UT_ASSERT(rc == 0);

		e = &(*list_enum)->lle_base;

		c2_free(cob_list);
	} else { /* LINEAR_ENUM_ID */
		C2_UT_ASSERT(B != B_NONE && list_enum == NULL);
		C2_UT_ASSERT(lin_enum != NULL);

		rc = c2_linear_enum_build(&domain, nr, A, B, lin_enum);
		C2_UT_ASSERT(rc == 0);

		e = &(*lin_enum)->lle_base;
	}

	/*
	 * Build a layout object with PDCLUST layout type and using the
	 * enumeration object built earlier here.
	 */
	rc = pdclust_l_build(lid, N, K, unitsize, seed, e, pl);
	C2_UT_ASSERT(rc == 0);

	return rc;
}

/* Verifies generic part of the layout object. */
static void l_verify(uint64_t lid, struct c2_layout *l)
{
	C2_UT_ASSERT(l->l_id == lid);
	C2_UT_ASSERT(l->l_ref >= DEFAULT_REF_COUNT);
	C2_UT_ASSERT(l->l_ops != NULL);
}

/*
 * Verifies generic part of the layout object and the PDCLUST layout type
 * specific part of it.
 */
static void pdclust_l_verify(uint64_t lid,
			     uint32_t N, uint32_t K,
			     uint64_t unitsize, struct c2_uint128 *seed,
			     struct c2_pdclust_layout *pl)
{
	/* Verify generic part of the layout object. */
	l_verify(lid, &pl->pl_base.ls_base);

	/* Verify PDCLUST layout type specific part of the layout object. */
	C2_UT_ASSERT(pl->pl_attr.pa_N == N);
	C2_UT_ASSERT(pl->pl_attr.pa_K == K);
	C2_UT_ASSERT(pl->pl_attr.pa_P == POOL_WIDTH);
	C2_UT_ASSERT(pl->pl_attr.pa_unit_size == unitsize);
	C2_UT_ASSERT(c2_uint128_eq(&pl->pl_attr.pa_seed, seed));
}

/* Verifies the layout object against the various input arguments. */
static void pdclust_layout_verify(uint32_t enum_id, uint64_t lid,
				  uint32_t N, uint32_t K,
				  uint64_t unitsize, struct c2_uint128 *seed,
				  uint32_t nr,
				  uint32_t A, uint32_t B, /* For lin enum */
				  struct c2_layout *l)
{
	struct c2_pdclust_layout     *pl;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	int                           i;
	struct c2_fid                 cob_id;

	C2_UT_ASSERT(l != NULL);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);
	C2_UT_ASSERT(l->l_type == &c2_pdclust_layout_type);

	pl = container_of(l, struct c2_pdclust_layout, pl_base.ls_base);

	/*
	 * Verify generic and PDCLUST layout type specific parts of the
	 * layout object.
	 */
	pdclust_l_verify(lid, N, K, unitsize, seed, pl);

	/* Verify enum type specific part of the layout object. */
	C2_UT_ASSERT(pl->pl_base.ls_enum != NULL);

	if (enum_id == LIST_ENUM_ID) {
		C2_UT_ASSERT(A == A_NONE && B == B_NONE);
		list_enum = container_of(pl->pl_base.ls_enum,
					 struct c2_layout_list_enum, lle_base);
		for(i = 0; i < list_enum->lle_nr; ++i) {
			c2_fid_set(&cob_id, i * 100 + 1, i + 1);
			C2_UT_ASSERT(c2_fid_eq(&cob_id,
					      &list_enum->lle_list_of_cobs[i]));
		}
	} else {
		C2_UT_ASSERT(B != B_NONE);
		lin_enum = container_of(pl->pl_base.ls_enum,
					struct c2_layout_linear_enum, lle_base);
		C2_UT_ASSERT(lin_enum->lle_attr.lla_nr == nr);
		C2_UT_ASSERT(lin_enum->lle_attr.lla_A == A);
		C2_UT_ASSERT(lin_enum->lle_attr.lla_B == B);
	}
}

/*
 * Tests the APIs supported for enumeration object build, layout object build
 * and layout dstruction that happens using c2_layout_put(), and also the API
 * c2_layout_find(), specifically for the PDCLUST layout type.
 */
static int test_build_pdclust(uint32_t enum_id, uint64_t lid,
			      bool only_inline_test)
{
	uint32_t                      nr;
	struct c2_uint128             seed;
	uint32_t                      A;
	uint32_t                      B;
	struct c2_pdclust_layout     *pl;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;

	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	c2_uint128_init(&seed, "buildpdclustlayo");

	if (enum_id == LIST_ENUM_ID) {
		nr = only_inline_test ? 9 : 109;
		A = A_NONE;
		B = B_NONE;
		rc = pdclust_layout_build(enum_id, lid,
					  40, 10, 4096, &seed,
					  nr, A, B,
					  &pl, &list_enum, NULL);
	} else {
		nr = 12000;
		A = 10;
		B = 20;
		rc = pdclust_layout_build(enum_id, lid,
					  40, 10, 4096, &seed,
					  nr, A, B,
					  &pl, NULL, &lin_enum);
	}
	C2_UT_ASSERT(rc == 0);

	/* Verify the layout object built earlier here. */
	pdclust_layout_verify(enum_id, lid,
			      40, 10, 4096, &seed,
			      nr, A, B, &pl->pl_base.ls_base);

	C2_UT_ASSERT(c2_layout_find(&domain, lid) == &pl->pl_base.ls_base);

	/*
	 * Delete the layout by first increasing a reference and then
	 * releasing that only reference.
	 */
	c2_layout_get(&pl->pl_base.ls_base);
	c2_layout_put(&pl->pl_base.ls_base);
	C2_UT_ASSERT(c2_layout_find(&domain, lid) == NULL);

	return rc;
}

/*
 * Tests the APIs supported for enumeration object build, layout object build
 * and layout dstruction that happens using c2_layout_put() and also the API
 * c2_layout_find().
 */
static void test_build(void)
{
	uint64_t lid;

	/*
	 * Build a layout object with PDCLUST layout type, LIST enum type
	 * with inline entries only and then destroy it.
	 */
	lid = 1001;
	rc = test_build_pdclust(LIST_ENUM_ID, lid, ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout object with PDCLUST layout type and LIST enum
	 * type and then destroy it.
	 */
	lid = 1002;
	rc = test_build_pdclust(LIST_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout object with PDCLUST layout type and LINEAR enum
	 * type and then destroy it.
	 */
	lid = 1003;
	rc = test_build_pdclust(LINEAR_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);
}

/* Builds part of the buffer representing generic part of the layout object. */
static void buf_build(uint32_t lt_id, struct c2_bufvec_cursor *dcur)
{
	struct c2_layout_rec rec;
	c2_bcount_t          nbytes_copied;

	rec.lr_lt_id     = lt_id;
	rec.lr_ref_count = DEFAULT_REF_COUNT;
	rec.lr_pool_id   = DEFAULT_POOL_ID;

	nbytes_copied = c2_bufvec_cursor_copyto(dcur, &rec, sizeof rec);
	C2_UT_ASSERT(nbytes_copied == sizeof rec);
}

/*
 * Builds part of the buffer representing generic and PDCLUST layout type
 * specific parts of the layout object.
 */
static void pdclust_buf_build(uint32_t let_id, uint64_t lid,
			      uint32_t N, uint32_t K,
			      uint64_t unitsize, struct c2_uint128 *seed,
			      struct c2_bufvec_cursor *dcur)
{
	struct c2_layout_pdclust_rec pl_rec;
	c2_bcount_t                  nbytes_copied;

	buf_build(c2_pdclust_layout_type.lt_id, dcur);

	pl_rec.pr_let_id            = let_id;
	pl_rec.pr_attr.pa_N         = N;
	pl_rec.pr_attr.pa_K         = K;
	pl_rec.pr_attr.pa_P         = POOL_WIDTH;
	pl_rec.pr_attr.pa_unit_size = unitsize;
	pl_rec.pr_attr.pa_seed      = *seed;

	nbytes_copied = c2_bufvec_cursor_copyto(dcur, &pl_rec, sizeof pl_rec);
	C2_UT_ASSERT(nbytes_copied == sizeof pl_rec);
}

/* Builds a buffer containing serialised representation of a layout object. */
static int pdclust_layout_buf_build(uint32_t enum_id, uint64_t lid,
				    uint32_t N, uint32_t K,
				    uint64_t unitsize, struct c2_uint128 *seed,
				    uint32_t nr,
				    uint32_t A, uint32_t B,/* For linear enum */
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
	C2_UT_ASSERT(ergo(enum_id == LIST_ENUM_ID,
			  A == A_NONE && B == B_NONE));
	C2_UT_ASSERT(ergo(enum_id == LINEAR_ENUM_ID, B != B_NONE));

	/*
	 * Build part of the buffer representing generic and the PDCLUST layout
	 * type specific parts of the layout object.
	 */
	let_id = enum_id == LIST_ENUM_ID ? c2_list_enum_type.let_id :
					   c2_linear_enum_type.let_id;
	pdclust_buf_build(let_id, lid, N, K, unitsize, seed, dcur);

	/*
	 * Build part of the buffer representing enum type specific part of
	 * the layout object.
	 */
	if (enum_id == LIST_ENUM_ID) {
		ce_header.ces_nr = nr;
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
		lin_rec.lla_nr = nr;
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
			       bool only_inline_test)
{
	void                    *area;
	c2_bcount_t              num_bytes;
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;
	struct c2_layout        *l;
	struct c2_uint128        seed;
	uint32_t                 nr;
	uint32_t                 A;
	uint32_t                 B;

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

	if (enum_id == LIST_ENUM_ID) {
		nr = only_inline_test ? 5 : 125;
		A = A_NONE;
		B = B_NONE;
	} else {
		nr = 1500;
		A = 777;
		B = 888;
	}

	rc = pdclust_layout_buf_build(enum_id, lid,
				      60, 6, 4096, &seed,
				      nr, A, B, &cur);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);

	/* Decode the layout buffer into a layout object. */
	rc = c2_layout_decode(&domain, lid, C2_LXO_BUFFER_OP, NULL, &cur, &l);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(c2_layout_find(&domain, lid) == l);

	/* Verify the layout object built by c2_layout_decode(). */
	pdclust_layout_verify(enum_id, lid,
			      60, 6, 4096, &seed,
			      nr, A, B, l);

	/* Destroy the layout object. */
	c2_layout_get(l);
	c2_layout_put(l);
	C2_UT_ASSERT(c2_layout_find(&domain, lid) == NULL);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

/* Tests the API c2_layout_decode(). */
static void test_decode(void)
{
	uint64_t lid;

	/*
	 * Decode a layout object with PDCLUST layout type, LIST enum type
	 * with inline entries only.
	 */
	lid = 2001;
	rc = test_decode_pdclust(LIST_ENUM_ID, lid, ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Decode a layout object with PDCLUST layout type and LIST enum
	 * type.
	 */
	lid = 2002;
	rc = test_decode_pdclust(LIST_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Decode a layout object with PDCLUST layout type and LINEAR enum
	 * type.
	 */
	lid = 2003;
	rc = test_decode_pdclust(LINEAR_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);
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

	C2_UT_ASSERT(rec->lr_ref_count == DEFAULT_REF_COUNT);
	C2_UT_ASSERT(rec->lr_pool_id == DEFAULT_POOL_ID);

	c2_bufvec_cursor_move(cur, sizeof *rec);
}

/*
 * Verifies part of the layout buffer representing PDCLUST layout type specific
 * part of the layout object.
 */
static void pdclust_lbuf_verify(uint32_t N, uint32_t K, uint64_t unitsize,
				struct c2_uint128 *seed,
				struct c2_bufvec_cursor *cur,
				uint32_t *let_id)
{
	struct c2_layout_pdclust_rec *pl_rec;

	C2_UT_ASSERT(c2_bufvec_cursor_step(cur) >= sizeof *pl_rec);

	pl_rec = c2_bufvec_cursor_addr(cur);

	C2_UT_ASSERT(pl_rec->pr_attr.pa_N == N);
	C2_UT_ASSERT(pl_rec->pr_attr.pa_K == K);
	C2_UT_ASSERT(pl_rec->pr_attr.pa_P == POOL_WIDTH);
	C2_UT_ASSERT(c2_uint128_eq(&pl_rec->pr_attr.pa_seed, seed));
	C2_UT_ASSERT(pl_rec->pr_attr.pa_unit_size == unitsize);

	*let_id = pl_rec->pr_let_id;
	c2_bufvec_cursor_move(cur, sizeof *pl_rec);
}

/* Verifies layout buffer against the various input arguments. */
static void pdclust_layout_buf_verify(uint32_t enum_id, uint64_t lid,
				      uint32_t N, uint32_t K,
				      uint64_t unitsize,
				      struct c2_uint128 *seed,
				      uint32_t nr,
				      uint32_t A, uint32_t B, /* For lin enum*/
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
	pdclust_lbuf_verify(N, K, unitsize, seed, cur, &let_id);

	/* Verify enum type specific part of the layout buffer. */
	if (enum_id == LIST_ENUM_ID) {
		C2_UT_ASSERT(let_id == c2_list_enum_type.let_id);

		C2_UT_ASSERT(c2_bufvec_cursor_step(cur) >= sizeof *ce_header);

		ce_header = c2_bufvec_cursor_addr(cur);
		C2_UT_ASSERT(ce_header != NULL);
		c2_bufvec_cursor_move(cur, sizeof *ce_header);

		C2_UT_ASSERT(ce_header->ces_nr == nr);
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
		C2_UT_ASSERT(lin_attr->lla_nr == nr);
		C2_UT_ASSERT(lin_attr->lla_A == A);
		C2_UT_ASSERT(lin_attr->lla_B == B);
	}
}

/* Tests the API c2_layout_encode() for PDCLUST layout type. */
static int test_encode_pdclust(uint32_t enum_id, uint64_t lid,
			       bool only_inline_test)
{
	struct c2_pdclust_layout     *pl;
	void                         *area;
	c2_bcount_t                   num_bytes;
	struct c2_bufvec              bv;
	struct c2_bufvec_cursor       cur;
	struct c2_uint128             seed;
	uint32_t                      nr;
	uint32_t                      A;
	uint32_t                      B;
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

	if (enum_id == LIST_ENUM_ID) {
		nr = only_inline_test ? 10 : 120;
		A = A_NONE;
		B = B_NONE;
		rc = pdclust_layout_build(LIST_ENUM_ID, lid,
					  40, 10, 4096, &seed,
					  nr, A, B,
					  &pl, &list_enum, NULL);
	} else {
		nr = 120;
		A = 10;
		B = 20;
		rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
					  40, 10, 4096, &seed,
					  nr, A, B,
					  &pl, NULL, &lin_enum);
	}
	C2_UT_ASSERT(rc == 0);

	/* Encode the layout object into a layout buffer. */
	rc  = c2_layout_encode(&pl->pl_base.ls_base, C2_LXO_BUFFER_OP, NULL,
			       NULL, &cur);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);

	/* Verify the layout buffer produced by c2_layout_encode(). */
	pdclust_layout_buf_verify(enum_id, lid,
				  40, 10, 4096, &seed,
				  nr, A, B, &cur);

	/* Delete the layout object. */
	c2_layout_get(&pl->pl_base.ls_base);
	c2_layout_put(&pl->pl_base.ls_base);
	C2_UT_ASSERT(c2_layout_find(&domain, lid) == NULL);
	c2_free(area);

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
	 * with only inline entries.
	 */
	lid = 3001;
	rc = test_encode_pdclust(LIST_ENUM_ID, lid, ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* Encode for PDCLUST layout type and LIST enumeration type. */
	lid = 3002;
	rc = test_encode_pdclust(LIST_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* Encode for PDCLUST layout type and LINEAR enumeration type. */
	lid = 3003;
	rc = test_encode_pdclust(LINEAR_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);
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
	C2_UT_ASSERT(rec1->lr_ref_count == rec2->lr_ref_count);
	C2_UT_ASSERT(rec1->lr_pool_id == rec2->lr_pool_id);

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

	C2_UT_ASSERT(pl_rec1->pr_attr.pa_N == pl_rec1->pr_attr.pa_N);
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
				      bool only_inline_test)
{
	void                    *area1;
	struct c2_bufvec         bv1;
	struct c2_bufvec_cursor  cur1;
	void                    *area2;
	struct c2_bufvec         bv2;
	struct c2_bufvec_cursor  cur2;
	c2_bcount_t              num_bytes;
	uint32_t                 nr;
	struct c2_uint128        seed;
	struct c2_layout        *l;

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

	if (enum_id == LIST_ENUM_ID) {
		nr = only_inline_test ? 3 : 103;
		rc = pdclust_layout_buf_build(LIST_ENUM_ID, lid,
					      50, 4, 4096, &seed,
					      nr, A_NONE, B_NONE, &cur1);
	} else
		rc = pdclust_layout_buf_build(LINEAR_ENUM_ID, lid,
					      50, 4, 4096, &seed,
					      1510, 777, 888, &cur1);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur1, &bv1);

	/* Decode the layout buffer into a layout object. */
	rc = c2_layout_decode(&domain, lid, C2_LXO_BUFFER_OP, NULL, &cur1, &l);
	C2_UT_ASSERT(rc == 0);

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

	rc = c2_layout_encode(l, C2_LXO_BUFFER_OP, NULL, NULL, &cur2);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur2, &bv2);

	/*
	 * Compare the two layout buffers - one created earlier here and
	 * the one that is produced by c2_layout_encode().
	 */
	pdclust_layout_buf_compare(enum_id, &cur1, &cur2);

	/* Destory the layout. */
	c2_layout_get(l);
	c2_layout_put(l);
	C2_UT_ASSERT(c2_layout_find(&domain, lid) == NULL);

	c2_free(area1);
	c2_free(area2);

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
	 * and LIST enum type, with only inline entries.
	 * Decode it into a layout object. Then encode that layout object again
	 * into another layout buffer.
	 * Then, compare the original layout buffer with the encoded layout
	 * buffer.
	 */
	lid = 4001;
	rc = test_decode_encode_pdclust(LIST_ENUM_ID, lid, ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout buffer representing a layout with PDCLUST layout type
	 * and LIST enum type.
	 * Decode it into a layout object. Then encode that layout object again
	 * into another layout buffer.
	 * Then, compare the original layout buffer with the encoded layout
	 * buffer.
	 */
	lid = 4002;
	rc = test_decode_encode_pdclust(LIST_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout buffer representing a layout with PDCLUST layout type
	 * and LINEAR enum type.
	 * Decode it into a layout object. Then encode that layout object again
	 * into another layout buffer.
	 * Then, compare the original layout buffer with the encoded layout
	 * buffer.
	 */
	lid = 4003;
	rc = test_decode_encode_pdclust(LINEAR_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);
}

/*
 * Compares two layout objects with PDCLUST layout type, provided as input
 * arguments.
 */
static void pdclust_layout_compare(uint32_t enum_id,
				   const struct c2_layout *l1,
				   const struct c2_layout *l2)
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
	C2_UT_ASSERT(l1->l_ref == l2->l_ref);
	C2_UT_ASSERT(l1->l_pool_id == l2->l_pool_id);
	C2_UT_ASSERT(l1->l_ops == l2->l_ops);

	/* Compare PDCLUST layout type specific part of the layout objects. */
	pl1 = container_of(l1, struct c2_pdclust_layout, pl_base.ls_base);
	pl2 = container_of(l2, struct c2_pdclust_layout, pl_base.ls_base);

	C2_UT_ASSERT(pl1->pl_attr.pa_N == pl2->pl_attr.pa_N);
	C2_UT_ASSERT(pl1->pl_attr.pa_K == pl2->pl_attr.pa_K);
	C2_UT_ASSERT(pl1->pl_attr.pa_P == pl2->pl_attr.pa_P);
	C2_UT_ASSERT(c2_uint128_eq(&pl1->pl_attr.pa_seed,
				   &pl2->pl_attr.pa_seed));

	/* Compare enumeration specific part of the layout objects. */
	C2_UT_ASSERT(pl1->pl_base.ls_enum->le_type ==
		     pl2->pl_base.ls_enum->le_type);
	C2_UT_ASSERT(pl1->pl_base.ls_enum->le_l == &pl1->pl_base.ls_base);
	C2_UT_ASSERT(pl1->pl_base.ls_enum->le_l->l_id ==
		     pl2->pl_base.ls_enum->le_l->l_id);
	C2_UT_ASSERT(pl1->pl_base.ls_enum->le_ops ==
		     pl2->pl_base.ls_enum->le_ops);

	/* Compare enumeration type specific part of the layout objects. */
	if (enum_id == LIST_ENUM_ID) {
		list_e1 = container_of(pl1->pl_base.ls_enum,
				       struct c2_layout_list_enum, lle_base);
		list_e2 = container_of(pl2->pl_base.ls_enum,
				       struct c2_layout_list_enum, lle_base);

		C2_UT_ASSERT(list_e1->lle_nr == list_e2->lle_nr);

		for (i = 0; i < list_e1->lle_nr; ++i)
			C2_UT_ASSERT(c2_fid_eq(&list_e1->lle_list_of_cobs[i],
					       &list_e2->lle_list_of_cobs[i]));
	} else { /* LINEAR_ENUM_ID */
		lin_e1 = container_of(pl1->pl_base.ls_enum,
				      struct c2_layout_linear_enum, lle_base);
		lin_e2 = container_of(pl2->pl_base.ls_enum,
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

	pl_src = container_of(l_src, struct c2_pdclust_layout, pl_base.ls_base);
	pl_dest = c2_alloc(sizeof *pl_src);
	C2_UT_ASSERT(pl_dest != NULL);
	*l_dest = &pl_dest->pl_base.ls_base;

	/* Copy generic part of the layout object. */
	(*l_dest)->l_id = l_src->l_id;
	(*l_dest)->l_type = l_src->l_type;
	(*l_dest)->l_dom = l_src->l_dom;
	(*l_dest)->l_ref = l_src->l_ref;
	(*l_dest)->l_pool_id = l_src->l_pool_id;
	(*l_dest)->l_ops = l_src->l_ops;

	/* Copy PDCLUST layout type specific part of the layout objects. */
	pl_dest->pl_attr.pa_N = pl_src->pl_attr.pa_N;
	pl_dest->pl_attr.pa_K = pl_src->pl_attr.pa_K;
	pl_dest->pl_attr.pa_P = pl_src->pl_attr.pa_P;
	pl_dest->pl_attr.pa_seed.u_hi = pl_src->pl_attr.pa_seed.u_hi;
	pl_dest->pl_attr.pa_seed.u_lo = pl_src->pl_attr.pa_seed.u_lo;

	/* Copy enumeration type specific part of the layout objects. */
	if (enum_id == LIST_ENUM_ID) {
		list_src = container_of(pl_src->pl_base.ls_enum,
					struct c2_layout_list_enum, lle_base);
		list_dest = c2_alloc(sizeof *list_src);
		C2_UT_ASSERT(list_src != NULL);

		list_dest->lle_nr = list_src->lle_nr;
		C2_ALLOC_ARR(list_dest->lle_list_of_cobs, list_dest->lle_nr);

		for (i = 0; i < list_src->lle_nr; ++i)
			list_dest->lle_list_of_cobs[i] =
					       list_src->lle_list_of_cobs[i];

		pl_dest->pl_base.ls_enum = &list_dest->lle_base;
	} else { /* LINEAR_ENUM_ID */
		lin_src = container_of(pl_src->pl_base.ls_enum,
				       struct c2_layout_linear_enum, lle_base);
		lin_dest = c2_alloc(sizeof *lin_src);
		C2_UT_ASSERT(lin_src != NULL);

		lin_dest->lle_attr.lla_nr = lin_src->lle_attr.lla_nr;
		lin_dest->lle_attr.lla_A = lin_src->lle_attr.lla_A;
		lin_dest->lle_attr.lla_B = lin_src->lle_attr.lla_B;

		pl_dest->pl_base.ls_enum = &lin_dest->lle_base;
	}

	/* Copy enumeration specific part of the layout objects. */
	pl_dest->pl_base.ls_enum->le_type = pl_src->pl_base.ls_enum->le_type;
	pl_dest->pl_base.ls_enum->le_ops = pl_src->pl_base.ls_enum->le_ops;
	pl_dest->pl_base.ls_enum->le_l = &pl_dest->pl_base.ls_base;

	pdclust_layout_compare(enum_id, &pl_src->pl_base.ls_base,
			       &pl_dest->pl_base.ls_base);
}

static void pdclust_layout_copy_delete(uint32_t enum_id, struct c2_layout *l)
{
	struct c2_pdclust_layout     *pl;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;

	C2_UT_ASSERT(l != NULL);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	pl = container_of(l, struct c2_pdclust_layout, pl_base.ls_base);
	if (enum_id == LIST_ENUM_ID) {
		list_enum = container_of(pl->pl_base.ls_enum,
					struct c2_layout_list_enum, lle_base);
		c2_free(list_enum->lle_list_of_cobs);
		c2_free(list_enum);
	} else { /* LINEAR_ENUM_ID */
		lin_enum = container_of(pl->pl_base.ls_enum,
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
				      bool only_inline_test)
{
	struct c2_pdclust_layout     *pl;
	void                         *area;
	c2_bcount_t                   num_bytes;
	struct c2_bufvec              bv;
	struct c2_bufvec_cursor       cur;
	struct c2_uint128             seed;
	uint32_t                      nr;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	struct c2_layout             *l;
	struct c2_layout             *l_copy = NULL;

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

	if (enum_id == LIST_ENUM_ID) {
		nr = only_inline_test ? 13 : 113;
		rc = pdclust_layout_build(LIST_ENUM_ID, lid,
					  45, 5, 4096, &seed,
					  nr, A_NONE, B_NONE,
					  &pl, &list_enum, NULL);
	} else {
		nr = 1130;
		rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
					  45, 5, 4096, &seed,
					  nr, 10, 20,
					  &pl, NULL, &lin_enum);
	}
	C2_UT_ASSERT(rc == 0);

	pdclust_layout_copy(enum_id, &pl->pl_base.ls_base, &l_copy);
	C2_UT_ASSERT(l_copy != NULL);

	/* Encode the layout object into a layout buffer. */
	rc = c2_layout_encode(&pl->pl_base.ls_base, C2_LXO_BUFFER_OP, NULL,
			      NULL, &cur);
	C2_UT_ASSERT(rc == 0);

	/* Destory the layout. */
	c2_layout_get(&pl->pl_base.ls_base);
	c2_layout_put(&pl->pl_base.ls_base);
	C2_UT_ASSERT(c2_layout_find(&domain, lid) == NULL);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);

	/*
	 * Decode the layout buffer produced by c2_layout_encode() into another
	 * layout object.
	 */
	rc = c2_layout_decode(&domain, lid, C2_LXO_BUFFER_OP, NULL, &cur, &l);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Comapre the two layout objects - one created earlier here and the
	 * one that is produced by c2_layout_decode().
	 */
	pdclust_layout_compare(enum_id, l_copy, l);
	pdclust_layout_copy_delete(enum_id, l_copy);

	/* Destory the layout. */
	c2_layout_get(l);
	c2_layout_put(l);
	C2_UT_ASSERT(c2_layout_find(&domain, lid) == NULL);

	c2_free(area);

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
	 * with only inline entries.
	 * Encode it into a layout buffer. Then decode that layout buffer again
	 * into another layout object.
	 * Then, compare the original layout object with the decoded layout
	 * object.
	 */
	lid = 5001;
	rc = test_encode_decode_pdclust(LIST_ENUM_ID, lid, ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout object with PDCLUST layout type and LIST enum type.
	 * Encode it into a layout buffer. Then decode that layout buffer again
	 * into another layout object.
	 * Then, compare the original layout object with the decoded layout
	 * object.
	 */
	lid = 5002;
	rc = test_encode_decode_pdclust(LIST_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout object with PDCLUST layout type and LINEAR enum type.
	 * Encode it into a layout buffer. Then decode that layout buffer again
	 * into a another layout object.
	 * Now, compare the original layout object with the decoded layout
	 * object.
	 */
	lid = 5003;
	rc = test_encode_decode_pdclust(LINEAR_ENUM_ID, lid, !ONLY_INLINE_TEST);
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
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	uint32_t                      i;

	C2_ENTRY("lid %llu", (unsigned long long)lid);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	c2_uint128_init(&seed, "refgetputpdclust");

	/* Build a layout object. */
	if (enum_id == LIST_ENUM_ID)
		rc = pdclust_layout_build(LIST_ENUM_ID, lid,
					  100, 9, 4096, &seed,
					  1212, A_NONE, B_NONE,
					  &pl, &list_enum, NULL);
	else
		rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
					  100, 9, 4096, &seed,
					  1212, 10, 20,
					  &pl, NULL, &lin_enum);
	C2_UT_ASSERT(rc == 0);

	/* Verify that the ref count is set to DEFAULT_REF_COUNT. */
	C2_UT_ASSERT(pl->pl_base.ls_base.l_ref == DEFAULT_REF_COUNT);

	/* Add a reference on the layout object. */
	c2_layout_get(&pl->pl_base.ls_base);
	C2_UT_ASSERT(pl->pl_base.ls_base.l_ref == DEFAULT_REF_COUNT + 1);

	/* Add multiple references on the layout object. */
	for (i = 0; i < 123; ++i)
		c2_layout_get(&pl->pl_base.ls_base);
	C2_UT_ASSERT(pl->pl_base.ls_base.l_ref ==
		     DEFAULT_REF_COUNT + 1 + 123);

	/* Release multiple references on the layout object. */
	for (i = 0; i < 123; ++i)
		c2_layout_put(&pl->pl_base.ls_base);
	C2_UT_ASSERT(pl->pl_base.ls_base.l_ref == DEFAULT_REF_COUNT + 1);

	/* Release the last reference so as to delete the layout. */
	c2_layout_put(&pl->pl_base.ls_base);
	C2_UT_ASSERT(c2_layout_find(&domain, lid) == NULL);

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
	struct c2_layout_striped     *stl;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	struct c2_fid                 fid_calculated;
	struct c2_fid                 fid_from_layout;
	struct c2_fid                 gfid;
	int                           i;

	C2_UT_ASSERT(l != NULL);

	stl = container_of(l, struct c2_layout_striped, ls_base);

	if (enum_id == LIST_ENUM_ID) {
		list_enum = container_of(stl->ls_enum,
					 struct c2_layout_list_enum, lle_base);

		C2_UT_ASSERT(list_enum->lle_base.le_ops->leo_nr(
					     &list_enum->lle_base) == nr);

		for(i = 0; i < list_enum->lle_nr; ++i) {
			c2_fid_set(&fid_calculated, i * 100 + 1, i + 1);
			list_enum->lle_base.le_ops->leo_get(
							&list_enum->lle_base,
							i, NULL,
							&fid_from_layout);
			C2_UT_ASSERT(c2_fid_eq(&fid_calculated,
					       &fid_from_layout));
		}
	} else {
		lin_enum = container_of(stl->ls_enum,
					struct c2_layout_linear_enum, lle_base);
		C2_UT_ASSERT(lin_enum->lle_base.le_ops->leo_nr(
					      &lin_enum->lle_base) == nr);

		/* Set gfid to some dummy value. */
		c2_fid_set(&gfid, 0, 999);

		for(i = 0; i < nr; ++i) {
			c2_fid_set(&fid_calculated,
				   lin_enum->lle_attr.lla_A +
				   i * lin_enum->lle_attr.lla_B,
				   gfid.f_key);
			lin_enum->lle_base.le_ops->leo_get(&lin_enum->lle_base,
							   i, &gfid,
							   &fid_from_layout);
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
				 bool only_inline_test)
{
	struct c2_uint128             seed;
	uint32_t                      nr;
	struct c2_pdclust_layout     *pl;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;

	C2_ENTRY();

	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	/* Build a layout object. */
	c2_uint128_init(&seed, "enumopspdclustla");

	if (enum_id == LIST_ENUM_ID) {
		nr = only_inline_test == ONLY_INLINE_TEST ? 14 : 1014;
		rc = pdclust_layout_build(LIST_ENUM_ID, lid,
					  100, 10, 4096, &seed,
					  nr, A_NONE, B_NONE,
					  &pl, &list_enum, NULL);
	} else {
		nr = 1014;
		rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
					  80, 8, 4096, &seed,
					  nr, 777, 888,
					  &pl, NULL, &lin_enum);
	}
	C2_UT_ASSERT(rc == 0);

	c2_layout_get(&pl->pl_base.ls_base);

	/* Verify enum operations. */
	enum_op_verify(enum_id, lid, nr, &pl->pl_base.ls_base);

	/* Destroy the layout object. */
	c2_layout_put(&pl->pl_base.ls_base);
	C2_UT_ASSERT(c2_layout_find(&domain, lid) == NULL);

	C2_LEAVE();
	return rc;
}

/* Tests the enum operations pointed by leo_nr and leo_get. */
static void test_enum_operations(void)
{
	uint64_t lid;

	/*
	 * Decode a layout with PDCLUST layout type, LIST enum type and
	 * with only inline entries.
	 * And then verify its enum ops.
	 */
	lid = 7001;
	rc = test_enum_ops_pdclust(LIST_ENUM_ID, lid, ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Decode a layout with PDCLUST layout type and LIST enum type.
	 * And then verify its enum ops.
	 */
	lid = 7002;
	rc = test_enum_ops_pdclust(LIST_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Decode a layout with PDCLUST layout type and LINEAR enum type.
	 * And then verify its enum ops.
	 */
	lid = 7003;
	rc = test_enum_ops_pdclust(LINEAR_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);
}

/* Tests the API c2_layout_max_recsize(). */
static void test_max_recsize(void)
{
	const char              t_db_name[] = "t3-layout";
	struct c2_dbenv         t_dbenv;
	struct c2_layout_domain t_domain;
	c2_bcount_t             max_size_from_api;
	c2_bcount_t             max_size_calculated;

	C2_ENTRY();

	rc = c2_dbenv_init(&t_dbenv, t_db_name, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	/* Initialise the domain. */
	rc = c2_layout_domain_init(&t_domain, &t_dbenv);
	C2_UT_ASSERT(rc == 0);

	max_size_from_api = c2_layout_max_recsize(&t_domain);
	C2_UT_ASSERT(max_size_from_api == 0);

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

	C2_LEAVE();
}

#ifndef __KERNEL__
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

	pl = container_of(l, struct c2_pdclust_layout, pl_base.ls_base);

	/* Account for the enum type specific recsize. */
	if (enum_id == LIST_ENUM_ID) {
		list_enum = container_of(pl->pl_base.ls_enum,
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

/* Tests the internal function recsize_get(), for the PDCLUST layout type. */
static int test_recsize_pdclust(uint32_t enum_id, uint64_t lid,
				bool only_inline_test)
{
	struct c2_pdclust_layout     *pl;
	struct c2_uint128             seed;
	uint32_t                      nr;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	c2_bcount_t                   recsize;

	C2_ENTRY("lid %llu", (unsigned long long)lid);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	c2_uint128_init(&seed, "recsizepdclustla");

	/* Build a layout object. */
	if (enum_id == LIST_ENUM_ID) {
		nr = only_inline_test ? 10 : 1200;
		rc = pdclust_layout_build(LIST_ENUM_ID, lid,
					  60, 6, 4096, &seed,
					  nr, A_NONE, B_NONE,
					  &pl, &list_enum, NULL);
	} else {
		nr = 1111;
		rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
					  60, 6, 4096, &seed,
					  nr, 10, 20,
					  &pl, NULL, &lin_enum);
	}
	C2_UT_ASSERT(rc == 0);

	c2_layout_get(&pl->pl_base.ls_base);

	/* Obtain the recsize by using the internal function recsize_get(). */
	recsize = recsize_get(&pl->pl_base.ls_base);

	/* Verify the recsize returned by recsize_get(). */
	pdclust_recsize_verify(enum_id, &pl->pl_base.ls_base, recsize);

	/* Destroy the layout object. */
	c2_layout_put(&pl->pl_base.ls_base);
	C2_UT_ASSERT(c2_layout_find(&domain, lid) == NULL);

	C2_LEAVE();
	return rc;
}

/* Tests the internal function recsize_get(). */
static void test_recsize(void)
{
	uint64_t lid;
	int      rc;

	/*
	 * recsize_get() for PDCLUST layout type and LIST enumeration type,
	 * with only inline entries.
	 */
	lid = 8001;
	rc = test_recsize_pdclust(LIST_ENUM_ID, lid, ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* recsize_get() for PDCLUST layout type and LIST enumeration type. */
	lid = 8002;
	rc = test_recsize_pdclust(LIST_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* recsize_get() for PDCLUST layout type and LINEAR enumeration type. */
	lid = 8003;
	rc = test_recsize_pdclust(LINEAR_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);
}

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
			    bool only_inline_test,
			    bool duplicate_test,
			    bool layout_destroy, struct c2_layout **l_obj);

/* Tests the API c2_layout_lookup(), for the PDCLUST layout type. */
static int test_lookup_pdclust(uint32_t enum_id, uint64_t lid,
			       bool existing_test,
			       bool only_inline_test)
{
	c2_bcount_t        num_bytes;
	void              *area;
	struct c2_layout  *l1;
	struct c2_layout  *l1_copy;
	struct c2_layout  *l2;
	struct c2_db_pair  pair;
	struct c2_db_tx    tx;

	C2_ENTRY();
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	/*
	 * If existing_test is true, then first add a layout object to the
	 * DB.
	 */
	if (existing_test) {
		rc = test_add_pdclust(enum_id, lid,
				      only_inline_test,
				      DUPLICATE_TEST,
				      !LAYOUT_DESTROY, &l1);
		C2_UT_ASSERT(rc == 0);
		pdclust_layout_copy(enum_id, l1, &l1_copy);

		/* Destroy the layout object. */
		c2_layout_get(l1);
		c2_layout_put(l1);
		C2_UT_ASSERT(c2_layout_find(&domain, lid) == NULL);
	}

	C2_UT_ASSERT(c2_layout_find(&domain, lid) == NULL);

	/* Lookup for the layout object from the DB. */
	allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_set(&pair, &lid, area, num_bytes);

	rc = c2_layout_lookup(&domain, lid, &tx, &pair, &l2);

	if (existing_test)
		C2_UT_ASSERT(rc == 0);
	else
		C2_UT_ASSERT(rc == -ENOENT);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	/* Destory the layout object. */
	if (existing_test) {
		C2_UT_ASSERT(c2_layout_find(&domain, lid) == l2);
		pdclust_layout_compare(enum_id, l1_copy, l2);
		pdclust_layout_copy_delete(enum_id, l1_copy);

		/* Destroy the layout object. */
		c2_layout_get(l2);
		c2_layout_put(l2);
		C2_UT_ASSERT(c2_layout_find(&domain, lid) == NULL);
	}

	c2_free(area);

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
	lid = 9001;
	rc = test_lookup_pdclust(LIST_ENUM_ID, lid,
				 !EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Add a layout object with PDCLUST layout type, LIST enum type and
	 * with only inline entries.
	 * Then perform lookup for it.
	 */
	lid = 9002;
	rc = test_lookup_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Add a layout object with PDCLUST layout type, LIST enum type.
	 * Then perform lookup for it.
	 */
	lid = 9003;
	rc = test_lookup_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Once again, lookup for a layout object that does not exist in the
	 * DB.
	 */
	lid = 9004;
	rc = test_lookup_pdclust(LIST_ENUM_ID, lid,
				 !EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Lookup for a layout object with LINEAR enum type, that does not
	 * exist in the DB.
	 */
	lid = 9005;
	rc = test_lookup_pdclust(LINEAR_ENUM_ID, lid,
				 !EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Add a layout object with PDCLUST layout type and LINEAR enum type.
	 * Then perform lookup for it.
	 */
	lid = 9006;
	rc = test_lookup_pdclust(LINEAR_ENUM_ID, lid,
				 EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);
}

/* Tests the API c2_layout_add(), for the PDCLUST layout type. */
static int test_add_pdclust(uint32_t enum_id, uint64_t lid,
			    bool only_inline_test,
			    bool duplicate_test,
			    bool layout_destroy, struct c2_layout **l_obj)
{
	c2_bcount_t                   num_bytes;
	uint32_t                      nr;
	void                         *area;
	struct c2_pdclust_layout     *pl;
	struct c2_db_pair             pair;
	struct c2_db_tx               tx;
	struct c2_uint128             seed;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	struct c2_layout             *l;

	C2_ENTRY();
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);
	C2_UT_ASSERT(ergo(layout_destroy, l_obj == NULL));
	C2_UT_ASSERT(ergo(!layout_destroy, l_obj != NULL));

	c2_uint128_init(&seed, "addpdclustlayout");

	allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	/* Build a layout object. */
	if (enum_id == LIST_ENUM_ID) {
		nr = only_inline_test ? 7 : 1900;
		rc = pdclust_layout_build(LIST_ENUM_ID, lid,
					  5, 2, 4096, &seed,
					  nr, A_NONE, B_NONE,
					  &pl, &list_enum, NULL);
	}
	else {
		nr = 1900;
		rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
					  4, 1, 4096, &seed,
					  nr, 100, 200,
					  &pl, NULL, &lin_enum);
	}
	C2_UT_ASSERT(rc == 0);

	/* Add the layout object to the DB. */
	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_set(&pair, &lid, area, num_bytes);

	rc = c2_layout_add(&pl->pl_base.ls_base, &tx, &pair);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	l = c2_layout_find(&domain, lid);
	C2_UT_ASSERT(&pl->pl_base.ls_base == l);

	/*
	 * If duplicate_test is true, again try to add the same layout object
	 * to the DB, to verify that it results into EEXIST error.
	 */
	if (duplicate_test) {
		rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
		C2_UT_ASSERT(rc == 0);

		pair_set(&pair, &lid, area, num_bytes);

		rc = c2_layout_add(&pl->pl_base.ls_base, &tx, &pair);
		C2_UT_ASSERT(rc == -EEXIST);

		rc = c2_db_tx_commit(&tx);
		C2_UT_ASSERT(rc == 0);
	}

	if (layout_destroy) {
		c2_layout_get(&pl->pl_base.ls_base);
		c2_layout_put(&pl->pl_base.ls_base);
		C2_UT_ASSERT(c2_layout_find(&domain, lid) == NULL);
	}
	else
		*l_obj = &pl->pl_base.ls_base;

	c2_free(area);

	C2_LEAVE();
	return rc;
}

/* Tests the API c2_layout_add(). */
static void test_add(void)
{
	uint64_t lid;

	/*
	 * Add a layout object with PDCLUST layout type, LIST enum type and
	 * with only inline entries.
	 */
	lid = 10001;
	rc = test_add_pdclust(LIST_ENUM_ID, lid,
			      ONLY_INLINE_TEST,
			      DUPLICATE_TEST,
			      LAYOUT_DESTROY, NULL);
	C2_UT_ASSERT(rc == 0);

	/* Add a layout object with PDCLUST layout type and LIST enum type. */
	lid = 10002;
	rc = test_add_pdclust(LIST_ENUM_ID, lid,
			      !ONLY_INLINE_TEST,
			      DUPLICATE_TEST,
			      LAYOUT_DESTROY, NULL);
	C2_UT_ASSERT(rc == 0);

	/* Add a layout object with PDCLUST layout type and LINEAR enum type. */
	lid = 10003;
	rc = test_add_pdclust(LINEAR_ENUM_ID, lid,
			      !ONLY_INLINE_TEST,
			      DUPLICATE_TEST,
			      LAYOUT_DESTROY, NULL);
	C2_UT_ASSERT(rc == 0);
}

/* Tests the API c2_layout_update(), for the PDCLUST layout type. */
static int test_update_pdclust(uint32_t enum_id, uint64_t lid,
			       bool existing_test,
			       bool only_inline_test)
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
	uint32_t                      nr;
	struct c2_pdclust_layout     *pl;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;

	C2_ENTRY();
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	if (existing_test) {
		/* Add a layout object to the DB. */
		rc = test_add_pdclust(enum_id, lid,
				      only_inline_test,
				      !DUPLICATE_TEST,
				      !LAYOUT_DESTROY, &l1);
		C2_UT_ASSERT(rc == 0);
	} else {
		/* Build a layout object. */
		c2_uint128_init(&seed, "updatepdclustlay");

		if (enum_id == LIST_ENUM_ID) {
			nr = only_inline_test ? 13 : 123;
			rc = pdclust_layout_build(LIST_ENUM_ID, lid,
						  4, 1, 4096, &seed,
						  nr, A_NONE, B_NONE,
						  &pl, &list_enum, NULL);
		} else {
			nr = 1230;
			rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
						  4, 1, 4096, &seed,
						  nr, 10, 20,
						  &pl, NULL, &lin_enum);
		}
		C2_UT_ASSERT(rc == 0);
		l1 = &pl->pl_base.ls_base;
	}

	/* Verify the original reference count is as expected. */
	C2_UT_ASSERT(l1->l_ref == DEFAULT_REF_COUNT);

	/* Update the layout object - update its reference count. */
	for (i = 0; i < 99; ++i)
		c2_layout_get(l1);
	C2_UT_ASSERT(l1->l_ref == DEFAULT_REF_COUNT + 99);

	/* Update the layout object in the DB. */
	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_set(&pair, &lid, area, num_bytes);

	rc = c2_layout_update(l1, &tx, &pair);
	if (existing_test) {
		C2_UT_ASSERT(rc == 0);
		pdclust_layout_copy(enum_id, l1, &l1_copy);
	}
	else
		C2_UT_ASSERT(rc == -ENOENT);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	/* Release all the references obtained here but one. */
	for (i = 0; i < 99 - 1; ++i)
		c2_layout_put(l1);
	C2_UT_ASSERT(l1->l_ref == DEFAULT_REF_COUNT + 1);

	/* Release the last reference so as to delete the layout. */
	c2_layout_put(l1);
	C2_UT_ASSERT(c2_layout_find(&domain, lid) == NULL);

	if (existing_test) {
		/*
		 * Lookup for the layout object from the DB to verify that its
		 * reference count is indeed updated.
		 */
		rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
		C2_UT_ASSERT(rc == 0);

		pair_set(&pair, &lid, area, num_bytes);

		rc = c2_layout_lookup(&domain, lid, &tx, &pair, &l2);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(l2->l_ref == DEFAULT_REF_COUNT + 99);

		rc = c2_db_tx_commit(&tx);
		C2_UT_ASSERT(rc == 0);

		/*
		 * Compare the two layouts - one created earlier here and the
		 * one that is looked up from the DB.
		 */
		pdclust_layout_compare(enum_id, l1_copy, l2);
		pdclust_layout_copy_delete(enum_id, l1_copy);

		/* Release all the references as read from the DB but one. */
		for (i = 0; i < 99 - 1; ++i)
			c2_layout_put(l2);
		C2_UT_ASSERT(l2->l_ref == DEFAULT_REF_COUNT + 1);

		/* Release the last reference so as to delete the layout. */
		c2_layout_put(l2);
		C2_UT_ASSERT(c2_layout_find(&domain, lid) == NULL);
	}

	c2_free(area);

	C2_LEAVE();
	return rc;
}

/* Tests the API c2_layout_update(). */
static void test_update(void)
{
	uint64_t lid;

	/*
	 * Try to Update a layout object with PDCLUST layout type and LIST enum
	 * type, that does not exist in the DB to verify that the operation
	 * fails with the error ENOENT.
	 */
	lid = 11001;
	rc = test_update_pdclust(LIST_ENUM_ID, lid,
				 !EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Update a layout object with PDCLUST layout type, LIST enum type and
	 * with only inline entries.
	 */
	lid = 11002;
	rc = test_update_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Update a layout object with PDCLUST layout type and LIST enum
	 * type.
	 */
	lid = 11003;
	rc = test_update_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Update a layout object with PDCLUST layout type and LINEAR enum
	 * type.
	 */
	lid = 11004;
	rc = test_update_pdclust(LINEAR_ENUM_ID, lid,
				 EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);
}

/* Tests the API c2_layout_delete(), for the PDCLUST layout type. */
static int test_delete_pdclust(uint32_t enum_id, uint64_t lid,
			       bool existing_test,
			       bool only_inline_test)
{
	c2_bcount_t                   num_bytes;
	void                         *area;
	struct c2_db_pair             pair;
	struct c2_db_tx               tx;
	struct c2_layout             *l;
	struct c2_uint128             seed;
	uint32_t                      nr;
	struct c2_pdclust_layout     *pl;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;

	C2_ENTRY();
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	if (existing_test) {
		/* Add a layout object to the DB. */
		rc = test_add_pdclust(enum_id, lid,
				      only_inline_test,
				      !DUPLICATE_TEST,
				      !LAYOUT_DESTROY, &l);
		C2_UT_ASSERT(rc == 0);
	} else {
		/* Build a layout object. */
		c2_uint128_init(&seed, "deletepdclustlay");

		if (enum_id == LIST_ENUM_ID) {
			nr = only_inline_test ? 12 : 122;
			rc = pdclust_layout_build(LIST_ENUM_ID, lid,
						  4, 1, 4096, &seed,
						  nr, A_NONE, B_NONE,
						  &pl, &list_enum, NULL);
		} else {
			nr = 1220;
			rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
						  4, 1, 4096, &seed,
						  nr, 10, 20,
						  &pl, NULL, &lin_enum);
		}
		C2_UT_ASSERT(rc == 0);
		l = &pl->pl_base.ls_base;
	}

	/* Add a reference since we want to operate upon the layout. */
	c2_layout_get(l);

	/* Delete the layout object from the DB. */
	pair_set(&pair, &lid, area, num_bytes);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	rc = c2_layout_delete(l, &tx, &pair);
	if (existing_test)
		C2_UT_ASSERT(rc == 0);
	else
		C2_UT_ASSERT(rc == -ENOENT);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	/* Destroy the layout object. */
	c2_layout_put(l);
	C2_UT_ASSERT(c2_layout_find(&domain, lid) == NULL);

	/*
	 * Lookup for the layout object from the DB, to verify that it does not
	 * exist there and that the lookup results into ENOENT error.
	 */
	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_set(&pair, &lid, area, num_bytes);

	rc = c2_layout_lookup(&domain, lid, &tx, &pair, &l);
	C2_UT_ASSERT(rc == -ENOENT);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	c2_free(area);

	C2_LEAVE();
	return rc;
}

/* Tests the API c2_layout_delete(). */
static void test_delete(void)
{
	uint64_t lid;

	/*
	 * Try to delete a layout object with PDCLUST layout type and LINEAR
	 * enum type, that does not exist in the DB, to verify that it results
	 * into the error ENOENT.
	 */
	lid = 12001;
	rc = test_delete_pdclust(LINEAR_ENUM_ID, lid,
				 !EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Delete a layout object with PDCLUST layout type, LIST enum type and
	 * with only inline entries.
	 */
	lid = 12002;
	rc = test_delete_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Delete a layout object with PDCLUST layout type and LIST enum
	 * type.
	 */
	lid = 12003;
	rc = test_delete_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Delete a layout object with PDCLUST layout type and LINEAR enum
	 * type.
	 */
	lid = 12004;
	rc = test_delete_pdclust(LINEAR_ENUM_ID, lid,
				 EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);
}
#endif

const struct c2_test_suite layout_ut = {
	.ts_name  = "layout-ut",
	.ts_init  = test_init,
	.ts_fini  = test_fini,
	.ts_tests = {
		{ "layout-domain-init-fini", test_domain_init_fini },
		{ "layout-type-register-unregister", test_type_reg_unreg },
		{ "layout-etype-register-unregister", test_etype_reg_unreg },
		{ "layout-register-unregister", test_reg_unreg },
		{ "layout-build", test_build },
		{ "layout-decode", test_decode },
		{ "layout-encode", test_encode },
		{ "layout-decode-encode", test_decode_encode },
		{ "layout-encode-decode", test_encode_decode },
		{ "layout-ref-get-put", test_ref_get_put },
		{ "layout-enum-ops", test_enum_operations },
		{ "layout-max-recsize", test_max_recsize },
#ifndef __KERNEL__
		{ "layout-recsize", test_recsize },
                { "layout-lookup", test_lookup },
                { "layout-add", test_add },
                { "layout-update", test_update },
                { "layout-delete", test_delete },
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
