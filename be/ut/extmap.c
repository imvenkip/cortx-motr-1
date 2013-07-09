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
#include "be/ut/helper.h"
#include "be/extmap.h"

static struct m0_be_ut_h be_ut_emap_h;

//static struct m0_be               be;
static struct m0_be_tx            tx;
static struct m0_be_op            op;
//static struct m0_be_tx_credit     cred;
//static uint64_t			  tid = 1ULL;

static struct m0_be_emap         *emap;
static struct m0_uint128          prefix;
static struct m0_be_emap_cursor   it;
static struct m0_be_emap_seg     *seg;
static struct m0_be_op           *it_op;

static int                        result;


static void seg_create(void)
{
	m0_be_ut_seg_create_open(&be_ut_emap_h);
}

static void seg_destroy(void)
{
	m0_be_ut_seg_close_destroy(&be_ut_emap_h);
}

//static void persistent(const struct m0_be_tx *tx)
//{
//}
//static void discarded(const struct m0_be_tx *tx)
//{
//}

static void test_init(void)
{
	seg_create();

	//m0_be_tx_init(&tx, tid, &be, persistent, discarded, NULL);

	emap = be_ut_emap_h.buh_seg.bs_addr + 8192;
	m0_be_emap_init(emap, &be_ut_emap_h.buh_seg);
	M0_ASSERT(result == 0);
	//m0_be_emap_credit(&tree, M0_EMO_CREATE, 1, &cred);
	//m0_be_emap_credit(&tree, M0_EMO_CREATE, 1, &cred);
	//m0_be_emap_credit(&tree, M0_EMO_CREATE, 1, &cred);
	//m0_be_emap_credit(&tree, M0_EMO_CREATE, 1, &cred);
	//m0_be_emap_credit(&tree, M0_EMO_CREATE, 1, &cred);

	//m0_be_tx_prep(&tx, &cred);
	//m0_be_tx_open(&tx);

	m0_uint128_init(&prefix, "some random iden");
	seg = m0_be_emap_seg_get(&it);

	it_op = m0_be_emap_op(&it);
}

static void test_fini(void)
{
	//m0_be_tx_close(&tx);
	//M0_UT_ASSERT(m0_be_tx_timedwait(&tx, M0_TIME_NEVER) == 0);

	m0_be_emap_fini(emap);
	seg_destroy();
}

static void checkpoint(void)
{
}

static void test_obj_init(void)
{
	m0_be_emap_obj_insert(emap, &tx, &op, &prefix, 42);
	M0_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	checkpoint();
}

static void test_lookup(void)
{
	m0_be_emap_lookup(emap, &prefix, 0, &it);
	M0_ASSERT(m0_be_op_state(it_op) == M0_BOS_SUCCESS);
	M0_UT_ASSERT(it_op->bo_sm.sm_rc == 0);
	M0_UT_ASSERT(m0_be_emap_ext_is_first(&seg->ee_ext));
	M0_UT_ASSERT(m0_be_emap_ext_is_last(&seg->ee_ext));
	M0_UT_ASSERT(seg->ee_val == 42);

	m0_be_emap_close(&it);

	m0_be_emap_lookup(emap, &prefix, 1000000, &it);
	M0_ASSERT(m0_be_op_state(it_op) == M0_BOS_SUCCESS);
	M0_UT_ASSERT(it_op->bo_sm.sm_rc == 0);
	M0_UT_ASSERT(m0_be_emap_ext_is_first(&seg->ee_ext));
	M0_UT_ASSERT(m0_be_emap_ext_is_last(&seg->ee_ext));
	M0_UT_ASSERT(seg->ee_val == 42);

	m0_be_emap_close(&it);
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

	m0_be_emap_lookup(emap, &prefix, offset, &it);
	M0_ASSERT(m0_be_op_state(it_op) == M0_BOS_SUCCESS);
	M0_UT_ASSERT(it_op->bo_sm.sm_rc == 0);

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
		m0_be_emap_split(&it, &tx, &vec);
		M0_ASSERT(m0_be_op_state(&it.ec_op) == M0_BOS_SUCCESS);
		M0_UT_ASSERT(it.ec_op.bo_sm.sm_rc == 0);
	}
	m0_be_emap_close(&it);
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

	m0_be_emap_lookup(emap, &prefix, 0, &it);
	M0_ASSERT(m0_be_op_state(it_op) == M0_BOS_SUCCESS);
	M0_UT_ASSERT(it_op->bo_sm.sm_rc == 0);

#if 0
	printf("%010lx:%010lx:\n", prefix.u_hi, prefix.u_lo);
#endif
	for (i = 0; ; ++i) {
#if 0
		printf("\t%5.5i %16lx .. %16lx: %16lx %10lx\n", i,
		       seg->ee_ext.e_start, seg->ee_ext.e_end,
		       m0_ext_length(&seg->ee_ext), seg->ee_val);
#endif
		if (m0_be_emap_ext_is_last(&seg->ee_ext))
			break;
		m0_be_emap_next(&it);
		M0_ASSERT(m0_be_op_state(it_op) == M0_BOS_SUCCESS);
		M0_UT_ASSERT(it_op->bo_sm.sm_rc == 0);
	}
	m0_be_emap_close(&it);
}

static void test_merge(void)
{
	m0_be_emap_lookup(emap, &prefix, 0, &it);
	M0_ASSERT(m0_be_op_state(it_op) == M0_BOS_SUCCESS);
	M0_UT_ASSERT(it_op->bo_sm.sm_rc == 0);

	while (!m0_be_emap_ext_is_last(&seg->ee_ext)) {
		m0_be_emap_merge(&it, &tx, m0_ext_length(&seg->ee_ext));
		M0_ASSERT(m0_be_op_state(it_op) == M0_BOS_SUCCESS);
		M0_UT_ASSERT(it_op->bo_sm.sm_rc == 0);
	}
	m0_be_emap_close(&it);
	checkpoint();
}

static void test_obj_fini(void)
{
	m0_be_emap_obj_delete(emap, &tx, &op, &prefix);
	M0_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	checkpoint();
}


const struct m0_test_suite be_emap_ut = {
	.ts_name = "be-emap-ut",
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
	seg_create();
	test_init();
	return 0;
}

static void ub_fini(void)
{
	test_fini();
	seg_destroy();
}

static struct m0_uint128 p;

static void ub_obj_init(int i)
{
	p = prefix;

	p.u_hi += i;
	p.u_lo -= i*i;

	m0_be_emap_obj_insert(emap, &tx, &op, &p, 42);
	M0_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	checkpoint();
}

static void ub_obj_fini(int i)
{
	p = prefix;

	p.u_hi += i;
	p.u_lo -= i*i;

	m0_be_emap_obj_delete(emap, &tx, &op, &p);
	M0_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	checkpoint();
}

static void ub_obj_init_same(int i)
{
	p = prefix;

	p.u_hi += i;
	p.u_lo -= i*i;

	m0_be_emap_obj_insert(emap, &tx, &op, &p, 42);
	M0_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
}

static void ub_obj_fini_same(int i)
{
	p = prefix;

	p.u_hi += i;
	p.u_lo -= i*i;

	m0_be_emap_obj_delete(emap, &tx, &op, &p);
	M0_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
}

static void ub_split(int i)
{
	split(5000, 1, false);
}

struct m0_ub_set m0_be_emap_ub = {
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
