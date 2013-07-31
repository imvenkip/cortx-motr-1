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
 * Original author: Andriy Tkachuk <andriy_tkachuk@xyratex.com>
 * Original creation date: 08/13/2010
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include <stdio.h>        /* printf */

#include "lib/arith.h"    /* M0_3WAY, m0_uint128 */
#include "lib/vec.h"
#include "lib/types.h"
#include "lib/ub.h"
#include "lib/misc.h"
#include "ut/ut.h"
#include "be/ut/helper.h"
#include "be/extmap.h"

static struct m0_be_ut_h be_ut_emap_h;

static struct m0_be_tx          tx1;
static struct m0_be_tx          tx2;
static struct m0_be_op          op;
static struct m0_be_emap       *emap;
static struct m0_uint128        prefix;
static struct m0_be_emap_cursor it;
static struct m0_be_emap_seg   *seg; /* cursor segment */
static struct m0_be_seg        *be_seg;
static struct m0_be_op         *it_op;

static void emap_alloc(struct m0_be_tx *tx)
{
	int                     rc;
	struct m0_be_tx_credit  cred;
	struct m0_be_allocator *a = &be_seg->bs_allocator;

	m0_be_tx_credit_init(&cred);
	m0_be_allocator_credit(a, M0_BAO_ALLOC, sizeof *emap, 0, &cred);

	m0_sm_group_lock(&ut__txs_sm_group);

	m0_be_ut_h_tx_init(tx, &be_ut_emap_h);
	m0_be_tx_prep(tx, &cred);
	m0_be_tx_open(tx);
	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_ACTIVE), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);

	m0_be_op_init(&op);
	emap = m0_be_alloc(a, tx, &op, sizeof *emap, 0);
	m0_be_op_wait(&op);
	M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	m0_be_op_fini(&op);

	M0_UT_ASSERT(emap != NULL);

	m0_be_tx_close(tx);
	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_PLACED), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);

	m0_sm_group_unlock(&ut__txs_sm_group);
}

static void emap_create(struct m0_be_tx *tx)
{
	m0_be_op_init(&op);
	m0_be_emap_create(emap, tx, &op, be_seg);
	m0_be_op_wait(&op);
	M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	m0_be_op_fini(&op);
}

static void emap_destroy(struct m0_be_tx *tx)
{
	m0_be_op_init(&op);
	m0_be_emap_destroy(emap, tx, &op);
	m0_be_op_wait(&op);
	M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	m0_be_op_fini(&op);
}

static void checkpoint(void)
{
}

static void test_obj_init(struct m0_be_tx *tx)
{
	m0_be_op_init(&op);
	m0_be_emap_obj_insert(emap, tx, &op, &prefix, 42);
	m0_be_op_wait(&op);
	M0_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	m0_be_op_fini(&op);
	checkpoint();
}

static void test_obj_fini(struct m0_be_tx *tx)
{
	m0_be_op_init(&op);
	m0_be_emap_obj_delete(emap, tx, &op, &prefix);
	m0_be_op_wait(&op);
	M0_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	M0_UT_ASSERT(op.bo_u.u_emap.e_rc == 0);
	m0_be_op_fini(&op);
	checkpoint();
}

static void test_init(void)
{
	int			 rc;
	struct m0_be_tx_credit	 cred;

	M0_ENTRY();

	/* Init BE */
	m0_be_ut_h_init(&be_ut_emap_h);
	be_seg = &be_ut_emap_h.buh_seg;

	emap_alloc(&tx1);
	m0_be_emap_init(emap, be_seg);

	m0_be_tx_credit_init(&cred);
	m0_be_emap_credit(emap, M0_BEO_CREATE, 1, &cred);
	m0_be_emap_credit(emap, M0_BEO_DESTROY, 1, &cred);
	m0_be_emap_credit(emap, M0_BEO_INSERT, 1, &cred);
	m0_be_emap_credit(emap, M0_BEO_DELETE, 1, &cred);
	m0_be_emap_credit(emap, M0_BEO_SPLIT, 100 * 4, &cred);
	m0_be_emap_credit(emap, M0_BEO_MERGE, 100, &cred);
	m0_be_emap_credit(emap, M0_BEO_PASTE, 3, &cred);

	m0_sm_group_lock(&ut__txs_sm_group);

	m0_be_ut_h_tx_init(&tx2, &be_ut_emap_h);
	m0_be_tx_prep(&tx2, &cred);
	m0_be_tx_open(&tx2);
	rc = m0_be_tx_timedwait(&tx2, M0_BITS(M0_BTS_ACTIVE), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);

	emap_create(&tx2);

	m0_uint128_init(&prefix, "some random iden");
	seg = m0_be_emap_seg_get(&it);
	it_op = m0_be_emap_op(&it);

	M0_LEAVE();
}

static void test_fini(void)
{
	int rc;

	emap_destroy(&tx2);

	m0_be_tx_close(&tx2);
	rc = m0_be_tx_timedwait(&tx2, M0_BITS(M0_BTS_PLACED), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);

	m0_sm_group_unlock(&ut__txs_sm_group);

	m0_be_ut_h_fini(&be_ut_emap_h);
}

static int be_emap_lookup(struct m0_be_emap        *map,
			  const struct m0_uint128  *prefix,
			  m0_bindex_t               offset,
			  struct m0_be_emap_cursor *it)
{
	int rc;

	m0_be_op_init(&it->ec_op);
	m0_be_emap_lookup(emap, prefix, offset, it);
	m0_be_op_wait(&it->ec_op);
	M0_ASSERT(m0_be_op_state(&it->ec_op) == M0_BOS_SUCCESS);
	rc = it->ec_op.bo_u.u_emap.e_rc;
	m0_be_op_fini(&it->ec_op);

	return rc;
}

static void test_lookup(void)
{
	int rc;

	rc = be_emap_lookup(emap, &prefix, 0, &it);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_be_emap_ext_is_first(&seg->ee_ext));
	M0_UT_ASSERT(m0_be_emap_ext_is_last(&seg->ee_ext));
	M0_UT_ASSERT(seg->ee_val == 42);

	m0_be_emap_close(&it);

	rc = be_emap_lookup(emap, &prefix, 1000000, &it);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_be_emap_ext_is_first(&seg->ee_ext));
	M0_UT_ASSERT(m0_be_emap_ext_is_last(&seg->ee_ext));
	M0_UT_ASSERT(seg->ee_val == 42);

	m0_be_emap_close(&it);

	++prefix.u_lo;
	rc = be_emap_lookup(emap, &prefix, 0, &it);
	M0_UT_ASSERT(rc == -ENOENT);
	--prefix.u_lo;

	m0_be_emap_close(&it);

	checkpoint();
}

static void split(m0_bindex_t offset, int nr, bool commit)
{
	int i;
	int rc;
	m0_bcount_t len[] = { 100, 2, 0, 0 };
	uint64_t    val[] = { 1,   2, 3, 4 };
	struct m0_indexvec vec = {
		.iv_vec = {
			.v_nr    = ARRAY_SIZE(len),
			.v_count = len
		},
		.iv_index = val
	};

	rc = be_emap_lookup(emap, &prefix, offset, &it);
	M0_UT_ASSERT(rc == 0);

	for (i = 0; i < nr; ++i) {
		m0_bcount_t seglen;
		m0_bcount_t total;

		seglen = m0_ext_length(&seg->ee_ext);
		M0_LOG(M0_DEBUG, "%3i: seglen=%llu", i,
					(unsigned long long)seglen);
		total  = 102; /* 100 + 2, the sum of elements in len[]. */
		M0_UT_ASSERT(seglen > total);
		len[ARRAY_SIZE(len) - 1] = seglen - total;
		m0_be_op_init(it_op);
		m0_be_emap_split(&it, &tx2, &vec);
		m0_be_op_wait(it_op);
		M0_ASSERT(m0_be_op_state(it_op) == M0_BOS_SUCCESS);
		M0_UT_ASSERT(it.ec_op.bo_u.u_emap.e_rc == 0);
		m0_be_op_fini(it_op);
		M0_UT_ASSERT(m0_ext_length(&seg->ee_ext) ==
						len[ARRAY_SIZE(len) - 1]);
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
	int rc;

	rc = be_emap_lookup(emap, &prefix, 0, &it);
	M0_UT_ASSERT(rc == 0);

	M0_LOG(M0_DEBUG, "%010lx:%010lx:", prefix.u_hi, prefix.u_lo);
	for (i = 0; ; ++i) {
		M0_LOG(M0_DEBUG, "\t%5.5i %16lx .. %16lx: %16lx %10lx", i,
		       seg->ee_ext.e_start, seg->ee_ext.e_end,
		       m0_ext_length(&seg->ee_ext), seg->ee_val);
		if (m0_be_emap_ext_is_last(&seg->ee_ext))
			break;
		m0_be_op_init(it_op);
		m0_be_emap_next(&it);
		m0_be_op_wait(it_op);
		M0_ASSERT(m0_be_op_state(it_op) == M0_BOS_SUCCESS);
		M0_UT_ASSERT(it_op->bo_u.u_emap.e_rc == 0);
		m0_be_op_fini(it_op);
	}
	m0_be_emap_close(&it);
}

static void test_merge(void)
{
	int rc;

	rc = be_emap_lookup(emap, &prefix, 0, &it);
	M0_UT_ASSERT(rc == 0);

	while (!m0_be_emap_ext_is_last(&seg->ee_ext)) {
		m0_be_op_init(it_op);
		m0_be_emap_merge(&it, &tx2, m0_ext_length(&seg->ee_ext));
		m0_be_op_wait(it_op);
		M0_ASSERT(m0_be_op_state(it_op) == M0_BOS_SUCCESS);
		M0_UT_ASSERT(it_op->bo_u.u_emap.e_rc == 0);
		m0_be_op_fini(it_op);
	}
	m0_be_emap_close(&it);
	checkpoint();
}

static void test_paste(void)
{
	int		 rc;
	struct m0_ext	 e;

	rc = be_emap_lookup(emap, &prefix, 0, &it);
	M0_UT_ASSERT(rc == 0);

	e.e_start = 10;
	e.e_end   = 20;

	m0_be_op_init(it_op);
	m0_be_emap_paste(&it, &tx2, &e, 12, NULL, NULL, NULL);
	m0_be_op_wait(it_op);
	M0_ASSERT(m0_be_op_state(it_op) == M0_BOS_SUCCESS);
	M0_UT_ASSERT(it_op->bo_u.u_emap.e_rc == 0);
	m0_be_op_fini(it_op);

	M0_UT_ASSERT(seg->ee_ext.e_start == 20);
	M0_UT_ASSERT(seg->ee_ext.e_end   == M0_BINDEX_MAX + 1);

	test_print();

	rc = be_emap_lookup(emap, &prefix, 0, &it);
	M0_UT_ASSERT(rc == 0);

	M0_UT_ASSERT(seg->ee_ext.e_start ==  0);
	M0_UT_ASSERT(seg->ee_ext.e_end   == 10);

	rc = be_emap_lookup(emap, &prefix, 10, &it);
	M0_UT_ASSERT(rc == 0);

	M0_UT_ASSERT(seg->ee_ext.e_start == 10);
	M0_UT_ASSERT(seg->ee_ext.e_end   == 20);
	M0_UT_ASSERT(seg->ee_val         == 12);

	rc = be_emap_lookup(emap, &prefix, 20, &it);
	M0_UT_ASSERT(rc == 0);

	M0_UT_ASSERT(seg->ee_ext.e_start == 20);
	M0_UT_ASSERT(seg->ee_ext.e_end   == M0_BINDEX_MAX + 1);

	rc = be_emap_lookup(emap, &prefix, 0, &it);
	M0_UT_ASSERT(rc == 0);

	e.e_start = 5;
	e.e_end   = 25;

	m0_be_op_init(it_op);
	m0_be_emap_paste(&it, &tx2, &e, 11, NULL, NULL, NULL);
	m0_be_op_wait(it_op);
	M0_ASSERT(m0_be_op_state(it_op) == M0_BOS_SUCCESS);
	M0_UT_ASSERT(it_op->bo_u.u_emap.e_rc == 0);
	m0_be_op_fini(it_op);

	M0_UT_ASSERT(seg->ee_ext.e_start == 25);
	M0_UT_ASSERT(seg->ee_ext.e_end   == M0_BINDEX_MAX + 1);

	test_print();

	rc = be_emap_lookup(emap, &prefix, 0, &it);
	M0_UT_ASSERT(rc == 0);

	M0_UT_ASSERT(seg->ee_ext.e_start == 0);
	M0_UT_ASSERT(seg->ee_ext.e_end   == 5);

	rc = be_emap_lookup(emap, &prefix, 5, &it);
	M0_UT_ASSERT(rc == 0);

	M0_UT_ASSERT(seg->ee_ext.e_start ==  5);
	M0_UT_ASSERT(seg->ee_ext.e_end   == 25);
	M0_UT_ASSERT(seg->ee_val         == 11);

	rc = be_emap_lookup(emap, &prefix, 25, &it);
	M0_UT_ASSERT(rc == 0);

	M0_UT_ASSERT(seg->ee_ext.e_start == 25);
	M0_UT_ASSERT(seg->ee_ext.e_end   == M0_BINDEX_MAX + 1);

	rc = be_emap_lookup(emap, &prefix, 0, &it);
	M0_UT_ASSERT(rc == 0);

	e.e_start = 0;
	e.e_end   = M0_BINDEX_MAX + 1;

	m0_be_op_init(it_op);
	m0_be_emap_paste(&it, &tx2, &e, 0, NULL, NULL, NULL);
	m0_be_op_wait(it_op);
	M0_ASSERT(m0_be_op_state(it_op) == M0_BOS_SUCCESS);
	M0_UT_ASSERT(it_op->bo_u.u_emap.e_rc == 0);
	m0_be_op_fini(it_op);

	test_print();

	m0_be_emap_close(&it);
}

void m0_be_ut_emap(void)
{
	test_init();
	test_obj_init(&tx2);
	test_lookup();
	test_split();
	test_print();
	test_merge();
	test_paste();
	test_obj_fini(&tx2);
	test_fini();
}

#if 0
const struct m0_test_suite m0_be_ut_emap = {
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
#endif

/*
 * UB
 */
//
//enum {
//	UB_ITER = 100000,
//	UB_ITER_TX = 10000
//};
//
//static int ub_init(const char *opts M0_UNUSED)
//{
//	test_init();
//	return 0;
//}
//
//static void ub_fini(void)
//{
//	test_fini();
//}
//
//static struct m0_uint128 p;
//
//static void ub_obj_init(int i)
//{
//	p = prefix;
//
//	p.u_hi += i;
//	p.u_lo -= i*i;
//
//	m0_be_emap_obj_insert(emap, &tx, &op, &p, 42);
//	M0_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
//	checkpoint();
//}
//
//static void ub_obj_fini(int i)
//{
//	p = prefix;
//
//	p.u_hi += i;
//	p.u_lo -= i*i;
//
//	m0_be_emap_obj_delete(emap, &tx, &op, &p);
//	M0_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
//	checkpoint();
//}
//
//static void ub_obj_init_same(int i)
//{
//	p = prefix;
//
//	p.u_hi += i;
//	p.u_lo -= i*i;
//
//	m0_be_emap_obj_insert(emap, &tx, &op, &p, 42);
//	M0_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
//}
//
//static void ub_obj_fini_same(int i)
//{
//	p = prefix;
//
//	p.u_hi += i;
//	p.u_lo -= i*i;
//
//	m0_be_emap_obj_delete(emap, &tx, &op, &p);
//	M0_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
//}
//
//static void ub_split(int i)
//{
//	split(5000, 1, false);
//}
//
//struct m0_ub_set m0_be_emap_ub = {
//	.us_name = "emap-ub",
//	.us_init = ub_init,
//	.us_fini = ub_fini,
//	.us_run  = {
//		{ .ub_name = "obj-init",
//		  .ub_iter = UB_ITER,
//		  .ub_round = ub_obj_init },
//
//		{ .ub_name = "obj-fini",
//		  .ub_iter = UB_ITER,
//		  .ub_round = ub_obj_fini },
//
//		{ .ub_name = "obj-init-same-tx",
//		  .ub_iter = UB_ITER_TX,
//		  .ub_round = ub_obj_init_same },
//
//		{ .ub_name = "obj-fini-same-tx",
//		  .ub_iter = UB_ITER_TX,
//		  .ub_round = ub_obj_fini_same },
//
//		{ .ub_name = "split",
//		  .ub_iter = UB_ITER/5,
//		  .ub_init = test_obj_init,
//		  .ub_round = ub_split },
//
//		{ .ub_name = NULL }
//	}
//};

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
