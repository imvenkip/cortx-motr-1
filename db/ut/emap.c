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

#include "lib/arith.h"    /* C2_3WAY, c2_uint128 */
#include "lib/vec.h"
#include "lib/types.h"
#include "lib/ut.h"
#include "lib/ub.h"
#include "db/extmap.h"

static const char db_name[] = "ut-emap";
static const char emap_name[] = "test-emap";

static int db_reset(void)
{
        return c2_ut_db_reset(db_name);
}

static struct c2_dbenv       db;
static struct c2_emap        emap;
static struct c2_uint128     prefix;
static struct c2_db_tx       tx;
static struct c2_emap_cursor it;
static struct c2_emap_seg   *seg;

static int result;

static void test_init(void)
{
	result = c2_dbenv_init(&db, db_name, 0);
	C2_ASSERT(result == 0);

	result = c2_emap_init(&emap, &db, emap_name);
	C2_ASSERT(result == 0);

	result = c2_db_tx_init(&tx, &db, 0);
	C2_ASSERT(result == 0);

	c2_uint128_init(&prefix, "some random iden");
	seg = c2_emap_seg_get(&it);
}

static void test_fini(void)
{
	result = c2_db_tx_commit(&tx);
	C2_ASSERT(result == 0);

	c2_emap_fini(&emap);
	c2_dbenv_fini(&db);
}

static void checkpoint(void)
{
	result = c2_db_tx_commit(&tx);
	C2_ASSERT(result == 0);

	result = c2_db_tx_init(&tx, &db, 0);
	C2_ASSERT(result == 0);
}

static void test_obj_init(void)
{
	result = c2_emap_obj_insert(&emap, &tx, &prefix, 42);
	C2_ASSERT(result == 0);
	checkpoint();
}

static void test_lookup(void)
{
	result = c2_emap_lookup(&emap, &tx, &prefix, 0, &it);
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(c2_emap_ext_is_first(&seg->ee_ext));
	C2_UT_ASSERT(c2_emap_ext_is_last(&seg->ee_ext));
	C2_UT_ASSERT(seg->ee_val == 42);

	c2_emap_close(&it);

	result = c2_emap_lookup(&emap, &tx, &prefix, 1000000, &it);
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(c2_emap_ext_is_first(&seg->ee_ext));
	C2_UT_ASSERT(c2_emap_ext_is_last(&seg->ee_ext));
	C2_UT_ASSERT(seg->ee_val == 42);

	c2_emap_close(&it);
	checkpoint();
}

static void split(c2_bindex_t offset, int nr, bool commit)
{
	int i;
	c2_bcount_t len[] = { 100, 2, 0, 0 };
	uint64_t    val[] = { 1,   2, 3, 4 };
	struct c2_indexvec vec = {
		.iv_vec = {
			.v_nr    = ARRAY_SIZE(len),
			.v_count = len
		},
		.iv_index = val
	};

	result = c2_emap_lookup(&emap, &tx, &prefix, offset, &it);
	C2_ASSERT(result == 0);

	for (i = 0; i < nr; ++i) {
		c2_bcount_t seglen;
		c2_bcount_t total;

		seglen = c2_ext_length(&seg->ee_ext);
		total  = 102; /* 100 + 2, the sum of elements in len[]. */
		if (seglen < total) {
			len[0] = 1;
			len[1] = 0;
			total = 1;
		}
		len[ARRAY_SIZE(len) - 1] = c2_ext_length(&seg->ee_ext) - total;
		result = c2_emap_split(&it, &vec);
		C2_ASSERT(result == 0);
	}
	c2_emap_close(&it);
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

	result = c2_emap_lookup(&emap, &tx, &prefix, 0, &it);
	C2_UT_ASSERT(result == 0);

#if 0
	printf("%010lx:%010lx:\n", prefix.u_hi, prefix.u_lo);
#endif
	for (i = 0; ; ++i) {
#if 0
		printf("\t%5.5i %16lx .. %16lx: %16lx %10lx\n", i,
		       seg->ee_ext.e_start, seg->ee_ext.e_end,
		       c2_ext_length(&seg->ee_ext), seg->ee_val);
#endif
		if (c2_emap_ext_is_last(&seg->ee_ext))
			break;
		result = c2_emap_next(&it);
		C2_UT_ASSERT(result == 0);
	}
	c2_emap_close(&it);
}

static void test_merge(void)
{
	result = c2_emap_lookup(&emap, &tx, &prefix, 0, &it);
	C2_UT_ASSERT(result == 0);

	while (!c2_emap_ext_is_last(&seg->ee_ext)) {
		result = c2_emap_merge(&it, c2_ext_length(&seg->ee_ext));
		C2_UT_ASSERT(result == 0);
	}
	c2_emap_close(&it);
	checkpoint();
}

static void test_obj_fini(void)
{
	result = c2_emap_obj_delete(&emap, &tx, &prefix);
	C2_UT_ASSERT(result == 0);
	checkpoint();
}


const struct c2_test_suite emap_ut = {
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

static void ub_init(void)
{
	db_reset();
	test_init();
}

static void ub_fini(void)
{
	test_fini();
	db_reset();
}

static struct c2_uint128 p;

static void ub_obj_init(int i)
{
	p = prefix;

	p.u_hi += i;
	p.u_lo -= i*i;

	result = c2_emap_obj_insert(&emap, &tx, &p, 42);
	C2_ASSERT(result == 0);
	checkpoint();
}

static void ub_obj_fini(int i)
{
	p = prefix;

	p.u_hi += i;
	p.u_lo -= i*i;

	result = c2_emap_obj_delete(&emap, &tx, &p);
	C2_ASSERT(result == 0);
	checkpoint();
}

static void ub_obj_init_same(int i)
{
	p = prefix;

	p.u_hi += i;
	p.u_lo -= i*i;

	result = c2_emap_obj_insert(&emap, &tx, &p, 42);
	C2_ASSERT(result == 0);
}

static void ub_obj_fini_same(int i)
{
	p = prefix;

	p.u_hi += i;
	p.u_lo -= i*i;

	result = c2_emap_obj_delete(&emap, &tx, &p);
	C2_ASSERT(result == 0);
}

static void ub_split(int i)
{
	split(5000, 1, false);
}

struct c2_ub_set c2_emap_ub = {
	.us_name = "emap-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = {
		{ .ut_name = "obj-init",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_obj_init },

		{ .ut_name = "obj-fini",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_obj_fini },

		{ .ut_name = "obj-init-same-tx",
		  .ut_iter = UB_ITER_TX,
		  .ut_round = ub_obj_init_same },

		{ .ut_name = "obj-fini-same-tx",
		  .ut_iter = UB_ITER_TX,
		  .ut_round = ub_obj_fini_same },

		{ .ut_name = "split",
		  .ut_iter = UB_ITER/5,
		  .ut_init = test_obj_init,
		  .ut_round = ub_split },

		{ .ut_name = NULL }
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
