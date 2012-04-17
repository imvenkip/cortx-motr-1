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
#include "layout/layout_db.c" /* recsize_get() */
#include "layout/list_enum.h"
#include "layout/list_enum.c" /* cob_entries_header */
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
	A_NONE                   = 0, /* Invalid value for attribute A */
	B_NONE                   = 0, /* Invalid value for attribute B */
	ADDITIONAL_BYTES_NONE    = 0,
	ADDITIONAL_BYTES_DEFAULT = 2048,
	ONLY_INLINE_TEST         = true,
	EXISTING_TEST            = 1,
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

	/* Intialize the domain. */
	rc = c2_layout_domain_init(&domain);
	C2_ASSERT(rc == 0);

	rc = c2_dbenv_init(&dbenv, db_name, DBFLAGS);
	C2_ASSERT(rc == 0);

	/* Initialize the schema. */
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

	/* Register all the available layout types and enum types. */
	rc = c2_layout_register(&domain);
	C2_ASSERT(rc == 0 || rc == -EEXIST);

	/* Intialize the pool. */
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

	/* Should be able to initialize the schema again after finalizing it. */
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

static void test_reg_unreg(void)
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

	/* Finalize the schema. */
	c2_layout_schema_fini(&t_schema);

	c2_dbenv_fini(&t_dbenv);

	/* Finalize the domain. */
	c2_layout_domain_fini(&t_domain);

	C2_LEAVE();
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
	uint32_t                     let_id;
	c2_bcount_t                  nbytes_copied;
	struct cob_entries_header    ce_header;
	struct c2_fid                cob_id;
	uint32_t                     i;
	struct c2_layout_linear_attr lin_rec;

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

static int test_decode_pdclust(uint32_t enum_id, uint64_t lid,
			       bool only_inline_test)
{
	void                    *area = NULL;
	c2_bcount_t              num_bytes;
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;
	struct c2_layout        *l = NULL;
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
	rc = c2_layout_decode(&domain, lid, &cur, C2_LXO_BUFFER_OP,
			      NULL, NULL, &l);
	C2_UT_ASSERT(rc == 0);

	/* Verify the layout object built by c2_layout_decode(). */
	rc = pdclust_layout_verify(enum_id, lid,
				   60, 6, 4096, &seed,
				   nr, A, B, l);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(l, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}


static void test_decode(void)
{
	uint64_t lid;

	/*
	 * Decode a layout object with PDCLUST layout type, LIST enum type
	 * with inline entries only.
	 */
	lid = 1001;
	rc = test_decode_pdclust(LIST_ENUM_ID, lid, ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Decode a layout object with PDCLUST layout type and LIST enum
	 * type.
	 */
	lid = 1002;
	rc = test_decode_pdclust(LIST_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Decode a layout object with PDCLUST layout type and LINEAR enum
	 * type.
	 */
	lid = 1003;
	rc = test_decode_pdclust(LINEAR_ENUM_ID, lid, false);
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
				struct c2_layout_list_enum **list_enum,
				struct c2_layout_linear_enum **lin_enum)
{
	struct c2_fid         *cob_list;
	int                    i;
	struct c2_layout_enum *e;

	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	if (enum_id == LIST_ENUM_ID) {
		C2_UT_ASSERT(A == A_NONE && B == B_NONE && lin_enum == NULL);
		C2_UT_ASSERT(list_enum != NULL && *list_enum == NULL);

		C2_ALLOC_ARR(cob_list, nr);
		C2_UT_ASSERT(cob_list != NULL);

		for (i = 0; i < nr; ++i)
			c2_fid_set(&cob_list[i], i * 100 + 1, i + 1);

		rc = c2_list_enum_build(&domain, lid, cob_list, nr, list_enum);
		C2_UT_ASSERT(rc == 0);

		C2_UT_ASSERT(list_enum != NULL);
		C2_UT_ASSERT((*list_enum)->lle_base.le_type ==
			     &c2_list_enum_type);
		C2_UT_ASSERT((*list_enum)->lle_base.le_type->let_id ==
			     c2_list_enum_type.let_id);
		e = &(*list_enum)->lle_base;
		c2_free(cob_list);
	} else { /* LINEAR_ENUM_ID */
		C2_UT_ASSERT(B != B_NONE && list_enum == NULL);
		C2_UT_ASSERT(lin_enum != NULL && *lin_enum == NULL);

		rc = c2_linear_enum_build(&domain, lid, pool.po_width,
					  A, B, lin_enum);
		C2_UT_ASSERT(rc == 0);

		C2_UT_ASSERT(*lin_enum != NULL);
		C2_UT_ASSERT((*lin_enum)->lle_base.le_type ==
			     &c2_linear_enum_type);
		C2_UT_ASSERT((*lin_enum)->lle_base.le_type->let_id ==
			      c2_linear_enum_type.let_id);
		e = &(*lin_enum)->lle_base;
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
	uint32_t                      lt_id;
	uint32_t                      let_id;
	uint32_t                      i;
	struct cob_entries_header    *ce_header;
	struct c2_fid                *cob_id;
	struct c2_fid                 cob_id1;
	struct c2_layout_linear_attr *lin_attr;

	C2_UT_ASSERT(cur != NULL);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	lbuf_verify(cur, &lt_id);
	C2_UT_ASSERT(lt_id == c2_pdclust_layout_type.lt_id);

	pdclust_lbuf_verify(N, K, unitsize, seed, cur, &let_id);

	if (enum_id == LIST_ENUM_ID) {
		C2_UT_ASSERT(let_id == c2_list_enum_type.let_id);

		C2_UT_ASSERT(c2_bufvec_cursor_step(cur) >= sizeof *ce_header);

		ce_header = c2_bufvec_cursor_addr(cur);
		C2_UT_ASSERT(ce_header != NULL);
		c2_bufvec_cursor_move(cur, sizeof *ce_header);

		C2_UT_ASSERT(ce_header->ces_nr > 0);
		C2_UT_ASSERT(c2_bufvec_cursor_step(cur) >=
			     ce_header->ces_nr * sizeof *cob_id);

		for (i = 0; i < ce_header->ces_nr; ++i) {
			cob_id = c2_bufvec_cursor_addr(cur);
			C2_UT_ASSERT(cob_id != NULL);

			c2_fid_set(&cob_id1, i * 100 + 1, i + 1);
			C2_UT_ASSERT(c2_fid_eq(cob_id, &cob_id1));

			c2_bufvec_cursor_move(cur, sizeof *cob_id);
		}
	} else {
		C2_UT_ASSERT(let_id == c2_linear_enum_type.let_id);

		C2_UT_ASSERT(c2_bufvec_cursor_step(cur) >= sizeof *lin_attr);

		lin_attr = c2_bufvec_cursor_addr(cur);
		C2_UT_ASSERT(lin_attr->lla_nr == pool.po_width);
		C2_UT_ASSERT(lin_attr->lla_A == A);
		C2_UT_ASSERT(lin_attr->lla_B == B);
	}

	return rc;
}

static int test_encode_pdclust(uint32_t enum_id, uint64_t lid,
			       bool only_inline_test)
{
	struct c2_pdclust_layout     *pl = NULL;
	void                         *area = NULL;
	c2_bcount_t                   num_bytes;
	struct c2_bufvec              bv;
	struct c2_bufvec_cursor       cur;
	struct c2_uint128             seed;
	uint32_t                      nr;
	struct c2_layout_list_enum   *list_enum = NULL;
	struct c2_layout_linear_enum *lin_enum = NULL;

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
		rc = pdclust_layout_build(LIST_ENUM_ID, lid,
					  4, 1, 4096, &seed,
					  nr, A_NONE, B_NONE,
					  &pl, &list_enum, NULL);
	} else {
		rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
					  4, 1, 4096, &seed,
					  pool.po_width, 10, 20,
					  &pl, NULL, &lin_enum);
	}
	C2_UT_ASSERT(rc == 0);

	/* Encode the layout object into a layout buffer. */
	rc  = c2_layout_encode(&domain, &pl->pl_base.ls_base, C2_LXO_BUFFER_OP,
			       NULL, NULL, NULL, &cur);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);

	/* Verify the layout buffer produced by c2_layout_encode(). */
	if (enum_id == LIST_ENUM_ID)
		rc = pdclust_layout_buf_verify(LIST_ENUM_ID, lid,
					       4, 1, 4096, &seed,
					       nr, A_NONE, B_NONE, &cur);
	else
		rc = pdclust_layout_buf_verify(LINEAR_ENUM_ID, lid,
					       4, 1, 4096, &seed,
					       pool.po_width, 10, 20, &cur);

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
	 * Encode for PDCLUST layout type and LIST enumeration type,
	 * with only inline entries.
	 */
	lid = 2001;
	rc = test_encode_pdclust(LIST_ENUM_ID, lid, ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* Encode for PDCLUST layout type and LIST enumeration type. */
	lid = 2002;
	rc = test_encode_pdclust(LIST_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* Encode for PDCLUST layout type and LINEAR enumeration type. */
	lid = 2003;
	rc = test_encode_pdclust(LINEAR_ENUM_ID, lid, false);
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

	/* Compare generic part of the layout buffer. */
	lbuf_compare(cur1, cur2);

	/* Compare PDCLUST layout type specific part of the layout buffer. */
	pdclust_lbuf_compare(cur1, cur2);

	/* Compare enumeration type specific part of the layout buffer. */
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

static int test_decode_encode_pdclust(uint32_t enum_id, uint64_t lid,
				      bool only_inline_test)
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
	rc = c2_layout_decode(&domain, lid, &cur1, C2_LXO_BUFFER_OP,
			      NULL, NULL, &l);
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

	rc = c2_layout_encode(&domain, l, C2_LXO_BUFFER_OP,
			      NULL, NULL, NULL, &cur2);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur2, &bv2);

	/*
	 * Compare the two layout buffers - one created earlier here and
	 * the one that is produced by c2_layout_encode().
	 */
	pdclust_layout_buf_compare(enum_id, &cur1, &cur2);

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
	 * Build a layout buffer representing a layout with PDCLUST layout type
	 * and LIST enum type, with only inline entries.
	 * Decode it into a layout object. Then encode that layout object again
	 * into another layout buffer.
	 * Then, compare the original layout buffer with this encoded layout
	 * buffer.
	 */
	lid = 3001;
	rc = test_decode_encode_pdclust(LIST_ENUM_ID, lid, ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout buffer representing a layout with PDCLUST layout type
	 * and LIST enum type.
	 * Decode it into a layout object. Then encode that layout object again
	 * into another layout buffer.
	 * Then, compare the original layout buffer with this encoded layout
	 * buffer.
	 */
	lid = 3002;
	rc = test_decode_encode_pdclust(LIST_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout buffer representing a layout with PDCLUST layout type
	 * and LINEAR enum type.
	 * Decode it into a layout object. Then encode that layout object again
	 * into another layout buffer.
	 * Then, compare the original layout buffer with this encoded layout
	 * buffer.
	 */
	lid = 3003;
	rc = test_decode_encode_pdclust(LINEAR_ENUM_ID, lid, false);
	C2_UT_ASSERT(rc == 0);
}


static void pdclust_layout_compare(uint32_t enum_id,
				   struct c2_layout *l1,
				   struct c2_layout *l2)
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

	/* Compare generic part of the layout object. */
	C2_UT_ASSERT(l1->l_type == l2->l_type);
	C2_UT_ASSERT(l1->l_ref == l2->l_ref);
	C2_UT_ASSERT(l1->l_pool_id == l2->l_pool_id);

	/* Compare PDCLUST layout type specific part of the layout object. */
	pl1 = container_of(l1, struct c2_pdclust_layout, pl_base.ls_base);
	pl2 = container_of(l2, struct c2_pdclust_layout, pl_base.ls_base);

	C2_UT_ASSERT(pl1->pl_base.ls_enum->le_type ==
		     pl2->pl_base.ls_enum->le_type);
	C2_UT_ASSERT(pl1->pl_attr.pa_N == pl2->pl_attr.pa_N);
	C2_UT_ASSERT(pl1->pl_attr.pa_K == pl2->pl_attr.pa_K);
	C2_UT_ASSERT(pl1->pl_attr.pa_P == pl2->pl_attr.pa_P);
	C2_UT_ASSERT(c2_uint128_eq(&pl1->pl_attr.pa_seed,
				   &pl2->pl_attr.pa_seed));

	/* Compare enumeration type specific part of the layout object. */
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

static int test_encode_decode_pdclust(uint32_t enum_id, uint64_t lid,
				      bool only_inline_test)
{
	struct c2_pdclust_layout     *pl = NULL;
	void                         *area = NULL;
	c2_bcount_t                   num_bytes;
	struct c2_bufvec              bv;
	struct c2_bufvec_cursor       cur;
	struct c2_uint128             seed;
	uint32_t                      nr;
	struct c2_layout_list_enum   *list_enum = NULL;
	struct c2_layout_linear_enum *lin_enum = NULL;
	struct c2_layout             *l = NULL;

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
					  4, 1, 4096, &seed,
					  nr, A_NONE, B_NONE,
					  &pl, &list_enum, NULL);
	} else
		rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
					  4, 1, 4096, &seed,
					  pool.po_width, 10, 20,
					  &pl, NULL, &lin_enum);
	C2_UT_ASSERT(rc == 0);

	/* Encode the layout object into a layout buffer. */
	rc  = c2_layout_encode(&domain, &pl->pl_base.ls_base, C2_LXO_BUFFER_OP,
			       NULL, NULL, NULL, &cur);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);

	/*
	 * Decode the layout buffer produced by c2_layout_encode() into another
	 * layout object.
	 */
	rc = c2_layout_decode(&domain, lid, &cur, C2_LXO_BUFFER_OP,
			      NULL, NULL, &l);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Comapre the two layout objects - one created earlier here and the
	 * one that is produced by c2_layout_decode().
	 */
	pdclust_layout_compare(enum_id, &pl->pl_base.ls_base, l);

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
	 * Build a layout object with PDCLUST layout type and LIST enum type,
	 * with only inline entries.
	 * Encode it into a layout buffer. Then decode that layout buffer again
	 * into another layout object.
	 * Then, compare the original layout object with the decoded layout
	 * object.
	 */
	lid = 4001;
	rc = test_encode_decode_pdclust(LIST_ENUM_ID, lid, ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout object with PDCLUST layout type and LIST enum type.
	 * Encode it into a layout buffer. Then decode that layout buffer again
	 * into another layout object.
	 * Then, compare the original layout object with the decoded layout
	 * object.
	 */
	lid = 4002;
	rc = test_encode_decode_pdclust(LIST_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Build a layout object with PDCLUST layout type and LINEAR enum type.
	 * Encode it into a layout buffer. Then decode that layout buffer again
	 * into a another layout object.
	 * Now, compare the original layout object with the decoded layout
	 * object.
	 */
	lid = 4003;
	rc = test_encode_decode_pdclust(LINEAR_ENUM_ID, lid, false);
	C2_UT_ASSERT(rc == 0);
}

static void test_max_recsize()
{
	c2_bcount_t max_size_from_api;
	c2_bcount_t max_size_calculated;
	c2_bcount_t list_size;

	/* Get the max size using the API. */
	max_size_from_api = c2_layout_max_recsize(&domain);

	/* Calculate the max size. */
	list_size = sizeof(struct cob_entries_header) +
		    LDB_MAX_INLINE_COB_ENTRIES * sizeof(struct c2_fid);

	max_size_calculated = sizeof(struct c2_layout_rec) +
			      sizeof(struct c2_layout_pdclust_rec) +
			      max64u(list_size,
				     sizeof(struct c2_layout_linear_attr));

	/* Compare the two sizes. */
	C2_UT_ASSERT(max_size_from_api == max_size_calculated);
}

static void pdclust_recsize_verify(uint32_t enum_id,
				   struct c2_layout *l,
				   c2_bcount_t recsize_to_verify)
{
	struct c2_pdclust_layout     *pl;
	struct c2_layout_list_enum   *list_enum;
	c2_bcount_t                   recsize;

	C2_UT_ASSERT(l != NULL);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);
	C2_UT_ASSERT(l->l_type == &c2_pdclust_layout_type);

	pl = container_of(l, struct c2_pdclust_layout, pl_base.ls_base);

	C2_UT_ASSERT(pl->pl_base.ls_enum != NULL);

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

	recsize = sizeof(struct c2_layout_rec) +
		  sizeof(struct c2_layout_pdclust_rec) + recsize;

	/* Compare the two sizes. */
	C2_UT_ASSERT(recsize == recsize_to_verify);
}

static int test_recsize_pdclust(uint32_t enum_id, uint64_t lid,
				bool only_inline_test)
{
	struct c2_pdclust_layout     *pl = NULL;
	void                         *area = NULL;
	c2_bcount_t                   num_bytes;
	struct c2_bufvec              bv;
	struct c2_bufvec_cursor       cur;
	struct c2_uint128             seed;
	uint32_t                      nr;
	struct c2_layout_list_enum   *list_enum = NULL;
	struct c2_layout_linear_enum *lin_enum = NULL;
	c2_bcount_t                   recsize;

	C2_ENTRY("lid %llu", (unsigned long long)lid);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	c2_uint128_init(&seed, "recsizepdclustla");

	/* Build a layout object. */
	if (enum_id == LIST_ENUM_ID)
		allocate_area(&area, ADDITIONAL_BYTES_DEFAULT, &num_bytes);
	else
		allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	if (enum_id == LIST_ENUM_ID) {
		nr = only_inline_test ? 10 : 120;
		rc = pdclust_layout_build(LIST_ENUM_ID, lid,
					  4, 1, 4096, &seed,
					  nr, A_NONE, B_NONE,
					  &pl, &list_enum, NULL);
	} else {
		rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
					  4, 1, 4096, &seed,
					  pool.po_width, 10, 20,
					  &pl, NULL, &lin_enum);
	}
	C2_UT_ASSERT(rc == 0);

	recsize = recsize_get(&domain, &pl->pl_base.ls_base);

	/* Verify the recsize returned by recsize_get(). */
	pdclust_recsize_verify(enum_id, &pl->pl_base.ls_base, recsize);

	layout_destroy(&pl->pl_base.ls_base, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static void test_recsize()
{
	uint64_t lid;
	int      rc;

	/*
	 * recsize_get() for PDCLUST layout type and LIST enumeration type,
	 * with only inline entries.
	 */
	lid = 5001;
	rc = test_recsize_pdclust(LIST_ENUM_ID, lid, ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* recsize_get() for PDCLUST layout type and LIST enumeration type. */
	lid = 5002;
	rc = test_recsize_pdclust(LIST_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* recsize_get() for PDCLUST layout type and LINEAR enumeration type. */
	lid = 5003;
	rc = test_recsize_pdclust(LINEAR_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);
}

static int test_ref_get_put_pdclust(uint32_t enum_id, uint64_t lid,
				    bool only_inline_test)
{
	struct c2_pdclust_layout     *pl = NULL;
	void                         *area = NULL;
	c2_bcount_t                   num_bytes;
	struct c2_bufvec              bv;
	struct c2_bufvec_cursor       cur;
	struct c2_uint128             seed;
	uint32_t                      nr;
	struct c2_layout_list_enum   *list_enum = NULL;
	struct c2_layout_linear_enum *lin_enum = NULL;
	uint32_t                      i;

	C2_ENTRY("lid %llu", (unsigned long long)lid);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	c2_uint128_init(&seed, "refgetputpdclust");

	/* Build a layout object. */
	if (enum_id == LIST_ENUM_ID)
		allocate_area(&area, ADDITIONAL_BYTES_DEFAULT, &num_bytes);
	else
		allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	if (enum_id == LIST_ENUM_ID) {
		nr = only_inline_test ? 11 : 121;
		rc = pdclust_layout_build(LIST_ENUM_ID, lid,
					  4, 1, 4096, &seed,
					  nr, A_NONE, B_NONE,
					  &pl, &list_enum, NULL);
	} else {
		rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
					  4, 1, 4096, &seed,
					  pool.po_width, 10, 20,
					  &pl, NULL, &lin_enum);
	}
	C2_UT_ASSERT(rc == 0);

	/* Verify that the ref count is set to DEFAULT_REF_COUNT. */
	C2_UT_ASSERT(pl->pl_base.ls_base.l_ref == DEFAULT_REF_COUNT);

	/* Add a reference on the layout object. */
	c2_layout_get(&pl->pl_base.ls_base);
	C2_UT_ASSERT(pl->pl_base.ls_base.l_ref == DEFAULT_REF_COUNT + 1);

	/* Release a reference on the layout object. */
	c2_layout_put(&pl->pl_base.ls_base);
	C2_UT_ASSERT(pl->pl_base.ls_base.l_ref == DEFAULT_REF_COUNT);

	/* Add multiple references on the layout object. */
	for (i = 0; i < 12345; ++i)
		c2_layout_get(&pl->pl_base.ls_base);
	C2_UT_ASSERT(pl->pl_base.ls_base.l_ref == DEFAULT_REF_COUNT + 12345);

	/* Release multiple references on the layout object. */
	for (i = 0; i < 12345; ++i)
		c2_layout_put(&pl->pl_base.ls_base);
	C2_UT_ASSERT(pl->pl_base.ls_base.l_ref == DEFAULT_REF_COUNT);

	layout_destroy(&pl->pl_base.ls_base, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static void test_ref_get_put(void)
{
	uint64_t lid;
	int      rc;

	/*
	 * Reference get and put operations for PDCLUST layout type and LIST
	 * enumeration type.
	 */
	lid = 6001;
	rc = test_ref_get_put_pdclust(LIST_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Reference get and put operations for PDCLUST layout type and LINEAR
	 * enumeration type.
	 */
	lid = 6002;
	rc = test_ref_get_put_pdclust(LINEAR_ENUM_ID, lid, !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);
}

static void pair_reset(struct c2_db_pair *pair, uint64_t *lid,
		       void *area, c2_bcount_t num_bytes)
{
	pair->dp_key.db_buf.b_addr = lid;
	pair->dp_key.db_buf.b_nob  = sizeof *lid;
	pair->dp_rec.db_buf.b_addr = area;
	pair->dp_rec.db_buf.b_nob  = num_bytes;
}

static int test_add_pdclust(uint32_t enum_id, uint64_t lid,
			    bool only_inline_test,
			    bool lookup_test,
			    bool duplicate_test)
{
	c2_bcount_t                   num_bytes;
	uint32_t                      nr;
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

	/* Build a layout object. */
	if (enum_id == LIST_ENUM_ID) {
		allocate_area(&area, ADDITIONAL_BYTES_DEFAULT, &num_bytes);

		nr = only_inline_test ? 7 : 90;

		rc = pdclust_layout_build(LIST_ENUM_ID,
					  lid, 5, 2, 4096, &seed,
					  nr, A_NONE, B_NONE,
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

	/* Add the layout object to the DB. */
	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_layout_add(&schema, &pl->pl_base.ls_base, &pair, &tx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	/*
	 * If lookup_test is true, lookup for the layout object from the DB,
	 * to verify that the layout object is indeed added to the DB.
	 */
	if (lookup_test) {
		rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
		C2_UT_ASSERT(rc == 0);

		pair_reset(&pair, &lid, area, num_bytes);

		rc = c2_layout_lookup(&schema, lid, &pair, &tx, &l);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(l->l_id == lid);

		rc = c2_db_tx_commit(&tx);
		C2_UT_ASSERT(rc == 0);

		/*
		 * Comapre the two layout objects - one created earlier here
		 * and the one that is the result of c2_layout_lookup().
		 * This verifies the persistence of the data added to DB,
		 * using c2_layout_add().
		 */
		pdclust_layout_compare(enum_id, &pl->pl_base.ls_base, l);

		layout_destroy(l, lid);
	}

	/*
	 * If duplicate_test is true, again try to add the same layout object
	 * to the DB, to verify that it results into EEXIST error.
	 */
	if (duplicate_test) {
		rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
		C2_UT_ASSERT(rc == 0);

		pair_reset(&pair, &lid, area, num_bytes);

		rc = c2_layout_add(&schema, &pl->pl_base.ls_base, &pair, &tx);
		C2_UT_ASSERT(rc == -EEXIST);

		rc = c2_db_tx_commit(&tx);
		C2_UT_ASSERT(rc == 0);
	}

	layout_destroy(&pl->pl_base.ls_base, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static int test_lookup_pdclust(uint32_t enum_id, uint64_t lid,
			       bool existing_test,
			       bool only_inline_test)
{
	c2_bcount_t        num_bytes;
	void              *area = NULL;
	struct c2_layout  *l = NULL;
	struct c2_db_pair  pair;
	struct c2_db_tx    tx;

	C2_ENTRY();
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	/*
	 * If existing_test is true, then first add a layout object to the
	 * DB.
	 */
	if (existing_test) {
		rc = test_add_pdclust(LINEAR_ENUM_ID, lid,
				      only_inline_test,
				      !LOOKUP_TEST,
				      !DUPLICATE_TEST);
		C2_UT_ASSERT(rc == 0);
	}

	/* Lookup for the layout object from the DB. */
	allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_layout_lookup(&schema, lid, &pair, &tx, &l);

	if (existing_test) {
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(l->l_id == lid);
	} else
		C2_UT_ASSERT(rc == -ENOENT);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	if (existing_test)
		layout_destroy(l, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static void test_lookup(void)
{
	uint64_t lid;

	/*
	 * Lookup for a layout object with LIST enum type, that does not
	 * exist in the DB.
	 */
	lid = 7001;
	rc = test_lookup_pdclust(LIST_ENUM_ID, lid,
				 !EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Add a layout object with PDCLUST layout type, LIST enum type and
	 * with only inline entries.
	 * Then perform lookup for it.
	 */
	lid = 7002;
	rc = test_lookup_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Add a layout object with PDCLUST layout type, LIST enum type.
	 * Then perform lookup for it.
	 */
	lid = 7003;
	rc = test_lookup_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);


	/*
	 * Once again, lookup for a layout object that does not exist in the
	 * DB.
	 */
	lid = 7005;
	rc = test_lookup_pdclust(LIST_ENUM_ID, lid,
				 !EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Lookup for a layout object with LINEAR enum type, that does not
	 * exist in the DB.
	 */
	lid = 7006;
	rc = test_lookup_pdclust(LINEAR_ENUM_ID, lid,
				 !EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Add a layout object with PDCLUST layout type and LINEAR enum type.
	 * Then perform lookup for it.
	 */
	lid = 7007;
	rc = test_lookup_pdclust(LINEAR_ENUM_ID, lid,
				 EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);
}

static void test_add(void)
{
	uint64_t lid;

	/*
	 * Add a layout object with PDCLUST layout type, LIST enum type and
	 * with only inline entries.
	 */
	lid = 8001;
	rc = test_add_pdclust(LIST_ENUM_ID, lid,
			      ONLY_INLINE_TEST,
			      LOOKUP_TEST,
			      DUPLICATE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* Add a layout object with PDCLUST layout type and LIST enum type. */
	lid = 8002;
	rc = test_add_pdclust(LIST_ENUM_ID, lid,
			      !ONLY_INLINE_TEST,
			      LOOKUP_TEST,
			      DUPLICATE_TEST);
	C2_UT_ASSERT(rc == 0);

	/* Add a layout object with PDCLUST layout type and LINEAR enum type. */
	lid = 8003;
	rc = test_add_pdclust(LINEAR_ENUM_ID, lid,
			      !ONLY_INLINE_TEST,
			      LOOKUP_TEST,
			      DUPLICATE_TEST);
	C2_UT_ASSERT(rc == 0);
}

static int test_update_pdclust(uint32_t enum_id, uint64_t lid,
			       bool existing_test,
			       bool only_inline_test)
{
	c2_bcount_t                   num_bytes;
	void                         *area = NULL;
	struct c2_db_pair             pair;
	struct c2_db_tx               tx;
	struct c2_layout             *l1 = NULL;
	struct c2_layout             *l2 = NULL;
	uint32_t                      i;
	struct c2_uint128             seed;
	uint32_t                      nr;
	struct c2_pdclust_layout     *pl = NULL;
	struct c2_layout_list_enum   *list_enum = NULL;
	struct c2_layout_linear_enum *lin_enum = NULL;

	C2_ENTRY();
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	if (enum_id == LIST_ENUM_ID)
		allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);
	else
		allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	if (existing_test) {
		/* Add a layout object to the DB. */
		rc = test_add_pdclust(enum_id, lid,
				      only_inline_test,
				      !LOOKUP_TEST,
				      !DUPLICATE_TEST);
		C2_UT_ASSERT(rc == 0);

		/*
		 * Lookup for the layout object so as to be able to update
		 * it.
		 */
		rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
		C2_UT_ASSERT(rc == 0);

		pair_reset(&pair, &lid, area, num_bytes);

		rc = c2_layout_lookup(&schema, lid, &pair, &tx, &l1);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(l1->l_ref == DEFAULT_REF_COUNT);

		rc = c2_db_tx_commit(&tx);
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
			rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
						  4, 1, 4096, &seed,
						  pool.po_width, 10, 20,
						  &pl, NULL, &lin_enum);
		}
		C2_UT_ASSERT(rc == 0);
		l1 = &pl->pl_base.ls_base;
	}

	/* Update the layout object - update its reference count. */
	for (i = 0; i < 7654; ++i)
		c2_layout_get(l1);
	C2_UT_ASSERT(l1->l_ref == DEFAULT_REF_COUNT + 7654);

	/* Update the layout object in the DB. */
	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_layout_update(&schema, l1, &pair, &tx);
	if (existing_test)
		C2_UT_ASSERT(rc == 0)
	else
		C2_UT_ASSERT(rc == -ENOENT);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	if (existing_test) {
		/*
		 * Lookup for the layout object from the DB to verify that its
		 * reference count is indeed updated.
		 */
		rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
		C2_UT_ASSERT(rc == 0);

		pair_reset(&pair, &lid, area, num_bytes);

		rc = c2_layout_lookup(&schema, lid, &pair, &tx, &l2);
		C2_UT_ASSERT(rc == 0);
		C2_UT_ASSERT(l2->l_ref == DEFAULT_REF_COUNT + 7654);

		rc = c2_db_tx_commit(&tx);
		C2_UT_ASSERT(rc == 0);

		/*
		 * Compare the two layouts - one created earlier here and the
		 * one that is looked up from the DB.
		 */
		pdclust_layout_compare(enum_id, l1, l2);

		layout_destroy(l2, lid);
	}

	layout_destroy(l1, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static void test_update(void)
{
	uint64_t lid;

	/*
	 * Update a layout object with PDCLUST layout type, LIST enum type and
	 * with only inline entries.
	 */
	lid = 9001;
	rc = test_update_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Update a layout object with PDCLUST layout type and LIST enum
	 * type.
	 */
	lid = 9002;
	rc = test_update_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Update a layout object with PDCLUST layout type and LINEAR enum
	 * type.
	 */
	lid = 9003;
	rc = test_update_pdclust(LINEAR_ENUM_ID, lid,
				 EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Update a layout object with PDCLUST layout type and LINEAR enum
	 * type.
	 */
	lid = 9004;
	rc = test_update_pdclust(LINEAR_ENUM_ID, lid,
				 !EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

}

static int test_delete_pdclust(uint32_t enum_id, uint64_t lid,
			       bool existing_test,
			       bool only_inline_test)
{
	c2_bcount_t                   num_bytes;
	void                         *area = NULL;
	struct c2_db_pair             pair;
	struct c2_db_tx               tx;
	struct c2_layout             *l = NULL;
	struct c2_uint128             seed;
	uint32_t                      nr;
	struct c2_pdclust_layout     *pl = NULL;
	struct c2_layout_list_enum   *list_enum = NULL;
	struct c2_layout_linear_enum *lin_enum = NULL;

	C2_ENTRY();
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	if (enum_id == LIST_ENUM_ID)
			allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);
		else
			allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	if (existing_test) {
		/* Add a layout object to the DB. */
		rc = test_add_pdclust(enum_id, lid,
				      only_inline_test,
				      !LOOKUP_TEST,
				      !DUPLICATE_TEST);
		C2_UT_ASSERT(rc == 0);

		/*
		 * Lookup for the layout object from the DB, so as to be able
		 * to delete it.
		 */
		rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
		C2_UT_ASSERT(rc == 0);

		pair_reset(&pair, &lid, area, num_bytes);

		rc = c2_layout_lookup(&schema, lid, &pair, &tx, &l);
		C2_UT_ASSERT(rc == 0);

		rc = c2_db_tx_commit(&tx);
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
			rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
						  4, 1, 4096, &seed,
						  pool.po_width, 10, 20,
						  &pl, NULL, &lin_enum);
		}
		C2_UT_ASSERT(rc == 0);
		l = &pl->pl_base.ls_base;
	}

	/* Delete the layout from the DB. */
	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	rc = c2_layout_delete(&schema, l, &pair, &tx);
	if (existing_test)
		C2_UT_ASSERT(rc == 0)
	else
		C2_UT_ASSERT(rc == -ENOENT);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(l, lid);
	l = NULL;

	/*
	 * Lookup for the layout object from the DB, to verify that it does not
	 * exist there and that the lookup results into ENOENT error.
	 */
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

	/*
	 * Delete a layout object with PDCLUST layout type, LIST enum type and
	 * with only inline entries.
	 */
	lid = 10001;
	rc = test_delete_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Delete a layout object with PDCLUST layout type and LIST enum
	 * type.
	 */
	lid = 10002;
	rc = test_delete_pdclust(LIST_ENUM_ID, lid,
				 EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Delete a layout object with PDCLUST layout type and LINEAR enum
	 * type.
	 */
	lid = 10003;
	rc = test_delete_pdclust(LINEAR_ENUM_ID, lid,
				 EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Delete a layout object with PDCLUST layout type and LINEAR enum
	 * type.
	 */
	lid = 10004;
	rc = test_delete_pdclust(LINEAR_ENUM_ID, lid,
				 !EXISTING_TEST,
				 !ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

}

static void pdclust_enum_op_verify(uint32_t enum_id, uint64_t lid,
				   uint32_t nr, struct c2_layout *l)
{
	struct c2_pdclust_layout     *pl;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	int                           i;
	struct c2_fid                 fid_calculated;
	struct c2_fid                 fid_from_layout;
	struct c2_fid                 gfid;

	C2_UT_ASSERT(l != NULL);

	C2_UT_ASSERT(l->l_type == &c2_pdclust_layout_type);

	pl = container_of(l, struct c2_pdclust_layout, pl_base.ls_base);

	if (enum_id == LIST_ENUM_ID) {
		list_enum = container_of(pl->pl_base.ls_enum,
					 struct c2_layout_list_enum, lle_base);

		C2_UT_ASSERT(
			list_enum->lle_base.le_ops->leo_nr(&list_enum->lle_base,
							   lid) == nr);

		for(i = 0; i < list_enum->lle_nr; ++i) {
			c2_fid_set(&fid_calculated, i * 100 + 1, i + 1);
			list_enum->lle_base.le_ops->leo_get(
							&list_enum->lle_base,
							lid, i, NULL,
							&fid_from_layout);
			C2_UT_ASSERT(c2_fid_eq(&fid_calculated,
					       &fid_from_layout));
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
			c2_fid_set(&fid_calculated,
				   lin_enum->lle_attr.lla_A +
				   i * lin_enum->lle_attr.lla_B,
				   gfid.f_key);
			lin_enum->lle_base.le_ops->leo_get(&lin_enum->lle_base,
							   lid, i, &gfid,
							   &fid_from_layout);
			C2_UT_ASSERT(c2_fid_eq(&fid_calculated,
					       &fid_from_layout));
		}
	}
}

static int test_enum_ops_pdclust(uint32_t enum_id, uint64_t lid,
				 bool only_inline_test)
{
	void                         *area = NULL;
	c2_bcount_t                   num_bytes;
	struct c2_bufvec              bv;
	struct c2_bufvec_cursor       cur;
	struct c2_uint128             seed;
	uint32_t                      nr;
	struct c2_pdclust_layout     *pl = NULL;
	struct c2_layout_list_enum   *list_enum = NULL;
	struct c2_layout_linear_enum *lin_enum = NULL;

	C2_ENTRY();

	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);

	/* Build a layout object. */
	c2_uint128_init(&seed, "enumopspdclustla");

	if (enum_id == LIST_ENUM_ID)
		allocate_area(&area, ADDITIONAL_BYTES_DEFAULT, &num_bytes);
	else
		allocate_area(&area, ADDITIONAL_BYTES_NONE, &num_bytes);

	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	if (enum_id == LIST_ENUM_ID) {
		nr = only_inline_test == ONLY_INLINE_TEST ? 14 : 114;
		rc = pdclust_layout_build(LIST_ENUM_ID, lid,
					  100, 10, 4096, &seed,
					  nr, A_NONE, B_NONE,
					  &pl, &list_enum, NULL);
	} else {
		nr = pool.po_width;
		rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
					  80, 8, 4096, &seed,
					  nr, 777, 888,
					  &pl, NULL, &lin_enum);
	}
	C2_UT_ASSERT(rc == 0);

	/* Verify enum operations. */
	pdclust_enum_op_verify(enum_id, lid, nr, &pl->pl_base.ls_base);

	layout_destroy(&pl->pl_base.ls_base, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static void test_enum_operations(void)
{
	uint64_t lid;

	/*
	 * Decode a layout with PDCLUST layout type, LIST enum type and
	 * with only inline entries.
	 * And then verify its enum ops.
	 */
	lid = 11001;
	rc = test_enum_ops_pdclust(LIST_ENUM_ID, lid, ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Decode a layout with PDCLUST layout type and LIST enum type.
	 * And then verify its enum ops.
	 */
	lid = 11002;
	rc = test_enum_ops_pdclust(LIST_ENUM_ID, lid, ONLY_INLINE_TEST);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Decode a layout with PDCLUST layout type and LINEAR enum type.
	 * And then verify its enum ops.
	 */
	lid = 11003;
	rc = test_enum_ops_pdclust(LINEAR_ENUM_ID, lid, !ONLY_INLINE_TEST);
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
		{ "layout-register-unregister", test_reg_unreg },
		{ "layout-decode", test_decode },
		{ "layout-encode", test_encode },
		{ "layout-decode-encode", test_decode_encode },
		{ "layout-encode-decode", test_encode_decode },
		{ "layout-max-recsize", test_max_recsize },
		{ "layout-recsize", test_recsize },
		{ "layout-ref-get-put", test_ref_get_put },
		{ "layout-enum-ops", test_enum_operations },
                { "layout-lookup", test_lookup },
                { "layout-add", test_add },
                { "layout-update", test_update },
                { "layout-delete", test_delete },
                { "layout-buf-copyto", test_bufvec_copyto },
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
