/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
#include "lib/misc.h"        /* C2_SET0 */
#include "lib/bitstring.h"
#include "lib/vec.h"
#include "lib/arith.h"       /* c2_rnd() */
#include "lib/trace.h"       /* C2_LOG */

#include "pool/pool.h"       /* c2_pool_init() */
#include "layout/layout.h"
#include "layout/pdclust.h"
#include "layout/layout_db.h"
#include "layout/list_enum.h"
#include "layout/list_enum.c"
#include "layout/linear_enum.h"

static const char            db_name[] = "ut-layout";
static struct c2_ldb_schema  schema;
static struct c2_dbenv       dbenv;
static uint64_t              dbflags = 0;
static struct c2_pool        pool;
static uint32_t              P = 20;
static int                   rc;
uint64_t                     lid_seed = 1;
uint64_t                     lid_max = 0xFFFF;

extern const struct c2_layout_type c2_pdclust_layout_type;
extern const struct c2_layout_type c2_composite_layout_type;

extern const struct c2_layout_enum_type c2_list_enum_type;
extern const struct c2_layout_enum_type c2_linear_enum_type;

/** Descriptor for the tlist of COB identifiers. */
C2_TL_DESCR_DECLARE(cob_list, static);
//C2_TL_DECLARE(cob_list, static, struct ldb_list_cob_entry);

/** Descriptor for the tlist of sub-layouts. */
C2_TL_DESCR_DECLARE(sub_lay_list, static);
//C2_TL_DECLARE(sub_lay_list, static, struct c2_layout);

static int test_init(void)
{
	c2_ut_db_reset(db_name);

	C2_LOG0("Inside test_init");

	/*
	 * Note: Need to use C2_ASSERT() instead of C2_UT_ASSERT() in
	 * test_init() and test_fini().
	 */

	rc = c2_dbenv_init(&dbenv, db_name, 0);
	C2_ASSERT(rc == 0);

	rc = c2_ldb_schema_init(&schema, &dbenv);
	C2_ASSERT(rc == 0);

	return rc;
}

static int test_fini(void)
{
	rc = c2_ldb_schema_fini(&schema);
	C2_ASSERT(rc == 0);

	c2_dbenv_fini(&dbenv);

	return 0;
}

const struct c2_layout_type test_layout_type = {
	.lt_name     = "test",
	.lt_id       = 2,
	.lt_ops      = NULL
};

static void test_type_reg_unreg(void)
{
	C2_LOG0("Inside test_type_reg_unreg");

	/* Register a layout type. */
	rc = c2_ldb_type_register(&schema, &test_layout_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(schema.ls_type[test_layout_type.lt_id] ==
		     &test_layout_type);

	/* Should be able to unregister it. */
	c2_ldb_type_unregister(&schema, &test_layout_type);
	C2_UT_ASSERT(schema.ls_type[test_layout_type.lt_id] == NULL);

	/* Should be able to register it again. */
	c2_ldb_type_register(&schema, &test_layout_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(schema.ls_type[test_layout_type.lt_id] ==
		     &test_layout_type);

	/*
	 * Should not be able to register it again without first unregistering
	 * it.
	 */
	rc = c2_ldb_type_register(&schema, &test_layout_type);
	C2_UT_ASSERT(rc != 0);

	/* Unregister it. */
	c2_ldb_type_unregister(&schema, &test_layout_type);
	C2_UT_ASSERT(schema.ls_type[test_layout_type.lt_id] == NULL);
}

const struct c2_layout_enum_type test_enum_type = {
	.let_name    = "test",
	.let_id      = 2,
	.let_ops     = NULL
};

static void test_etype_reg_unreg(void)
{
	/* Register a layout enum type. */
	rc = c2_ldb_enum_register(&schema, &test_enum_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(schema.ls_enum[test_enum_type.let_id] == &test_enum_type);

	/* Should be able to unregister it. */
	c2_ldb_enum_unregister(&schema, &test_enum_type);
	C2_UT_ASSERT(schema.ls_enum[test_enum_type.let_id] == NULL);

	/* Should be able to register it again. */
	c2_ldb_enum_register(&schema, &test_enum_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(schema.ls_enum[test_enum_type.let_id] == &test_enum_type);

	/*
	 * Should not be able to register it again without first unregistering
	 * it.
	 */
	rc = c2_ldb_enum_register(&schema, &test_enum_type);
	C2_UT_ASSERT(rc != 0);

	/* Unregister it. */
	c2_ldb_enum_unregister(&schema, &test_enum_type);
	C2_UT_ASSERT(schema.ls_enum[test_enum_type.let_id] == NULL);
}

static void test_schema_init_fini(void)
{
	const char            t_db_name[] = "t-layout";
	struct c2_ldb_schema  t_schema;
	struct c2_dbenv       t_dbenv;

	rc = c2_dbenv_init(&t_dbenv, t_db_name, 0);
	C2_UT_ASSERT(rc == 0);

	/* Initialize the schema. */
	rc = c2_ldb_schema_init(&t_schema, &t_dbenv);
	C2_UT_ASSERT(rc == 0);

	/* Should be able finalize it. */
	rc = c2_ldb_schema_fini(&t_schema);
	C2_UT_ASSERT(rc == 0);

	/* Should be able to initialize it again. */
	rc = c2_ldb_schema_init(&t_schema, &t_dbenv);
	C2_UT_ASSERT(rc == 0);

	/* Register a layout type. */
	c2_ldb_type_register(&t_schema, &test_layout_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(t_schema.ls_type[test_layout_type.lt_id] ==
		     &test_layout_type);

	/* Register a layout enum type. */
	c2_ldb_enum_register(&t_schema, &test_enum_type);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(t_schema.ls_enum[test_enum_type.let_id] ==
		     &test_enum_type);

	/*
	 * Should not be able to finalize it since a layout type and an enum
	 * type are still registered.
	 */
	rc = c2_ldb_schema_fini(&t_schema);
	C2_UT_ASSERT(rc != 0);

	/* Unregister the layout type. */
	c2_ldb_type_unregister(&t_schema, &test_layout_type);
	C2_UT_ASSERT(t_schema.ls_type[test_layout_type.lt_id] == NULL);

	/*
	 * Still, should not be able to finalize it since an enum type is still
	 * registered.
	 */
	rc = c2_ldb_schema_fini(&t_schema);
	C2_UT_ASSERT(rc != 0);

	/* Unregister the enum type. */
	c2_ldb_enum_unregister(&t_schema, &test_enum_type);
	C2_UT_ASSERT(t_schema.ls_enum[test_enum_type.let_id] == NULL);

	/* Should now be able to finalize it. */
	rc = c2_ldb_schema_fini(&t_schema);
	C2_UT_ASSERT(rc == 0);

	c2_dbenv_fini(&t_dbenv);
}

static void test_schema_max_recsize()
{
}

static int internal_init()
{
	/*
	 * @todo Do not care if any of the layout type or enum type is already
	 * registered. But what about unregistering those.
	 */
	c2_ldb_type_register(&schema, &c2_pdclust_layout_type);
	c2_ldb_type_register(&schema, &c2_composite_layout_type);

	c2_ldb_enum_register(&schema, &c2_list_enum_type);
	c2_ldb_enum_register(&schema, &c2_linear_enum_type);

	return c2_pool_init(&pool, P);
}

static void internal_fini()
{
	c2_pool_fini(&pool);

	c2_ldb_enum_unregister(&schema, &c2_list_enum_type);
	c2_ldb_enum_unregister(&schema, &c2_linear_enum_type);

	c2_ldb_type_unregister(&schema, &c2_pdclust_layout_type);
	c2_ldb_type_unregister(&schema, &c2_composite_layout_type);
}

static void layout_buf_build(struct c2_bufvec_cursor *dcur,
			     uint64_t lid)
{
	struct c2_ldb_rec    rec;

	rec.lr_lt_id         = lid;
	rec.lr_ref_count     = 0;

	c2_bufvec_cursor_copyto(dcur, &rec, sizeof rec);
}

static void layout_buf_pdclust_build(struct c2_bufvec_cursor *dcur,
				     uint64_t lid,
				     uint32_t N, uint32_t K)
{
	struct c2_ldb_pdclust_rec pl_rec;

	layout_buf_build(dcur, lid);

	pl_rec.pr_let_id      = c2_list_enum_type.let_id;
	pl_rec.pr_attr.pa_N   = N;
	pl_rec.pr_attr.pa_K   = K;
	pl_rec.pr_attr.pa_P   = pool.po_width;

	c2_bufvec_cursor_copyto(dcur, &pl_rec, sizeof pl_rec);
}

static int layout_buf_pdclust_list_build(struct c2_bufvec_cursor *dcur,
					 uint64_t lid,
					 uint32_t N, uint32_t K, uint32_t nr)

{
	struct ldb_inline_cob_entries  list_rec;
	struct ldb_list_cob_entry      cob_list[nr];
	int                            i;

	layout_buf_pdclust_build(dcur, lid, N, K);

	list_rec.llces_nr      = nr;

	for (i = 0; i < list_rec.llces_nr; ++i) {
		cob_list[i].llce_cob_index = lid + i;
		cob_list[i].llce_cob_id.f_container = (lid + i + 1) * 100;
		cob_list[i].llce_cob_id.f_key =
				cob_list[i].llce_cob_id.f_container + 5;
	}

	c2_bufvec_cursor_copyto(dcur, &list_rec, sizeof list_rec);
	c2_bufvec_cursor_copyto(dcur, cob_list, ARRAY_SIZE(cob_list));

	return 0;
}

static int layout_buf_pdclust_lin_build(struct c2_bufvec_cursor *dcur,
					uint64_t lid,
					uint32_t N, uint32_t K,
					uint32_t A, uint32_t B,
					uint32_t nr)

{
	struct c2_layout_linear_attr   lin_rec;

	layout_buf_pdclust_build(dcur, lid, N, K);

	lin_rec.lla_nr    = nr;
	lin_rec.lla_A     = A;
	lin_rec.lla_B     = B;

	c2_bufvec_cursor_copyto(dcur, &lin_rec, sizeof lin_rec);

	return 0;
}


static int layout_buf_verify(struct c2_bufvec_cursor *cur,
			     uint64_t lid,
			     struct c2_layout *l)
{
	C2_UT_ASSERT(l->l_id == lid);
	C2_UT_ASSERT(l->l_ref == 0);
	C2_UT_ASSERT(l->l_ops != NULL);

	return 0;
}

static int layout_buf_pdclust_verify(struct c2_bufvec_cursor *cur,
				     uint64_t lid,
				     uint32_t N, uint32_t K,
				     struct c2_layout *l)
{
	layout_buf_verify(cur, lid, l);

	C2_UT_ASSERT(l->l_type == &c2_pdclust_layout_type);

	return 0;
}

static int layout_buf_pdclust_list_verify(struct c2_bufvec_cursor *cur,
					  uint64_t lid,
					  uint32_t N, uint32_t K,
					  uint32_t nr,
					  struct c2_layout *l)
{
	struct c2_pdclust_layout     *pl;
	struct c2_layout_striped     *stl;

	layout_buf_pdclust_verify(cur, lid, N, K, l);

	stl = container_of(l, struct c2_layout_striped, ls_base);
	pl = container_of(stl, struct c2_pdclust_layout, pl_base);

	/* @todo Verify the list */

	return 0;
}

static int layout_buf_pdclust_lin_verify(struct c2_bufvec_cursor *cur,
					  uint64_t lid,
					  uint32_t N, uint32_t K,
					  uint32_t A, uint32_t B,
					  uint32_t nr,
					  struct c2_layout *l)
{
	struct c2_layout_striped     *stl;
	struct c2_layout_linear_enum *lin_enum;

	layout_buf_pdclust_verify(cur, lid, N, K, l);

	stl = container_of(l, struct c2_layout_striped, ls_base);

	lin_enum = container_of(stl->ls_enum, struct c2_layout_linear_enum,
				lle_base);

	C2_UT_ASSERT(lin_enum->lle_attr.lla_nr == nr);
	C2_UT_ASSERT(lin_enum->lle_attr.lla_A == A);
	C2_UT_ASSERT(lin_enum->lle_attr.lla_B == B);

	return 0;
}

static int decode_pdclust_list(uint64_t lid)
{
	void                      *area;
	struct c2_bufvec           bv;
	struct c2_bufvec_cursor    cur;
	c2_bcount_t                num_bytes;
	struct c2_layout           *l;
	struct c2_db_tx            *tx = NULL;

	num_bytes = c2_ldb_max_recsize(&schema) + 1024;
	area = c2_alloc(num_bytes);
	C2_UT_ASSERT(area != NULL);

	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = layout_buf_pdclust_list_build(&cur, lid, 4, 2, 5);
	C2_UT_ASSERT(rc == 0);

	rc = c2_layout_decode(&schema, lid, &cur, C2_LXO_DB_NONE, tx, &l);
	C2_UT_ASSERT(rc == 0);

	rc = layout_buf_pdclust_list_verify(&cur, lid, 4, 2, 5, l);
	C2_UT_ASSERT(rc == 0);

	return rc;
}

static int decode_pdclust_linear(uint64_t lid)
{
	void                      *area;
	struct c2_bufvec           bv;
	struct c2_bufvec_cursor    cur;
	c2_bcount_t                num_bytes;
	struct c2_layout           *l;
	struct c2_db_tx            *tx = NULL;

	num_bytes = c2_ldb_max_recsize(&schema) + 1024;
	area = c2_alloc(num_bytes);
	C2_UT_ASSERT(area != NULL);

	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = layout_buf_pdclust_lin_build(&cur, lid, 4, 2, 10, 20, 5);
	C2_UT_ASSERT(rc == 0);

	rc = c2_layout_decode(&schema, lid, &cur, C2_LXO_DB_NONE, tx, &l);
	C2_UT_ASSERT(rc == 0);

	rc = layout_buf_pdclust_lin_verify(&cur, lid, 4, 2, 10, 20, 5, l);
	C2_UT_ASSERT(rc == 0);

	return rc;
}

static void test_decode(void)
{
	uint64_t                   lid;

	rc = internal_init();
	C2_UT_ASSERT(rc == 0);

	/* Decode a layout with PDCLUST layout type and LIST enum type. */
	lid = 0x50444C4953543031; /*PDLIST01 */
	rc = decode_pdclust_list(lid);

	/* Decode a layout with PDCLUST layout type and LINEAR enum type. */
	lid = 0x50444C4953543032; /*PDLIST02 */

	rc = decode_pdclust_linear(lid);

	internal_fini();
}
static int layout_pdclust_build(struct c2_pdclust_layout **pl, uint64_t lid,
			 uint32_t N, uint32_t K,
			 struct c2_layout_enum *le)
{
	struct c2_uint128             seed;

	c2_uint128_init(&seed, "updownupdownupdo");

	rc = c2_pdclust_build(&pool, &lid, N, K, &seed, le, pl);
	C2_UT_ASSERT(rc == 0);

	return rc;
}

static int layout_pdclust_linear_build(struct c2_pdclust_layout **pl, uint64_t lid,
				uint32_t N, uint32_t K,
				uint32_t A, uint32_t B)
{

	struct c2_layout_linear_enum *le;

	rc = c2_linear_enum_build(pool.po_width, A, B, &le);
	C2_UT_ASSERT(rc == 0);

	C2_UT_ASSERT(le != NULL);
	C2_UT_ASSERT(le->lle_base.le_type == &c2_linear_enum_type);
	C2_UT_ASSERT(le->lle_base.le_type->let_id
		      == c2_linear_enum_type.let_id);

	rc = layout_pdclust_build(pl, lid, N, K, &le->lle_base);
	C2_UT_ASSERT(rc == 0);

	return rc;
}

static int layout_pdclust_list_build(struct c2_pdclust_layout **pl, uint64_t lid,
			      uint32_t N, uint32_t K,
			      uint32_t nr)
{

	struct c2_layout_list_enum *le;
	struct c2_fid               cob_fid;
	int                         i;

	rc = c2_list_enum_build(&le);
	C2_UT_ASSERT(rc == 0);

	C2_UT_ASSERT(le != NULL);
	C2_UT_ASSERT(le->lle_base.le_type == &c2_list_enum_type);
	C2_UT_ASSERT(le->lle_base.le_type->let_id == c2_list_enum_type.let_id);

	for (i = 0; i < nr; ++i) {
		cob_fid.f_container = i * 100 + 1;
		cob_fid.f_key = i + 1;
		rc = c2_list_enum_add(le, lid, i, &cob_fid);
		C2_UT_ASSERT(rc == 0);
	}
	
	rc = layout_pdclust_build(pl, lid, N, K, &le->lle_base);
	C2_UT_ASSERT(rc == 0);

	return rc;
}


static int pdclust_verify(struct c2_pdclust_layout *pl,
			  struct c2_layout *l)
{
	C2_UT_ASSERT(pl->pl_base.ls_base.l_id == l->l_id);
	C2_UT_ASSERT(&c2_pdclust_layout_type == pl->pl_base.ls_base.l_type);
	C2_UT_ASSERT(pl->pl_base.ls_base.l_type == l->l_type);
	C2_UT_ASSERT(pl->pl_base.ls_base.l_ref == l->l_ref);
	C2_UT_ASSERT(pl->pl_base.ls_base.l_ops == l->l_ops);

	/* @todo And further verification. */
	return 0;
}

static int encode_pdclust_linear(uint64_t lid)
{
	struct c2_pdclust_layout  *pl;
	size_t                     num_bytes;
	void                      *area;
	struct c2_bufvec           bv;
	struct c2_bufvec_cursor    cur;
	struct c2_layout          *l;
	struct c2_db_tx           *tx = NULL;

	rc = internal_init();
	C2_UT_ASSERT(rc == 0);

	num_bytes = c2_ldb_max_recsize(&schema) + 1024;
	area = c2_alloc(num_bytes);
	C2_UT_ASSERT(area != NULL);

	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = layout_pdclust_linear_build(&pl, lid, 4, 1, 10, 20);
	C2_UT_ASSERT(rc == 0);

	rc  = c2_layout_encode(&schema, &pl->pl_base.ls_base,
			C2_LXO_DB_NONE, NULL, &cur);
	C2_UT_ASSERT(rc == 0);

	/* Now verify .... */
	/* @todo Should verify by reading it from the buffer. */
	rc = c2_layout_decode(&schema, lid, &cur, C2_LXO_DB_NONE, tx, &l);
	C2_UT_ASSERT(rc == 0);

	rc = pdclust_verify(pl, l);
	C2_UT_ASSERT(rc == 0);

	internal_fini();

	return rc;
}

static int encode_pdclust_list(uint64_t lid)
{
	struct c2_pdclust_layout  *pl;
	size_t                     num_bytes;
	void                      *area;
	struct c2_bufvec           bv;
	struct c2_bufvec_cursor    cur;
	struct c2_layout          *l;
	struct c2_db_tx           *tx = NULL;

	rc = internal_init();
	C2_UT_ASSERT(rc == 0);

	num_bytes = c2_ldb_max_recsize(&schema) + 1024;
	area = c2_alloc(num_bytes);
	C2_UT_ASSERT(area != NULL);

	bv = (struct c2_bufvec) C2_BUFVEC_INIT_BUF(&area, &num_bytes);
	c2_bufvec_cursor_init(&cur, &bv);

	rc = layout_pdclust_list_build(&pl, lid, 4, 1, 5);
	C2_UT_ASSERT(rc == 0);

	rc  = c2_layout_encode(&schema, &pl->pl_base.ls_base,
			C2_LXO_DB_NONE, NULL, &cur);
	C2_UT_ASSERT(rc == 0);

	/* Now verify .... */
	/* @todo Should verify by reading it from the buffer. */
	rc = c2_layout_decode(&schema, lid, &cur, C2_LXO_DB_NONE, tx, &l);
	C2_UT_ASSERT(rc == 0);

	rc = pdclust_verify(pl, l);
	C2_UT_ASSERT(rc == 0);

	internal_fini();

	return rc;
}



static int encode_composite(uint64_t lid)
{
	return 0;
}

static void test_encode(void)
{
	uint64_t                   lid;

	/*
	while (lid == 0) {
		lid = c2_rnd(lid_max, &lid_seed);
	}
	*/

	/* Encode for 'pdclust' layout type and 'list' enumeration type. */
	lid = 2222;
	rc = encode_pdclust_list(lid);
	C2_UT_ASSERT(rc == 0);

	/* Encode for 'pdclust' layout type and 'linear' enumeration type. */
	lid = 1111;
	rc = encode_pdclust_linear(lid);
	C2_UT_ASSERT(rc == 0);

	/* Encode for 'composite' layout type. */
	lid = 3333;
	rc = encode_composite(lid);
	C2_UT_ASSERT(rc == 0);
}

static void test_add(void)
{
	uint64_t                   lid;
	size_t                     num_bytes;
	void                      *area;
	struct c2_pdclust_layout  *pl;
	struct c2_db_pair          pair;
	struct c2_db_tx            tx;

	rc = internal_init();
	C2_UT_ASSERT(rc == 0);

	num_bytes = c2_ldb_max_recsize(&schema) + 1024;
	area = c2_alloc(num_bytes);
	C2_UT_ASSERT(area != NULL);

	pair.dp_key.db_buf.b_addr = &lid;
	pair.dp_key.db_buf.b_nob = sizeof lid;

	pair.dp_rec.db_buf.b_addr = area;
	pair.dp_rec.db_buf.b_nob = num_bytes;

	lid = 4444;
	rc = layout_pdclust_linear_build(&pl, lid, 4, 1, 100, 200);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_init(&tx, &dbenv, dbflags);
	C2_UT_ASSERT(rc == 0);

	rc = c2_ldb_add(&schema, &pl->pl_base.ls_base, &pair, &tx);
	C2_UT_ASSERT(rc == 0);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	internal_fini();
}

static void test_lookup(void)
{
	uint64_t                   lid;
	size_t                     num_bytes;
	void                      *area;
	struct c2_layout          *l;
	struct c2_db_pair          pair;
	struct c2_db_tx            tx;

	rc = internal_init();
	C2_UT_ASSERT(rc == 0);

	num_bytes = c2_ldb_max_recsize(&schema) + 1024;
	area = c2_alloc(num_bytes);
	C2_UT_ASSERT(area != NULL);

	pair.dp_key.db_buf.b_addr = &lid;
	pair.dp_key.db_buf.b_nob = sizeof lid;

	pair.dp_rec.db_buf.b_addr = area;
	pair.dp_rec.db_buf.b_nob = num_bytes;

	lid = 4444;
	//lid = 4441; @todo Non-existing lid.
	rc = c2_db_tx_init(&tx, &dbenv, dbflags);
	C2_UT_ASSERT(rc == 0);

	rc = c2_ldb_lookup(&schema, lid, &pair, &tx, &l);
	C2_UT_ASSERT(l->l_id == lid);

	rc = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(rc == 0);

	internal_fini();
}

static void test_update(void)
{
}

static void test_delete(void)
{
}

static void test_persistence(void)
{
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
                { "layout-add", test_add },
                { "layout-lookup", test_lookup },
                { "layout-update", test_update },
                { "layout-delete", test_delete },
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
