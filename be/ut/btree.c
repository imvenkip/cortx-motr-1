/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 12-Jun-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"

#include "be/tx_group_fom.h"
#include "be/btree.h"
#include "lib/types.h"     /* m0_uint128_eq */
#include "lib/misc.h"      /* M0_BITS, M0_IN */
#include "lib/memory.h"    /* M0_ALLOC_PTR */
#include "be/ut/helper.h"
#include "ut/ut.h"

extern void btree_dbg_print(struct m0_be_btree *tree);

static int tree_cmp(const void *key0, const void *key1)
{
	return strcmp(key0, key1);
}

static m0_bcount_t tree_kv_size(const void *kv)
{
	return strlen(kv) + 1;
}

static const struct m0_be_btree_kv_ops kv_ops = {
	.ko_ksize   = tree_kv_size,
	.ko_vsize   = tree_kv_size,
	.ko_compare = tree_cmp
};

enum {
	INSERT_COUNT = 100,
	INSERT_SIZE  = 4
};

static void check(struct m0_be_btree *tree,
		  struct m0_be_ut_backend *ut_be,
		  struct m0_be_seg *seg);
static struct m0_be_btree *create_tree(struct m0_be_ut_backend *ut_be,
				       struct m0_be_seg *seg);
static void destroy_tree(struct m0_be_btree *tree,
			 struct m0_be_ut_backend *ut_be,
			 struct m0_be_seg *seg);

void m0_be_ut_btree_simple(void)
{
	struct m0_be_ut_backend	 ut_be;
	struct m0_be_ut_seg	 ut_seg;
	struct m0_be_seg	*seg;
	struct m0_be_btree	*tree0;
	struct m0_be_btree	*tree1;

	M0_ENTRY();
	/* Init BE */
	m0_be_ut_backend_init(&ut_be);
	m0_be_ut_seg_init(&ut_seg, 1ULL << 24);
	m0_be_ut_seg_allocator_init(&ut_seg);
	seg = &ut_seg.bus_seg;

	/* create btrees */
	tree0 = create_tree(&ut_be, seg);

	/* Reload segment and check data */
	m0_be_ut_seg_reload(&ut_seg);

	check(tree0, &ut_be, seg);
	destroy_tree(tree0, &ut_be, seg);

	/* Reload segment, create new tree and check data */
	m0_be_ut_seg_reload(&ut_seg);

	tree1 = create_tree(&ut_be, seg);
	check(tree1, &ut_be, seg);
	destroy_tree(tree1, &ut_be, seg);

	/* XXX FIXME something wasn't freed */
	/* m0_be_ut_seg_allocator_fini(&ut_seg); */
	m0_be_ut_seg_fini(&ut_seg);
	m0_be_ut_backend_fini(&ut_be);

	M0_LEAVE();
}

static struct m0_be_btree *create_tree(struct m0_be_ut_backend *ut_be,
				       struct m0_be_seg *seg)
{
	struct m0_be_btree_anchor anchor;
	struct m0_be_tx_credit    insert_cred;
	struct m0_be_tx_credit    cred;
	struct m0_be_btree       *tree;
	struct m0_be_op           op;
	struct m0_be_tx          *tx;
	struct m0_buf             key;
	struct m0_buf             val;
	int                       rc;
	int                       i;
	struct m0_be_allocator   *a = &seg->bs_allocator;

	M0_ENTRY();
	/*
	 * Init BE, BE IO, credits
	 */

	M0_ALLOC_PTR(tx);
	M0_UT_ASSERT(tx != NULL);

	m0_be_tx_credit_init(&cred);
	m0_be_tx_credit_init(&insert_cred);

	/*
	 * Init transaction and its credits
	 */
	m0_be_ut_backend_tx_init(ut_be, tx);

	{ /* XXX: should calculate these credits not for dummy tree,
	   but for allocated below. This needs at least two transactions. */
		struct m0_be_btree t = { .bb_seg = seg };
		m0_be_btree_create_credit(&t, 1, &cred);
		m0_be_btree_insert_credit(&t, 1, INSERT_SIZE, INSERT_SIZE,
						&insert_cred);
	}

	m0_be_allocator_credit(a, M0_BAO_ALLOC, INSERT_SIZE, 0, &insert_cred);
	m0_be_allocator_credit(a, M0_BAO_ALLOC, INSERT_SIZE, 0, &insert_cred);
	m0_be_tx_credit_mul(&insert_cred, INSERT_COUNT);

	m0_be_allocator_credit(a, M0_BAO_ALLOC, sizeof *tree, 0, &cred);

	m0_be_tx_credit_add(&cred, &insert_cred);

	m0_be_tx_prep(tx, &cred);

	/* Open transaction, allocate, dirty and capture region. */
	m0_be_tx_open(tx);
	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_ACTIVE, M0_BTS_FAILED),
				M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_be_tx_state(tx) == M0_BTS_ACTIVE);
	M0_LOG(M0_DEBUG, "Transaction has reached M0_BTS_ACTIVE");

	/* start */

	m0_be_op_init(&op);
	tree = m0_be_alloc(a, tx, &op, sizeof *tree, 0),
	M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
						 M0_BOS_FAILURE)));
	m0_be_op_fini(&op);

	M0_SET0(tree); /* XXX Why do we need this?  --vvv */
	m0_be_btree_init(tree, seg, &kv_ops);

	m0_be_op_init(&op);
	m0_be_btree_create(tree, tx, &op);
	M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
						 M0_BOS_FAILURE)));
	m0_be_op_fini(&op);

	/* insert */
	for (i = 0; i < INSERT_COUNT/2; ++i) {
		m0_be_op_init(&op);
		m0_buf_init(&key, m0_be_alloc(a, tx, &op, INSERT_SIZE, 0),
			    INSERT_SIZE);
		M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
							 M0_BOS_FAILURE)));
		m0_be_op_fini(&op);

		m0_be_op_init(&op);
		m0_buf_init(&val, m0_be_alloc(a, tx, &op, INSERT_SIZE, 0),
			    INSERT_SIZE);
		M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
							 M0_BOS_FAILURE)));
		m0_be_op_fini(&op);

		sprintf(key.b_addr, "%03d", i);
		sprintf(val.b_addr, "%03d", i);
		m0_be_tx_capture(tx, &M0_BE_REG(seg, INSERT_SIZE, key.b_addr));
		m0_be_tx_capture(tx, &M0_BE_REG(seg, INSERT_SIZE, val.b_addr));

		m0_be_op_init(&op);
		m0_be_btree_insert(tree, tx, &op, &key, &val);
		M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
							 M0_BOS_FAILURE)));
		m0_be_op_fini(&op);
	}

	/* insert inplace */
	for (i = INSERT_COUNT/2; i < INSERT_COUNT; ++i) {
		m0_be_op_init(&op);
		m0_buf_init(&key, m0_be_alloc(a, tx, &op, INSERT_SIZE, 0),
			    INSERT_SIZE);
		M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
							 M0_BOS_FAILURE)));
		m0_be_op_fini(&op);

		sprintf(key.b_addr, "%03d", i);
		m0_be_tx_capture(tx, &M0_BE_REG(seg, INSERT_SIZE, key.b_addr));

		anchor.ba_value.b_nob = INSERT_SIZE;
		m0_be_op_init(&op);
		m0_be_btree_insert_inplace(tree, tx, &op, &key, &anchor);
		M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
							 M0_BOS_FAILURE)));
		m0_be_op_fini(&op);

		/* update kv */
		sprintf(anchor.ba_value.b_addr, "%03d", i);

		m0_be_op_init(&op);
		m0_be_btree_release(tree, tx, &op, &anchor);
		M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
							 M0_BOS_FAILURE)));
		m0_be_op_fini(&op);
	}

	/* delete */
	for (i = INSERT_COUNT/4; i < INSERT_COUNT*3/4; ++i) {
		char k[4];

		m0_buf_init(&key, k, ARRAY_SIZE(k));
		sprintf(key.b_addr, "%03d", i);

		m0_be_op_init(&op);
		m0_be_btree_delete(tree, tx, &op, &key);
		M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
							 M0_BOS_FAILURE)));
		m0_be_op_fini(&op);
	}

	/* update */
	sprintf(key.b_addr, "%03d", 99);
	m0_be_op_init(&op);
	m0_buf_init(&val, m0_be_alloc(a, tx, &op, INSERT_SIZE, 0), INSERT_SIZE);
	M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
						 M0_BOS_FAILURE)));
	m0_be_op_fini(&op);

	sprintf(val.b_addr, "XYZ");
	m0_be_tx_capture(tx, &M0_BE_REG(seg, INSERT_SIZE, val.b_addr));

	m0_be_op_init(&op);
	m0_be_btree_update(tree, tx, &op, &key, &val);
	M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
						 M0_BOS_FAILURE)));
	m0_be_op_fini(&op);

	/* end */

	m0_be_tx_close(tx); /* Make things persistent. */

	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_DONE), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_LOG(M0_DEBUG, "Transaction has reached M0_BTS_PLACED");
	m0_be_tx_fini(tx);

	btree_dbg_print(tree);
	M0_LOG(M0_DEBUG, "segment closed, tree root: %p", tree);

	M0_LEAVE();
	return tree;
}

static void destroy_tree(struct m0_be_btree *tree,
			 struct m0_be_ut_backend *ut_be,
			 struct m0_be_seg *seg)
{
	struct m0_be_tx_credit    cred;
	struct m0_be_op           op;
	struct m0_be_tx          *tx;
	int                       rc;

	M0_ENTRY();

	M0_ALLOC_PTR(tx);
	M0_UT_ASSERT(tx != NULL);

	m0_be_ut_backend_tx_init(ut_be, tx);

	m0_be_tx_credit_init(&cred);
	m0_be_btree_destroy_credit(tree, 1, &cred);

	m0_be_tx_prep(tx, &cred);
	m0_be_tx_open(tx);
	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_ACTIVE, M0_BTS_FAILED),
				M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_be_tx_state(tx) == M0_BTS_ACTIVE);
	M0_LOG(M0_DEBUG, "Transaction has reached M0_BTS_ACTIVE");

	m0_be_op_init(&op);
	m0_be_btree_destroy(tree, tx, &op);
	M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
						 M0_BOS_FAILURE)));
	m0_be_op_fini(&op);

	/* end */

	m0_be_tx_close(tx); /* Make things persistent. */

	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_DONE), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_LOG(M0_DEBUG, "Transaction has reached M0_BTS_PLACED");
	m0_be_tx_fini(tx);

	btree_dbg_print(tree);
	M0_LOG(M0_DEBUG, "segment closed, tree root: %p", tree);

	M0_LEAVE();
}

static void cursor_test(struct m0_be_btree *tree)
{
	struct m0_be_btree_cursor cursor;
	struct m0_buf		  start = M0_BUF_INITS("050");
	struct m0_buf		  key;
	struct m0_buf		  val;
	int                       v;
	int                       i;
	int                       rc;

	m0_be_btree_cursor_init(&cursor, tree);

	m0_be_op_init(&cursor.bc_op);
	m0_be_btree_cursor_get(&cursor, &start, true);
	/* XXX Shouldn't we m0_be_op_wait() here?  --vvv */
	M0_UT_ASSERT(m0_be_op_state(&cursor.bc_op) == M0_BOS_SUCCESS);
	m0_be_op_fini(&cursor.bc_op);

	m0_be_btree_cursor_kv_get(&cursor, &key, &val);
	/* make sure we are on the right position */
	M0_UT_ASSERT(strcmp(key.b_addr, "075") == 0 &&
		     strcmp(val.b_addr, key.b_addr) == 0);

	for (i = 0; key.b_addr != NULL; ++i) {
		v = atoi(key.b_addr);
		M0_UT_ASSERT(v == i + 75);
		M0_LOG(M0_DEBUG, "i=%i k=%s", i, (char*)key.b_addr);

		m0_be_op_init(&cursor.bc_op);
		m0_be_btree_cursor_next(&cursor);
		M0_UT_ASSERT(m0_be_op_state(&cursor.bc_op) == M0_BOS_SUCCESS);
		rc = cursor.bc_op.bo_u.u_btree.t_rc;
		m0_be_op_fini(&cursor.bc_op);

		m0_be_btree_cursor_kv_get(&cursor, &key, &val);
	}

	M0_UT_ASSERT(i == 25);
	M0_UT_ASSERT(rc == -ENOENT);

	start = (struct m0_buf)M0_BUF_INITS("024");
	m0_be_op_init(&cursor.bc_op);
	m0_be_btree_cursor_get(&cursor, &start, false);
	/* XXX Why don't we m0_be_op_wait()?  --vvv */
	M0_UT_ASSERT(m0_be_op_state(&cursor.bc_op) == M0_BOS_SUCCESS);
	m0_be_op_fini(&cursor.bc_op);

	m0_be_btree_cursor_kv_get(&cursor, &key, &val);
	/* make sure we are on the right position */
	M0_UT_ASSERT(strcmp(key.b_addr, "024") == 0 &&
		     strcmp(val.b_addr, key.b_addr) == 0);

	for (i = 24; key.b_addr != NULL; --i) {
		v = atoi(key.b_addr);
		M0_UT_ASSERT(v == i);
		M0_LOG(M0_DEBUG, "i=%i k=%s", i, (char*)key.b_addr);

		m0_be_op_init(&cursor.bc_op);
		m0_be_btree_cursor_prev(&cursor);
		M0_UT_ASSERT(m0_be_op_state(&cursor.bc_op) == M0_BOS_SUCCESS);
		rc = cursor.bc_op.bo_u.u_btree.t_rc;
		m0_be_op_fini(&cursor.bc_op);

		m0_be_btree_cursor_kv_get(&cursor, &key, &val);
	}

	M0_UT_ASSERT(i == -1);
	M0_UT_ASSERT(rc == -ENOENT);

	rc = m0_be_btree_cursor_get_sync(&cursor,
					 &(struct m0_buf)M0_BUF_INITS("200"),
					 true);
	M0_UT_ASSERT(rc == -ENOENT);

	m0_be_btree_cursor_fini(&cursor);
}

static void check(struct m0_be_btree *tree,
		  struct m0_be_ut_backend *ut_be,
		  struct m0_be_seg *seg)
{
	struct m0_buf             key;
	struct m0_buf             val;
	struct m0_be_op           op;
	struct m0_be_btree_anchor anchor;
	char k[4];
	char v[4];
	int i;

	m0_be_btree_init(tree, seg, &kv_ops);

	/* lookup */
	for (i = 0; i < INSERT_COUNT; ++i) {
		sprintf(k, "%03d", i);
		m0_buf_init(&key, k, ARRAY_SIZE(k));
		m0_buf_init(&val, v, ARRAY_SIZE(v));

		m0_be_op_init(&op);
		m0_be_btree_lookup(tree, &op, &key, &val);
		M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
							 M0_BOS_FAILURE)));
		m0_be_op_fini(&op);

		if (INSERT_COUNT/4 <= i && i < INSERT_COUNT*3/4)
			M0_UT_ASSERT(op.bo_u.u_btree.t_rc == -ENOENT);
		else if (i == 99)
			M0_UT_ASSERT(strcmp("XYZ", v) == 0);
		else
			M0_UT_ASSERT(strcmp(k, v) == 0);
	}

	/* lookup inplace */
	for (i = 0; i < INSERT_COUNT; ++i) {
		M0_SET0(&anchor);
		sprintf(k, "%03d", i);
		m0_buf_init(&key, k, ARRAY_SIZE(k));

		m0_be_op_init(&op);
		m0_be_btree_lookup_inplace(tree, &op, &key, &anchor);
		M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
							 M0_BOS_FAILURE)));
		m0_be_op_fini(&op);

		val = anchor.ba_value;
		if (INSERT_COUNT/4 <= i && i < INSERT_COUNT*3/4)
			M0_UT_ASSERT(val.b_addr == NULL && val.b_nob  == 0);
		else if (i == 99)
			M0_UT_ASSERT(strcmp("XYZ", val.b_addr) == 0);
		else
			M0_UT_ASSERT(strcmp(k, val.b_addr) == 0);

		m0_be_op_init(&op);
		m0_be_btree_release(tree, NULL, &op, &anchor);
		M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
							 M0_BOS_FAILURE)));
		m0_be_op_fini(&op);
	}

	m0_be_op_init(&op);
	m0_be_btree_minkey(tree, &op, &key);
	M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
						 M0_BOS_FAILURE)));
	m0_be_op_fini(&op);
	M0_UT_ASSERT(strcmp(key.b_addr, "000") == 0);

	m0_be_op_init(&op);
	m0_be_btree_maxkey(tree, &op, &key);
	M0_UT_ASSERT(M0_IN(m0_be_op_state(&op), (M0_BOS_SUCCESS,
						 M0_BOS_FAILURE)));
	m0_be_op_fini(&op);
	M0_UT_ASSERT(strcmp(key.b_addr, "099") == 0);

	cursor_test(tree);
	btree_dbg_print(tree);
	m0_be_btree_fini(tree);
}

#undef M0_TRACE_SUBSYSTEM
