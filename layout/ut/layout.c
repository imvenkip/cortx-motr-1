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

#include "pool/pool.h" /* c2_pool_init() */
#include "layout/layout_internal.h" /* DEFAULT_REF_COUNT */
#include "layout/layout.h"
#include "layout/pdclust.h"
#include "layout/layout_db.h"
#include "layout/list_enum.h"
#include "layout/list_enum.c" /* struct ldb_cob_entries_header */
#include "layout/linear_enum.h"

static const char              db_name[] = "ut-layout";
static struct c2_layout_domain domain;
static struct c2_layout_schema schema;
static struct c2_dbenv         dbenv;
static struct c2_pool          pool;
static int                     rc;
enum c2_addb_ev_level          orig_addb_level;

enum {
	DBFLAGS                  = 0,
	DEFAULT_POOL_ID          = 1,
	POOL_WIDTH               = 200,
	LIST_ENUM_ID             = 0x4C495354, /* "LIST" */
	LINEAR_ENUM_ID           = 0x4C494E45, /* "LINE" */
	A_NONE                   = 0, /* Invalid value for attribute A*/
	B_NONE                   = 0, /* Invalid value for attribute B */
	PARTIAL_BUF              = true,
	ONLY_INLINE              = true,
	ADDITIONAL_BYTES_NONE    = 0,
	ADDITIONAL_BYTES_DEFAULT = 2048,
	TEST_EXISTING            = 1, // todo rename
	LOOKUP_TEST              = 1,
	DUPLICATE_TEST           = 1
};

extern const struct c2_layout_type c2_pdclust_layout_type;

extern const struct c2_layout_enum_type c2_list_enum_type;
extern const struct c2_layout_enum_type c2_linear_enum_type;

static int test_init(void)
{
	c2_ut_db_reset(db_name);

	/*
	 * Note: In test_init() and test_fini(), need to use C2_ASSERT()
	 * as against C2_UT_ASSERT().
	 */

	rc = c2_layout_domain_init(&domain);
	C2_ASSERT(rc == 0);

	rc = c2_dbenv_init(&dbenv, db_name, DBFLAGS);
	C2_ASSERT(rc == 0);

	rc = c2_layout_schema_init(&schema, &domain, &dbenv);
	C2_ASSERT(rc == 0);
	C2_ASSERT(schema.ls_domain == &domain);

	/*
	 * Store the original addb level before changing it and change it to
	 * AEL_WARN.
	 * Note: This is a provision to avoid recompiling the whole ADDB module,
	 * when interested in ADDB messages only for LAYOUT module.
	 * Just changing the level to AEL_NONE here and recompiling the LAYOUT
	 * module serves the purpose in that case.
	 */
	orig_addb_level = c2_addb_choose_default_level_console(AEL_WARN);

	rc = c2_layout_register(&domain);
	C2_ASSERT(rc == 0 || rc == -EEXIST);

	rc = c2_pool_init(&pool, DEFAULT_POOL_ID, POOL_WIDTH);
	C2_ASSERT(rc == 0);

	return rc;
}

static int test_fini(void)
{
	c2_pool_fini(&pool);

	c2_layout_unregister(&domain);

	/* Restore the original addb level. */
	c2_addb_choose_default_level_console(orig_addb_level);

	c2_layout_schema_fini(&schema);

	c2_dbenv_fini(&dbenv);

	c2_layout_domain_fini(&domain);

	return 0;
}

static void test_domain_init_fini(void)
{
	struct c2_layout_domain t_domain;

	/* Initialize the domain. */
	rc = c2_layout_domain_init(&t_domain);
	C2_UT_ASSERT(rc == 0);

	/* Finalize the domain. */
	c2_layout_domain_fini(&t_domain);
}

static void test_schema_init_fini(void)
{
	const char              t_db_name[] = "t-layout";
	struct c2_layout_domain t_domain;
	struct c2_layout_schema t_schema;
	struct c2_dbenv         t_dbenv;

	C2_ENTRY();

	/* Initialize the domain. */
	rc = c2_layout_domain_init(&t_domain);
	C2_UT_ASSERT(rc == 0);

	rc = c2_dbenv_init(&t_dbenv, t_db_name, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	/* Initialize the schema. */
	rc = c2_layout_schema_init(&t_schema, &t_domain, &t_dbenv);
	C2_UT_ASSERT(rc == 0);

	/* Finalize the schema. */
	c2_layout_schema_fini(&t_schema);

	/* Should be able to initialize the schema again. */
	rc = c2_layout_schema_init(&t_schema, &t_domain, &t_dbenv);
	C2_UT_ASSERT(rc == 0);

	/* Finalize the schema. */
	c2_layout_schema_fini(&t_schema);

	c2_dbenv_fini(&t_dbenv);

	/* Finalize the domain. */
	c2_layout_domain_fini(&t_domain);

	C2_LEAVE();
}


static int t_register(struct c2_layout_schema *schema,
		      const struct c2_layout_type *lt)
{
	return 0;
}

static void t_unregister(struct c2_layout_schema *schema,
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

static int t_enum_register(struct c2_layout_schema *schema,
			   const struct c2_layout_enum_type *et)
{
	return 0;
}

static void t_enum_unregister(struct c2_layout_schema *schema,
			      const struct c2_layout_enum_type *et)
{
}

static c2_bcount_t t_enum_max_recsize()
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

static void test_ldb_reg_unreg(void)
{
	const char              t_db_name[] = "t-layout";
	struct c2_layout_domain t_domain;
	struct c2_layout_schema t_schema;
	struct c2_dbenv         t_dbenv;

	C2_ENTRY();

	/* Initialize the domain. */
	rc = c2_layout_domain_init(&t_domain);
	C2_UT_ASSERT(rc == 0);

	rc = c2_dbenv_init(&t_dbenv, t_db_name, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	/* Initialize the schema. */
	rc = c2_layout_schema_init(&t_schema, &t_domain, &t_dbenv);
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
	 * types, again after unregistering them.
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

	/* Finalize the schema. */
	c2_layout_schema_fini(&t_schema);

	c2_dbenv_fini(&t_dbenv);

	/* Finalize the domain. */
	c2_layout_domain_fini(&t_domain);

	C2_LEAVE();
}

static void test_max_recsize()
{
	c2_bcount_t max_size;
	c2_bcount_t list_size;

	list_size = sizeof(struct ldb_cob_entries_header) +
		    LDB_MAX_INLINE_COB_ENTRIES * sizeof(struct c2_fid);

	max_size = sizeof(struct c2_layout_rec) +
		   sizeof(struct c2_layout_pdclust_rec) +
		   max64u(list_size, sizeof(struct c2_layout_linear_attr));

	C2_UT_ASSERT(max_size == c2_layout_max_recsize(&domain));
}

static void test_recsize()
{
	/* todo Define this test. */
}

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

static void pdclust_buf_build(uint64_t lid,
			      uint32_t N, uint32_t K,
			      uint64_t unitsize, struct c2_uint128 *seed,
			      uint32_t let_id, struct c2_bufvec_cursor *dcur)
{
	struct c2_layout_pdclust_rec pl_rec;
	c2_bcount_t                  nbytes_copied;

	buf_build(c2_pdclust_layout_type.lt_id, dcur);

	pl_rec.pr_let_id            = let_id;
	pl_rec.pr_attr.pa_N         = N;
	pl_rec.pr_attr.pa_K         = K;
	pl_rec.pr_attr.pa_P         = pool.po_width;
	pl_rec.pr_attr.pa_unit_size = unitsize;
	pl_rec.pr_attr.pa_seed      = *seed;

	nbytes_copied = c2_bufvec_cursor_copyto(dcur, &pl_rec, sizeof pl_rec);
	C2_UT_ASSERT(nbytes_copied == sizeof pl_rec);
}

static int pdclust_layout_buf_build(uint32_t enum_id, uint64_t lid,
				    uint32_t N, uint32_t K,
				    uint64_t unitsize, struct c2_uint128 *seed,
				    uint32_t nr,
				    uint32_t A, uint32_t B,/* For linear enum */
				    struct c2_bufvec_cursor *dcur)
{
	uint32_t                       let_id;
	c2_bcount_t                    nbytes_copied;
	struct ldb_cob_entries_header  ldb_ce_header;
	struct c2_fid                  cob_id;
	uint32_t                       i;
	struct c2_layout_linear_attr   lin_rec;

	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);
	C2_UT_ASSERT(dcur != NULL);

	if (enum_id == LIST_ENUM_ID) {
		C2_UT_ASSERT(A == A_NONE && B == B_NONE);
		let_id = c2_list_enum_type.let_id;
	}
	else {
		C2_UT_ASSERT(B != B_NONE);
		let_id = c2_linear_enum_type.let_id;
	}

	pdclust_buf_build(lid, N, K, unitsize, seed, let_id, dcur);

	if (enum_id == LIST_ENUM_ID) {
		ldb_ce_header.llces_nr = nr;
		nbytes_copied = c2_bufvec_cursor_copyto(dcur, &ldb_ce_header,
							sizeof ldb_ce_header);
		C2_UT_ASSERT(nbytes_copied == sizeof ldb_ce_header);

		for (i = 0; i < ldb_ce_header.llces_nr; ++i) {
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


static int l_verify(uint64_t lid, struct c2_layout *l)
{
	C2_UT_ASSERT(l->l_id == lid);
	C2_UT_ASSERT(l->l_ref >= DEFAULT_REF_COUNT);
	C2_UT_ASSERT(l->l_ops != NULL);

	return 0;
}

static int pdclust_l_verify(uint64_t lid,
			    uint32_t N, uint32_t K,
			    uint64_t unitsize, struct c2_uint128 *seed,
			    struct c2_pdclust_layout *pl)
{
	l_verify(lid, &pl->pl_base.ls_base);

	C2_UT_ASSERT(pl->pl_attr.pa_N == N);
	C2_UT_ASSERT(pl->pl_attr.pa_K == K);
	C2_UT_ASSERT(pl->pl_attr.pa_P == POOL_WIDTH);
	C2_UT_ASSERT(pl->pl_attr.pa_unit_size == unitsize);
	C2_UT_ASSERT(c2_uint128_eq(&pl->pl_attr.pa_seed, seed));

	return 0;
}

static int pdclust_layout_verify(uint32_t enum_id, uint64_t lid,
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

	C2_UT_ASSERT(pl->pl_base.ls_enum != NULL);

	pdclust_l_verify(lid, N, K, unitsize, seed, pl);

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

	return 0;
}

static void layout_destroy(struct c2_layout *l, uint64_t lid)
{
	C2_UT_ASSERT(l != NULL);

	/* Enum object is destroyed internally by lo_fini(). */
	l->l_ops->lo_fini(l, &domain);
}

static void allocate_area(void **area,
			  c2_bcount_t additional_bytes,
			  c2_bcount_t *num_bytes)
{
	C2_UT_ASSERT(area != NULL && *area == NULL);

	*num_bytes = c2_layout_max_recsize(&domain) + additional_bytes;

	*area = c2_alloc(*num_bytes);
	C2_UT_ASSERT(*area != NULL);
}

static int test_decode_pdclust_list(uint64_t lid, bool only_inline_cob_entries)
{
	void                    *area = NULL;
	c2_bcount_t              num_bytes;
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;
	struct c2_layout        *l = NULL;
	uint32_t                 nr;
	struct c2_uint128        seed;

	C2_ENTRY();

	c2_uint128_init(&seed, "decodepdclustlis");
	allocate_area(&area, ADDITIONAL_BYTES_DEFAULT, &num_bytes);
	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	nr = only_inline_cob_entries == ONLY_INLINE ? 5 : 125;

	rc = pdclust_layout_buf_build(LIST_ENUM_ID, lid,
				      50, 4, 4096, &seed,
				      nr, A_NONE, B_NONE, &cur);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);
	rc = c2_layout_decode(&domain, lid, &cur, C2_LXO_BUFFER_OP,
			      NULL, NULL, &l);
	C2_UT_ASSERT(rc == 0);

	rc = pdclust_layout_verify(LIST_ENUM_ID, lid,
				   50, 4, 4096, &seed,
				   nr, A_NONE, B_NONE, l);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(l, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static int test_decode_pdclust_linear(uint64_t lid)
{
	void                    *area = NULL;
	c2_bcount_t              num_bytes;
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;
	struct c2_layout        *l = NULL;
	struct c2_uint128        seed;

	C2_ENTRY();

	c2_uint128_init(&seed, "decodepdclustlin");
	allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);
	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = pdclust_layout_buf_build(LINEAR_ENUM_ID, lid,
				      60, 6, 4096, &seed,
				      1500, 777, 888, &cur);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);
	rc = c2_layout_decode(&domain, lid, &cur, C2_LXO_BUFFER_OP,
			      NULL, NULL, &l);
	C2_UT_ASSERT(rc == 0);

	rc = pdclust_layout_verify(LINEAR_ENUM_ID, lid,
				   60, 6, 4096, &seed,
				   1500, 777, 888, l);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(l, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static void test_decode(void)
{
	uint64_t    lid;

	/*
	 * Decode a layout with PDCLUST layout type and LIST enum type,
	 * with inline entries only.
	 */
	lid = 1001;
	rc = test_decode_pdclust_list(lid, ONLY_INLINE);
	C2_UT_ASSERT(rc == 0);

	/* Decode a layout with PDCLUST layout type and LIST enum type. */
	lid = 1002;
	rc = test_decode_pdclust_list(lid, !ONLY_INLINE);
	C2_UT_ASSERT(rc == 0);

	/* Decode a layout with PDCLUST layout type and LINEAR enum type. */
	lid = 1003;
	rc = test_decode_pdclust_linear(lid);
	C2_UT_ASSERT(rc == 0);
}

static int pdclust_l_build(uint64_t lid, uint32_t N, uint32_t K,
			   uint64_t unitsize, struct c2_uint128 *seed,
			   struct c2_layout_enum *le,
			   struct c2_pdclust_layout **pl)
{
	struct c2_pool *pool;

	rc = c2_pool_lookup(DEFAULT_POOL_ID, &pool);
	C2_UT_ASSERT(rc == 0);

	rc = c2_pdclust_build(&domain, pool, lid, N, K, unitsize, seed, le, pl);
	C2_UT_ASSERT(rc == 0);

	return rc;
}

static int pdclust_layout_build(uint32_t enum_id,
				uint64_t lid,
				uint32_t N, uint32_t K,
				uint64_t unitsize, struct c2_uint128 *seed,
				uint32_t nr,
				uint32_t A, uint32_t B, /* For linear enum.*/
				struct c2_pdclust_layout **pl,
				struct c2_layout_list_enum **list_e,
				struct c2_layout_linear_enum **lin_e)
{
	struct c2_fid         *cob_list;
	int                    i;
	struct c2_layout_enum *e;

	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	if (enum_id == LIST_ENUM_ID) {
		C2_UT_ASSERT(A == A_NONE && B == B_NONE &&
			     lin_e == NULL);
		C2_UT_ASSERT(list_e != NULL && *list_e == NULL);

		C2_ALLOC_ARR(cob_list, nr);
		C2_UT_ASSERT(cob_list != NULL);

		for (i = 0; i < nr; ++i)
			c2_fid_set(&cob_list[i], i * 100 + 1, i + 1);

		rc = c2_list_enum_build(&domain, lid, cob_list, nr, list_e);
		C2_UT_ASSERT(rc == 0);

		C2_UT_ASSERT(list_e != NULL);
		C2_UT_ASSERT((*list_e)->lle_base.le_type == &c2_list_enum_type);
		C2_UT_ASSERT((*list_e)->lle_base.le_type->let_id ==
				c2_list_enum_type.let_id);
		e = &(*list_e)->lle_base;
		c2_free(cob_list);
	} else { /* LINEAR_ENUM_ID */
		C2_UT_ASSERT(B != B_NONE && list_e == NULL);
		C2_UT_ASSERT(lin_e != NULL && *lin_e == NULL);

		rc = c2_linear_enum_build(&domain, lid, pool.po_width,
					  A, B, lin_e);
		C2_UT_ASSERT(rc == 0);

		C2_UT_ASSERT(*lin_e != NULL);
		C2_UT_ASSERT((*lin_e)->lle_base.le_type ==
			     &c2_linear_enum_type);
		C2_UT_ASSERT((*lin_e)->lle_base.le_type->let_id ==
			      c2_linear_enum_type.let_id);
		e = &(*lin_e)->lle_base;
	}

	rc = pdclust_l_build(lid, N, K, unitsize, seed, e, pl);
	C2_UT_ASSERT(rc == 0);

	return rc;
}

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

static int pdclust_layout_buf_verify(uint32_t enum_id, uint64_t lid,
				     uint32_t N, uint32_t K,
				     uint64_t unitsize, struct c2_uint128 *seed,
				     uint32_t nr,
				     uint32_t A, uint32_t B, /* For lin enum */
				     struct c2_bufvec_cursor *cur)
{
	uint32_t                       lt_id;
	uint32_t                       let_id;
	uint32_t                       i;
	struct ldb_cob_entries_header *ldb_ce_header;
	struct c2_fid                 *cob_id;
	struct c2_fid                  cob_id1;
	struct c2_layout_linear_attr  *lin_attr;

	C2_UT_ASSERT(cur != NULL);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	lbuf_verify(cur, &lt_id);
	C2_UT_ASSERT(lt_id == c2_pdclust_layout_type.lt_id);

	pdclust_lbuf_verify(N, K, unitsize, seed, cur, &let_id);


	if (enum_id == LIST_ENUM_ID) {
		C2_UT_ASSERT(let_id == c2_list_enum_type.let_id);

		C2_UT_ASSERT(c2_bufvec_cursor_step(cur) >=
			     sizeof *ldb_ce_header);

		ldb_ce_header = c2_bufvec_cursor_addr(cur);
		C2_UT_ASSERT(ldb_ce_header != NULL);
		c2_bufvec_cursor_move(cur, sizeof *ldb_ce_header);

		C2_UT_ASSERT(ldb_ce_header->llces_nr > 0);
		C2_UT_ASSERT(c2_bufvec_cursor_step(cur) >=
			     ldb_ce_header->llces_nr * sizeof *cob_id);

		for (i = 0; i < ldb_ce_header->llces_nr; ++i) {
			cob_id = c2_bufvec_cursor_addr(cur);
			C2_UT_ASSERT(cob_id != NULL);

			c2_fid_set(&cob_id1, i * 100 + 1, i + 1);
			C2_UT_ASSERT(c2_fid_eq(cob_id, &cob_id1));

			c2_bufvec_cursor_move(cur, sizeof *cob_id);
		}
	} else {
		C2_UT_ASSERT(let_id == c2_linear_enum_type.let_id);

		C2_UT_ASSERT(c2_bufvec_cursor_step(cur) >=
			     sizeof *lin_attr);

		lin_attr = c2_bufvec_cursor_addr(cur);
		C2_UT_ASSERT(lin_attr->lla_nr == pool.po_width);
		C2_UT_ASSERT(lin_attr->lla_A == A);
		C2_UT_ASSERT(lin_attr->lla_B == B);
	}

	return rc;
}


static int test_encode_pdclust_linear(uint64_t lid)
{
	struct c2_pdclust_layout     *pl = NULL;
	void                         *area = NULL;
	c2_bcount_t                   num_bytes;
	struct c2_bufvec              bv;
	struct c2_bufvec_cursor       cur;
	struct c2_uint128             seed;
	struct c2_layout_linear_enum *le = NULL;

	C2_ENTRY();

	c2_uint128_init(&seed, "encodepdclustlin");
	allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);
	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
				  4, 1, 4096, &seed,
				  pool.po_width, 10, 20,
				  &pl, NULL, &le);
	C2_UT_ASSERT(rc == 0);

	rc  = c2_layout_encode(&domain, &pl->pl_base.ls_base, C2_LXO_BUFFER_OP,
			       NULL, NULL, NULL, &cur);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);
	rc = pdclust_layout_buf_verify(LINEAR_ENUM_ID, lid,
				       4, 1, 4096, &seed,
				       pool.po_width, 10, 20, &cur);

	layout_destroy(&pl->pl_base.ls_base, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static int test_encode_pdclust_list(uint64_t lid, bool only_inline_cob_entries)
{
	struct c2_pdclust_layout   *pl = NULL;
	void                       *area = NULL;
	c2_bcount_t                 num_bytes;
	struct c2_bufvec            bv;
	struct c2_bufvec_cursor     cur;
	struct c2_uint128           seed;
	uint32_t                    nr;
	struct c2_layout_list_enum *le = NULL;

	C2_ENTRY("lid %llu", (unsigned long long)lid);

	c2_uint128_init(&seed, "encodepdclustlis");
	allocate_area(&area, ADDITIONAL_BYTES_DEFAULT, &num_bytes);
	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	nr = only_inline_cob_entries == ONLY_INLINE ? 5 : 125;

	rc = pdclust_layout_build(LIST_ENUM_ID, lid,
				  4, 1, 4096, &seed,
				  nr, A_NONE, B_NONE,
				  &pl, &le, NULL);
	C2_UT_ASSERT(rc == 0);

	rc  = c2_layout_encode(&domain, &pl->pl_base.ls_base, C2_LXO_BUFFER_OP,
			       NULL, NULL, NULL, &cur);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);
	rc = pdclust_layout_buf_verify(LIST_ENUM_ID, lid,
				       4, 1, 4096, &seed,
				       nr, A_NONE, B_NONE, &cur);

	layout_destroy(&pl->pl_base.ls_base, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static void test_encode(void)
{
	uint64_t lid;
	int      rc;

	/*
	 * Encode for 'pdclust' layout type and 'list' enumeration type,
	 * with only inline entries.
	 */
	lid = 2001;
	rc = test_encode_pdclust_list(lid, ONLY_INLINE);
	C2_UT_ASSERT(rc == 0);

	/* Encode for 'pdclust' layout type and 'list' enumeration type. */
	lid = 2002;
	rc = test_encode_pdclust_list(lid, !ONLY_INLINE);
	C2_UT_ASSERT(rc == 0);

	/* Encode for 'pdclust' layout type and 'linear' enumeration type. */
	lid = 2003;
	rc = test_encode_pdclust_linear(lid);
	C2_UT_ASSERT(rc == 0);
}

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

static void pdclust_layout_buf_compare(struct c2_bufvec_cursor *cur1,
				       struct c2_bufvec_cursor *cur2,
				       uint32_t enum_id)
{
	struct ldb_cob_entries_header *ldb_ce_header1;
	struct ldb_cob_entries_header *ldb_ce_header2;
	struct c2_fid                 *cob_id1;
	struct c2_fid                 *cob_id2;
	uint32_t                       i;
	struct c2_layout_linear_attr  *lin_attr1;
	struct c2_layout_linear_attr  *lin_attr2;

	C2_UT_ASSERT(cur1 != NULL);
	C2_UT_ASSERT(cur2 != NULL);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	lbuf_compare(cur1, cur2);

	pdclust_lbuf_compare(cur1, cur2);

	if (enum_id == LIST_ENUM_ID) {
		C2_UT_ASSERT(c2_bufvec_cursor_step(cur1) >=
			     sizeof *ldb_ce_header1);
		C2_UT_ASSERT(c2_bufvec_cursor_step(cur2) >=
			     sizeof *ldb_ce_header2);

		ldb_ce_header1 = c2_bufvec_cursor_addr(cur1);
		ldb_ce_header2 = c2_bufvec_cursor_addr(cur2);

		c2_bufvec_cursor_move(cur1, sizeof *ldb_ce_header1);
		c2_bufvec_cursor_move(cur2, sizeof *ldb_ce_header2);

		C2_UT_ASSERT(ldb_ce_header1->llces_nr ==
			     ldb_ce_header2->llces_nr);

		C2_UT_ASSERT(c2_bufvec_cursor_step(cur1) >=
			     ldb_ce_header1->llces_nr * sizeof *cob_id1);
		C2_UT_ASSERT(c2_bufvec_cursor_step(cur2) >=
			     ldb_ce_header2->llces_nr * sizeof *cob_id2);

		for (i = 0; i < ldb_ce_header1->llces_nr; ++i) {
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


static int test_decode_encode_pdclust_list(uint64_t lid,
					   bool only_inline_cob_entries)
{
	void                    *area1 = NULL;
	struct c2_bufvec         bv1;
	struct c2_bufvec_cursor  cur1;
	c2_bcount_t              num_bytes;
	struct c2_layout        *l = NULL;
	uint32_t                 nr;
	struct c2_uint128        seed;
	void                    *area2 = NULL;
	struct c2_bufvec         bv2;
	struct c2_bufvec_cursor  cur2;

	C2_ENTRY();

	c2_uint128_init(&seed, "decodeencodeplis");
	allocate_area(&area1, ADDITIONAL_BYTES_DEFAULT, &num_bytes);
	bv1 = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area1, &num_bytes);
	c2_bufvec_cursor_init(&cur1, &bv1);

	nr = only_inline_cob_entries == ONLY_INLINE ? 5 : 125;

	rc = pdclust_layout_buf_build(LIST_ENUM_ID, lid,
				      50, 4, 4096, &seed,
				      nr, A_NONE, B_NONE, &cur1);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur1, &bv1);
	rc = c2_layout_decode(&domain, lid, &cur1, C2_LXO_BUFFER_OP,
			      NULL, NULL, &l);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur1, &bv1);

	allocate_area(&area2, ADDITIONAL_BYTES_DEFAULT, &num_bytes);
	bv2 = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area2, &num_bytes);
	c2_bufvec_cursor_init(&cur2, &bv2);

	rc = c2_layout_encode(&domain, l, C2_LXO_BUFFER_OP,
			      NULL, NULL, NULL, &cur2);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur2, &bv2);

	pdclust_layout_buf_compare(&cur1, &cur2, LIST_ENUM_ID);

	layout_destroy(l, lid);
	c2_free(area1);
	c2_free(area2);

	C2_LEAVE();
	return rc;
}

static void test_decode_encode(void)
{
	uint64_t lid;
	int      rc;

	/*
	 * Build a buffer representing a layout with 'pdclust' layout type
	 * and 'list' enum type, with only inline entries.
	 * Decode it into a layout object. Then encode it again into a buffer.
	 * Now, compare the original buffer with this encoded buffer.
	 */
	lid = 3001;
	rc = test_decode_encode_pdclust_list(lid, ONLY_INLINE);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a buffer representing a layout with 'pdclust' layout type
	 * and 'list' enum type.
	 * Decode it into a layout object. Then encode it again into a buffer.
	 * Now, compare the original buffer with this encoded buffer.
	 */
	lid = 3002;
	rc = test_decode_encode_pdclust_list(lid, !ONLY_INLINE);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a buffer representing a layout with 'pdclust' layout type
	 * and 'linear' enum type.
	 * Decode it into a layout object. Then encode it again into a buffer.
	 * Now, compare the original buffer with this encoded buffer.
	 */
	lid = 3003;
	//rc = test_decode_encode_pdclust_linear(lid);
	C2_UT_ASSERT(rc == 0);
}


static void pdclust_layout_compare(struct c2_layout *l1,
				   struct c2_layout *l2,
				   uint32_t enum_id)
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

	C2_UT_ASSERT(l1->l_type == l2->l_type);
	C2_UT_ASSERT(l1->l_ref == l2->l_ref);
	C2_UT_ASSERT(l1->l_pool_id == l2->l_pool_id);

	/* todo check if can use bob_of */
	pl1 = container_of(l1, struct c2_pdclust_layout, pl_base.ls_base);
	pl2 = container_of(l2, struct c2_pdclust_layout, pl_base.ls_base);

	C2_UT_ASSERT(pl1->pl_base.ls_enum->le_type ==
		     pl2->pl_base.ls_enum->le_type);
	C2_UT_ASSERT(pl1->pl_attr.pa_N == pl2->pl_attr.pa_N);
	C2_UT_ASSERT(pl1->pl_attr.pa_K == pl2->pl_attr.pa_K);
	C2_UT_ASSERT(pl1->pl_attr.pa_P == pl2->pl_attr.pa_P);
	C2_UT_ASSERT(c2_uint128_eq(&pl1->pl_attr.pa_seed,
				   &pl2->pl_attr.pa_seed));

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


static int test_encode_decode_pdclust_list(uint64_t lid,
					   bool only_inline_cob_entries)
{
	struct c2_pdclust_layout   *pl = NULL;
	void                       *area = NULL;
	c2_bcount_t                 num_bytes;
	struct c2_bufvec            bv;
	struct c2_bufvec_cursor     cur;
	struct c2_uint128           seed;
	uint32_t                    nr;
	struct c2_layout_list_enum *le = NULL;
	struct c2_layout           *l = NULL;

	C2_ENTRY("lid %llu", (unsigned long long)lid);

	c2_uint128_init(&seed, "encodedecodeplis");
	allocate_area(&area, ADDITIONAL_BYTES_DEFAULT, &num_bytes);
	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	nr = only_inline_cob_entries == ONLY_INLINE ? 5 : 125;

	rc = pdclust_layout_build(LIST_ENUM_ID, lid,
				  4, 1, 4096, &seed,
				  nr, A_NONE, B_NONE,
				  &pl, &le, NULL);
	C2_UT_ASSERT(rc == 0);

	rc  = c2_layout_encode(&domain, &pl->pl_base.ls_base, C2_LXO_BUFFER_OP,
			       NULL, NULL, NULL, &cur);
	C2_UT_ASSERT(rc == 0);


	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);

	/* Decode the layout. */
	rc = c2_layout_decode(&domain, lid, &cur, C2_LXO_BUFFER_OP,
			      NULL, NULL, &l);
	C2_UT_ASSERT(rc == 0);

	pdclust_layout_compare(&pl->pl_base.ls_base, l, LIST_ENUM_ID);

	layout_destroy(&pl->pl_base.ls_base, lid);
	layout_destroy(l, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static int test_encode_decode_pdclust_linear(uint64_t lid)
{
	struct c2_pdclust_layout     *pl = NULL;
	void                         *area = NULL;
	c2_bcount_t                   num_bytes;
	struct c2_bufvec              bv;
	struct c2_bufvec_cursor       cur;
	struct c2_uint128             seed;
	struct c2_layout_linear_enum *le = NULL;
	struct c2_layout             *l = NULL;

	C2_ENTRY();

	c2_uint128_init(&seed, "encodedecodeplin");
	allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);
	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
				  4, 1, 4096, &seed,
				  pool.po_width, 10, 20,
				  &pl, NULL, &le);
	C2_UT_ASSERT(rc == 0);

	rc  = c2_layout_encode(&domain, &pl->pl_base.ls_base, C2_LXO_BUFFER_OP,
			       NULL, NULL, NULL, &cur);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);

	/* Decode the layout. */
	rc = c2_layout_decode(&domain, lid, &cur, C2_LXO_BUFFER_OP,
			      NULL, NULL, &l);
	C2_UT_ASSERT(rc == 0);
	pdclust_layout_compare(&pl->pl_base.ls_base, l, LINEAR_ENUM_ID);

	layout_destroy(&pl->pl_base.ls_base, lid);
	layout_destroy(l, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static void test_encode_decode(void)
{
	uint64_t lid;
	int      rc;

	/*
	 * Build a layout with 'pdclust' layout type and 'list' enum type,
	 * with only inline entries.
	 * Encode it into a buffer. Then decode it again into a layout.
	 * Now, compare the original layout with this decoded layout.
	 */
	lid = 3001;
	rc = test_encode_decode_pdclust_list(lid, ONLY_INLINE);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout with 'pdclust' layout type and 'list' enum type.
	 * Encode it into a buffer. Then decode it again into a layout.
	 * Now, compare the original layout with this decoded layout.
	 */
	lid = 3002;
	rc = test_encode_decode_pdclust_list(lid, !ONLY_INLINE);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout with 'pdclust' layout type and 'linear' enum type.
	 * Encode it into a buffer. Then decode it again into a layout.
	 * Now, compare the original layout with this decoded layout.
	 */
	lid = 3003;
	rc = test_encode_decode_pdclust_linear(lid);
	C2_UT_ASSERT(rc == 0);
}


static void pair_reset(struct c2_db_pair *pair, uint64_t *lid,
		       void *area, c2_bcount_t num_bytes)
{
	pair->dp_key.db_buf.b_addr = lid;
	pair->dp_key.db_buf.b_nob = sizeof *lid;
	pair->dp_rec.db_buf.b_addr = area;
	pair->dp_rec.db_buf.b_nob = num_bytes;
}

/* todo see if it can be merged into other similar wrapper for list */
static int test_add_pdclust(uint32_t enum_id, uint64_t lid,
			    bool lookup_test, bool duplicate_test)
{
	c2_bcount_t                   num_bytes;
	void                         *area = NULL;
	struct c2_pdclust_layout     *pl = NULL;
	struct c2_db_pair             pair;
	struct c2_db_tx               tx;
	struct c2_uint128             seed;
	struct c2_layout_list_enum   *list_enum = NULL;
	struct c2_layout_linear_enum *lin_enum = NULL;
	struct c2_layout             *l = NULL;

	C2_ENTRY();

	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	c2_uint128_init(&seed, "addpdclustlayout");

	if (enum_id == LIST_ENUM_ID) {
		allocate_area(&area, ADDITIONAL_BYTES_DEFAULT, &num_bytes);
		rc = pdclust_layout_build(LIST_ENUM_ID,
					  lid, 5, 2, 4096, &seed,
					  30, A_NONE, B_NONE,
					  &pl, &list_enum, NULL);
	}
	else {
		allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);
		rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
					  4, 1, 4096, &seed,
					  pool.po_width, 100, 200,
					  &pl, NULL, &lin_enum);
	}
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_layout_add(&schema, &pl->pl_base.ls_base, &pair, &tx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(&pl->pl_base.ls_base, lid);

	if (lookup_test) {
		/*
		 * Verify that the layout is indeed added to the DB, by
		 * performing lookup for the same lid.
		 */
		rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
		C2_UT_ASSERT(rc == 0);

		pair_reset(&pair, &lid, area, num_bytes);

		rc = c2_layout_lookup(&schema, lid, &pair, &tx, &l);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(l->l_id == lid);

		rc = c2_db_tx_commit(&tx);
		C2_UT_ASSERT(rc == 0);

		layout_destroy(l, lid);
	}

	if (duplicate_test) {
		/*
		 * Try to add layout with the same lid again and the operation
		 * should fail this time with the error EEXIST.
		 */
		pl = NULL;
		if (enum_id == LIST_ENUM_ID) {
			list_enum = NULL;
			rc = pdclust_layout_build(LIST_ENUM_ID,
						  lid, 50, 20, 4096, &seed,
						  300, A_NONE, B_NONE,
						  &pl, &list_enum, NULL);

		} else {
			lin_enum = NULL;
			rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
						  40, 10, 4096, &seed,
						  pool.po_width, 110, 210,
						  &pl, NULL, &lin_enum);
		}
		C2_UT_ASSERT(rc == 0);

		rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
		C2_UT_ASSERT(rc == 0);

		pair_reset(&pair, &lid, area, num_bytes);

		rc = c2_layout_add(&schema, &pl->pl_base.ls_base, &pair, &tx);
		C2_UT_ASSERT(rc == -EEXIST);

		rc = c2_db_tx_commit(&tx);
		C2_UT_ASSERT(rc == 0);

		layout_destroy(&pl->pl_base.ls_base, lid);
	}

	c2_free(area);

	C2_LEAVE();
	return rc;
}

static int test_lookup_pdclust_linear(uint64_t lid, bool test_existing)
{
	c2_bcount_t        num_bytes;
	void              *area = NULL;
	struct c2_layout  *l = NULL;
	struct c2_db_pair  pair;
	struct c2_db_tx    tx;

	C2_ENTRY();

	if (test_existing) {
		/*
		 * Add a layout with pdclust layout type and linear enum
		 * type.
		 */
		rc = test_add_pdclust(LINEAR_ENUM_ID, lid,
				      !LOOKUP_TEST, !DUPLICATE_TEST);
		C2_UT_ASSERT(rc == 0);
	}

	allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_layout_lookup(&schema, lid, &pair, &tx, &l);

	if (test_existing) {
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(l->l_id == lid);
	} else
		C2_UT_ASSERT(rc == -ENOENT);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	if (test_existing)
		layout_destroy(l, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static void test_lookup(void)
{
	uint64_t lid;

	/* Lookup for a non-existing lid. */
	lid = 4001;
	rc = test_lookup_pdclust_linear(lid, !TEST_EXISTING);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Add a layout with pdclust layout type and linear enum type, and
	 * then perform lookup for it.
	 */
	lid = 4002;
	rc = test_lookup_pdclust_linear(lid, TEST_EXISTING);
	C2_UT_ASSERT(rc == 0);

	/* Once again, lookup for a non-existing lid. */
	lid = 4003;
	rc = test_lookup_pdclust_linear(lid, !TEST_EXISTING);
	C2_UT_ASSERT(rc == 0);
}

static void test_add(void)
{
	uint64_t lid;

	lid = 5001;
	rc = test_add_pdclust(LIST_ENUM_ID, lid, LOOKUP_TEST, DUPLICATE_TEST);
	C2_UT_ASSERT(rc == 0);

	lid = 5002;
	rc = test_add_pdclust(LINEAR_ENUM_ID, lid, LOOKUP_TEST, DUPLICATE_TEST);
	C2_UT_ASSERT(rc == 0);
}

/* todo Optimize this test using test_add_pdclust(). */
static int test_update_pdclust_linear(uint64_t lid)
{
	c2_bcount_t                   num_bytes;
	void                         *area = NULL;
	struct c2_pdclust_layout     *pl = NULL;
	struct c2_db_pair             pair;
	struct c2_db_tx               tx;
	struct c2_uint128             seed;
	struct c2_layout             *l = NULL;
	struct c2_layout_linear_enum *le = NULL;

	C2_ENTRY();

	c2_uint128_init(&seed, "updatepdclustlin");
	allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
				  6, 1, 4096, &seed,
				  pool.po_width, 800, 900,
				  &pl, NULL, &le);
	C2_UT_ASSERT(rc == 0);


	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_layout_add(&schema, &pl->pl_base.ls_base, &pair, &tx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(&pl->pl_base.ls_base, lid);

	/* Lookup the record just for verification. */
	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_layout_lookup(&schema, lid, &pair, &tx, &l);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(l->l_ref == DEFAULT_REF_COUNT);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	l->l_ref = 1234567;

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_layout_update(&schema, l, &pair, &tx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(l, lid);
	l = NULL;

	/* Lookup the record just for verification. */
	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_layout_lookup(&schema, lid, &pair, &tx, &l);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(l->l_ref == 1234567);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(l, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static void test_update(void)
{
	uint64_t lid;

	/* TBD todo Test update for linear enum type.
	lid = 6001;
	rc = test_update_pdclust_list(lid);
	C2_UT_ASSERT(rc == 0);
	*/

	lid = 6002;
	rc = test_update_pdclust_linear(lid);
	C2_UT_ASSERT(rc == 0);
}

static int test_delete_pdclust_linear(uint64_t lid)
{
	c2_bcount_t                   num_bytes;
	void                         *area = NULL;
	struct c2_pdclust_layout     *pl = NULL;
	struct c2_db_pair             pair;
	struct c2_db_tx               tx;
	struct c2_uint128             seed;
	struct c2_layout             *l = NULL;
	struct c2_layout_linear_enum *le = NULL;

	C2_ENTRY();

	c2_uint128_init(&seed, "deletepdclustlin");
	allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
				  6, 1, 4096, &seed,
				  pool.po_width, 800, 900,
				  &pl, NULL, &le);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_layout_add(&schema, &pl->pl_base.ls_base, &pair, &tx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(&pl->pl_base.ls_base, lid);

	/* Lookup the record just for verification. */
	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_layout_lookup(&schema, lid, &pair, &tx, &l);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(l->l_ref == DEFAULT_REF_COUNT);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_layout_delete(&schema, l, &pair, &tx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(l, lid);
	l = NULL;

	/* Lookup the record just for verification. */
	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_layout_lookup(&schema, lid, &pair, &tx, &l);
	C2_UT_ASSERT(rc == -ENOENT);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	c2_free(area);

	C2_LEAVE();
	return rc;
}

static void test_delete(void)
{
	uint64_t lid;

	lid = 7001;
	rc = test_delete_pdclust_linear(lid);
	C2_UT_ASSERT(rc == 0);
}

static void test_persistence(void)
{
}

static int pdclust_enum_op_verify(uint32_t enum_id, uint64_t lid,
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
	struct c2_fid                 fid;
	struct c2_fid                 fid_from_layout;
	struct c2_fid                 gfid;

	C2_UT_ASSERT(l != NULL);
	C2_UT_ASSERT(l->l_type == &c2_pdclust_layout_type);

	pl = container_of(l, struct c2_pdclust_layout, pl_base.ls_base);

	pdclust_l_verify(lid, N, K, unitsize, seed, pl);

	if (enum_id == LIST_ENUM_ID) {
		list_enum = container_of(pl->pl_base.ls_enum,
					 struct c2_layout_list_enum, lle_base);

		C2_UT_ASSERT(
			list_enum->lle_base.le_ops->leo_nr(&list_enum->lle_base,
							   lid) == nr);

		for(i = 0; i < list_enum->lle_nr; ++i) {
			c2_fid_set(&fid, i * 100 + 1, i + 1);
			list_enum->lle_base.le_ops->leo_get(
							&list_enum->lle_base,
							lid, i, NULL,
							&fid_from_layout);
			C2_UT_ASSERT(c2_fid_eq(&fid_from_layout, &fid));
		}
	} else {
		lin_enum = container_of(pl->pl_base.ls_enum,
					struct c2_layout_linear_enum, lle_base);
		C2_UT_ASSERT(
			lin_enum->lle_base.le_ops->leo_nr(&lin_enum->lle_base,
							  lid) == nr);

		/* Set gfid to some dummy value. */
		c2_fid_set(&gfid, 0, 999);

		for(i = 0; i < nr; ++i) {
			c2_fid_set(&fid, lin_enum->lle_attr.lla_A +
					 i * lin_enum->lle_attr.lla_B,
				   gfid.f_key);
			lin_enum->lle_base.le_ops->leo_get(&lin_enum->lle_base,
							   lid, i, &gfid,
							   &fid_from_layout);
			C2_UT_ASSERT(c2_fid_eq(&fid_from_layout, &fid));
		}
	}

	return 0;
}

static int test_enum_ops_pdclust(uint32_t enum_id, uint64_t lid)
{
	void                    *area = NULL;
	c2_bcount_t              num_bytes;
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;
	struct c2_layout        *l = NULL;
	struct c2_uint128        seed;

	C2_ENTRY();

	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	c2_uint128_init(&seed, "updownupdownupdo");
	allocate_area(&area, ADDITIONAL_BYTES_DEFAULT, &num_bytes);
	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	if (enum_id == LIST_ENUM_ID)
		rc = pdclust_layout_buf_build(LIST_ENUM_ID, lid,
					      100, 10, 4096, &seed,
					      125, A_NONE, B_NONE, &cur);
	else
		rc = pdclust_layout_buf_build(LINEAR_ENUM_ID, lid,
					      80, 8, 4096, &seed,
					      3000, 777, 888, &cur);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);
	rc = c2_layout_decode(&domain, lid, &cur, C2_LXO_BUFFER_OP,
			      NULL, NULL, &l);
	C2_UT_ASSERT(rc == 0);

	if (enum_id == LIST_ENUM_ID)
		rc = pdclust_enum_op_verify(LIST_ENUM_ID, lid,
					    100, 10, 4096, &seed,
					    125, A_NONE, B_NONE, l);
	else
		rc = pdclust_enum_op_verify(LINEAR_ENUM_ID, lid,
					    80, 8, 4096, &seed,
					    3000, 777, 888, l);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(l, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static void test_enum_operations(void)
{
	uint64_t lid;

	/* Decode a layout with PDCLUST layout type and LIST enum type.
	 * And then verify its enum ops.
	 */
	lid = 8001;
	rc = test_enum_ops_pdclust(LIST_ENUM_ID, lid);
	C2_UT_ASSERT(rc == 0);

	/* Decode a layout with PDCLUST layout type and LINEAR enum type.
	 * And then verify its enum ops.
	 */
	lid = 8002;
	rc = test_enum_ops_pdclust(LINEAR_ENUM_ID, lid);
	C2_UT_ASSERT(rc == 0);
}

static void bufvec_copyto_use(struct c2_bufvec_cursor *dcur)
{
	c2_bcount_t                  nbytes_copied;
	struct c2_layout_rec         rec;
	struct c2_layout_pdclust_rec pl_rec;
	struct c2_layout_linear_attr lin_rec;

	rec.lr_lt_id     = c2_pdclust_layout_type.lt_id;
	rec.lr_ref_count = 0;
	rec.lr_pool_id   = DEFAULT_POOL_ID;

	nbytes_copied = c2_bufvec_cursor_copyto(dcur, &rec, sizeof rec);
	C2_UT_ASSERT(nbytes_copied == sizeof rec);

	pl_rec.pr_let_id    = c2_list_enum_type.let_id;
	pl_rec.pr_attr.pa_N = 4;
	pl_rec.pr_attr.pa_K = 1;
	pl_rec.pr_attr.pa_P = pool.po_width;

	nbytes_copied = c2_bufvec_cursor_copyto(dcur, &pl_rec,
						sizeof pl_rec);
	C2_UT_ASSERT(nbytes_copied == sizeof pl_rec);

	lin_rec.lla_nr = 20;
	lin_rec.lla_A  = 100;
	lin_rec.lla_B  = 200;

	nbytes_copied = c2_bufvec_cursor_copyto(dcur, &lin_rec,
						sizeof lin_rec);
	C2_UT_ASSERT(nbytes_copied == sizeof lin_rec);
}

static void bufvec_copyto_verify(struct c2_bufvec_cursor *cur)
{
	struct c2_layout_rec         *rec;
	struct c2_layout_pdclust_rec *pl_rec;
	struct c2_layout_linear_attr *lin_rec;

	rec = c2_bufvec_cursor_addr(cur);
	C2_UT_ASSERT(rec != NULL);

	C2_UT_ASSERT(rec->lr_lt_id == c2_pdclust_layout_type.lt_id);
	C2_UT_ASSERT(rec->lr_ref_count == 0);
	C2_UT_ASSERT(rec->lr_pool_id == DEFAULT_POOL_ID);

	rc = c2_bufvec_cursor_move(cur, sizeof *rec);

	pl_rec = c2_bufvec_cursor_addr(cur);
	C2_UT_ASSERT(pl_rec != NULL);

	C2_UT_ASSERT(pl_rec->pr_let_id == c2_list_enum_type.let_id);
	C2_UT_ASSERT(pl_rec->pr_attr.pa_N == 4);
	C2_UT_ASSERT(pl_rec->pr_attr.pa_K == 1);
	C2_UT_ASSERT(pl_rec->pr_attr.pa_P == pool.po_width);

	c2_bufvec_cursor_move(cur, sizeof *pl_rec);

	lin_rec = c2_bufvec_cursor_addr(cur);
	C2_UT_ASSERT(lin_rec != NULL);

	C2_UT_ASSERT(lin_rec->lla_nr == 20);
	C2_UT_ASSERT(lin_rec->lla_A == 100);
	C2_UT_ASSERT(lin_rec->lla_B == 200);
}

/* todo Following TC should be moved to bufvec UT. */
static void test_bufvec_copyto(void)
{
	void                    *area;
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  dcur;
	c2_bcount_t              num_bytes;

	C2_ENTRY();

	num_bytes = c2_layout_max_recsize(&domain) + 1024;
	area = c2_alloc(num_bytes);
	C2_UT_ASSERT(area != NULL);

	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&dcur, &bv);

	bufvec_copyto_use(&dcur);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&dcur, &bv);

	bufvec_copyto_verify(&dcur);

	c2_free(area);

	C2_LEAVE();
}


const struct c2_test_suite layout_ut = {
	.ts_name  = "layout-ut",
	.ts_init  = test_init,
	.ts_fini  = test_fini,
	.ts_tests = {
		{ "layout-domain-init-fini", test_domain_init_fini },
		{ "layout-schema-init-fini", test_schema_init_fini },
		{ "layout-type-register-unregister", test_type_reg_unreg },
		{ "layout-etype-register-unregister", test_etype_reg_unreg },
		{ "layout-ldb-register-unregister", test_ldb_reg_unreg },
		{ "layout-max-recsize", test_max_recsize },
		{ "layout-recsize", test_recsize },
		{ "layout-decode", test_decode },
		{ "layout-encode", test_encode },
		{ "layout-decode-encode", test_decode_encode },
		{ "layout-encode-decode", test_encode_decode },
		/* todo add TC for l_ref get-put */
                { "layout-lookup", test_lookup },
                { "layout-add", test_add },
                { "layout-update", test_update },
                { "layout-delete", test_delete },
		{ "layout-enum-ops", test_enum_operations },
                { "layout-buf-copyto", test_bufvec_copyto },
                { "layout-persistence", test_persistence },
		{ NULL, NULL }
	}
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
