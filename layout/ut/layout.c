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
#include "lib/misc.h"               /* C2_SET0 */
#include "lib/bitstring.h"
#include "lib/vec.h"

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_LAYOUT
#include "lib/trace.h"              /* C2_LOG */

#include "pool/pool.h"              /* c2_pool_init() */
#include "layout/layout_internal.h" /* DEFAULT_REF_COUNT */
#include "layout/layout.h"
#include "layout/pdclust.h"
#include "layout/layout_db.h"
#include "layout/list_enum.h"
#include "layout/list_enum.c"       /* struct ldb_cob_entries_header */
#include "layout/linear_enum.h"

static const char              db_name[] = "ut-layout";
static struct c2_layout_domain domain;
static struct c2_ldb_schema    schema;
static struct c2_dbenv         dbenv;
static struct c2_pool          pool;
static int                     rc;
enum c2_addb_ev_level          orig_addb_level;

enum {
	DBFLAGS                  = 0,
	DEF_POOL_ID              = 1,
	POOL_WIDTH               = 20,
	LIST_ENUM_ID             = 0x4C495354, /* "LIST" */
	LINEAR_ENUM_ID           = 0x4C494E45, /* "LINE" */
	A_NONE                   = 0,
	B_NONE                   = 0,
	PARTIAL_BUF              = true,
	ONLY_INLINE              = true,
	SPECIFIED_BYTES_NONE     = 0,
	ADDITIONAL_BYTES_NONE    = 0,
	ADDITIONAL_BYTES_DEFAULT = 1024
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

	rc = c2_dbenv_init(&dbenv, db_name, DBFLAGS);
	C2_ASSERT(rc == 0);

	rc = c2_layout_domain_init(&domain);
	C2_ASSERT(rc == 0);

	rc = c2_ldb_schema_init(&schema, &domain, &dbenv);
	C2_ASSERT(rc == 0);
	C2_ASSERT(schema.ls_domain == &domain);

	orig_addb_level = c2_addb_choose_default_level_console(AEL_NONE);

	return rc;
}

static int test_fini(void)
{
	c2_addb_choose_default_level_console(orig_addb_level);

	c2_ldb_schema_fini(&schema);

	c2_layout_domain_fini(&domain);

	c2_dbenv_fini(&dbenv);

	return 0;
}

static int t_register(struct c2_ldb_schema *schema,
		      const struct c2_layout_type *lt)
{
	return 0;
}

static void t_unregister(struct c2_ldb_schema *schema,
			 const struct c2_layout_type *lt)
{
}

static const struct c2_layout_type_ops test_layout_type_ops = {
	.lto_register    = t_register,
	.lto_unregister  = t_unregister,
	.lto_max_recsize = NULL,
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
	rc = c2_ldb_type_register(&schema, &test_layout_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(domain.ld_type[test_layout_type.lt_id] ==
		     &test_layout_type);

	/* Should be able to unregister it. */
	c2_ldb_type_unregister(&schema, &test_layout_type);
	C2_UT_ASSERT(domain.ld_type[test_layout_type.lt_id] == NULL);

	/* Should be able to register it again. */
	c2_ldb_type_register(&schema, &test_layout_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(domain.ld_type[test_layout_type.lt_id] ==
		     &test_layout_type);

	/*
	 * Should not be able to register it again without first unregistering
	 * it.
	 */
	rc = c2_ldb_type_register(&schema, &test_layout_type);
	C2_UT_ASSERT(rc == -EEXIST);

	/* Unregister it. */
	c2_ldb_type_unregister(&schema, &test_layout_type);
	C2_UT_ASSERT(domain.ld_type[test_layout_type.lt_id] == NULL);

	C2_LEAVE();
}

static int t_enum_register(struct c2_ldb_schema *schema,
			   const struct c2_layout_enum_type *et)
{
	return 0;
}

static void t_enum_unregister(struct c2_ldb_schema *schema,
			      const struct c2_layout_enum_type *et)
{
}

static const struct c2_layout_enum_type_ops test_enum_ops = {
	.leto_register    = t_enum_register,
	.leto_unregister  = t_enum_unregister,
	.leto_max_recsize = NULL,
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
	rc = c2_ldb_enum_register(&schema, &test_enum_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(domain.ld_enum[test_enum_type.let_id] ==
		     &test_enum_type);

	/* Should be able to unregister it. */
	c2_ldb_enum_unregister(&schema, &test_enum_type);
	C2_UT_ASSERT(domain.ld_enum[test_enum_type.let_id] == NULL);

	/* Should be able to register it again. */
	c2_ldb_enum_register(&schema, &test_enum_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(domain.ld_enum[test_enum_type.let_id] == &test_enum_type);

	/*
	 * Should not be able to register it again without first unregistering
	 * it.
	 */
	rc = c2_ldb_enum_register(&schema, &test_enum_type);
	C2_UT_ASSERT(rc == -EEXIST);

	/* Unregister it. */
	c2_ldb_enum_unregister(&schema, &test_enum_type);
	C2_UT_ASSERT(domain.ld_enum[test_enum_type.let_id] == NULL);

	C2_LEAVE();
}

static void test_schema_init_fini(void)
{
	const char              t_db_name[] = "t-layout";
	struct c2_layout_domain t_domain;
	struct c2_ldb_schema    t_schema;
	struct c2_dbenv         t_dbenv;

	C2_ENTRY();

	rc = c2_dbenv_init(&t_dbenv, t_db_name, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	/* Initialize the schema. */
	rc = c2_ldb_schema_init(&t_schema, &t_domain, &t_dbenv);
	C2_UT_ASSERT(rc == 0);

	/* Should be able finalize it. */
	c2_ldb_schema_fini(&t_schema);

	/* Should be able to initialize it again. */
	rc = c2_ldb_schema_init(&t_schema, &t_domain, &t_dbenv);
	C2_UT_ASSERT(rc == 0);

	/* Register a layout type. *
	c2_ldb_type_register(&t_schema, &test_layout_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(t_schema.ls_type[test_layout_type.lt_id] ==
		     &test_layout_type);

	* Register a layout enum type. *
	c2_ldb_enum_register(&t_schema, &test_enum_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(t_schema.ls_enum[test_enum_type.let_id] ==
		     &test_enum_type);

	 * todo
	 * Should not be able to finalize at this point.
	 * c2_ldb_schema_fini() asserts internally, if any of the layout type
	 * or enum type is still registered.
	 *

	* Unregister the layout type. *
	c2_ldb_type_unregister(&t_schema, &test_layout_type);
	C2_UT_ASSERT(t_schema.ls_type[test_layout_type.lt_id] == NULL);

	* Unregister the enum type. *
	c2_ldb_enum_unregister(&t_schema, &test_enum_type);
	C2_UT_ASSERT(t_schema.ls_enum[test_enum_type.let_id] == NULL);
	*/

	/* Should now be able to finalize it. */
	c2_ldb_schema_fini(&t_schema);

	c2_dbenv_fini(&t_dbenv);

	C2_LEAVE();
}

static void test_schema_max_recsize()
{
}

static int internal_init()
{
	int rc;

	/*
	 * @todo Keep track if any layout type or enum type is already
	 * registered or is registered by this test suite. If the later,
	 * then internal_fini() should unregister it, not otherwise. This is
	 * to avoid having any side-effect of running this test suite.
	 */
	c2_ldb_type_register(&schema, &c2_pdclust_layout_type);

	c2_ldb_enum_register(&schema, &c2_list_enum_type);
	c2_ldb_enum_register(&schema, &c2_linear_enum_type);

	rc = c2_pool_init(&pool, DEF_POOL_ID, POOL_WIDTH);
	C2_UT_ASSERT(rc == 0);

	return rc;
}

static void internal_fini()
{
	c2_pool_fini(&pool);

	c2_ldb_enum_unregister(&schema, &c2_list_enum_type);
	c2_ldb_enum_unregister(&schema, &c2_linear_enum_type);

	c2_ldb_type_unregister(&schema, &c2_pdclust_layout_type);
}

static void buf_build(uint32_t lt_id, struct c2_bufvec_cursor *dcur)
{
	struct c2_ldb_rec rec;
	c2_bcount_t       nbytes_copied;

	rec.lr_lt_id     = lt_id;
	rec.lr_ref_count = DEFAULT_REF_COUNT;
	rec.lr_pool_id   = DEF_POOL_ID;

	nbytes_copied = c2_bufvec_cursor_copyto(dcur, &rec, sizeof rec);
	C2_UT_ASSERT(nbytes_copied == sizeof rec);
}

static void pdclust_buf_build(uint64_t lid,
			      uint32_t N, uint32_t K,
			      uint64_t unitsize, struct c2_uint128 *seed,
			      uint32_t let_id, struct c2_bufvec_cursor *dcur)
{
	struct c2_ldb_pdclust_rec pl_rec;
	c2_bcount_t               nbytes_copied;

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
				    struct c2_bufvec_cursor *dcur,
				    bool build_partial_buf)
{
	struct ldb_cob_entries_header  ldb_ce_header;
	struct c2_fid                  cob_id;
	uint32_t                       i;
	c2_bcount_t                    nbytes_copied;
	uint32_t                       let_id;
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

	/* For negative test - having partial buffer. */
	if (build_partial_buf == PARTIAL_BUF)
		return 0;

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

		lin_rec.lla_nr    = nr;
		lin_rec.lla_A     = A;
		lin_rec.lla_B     = B;

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

/* todo this fn shud accept P */
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
	struct c2_layout_striped     *stl;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	int                           i;
	struct c2_fid                 cob_id;

	C2_UT_ASSERT(l != NULL);
	C2_UT_ASSERT(enum_id == LIST_ENUM_ID || enum_id == LINEAR_ENUM_ID);
	C2_UT_ASSERT(l->l_type == &c2_pdclust_layout_type);

	stl = container_of(l, struct c2_layout_striped, ls_base);
	pl = container_of(stl, struct c2_pdclust_layout, pl_base);

	C2_UT_ASSERT(stl->ls_enum != NULL);

	pdclust_l_verify(lid, N, K, unitsize, seed, pl);

	if (enum_id == LIST_ENUM_ID) {
		C2_UT_ASSERT(A == A_NONE && B == B_NONE);
		list_enum = container_of(stl->ls_enum,
					 struct c2_layout_list_enum, lle_base);
		for(i = 0; i < list_enum->lle_nr; ++i) {
			c2_fid_set(&cob_id, i * 100 + 1, i + 1);
			C2_UT_ASSERT(c2_fid_eq(&cob_id,
					      &list_enum->lle_list_of_cobs[i]));
		}
	} else {
		C2_UT_ASSERT(B != B_NONE);
		lin_enum = container_of(stl->ls_enum,
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

	/* enum object is destroyed internally by lo_fini(). */
	l->l_ops->lo_fini(l, &domain);
}

static void allocate_area(void **area,
			  c2_bcount_t additional_bytes,
			  c2_bcount_t specified_bytes,
			  c2_bcount_t *num_bytes)
{
	C2_UT_ASSERT(area != NULL && *area == NULL);
	C2_UT_ASSERT(ergo(additional_bytes > 0, specified_bytes == 0));
	C2_UT_ASSERT(ergo(specified_bytes > 0, additional_bytes == 0));

	if (specified_bytes > 0)
		*num_bytes = specified_bytes;
	else
		*num_bytes = c2_ldb_max_recsize(&domain) + additional_bytes;

	*area = c2_alloc(*num_bytes);
	C2_UT_ASSERT(*area != NULL);
}

static int test_decode_pdclust_list(uint64_t lid, bool only_inline_cob_entries)
{
	void                     *area = NULL;
	c2_bcount_t               num_bytes;
	struct c2_bufvec          bv;
	struct c2_bufvec_cursor   cur;
	struct c2_layout         *l = NULL;
	uint32_t                  nr;
	struct c2_uint128         seed;

	C2_ENTRY();

	c2_uint128_init(&seed, "decodepdclustlis");
	allocate_area(&area, ADDITIONAL_BYTES_DEFAULT, SPECIFIED_BYTES_NONE,
		      &num_bytes);
	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	nr = only_inline_cob_entries == ONLY_INLINE ? 5 : 25;

	rc = pdclust_layout_buf_build(LIST_ENUM_ID, lid,
				      4, 2, 4096, &seed,
				      nr, A_NONE, B_NONE,
				      &cur, !PARTIAL_BUF);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);
	rc = c2_layout_decode(&domain, lid, &cur, C2_LXO_BUFFER_OP,
			      NULL, NULL, &l);
	C2_UT_ASSERT(rc == 0);

	rc = pdclust_layout_verify(LIST_ENUM_ID, lid,
				   4, 2, 4096, &seed,
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
	allocate_area(&area, ADDITIONAL_BYTES_NONE, SPECIFIED_BYTES_NONE,
		      &num_bytes);
	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = pdclust_layout_buf_build(LINEAR_ENUM_ID, lid,
				      4, 2, 4096, &seed,
				      1000, 777, 888,
				      &cur, !PARTIAL_BUF);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);
	rc = c2_layout_decode(&domain, lid, &cur, C2_LXO_BUFFER_OP,
			      NULL, NULL, &l);
	C2_UT_ASSERT(rc == 0);

	rc = pdclust_layout_verify(LINEAR_ENUM_ID, lid,
				   4, 2, 4096, &seed,
				   1000, 777, 888, l);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(l, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

/*
static int test_decode_pdclust_linear_negative(uint64_t lid)
{
	void                      *area = NULL;
	c2_bcount_t                num_bytes;
	struct c2_bufvec           bv;
	struct c2_bufvec_cursor    cur;
	c2_bcount_t                specified_bytes;
	struct c2_layout          *l = NULL;
	struct c2_uint128          seed;

	C2_ENTRY();

	c2_uint128_init(&seed, "decodepdlinegati");
	* todo confirm the behavior w/o adding 1 below. *
	specified_bytes = sizeof (struct c2_ldb_rec) +
			 sizeof (struct c2_ldb_pdclust_rec) + 1;

	allocate_area(&area, ADDITIONAL_BYTES_NONE, specified_bytes,
		      &num_bytes);
	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = pdclust_layout_buf_build(LINEAR_ENUM_ID, lid,
				      4, 2, 4096, &seed,
				      2000, 777, 888,
				      &cur, true);
	C2_UT_ASSERT(rc == 0);

	* Rewind the cursor. *
	c2_bufvec_cursor_init(&cur, &bv);
	rc = c2_layout_decode(&schema, lid, &cur, C2_LXO_BUFFER_OP,
			      NULL, &l);
	C2_UT_ASSERT(rc == -ENOBUFS);
	* This results into an assert now. *

	c2_free(area);
	 * layout_destroy(l, lid) is not to be performed here since
	 * decode did not complete successfully.

	C2_LEAVE();
	return 0;
}
*/

static void test_decode(void)
{
	uint64_t    lid;

	rc = internal_init();
	C2_UT_ASSERT(rc == 0);

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

	/*
	 * Negative test - Insufficient buffer size.
	 * Decode a layout with PDCLUST layout type and LINEAR enum type.
	 * Can not test this since it results into an assert now.
	 */
	/*
	lid = 1004;
	rc = test_decode_pdclust_linear_negative(lid);
	C2_UT_ASSERT(rc == 0);
	*/

	internal_fini();
}

static int pdclust_l_build(uint64_t lid, uint32_t N, uint32_t K,
			   uint64_t unitsize, struct c2_uint128 *seed,
			   struct c2_layout_enum *le,
			   struct c2_pdclust_layout **pl)
{
	struct c2_pool *pool;

	rc = c2_pool_lookup(DEF_POOL_ID, &pool);
	C2_UT_ASSERT(rc == 0);

	rc = c2_pdclust_build(pool, lid, N, K, unitsize, seed,
			      le, &domain, pl);
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

		rc = c2_list_enum_build(lid, cob_list, nr, list_e);
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

		rc = c2_linear_enum_build(lid, pool.po_width, A, B, lin_e);
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
	struct c2_ldb_rec *rec;

	C2_UT_ASSERT(c2_bufvec_cursor_step(cur) >= sizeof *rec);

	rec = c2_bufvec_cursor_addr(cur);
	C2_UT_ASSERT(rec != NULL);

	*lt_id = rec->lr_lt_id;

	c2_bufvec_cursor_move(cur, sizeof *rec);
}

static void pdclust_lbuf_verify(uint32_t N, uint32_t K, uint64_t unitsize,
				struct c2_uint128 *seed,
				struct c2_bufvec_cursor *cur,
				uint32_t *let_id)
{
	struct c2_ldb_pdclust_rec  *pl_rec;

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

	C2_UT_ASSERT(c2_bufvec_cursor_step(cur) >= sizeof *ldb_ce_header);

	if (enum_id == LIST_ENUM_ID) {
		C2_UT_ASSERT(let_id == c2_list_enum_type.let_id);

		ldb_ce_header = c2_bufvec_cursor_addr(cur);
		C2_UT_ASSERT(ldb_ce_header != NULL);
		if (ldb_ce_header == NULL)
			return -EPROTO;
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
	allocate_area(&area, ADDITIONAL_BYTES_NONE, SPECIFIED_BYTES_NONE,
		      &num_bytes);
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
	allocate_area(&area, ADDITIONAL_BYTES_DEFAULT, SPECIFIED_BYTES_NONE,
		      &num_bytes);
	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	nr = only_inline_cob_entries == ONLY_INLINE ? 5 : 25;

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

	rc = internal_init();
	C2_UT_ASSERT(rc == 0);

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

	internal_fini();
}

static void pair_reset(struct c2_db_pair *pair, uint64_t *lid,
		       void *area, c2_bcount_t num_bytes)
{
	pair->dp_key.db_buf.b_addr = lid;
	pair->dp_key.db_buf.b_nob = sizeof *lid;
	pair->dp_rec.db_buf.b_addr = area;
	pair->dp_rec.db_buf.b_nob = num_bytes;
}

/* todo See if it can be merged into other lookup wrapper. */
static int test_lookup_nonexisting_lid(uint64_t lid)
{
	c2_bcount_t        num_bytes;
	void              *area = NULL;
	struct c2_layout  *l = NULL;
	struct c2_db_pair  pair;
	struct c2_db_tx    tx;

	C2_ENTRY();

	allocate_area(&area, ADDITIONAL_BYTES_DEFAULT, SPECIFIED_BYTES_NONE,
		      &num_bytes);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	rc = c2_ldb_lookup(&schema, lid, &pair, &tx, &l);
	C2_LOG("test_lookup_nonexisting_lid(): lid %lld\n",
	       (unsigned long long)lid);
	C2_UT_ASSERT(rc == -ENOENT);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	c2_free(area);

	C2_LEAVE();
	return rc;
}

/* todo see if it can be merged into other similar wrapper for list */
static int test_add_pdclust_linear(uint64_t lid)
{
	c2_bcount_t                   num_bytes;
	void                         *area = NULL;
	struct c2_pdclust_layout     *pl = NULL;
	struct c2_db_pair             pair;
	struct c2_db_tx               tx;
	struct c2_uint128             seed;
	struct c2_layout_linear_enum *le = NULL;

	C2_ENTRY();

	c2_uint128_init(&seed, "addpdclustlinear");
	allocate_area(&area, ADDITIONAL_BYTES_NONE, SPECIFIED_BYTES_NONE,
		      &num_bytes);

	rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
				  4, 1, 4096, &seed,
				  pool.po_width, 100, 200,
				  &pl, NULL, &le);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_ldb_add(&schema, &pl->pl_base.ls_base, &pair, &tx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(&pl->pl_base.ls_base, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static int test_lookup_pdclust_linear(uint64_t lid)
{
	c2_bcount_t        num_bytes;
	void              *area = NULL;
	struct c2_layout  *l = NULL;
	struct c2_db_pair  pair;
	struct c2_db_tx    tx;

	C2_ENTRY();

	/* First add a layout with pdclust layout type and linear enum type. */
	rc = test_add_pdclust_linear(lid);
	C2_UT_ASSERT(rc == 0);

	allocate_area(&area, ADDITIONAL_BYTES_NONE, SPECIFIED_BYTES_NONE,
		      &num_bytes);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_ldb_lookup(&schema, lid, &pair, &tx, &l);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(l->l_id == lid);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(l, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static void test_lookup(void)
{
	uint64_t lid;

	rc = internal_init();
	C2_UT_ASSERT(rc == 0);

	/* Lookup for a non-existing lid. */
	lid = 3001;
	rc = test_lookup_nonexisting_lid(lid);
	C2_UT_ASSERT(rc == 0);

	/*
	 * Add a layout with pdclust layout type and linear enum type, and
	 * then perform lookup for it.
	 */
	lid = 3002;
	rc = test_lookup_pdclust_linear(lid);
	C2_UT_ASSERT(rc == 0);

	/* Lookup for a non-existing lid. */
	lid = 3003;
	rc = test_lookup_nonexisting_lid(lid);
	C2_UT_ASSERT(rc == 0);

	internal_fini();
}


static int test_add_pdclust_list(uint64_t lid)
{
	c2_bcount_t                 num_bytes;
	void                       *area = NULL;
	struct c2_pdclust_layout   *pl = NULL;
	struct c2_db_pair           pair;
	struct c2_db_tx             tx;
	struct c2_uint128           seed;
	struct c2_layout_list_enum *le = NULL;

	C2_ENTRY();

	c2_uint128_init(&seed, "addpdclustlisten");
	allocate_area(&area, ADDITIONAL_BYTES_DEFAULT, SPECIFIED_BYTES_NONE,
		      &num_bytes);

	rc = pdclust_layout_build(LIST_ENUM_ID,
				  lid, 5, 2, 4096, &seed,
				  30, A_NONE, B_NONE,
				  &pl, &le, NULL);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_ldb_add(&schema, &pl->pl_base.ls_base, &pair, &tx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(&pl->pl_base.ls_base, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static void test_add(void)
{
	uint64_t lid;

	rc = internal_init();
	C2_UT_ASSERT(rc == 0);

	lid = 4001;
	rc = test_add_pdclust_list(lid);
	C2_UT_ASSERT(rc == 0);

	lid = 4002;
	rc = test_add_pdclust_linear(lid);
	C2_UT_ASSERT(rc == 0);

	internal_fini();
}


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
	allocate_area(&area, ADDITIONAL_BYTES_NONE, SPECIFIED_BYTES_NONE,
		      &num_bytes);

	rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
				  6, 1, 4096, &seed,
				  pool.po_width, 800, 900,
				  &pl, NULL, &le);
	C2_UT_ASSERT(rc == 0);


	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_ldb_add(&schema, &pl->pl_base.ls_base, &pair, &tx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(&pl->pl_base.ls_base, lid);

	/* Lookup the record just for verification. */
	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_ldb_lookup(&schema, lid, &pair, &tx, &l);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(l->l_ref == DEFAULT_REF_COUNT);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	l->l_ref = 1234567;

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_ldb_update(&schema, l, &pair, &tx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(l, lid);
	l = NULL;

	/* Lookup the record just for verification. */
	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_ldb_lookup(&schema, lid, &pair, &tx, &l);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(l->l_ref == 1234567);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(l, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static int test_update_pdclust_linear_negative(uint64_t lid)
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

	c2_uint128_init(&seed, "updatepdlinegati");
	allocate_area(&area, ADDITIONAL_BYTES_NONE, SPECIFIED_BYTES_NONE,
		      &num_bytes);

	rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
				  6, 1, 4096, &seed,
				  pool.po_width, 800, 900,
				  &pl, NULL, &le);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_ldb_add(&schema, &pl->pl_base.ls_base, &pair, &tx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(&pl->pl_base.ls_base, lid);
	l = NULL;

	/* Lookup the record just for verification. */
	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_ldb_lookup(&schema, lid, &pair, &tx, &l);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(l->l_ref == DEFAULT_REF_COUNT);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	pl->pl_base.ls_base.l_ref = 7654321;
	/*
	 * Changing values of pl_atrr::N, K, P will be caught by the
	 * c2_pdclust_layout_invariant() itself.
	 */
	pl->pl_attr.pa_seed.u_hi++;

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_ldb_update(&schema, l, &pair, &tx);
	C2_UT_ASSERT(rc == -EINVAL);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	/* Lookup the record just for verification. */
	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(l, lid);
	l = NULL;

	rc = c2_ldb_lookup(&schema, lid, &pair, &tx, &l);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(l->l_ref != 7654321);

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

	rc = internal_init();
	C2_UT_ASSERT(rc == 0);

	/* TBD todo Test update for linear enum type.
	lid = 5001;
	rc = test_update_pdclust_list(lid);
	C2_UT_ASSERT(rc == 0);
	*/

	lid = 5002;
	rc = test_update_pdclust_linear(lid);
	C2_UT_ASSERT(rc == 0);

	lid = 5003;
	rc = test_update_pdclust_linear_negative(lid);
	C2_UT_ASSERT(rc == 0);

	internal_fini();
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
	allocate_area(&area, ADDITIONAL_BYTES_NONE, SPECIFIED_BYTES_NONE,
		      &num_bytes);

	rc = pdclust_layout_build(LINEAR_ENUM_ID, lid,
				  6, 1, 4096, &seed,
				  pool.po_width, 800, 900,
				  &pl, NULL, &le);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_ldb_add(&schema, &pl->pl_base.ls_base, &pair, &tx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(&pl->pl_base.ls_base, lid);

	/* Lookup the record just for verification. */
	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_ldb_lookup(&schema, lid, &pair, &tx, &l);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(l->l_ref == DEFAULT_REF_COUNT);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_ldb_delete(&schema, l, &pair, &tx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(l, lid);
	l = NULL;

	/* Lookup the record just for verification. */
	rc = c2_db_tx_init(&tx, &dbenv, DBFLAGS);
	C2_UT_ASSERT(rc == 0);

	pair_reset(&pair, &lid, area, num_bytes);

	rc = c2_ldb_lookup(&schema, lid, &pair, &tx, &l);
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

	rc = internal_init();
	C2_UT_ASSERT(rc == 0);

	lid = 6001;
	rc = test_delete_pdclust_linear(lid);
	C2_UT_ASSERT(rc == 0);

	internal_fini();
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
	struct c2_layout_striped     *stl;
	struct c2_layout_list_enum   *list_enum;
	struct c2_layout_linear_enum *lin_enum;
	int                           i;
	struct c2_fid                 fid;
	struct c2_fid                 fid_from_layout;

	C2_UT_ASSERT(l != NULL);
	C2_UT_ASSERT(l->l_type == &c2_pdclust_layout_type);

	stl = container_of(l, struct c2_layout_striped, ls_base);
	pl = container_of(stl, struct c2_pdclust_layout, pl_base);

	pdclust_l_verify(lid, N, K, unitsize, seed, pl);

	if (enum_id == LIST_ENUM_ID) {
		list_enum = container_of(stl->ls_enum,
					 struct c2_layout_list_enum, lle_base);

		C2_UT_ASSERT(
			list_enum->lle_base.le_ops->leo_nr(&list_enum->lle_base,
							   lid) == nr);
		/* todo check if enum shud be accepted by this fn */

		for(i = 0; i < list_enum->lle_nr; ++i) {
			c2_fid_set(&fid, i * 100 + 1, i + 1);
			list_enum->lle_base.le_ops->leo_get(
							&list_enum->lle_base,
							lid, i, NULL,
							&fid_from_layout);
			C2_UT_ASSERT(c2_fid_eq(&fid_from_layout, &fid));
		}
	} else {
		lin_enum = container_of(stl->ls_enum,
					struct c2_layout_linear_enum, lle_base);
		C2_UT_ASSERT(
			lin_enum->lle_base.le_ops->leo_nr(&lin_enum->lle_base,
							  lid) == nr);
		/* Don't think there is efficient way to verify the cob ids
		 * generated by the formula. It will probably just duplicate the
		 * internal code. But, still should do it. todo
		 */
	}

	return 0;
}

/* check if two fns can be combined into one. */
static int test_enum_ops_pdclust_linear(uint64_t lid)
{
	void                    *area = NULL;
	c2_bcount_t              num_bytes;
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;
	struct c2_layout        *l = NULL;
	struct c2_uint128        seed;

	C2_ENTRY();

	c2_uint128_init(&seed, "updownupdownupdo");
	allocate_area(&area, ADDITIONAL_BYTES_NONE, SPECIFIED_BYTES_NONE,
		      &num_bytes);
	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = pdclust_layout_buf_build(LINEAR_ENUM_ID, lid,
				      4, 2, 4096, &seed,
				      3000, 777, 888,
				      &cur, !PARTIAL_BUF);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);
	rc = c2_layout_decode(&domain, lid, &cur, C2_LXO_BUFFER_OP,
			      NULL, NULL, &l);
	C2_UT_ASSERT(rc == 0);

	rc = pdclust_enum_op_verify(LINEAR_ENUM_ID, lid,
				    4, 2, 4096, &seed,
				    3000, 777, 888, l);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(l, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}

static int test_enum_ops_pdclust_list(uint64_t lid)
{
	void                    *area = NULL;
	c2_bcount_t              num_bytes;
	struct c2_bufvec         bv;
	struct c2_bufvec_cursor  cur;
	struct c2_layout        *l = NULL;
	struct c2_uint128        seed;

	C2_ENTRY();

	c2_uint128_init(&seed, "updownupdownupdo");
	allocate_area(&area, ADDITIONAL_BYTES_DEFAULT, SPECIFIED_BYTES_NONE,
		      &num_bytes);
	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = pdclust_layout_buf_build(LIST_ENUM_ID, lid,
				      4, 2, 4096, &seed,
				      25, A_NONE, B_NONE,
				      &cur, !PARTIAL_BUF);
	C2_UT_ASSERT(rc == 0);

	/* Rewind the cursor. */
	c2_bufvec_cursor_init(&cur, &bv);
	rc = c2_layout_decode(&domain, lid, &cur, C2_LXO_BUFFER_OP,
			      NULL, NULL, &l);
	C2_UT_ASSERT(rc == 0);

	rc = pdclust_enum_op_verify(LIST_ENUM_ID, lid,
				    4, 2, 4096, &seed,
				    25, A_NONE, B_NONE, l);
	C2_UT_ASSERT(rc == 0);

	layout_destroy(l, lid);
	c2_free(area);

	C2_LEAVE();
	return rc;
}


static void test_enum_operations(void)
{
	uint64_t lid;

	rc = internal_init();
	C2_UT_ASSERT(rc == 0);

	/* Decode a layout with PDCLUST layout type and LIST enum type.
	 * And then verify its enum ops.
	 */
	lid = 7001;
	rc = test_enum_ops_pdclust_list(lid);
	C2_UT_ASSERT(rc == 0);

	/* Decode a layout with PDCLUST layout type and LINEAR enum type.
	 * And then verify its enum ops.
	 */
	lid = 7002;
	rc = test_enum_ops_pdclust_linear(lid);
	C2_UT_ASSERT(rc == 0);

	internal_fini();
}

static void bufvec_copyto_use(struct c2_bufvec_cursor *dcur)
{
	c2_bcount_t                  nbytes_copied;
	struct c2_ldb_rec            rec;
	struct c2_ldb_pdclust_rec    pl_rec;
	struct c2_layout_linear_attr lin_rec;

	rec.lr_lt_id     = c2_pdclust_layout_type.lt_id;
	rec.lr_ref_count = 0;
	rec.lr_pool_id   = DEF_POOL_ID;

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
	struct c2_ldb_rec            *rec;
	struct c2_ldb_pdclust_rec    *pl_rec;
	struct c2_layout_linear_attr *lin_rec;

	rec = c2_bufvec_cursor_addr(cur);
	C2_UT_ASSERT(rec != NULL);

	C2_UT_ASSERT(rec->lr_lt_id == c2_pdclust_layout_type.lt_id);
	C2_UT_ASSERT(rec->lr_ref_count == 0);
	C2_UT_ASSERT(rec->lr_pool_id == DEF_POOL_ID);

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

	num_bytes = c2_ldb_max_recsize(&domain) + 1024;
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
		{ "layout-type-register-unregister", test_type_reg_unreg },
		{ "layout-etype-register-unregister", test_etype_reg_unreg },
		{ "layout-schema-init-fini", test_schema_init_fini },
		{ "layout-schema-max-recsize", test_schema_max_recsize },
		{ "layout-decode", test_decode },
		{ "layout-encode", test_encode },
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
