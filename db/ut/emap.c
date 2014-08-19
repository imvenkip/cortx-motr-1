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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 08/13/2010
 */

#include <stdio.h>        /* printf */

#include "lib/arith.h"    /* M0_3WAY, m0_uint128 */
#include "lib/vec.h"
#include "lib/types.h"
#include "lib/ub.h"
#include "ut/ut.h"
#include "db/db.h"        /* m0_dbenv_reset */
#include "db/extmap.h"

static const char db_name[] = "ut-emap";
static const char emap_name[] = "test-emap";

static int db_reset(void)
{
	m0_dbenv_reset(db_name);
	return 0;
}

static struct m0_dbenv       db;
static struct m0_emap        emap;
static struct m0_uint128     prefix;
static struct m0_db_tx       tx;
static struct m0_emap_cursor it;
static struct m0_emap_seg   *seg;

static int result;

static void test_init(void)
{
	result = m0_dbenv_init(&db, db_name, 0, true);
	M0_ASSERT(result == 0);

	result = m0_emap_init(&emap, &db, emap_name);
	M0_ASSERT(result == 0);

	result = m0_db_tx_init(&tx, &db, 0);
	M0_ASSERT(result == 0);

	m0_uint128_init(&prefix, "some random iden");
	seg = m0_emap_seg_get(&it);
}

static void test_fini(void)
{
	result = m0_db_tx_commit(&tx);
	M0_ASSERT(result == 0);

	m0_emap_fini(&emap);
	m0_dbenv_fini(&db);
}

static void checkpoint(void)
{
	result = m0_db_tx_commit(&tx);
	M0_ASSERT(result == 0);

	result = m0_db_tx_init(&tx, &db, 0);
	M0_ASSERT(result == 0);
}

static void test_obj_init(void)
{
	result = m0_emap_obj_insert(&emap, &tx, &prefix, 42);
	M0_ASSERT(result == 0);
	checkpoint();
}

static void test_lookup(void)
{
	result = m0_emap_lookup(&emap, &tx, &prefix, 0, &it);
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(m0_emap_ext_is_first(&seg->ee_ext));
	M0_UT_ASSERT(m0_emap_ext_is_last(&seg->ee_ext));
	M0_UT_ASSERT(seg->ee_val == 42);

	m0_emap_close(&it);

	result = m0_emap_lookup(&emap, &tx, &prefix, 1000000, &it);
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(m0_emap_ext_is_first(&seg->ee_ext));
	M0_UT_ASSERT(m0_emap_ext_is_last(&seg->ee_ext));
	M0_UT_ASSERT(seg->ee_val == 42);

	m0_emap_close(&it);
	checkpoint();
}

static void split(m0_bindex_t offset, int nr, bool commit)
{
	int i;
	m0_bcount_t len[] = { 100, 2, 0, 0 };
	uint64_t    val[] = { 1,   2, 3, 4 };
	struct m0_indexvec vec = {
		.iv_vec = {
			.v_nr    = ARRAY_SIZE(len),
			.v_count = len
		},
		.iv_index = val
	};

	result = m0_emap_lookup(&emap, &tx, &prefix, offset, &it);
	M0_ASSERT(result == 0);

	for (i = 0; i < nr; ++i) {
		m0_bcount_t seglen;
		m0_bcount_t total;

		seglen = m0_ext_length(&seg->ee_ext);
		total  = 102; /* 100 + 2, the sum of elements in len[]. */
		if (seglen < total) {
			len[0] = 1;
			len[1] = 0;
			total = 1;
		}
		len[ARRAY_SIZE(len) - 1] = m0_ext_length(&seg->ee_ext) - total;
		result = m0_emap_split(&it, &vec);
		M0_ASSERT(result == 0);
	}
	m0_emap_close(&it);
	if (commit)
		checkpoint();
}

static void test_split(void)
{
	split(0, 100, true);
}

static void test_print(void)
{
	int i;

	result = m0_emap_lookup(&emap, &tx, &prefix, 0, &it);
	M0_UT_ASSERT(result == 0);

#if 0
	printf(U128X_F":\n", U128_P(&prefix));
#endif
	for (i = 0; ; ++i) {
#if 0
		printf("\t%5.5i %16lx .. %16lx: %16lx %10lx\n", i,
		       seg->ee_ext.e_start, seg->ee_ext.e_end,
		       m0_ext_length(&seg->ee_ext), seg->ee_val);
#endif
		if (m0_emap_ext_is_last(&seg->ee_ext))
			break;
		result = m0_emap_next(&it);
		M0_UT_ASSERT(result == 0);
	}
	m0_emap_close(&it);
}

static void test_merge(void)
{
	result = m0_emap_lookup(&emap, &tx, &prefix, 0, &it);
	M0_UT_ASSERT(result == 0);

	while (!m0_emap_ext_is_last(&seg->ee_ext)) {
		result = m0_emap_merge(&it, m0_ext_length(&seg->ee_ext));
		M0_UT_ASSERT(result == 0);
	}
	m0_emap_close(&it);
	checkpoint();
}

static void test_obj_fini(void)
{
	result = m0_emap_obj_delete(&emap, &tx, &prefix);
	M0_UT_ASSERT(result == 0);
	checkpoint();
}


struct m0_ut_suite emap_ut = {
	.ts_name = "emap-ut",
	.ts_init = db_reset,
	.ts_fini = db_reset,
	.ts_tests = {
		{ "emap-init", test_init },
		{ "obj-init", test_obj_init },
		{ "lookup", test_lookup },
		{ "split", test_split },
		{ "print", test_print },
		{ "merge", test_merge },
		{ "obj-fini", test_obj_fini },
		{ "emap-fini", test_fini },
		{ NULL, NULL }
	}
};

/*
 * UB
 */

enum {
	UB_ITER = 100000,
	UB_ITER_TX = 10000
};

static int ub_init(const char *opts M0_UNUSED)
{
	db_reset();
	test_init();
	return 0;
}

static void ub_fini(void)
{
	test_fini();
	db_reset();
}

static struct m0_uint128 p;

static void ub_obj_init(int i)
{
	p = prefix;

	p.u_hi += i;
	p.u_lo -= i*i;

	result = m0_emap_obj_insert(&emap, &tx, &p, 42);
	M0_ASSERT(result == 0);
	checkpoint();
}

static void ub_obj_fini(int i)
{
	p = prefix;

	p.u_hi += i;
	p.u_lo -= i*i;

	result = m0_emap_obj_delete(&emap, &tx, &p);
	M0_ASSERT(result == 0);
	checkpoint();
}

static void ub_obj_init_same(int i)
{
	p = prefix;

	p.u_hi += i;
	p.u_lo -= i*i;

	result = m0_emap_obj_insert(&emap, &tx, &p, 42);
	M0_ASSERT(result == 0);
}

static void ub_obj_fini_same(int i)
{
	p = prefix;

	p.u_hi += i;
	p.u_lo -= i*i;

	result = m0_emap_obj_delete(&emap, &tx, &p);
	M0_ASSERT(result == 0);
}

static void ub_split(int i)
{
	split(5000, 1, false);
}

struct m0_ub_set m0_emap_ub = {
	.us_name = "emap-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = {
		{ .ub_name = "obj-init",
		  .ub_iter = UB_ITER,
		  .ub_round = ub_obj_init },

		{ .ub_name = "obj-fini",
		  .ub_iter = UB_ITER,
		  .ub_round = ub_obj_fini },

		{ .ub_name = "obj-init-same-tx",
		  .ub_iter = UB_ITER_TX,
		  .ub_round = ub_obj_init_same },

		{ .ub_name = "obj-fini-same-tx",
		  .ub_iter = UB_ITER_TX,
		  .ub_round = ub_obj_fini_same },

		{ .ub_name = "split",
		  .ub_iter = UB_ITER/5,
		  .ub_init = test_obj_init,
		  .ub_round = ub_split },

		{ .ub_name = NULL }
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
