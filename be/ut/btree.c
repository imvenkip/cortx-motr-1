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

#include "be/tx_fom.h"
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
	INSERT_COUNT = 200,
	INSERT_SIZE  = 4
};

static void check(struct m0_be_btree *tree, struct m0_be_ut_h *h);
static struct m0_be_btree *create_tree(struct m0_be_ut_h *h);
static void destroy_tree(struct m0_be_btree *tree, struct m0_be_ut_h *h);

void m0_be_ut_btree_simple(void)
{
	struct m0_be_btree *tree0;
	struct m0_be_ut_h   h;

	M0_ENTRY();
	/* Init BE */
	m0_be_ut_h_init(&h);

	tree0 = create_tree(&h);

	M0_LOG(M0_INFO, "Reload segment...");
	m0_be_ut_h_seg_reload(&h);

	check(tree0, &h);
	destroy_tree(tree0, &h);

	m0_be_ut_h_fini(&h);

	M0_LEAVE();
}

static void shuffle_array(int a[], size_t n)
{
	if (n > 1) {
		int      i;
		uint64_t seed;

		seed = 123;
		for (i = 0; i < n - 1; ++i)
			M0_SWAP(a[i], a[m0_rnd(n, &seed)]);
	}
}

static void btree_delete(struct m0_be_btree *tree, struct m0_be_tx *tx)
{
	struct m0_be_op		 op;
	struct m0_buf		 key;
	struct m0_buf		 val;
	char			 k[INSERT_SIZE];
	char			 v[INSERT_SIZE];
	int			 rand_keys[INSERT_COUNT];
	int			 rc;
	int			 i;

	m0_buf_init(&key, k, sizeof k);
	m0_buf_init(&val, v, sizeof v);

	M0_LOG(M0_INFO, "Check error code...");
	sprintf(k, "%03d", INSERT_COUNT);
	m0_be_op_init(&op);
	m0_be_btree_delete(tree, tx, &op, &key);
	m0_be_op_wait(&op);
	M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	rc = op.bo_u.u_btree.t_rc;
	m0_be_op_fini(&op);
	M0_UT_ASSERT(rc == -ENOENT);

	btree_dbg_print(tree);

	M0_LOG(M0_INFO, "Delete all in random order...");
	for (i = 0; i < INSERT_COUNT; ++i)
		rand_keys[i] = i;
	shuffle_array(rand_keys, INSERT_COUNT);
	for (i = 0; i < INSERT_COUNT; ++i) {
		sprintf(k, "%03d", rand_keys[i]);
		M0_LOG(M0_DEBUG, "%03d: delete key=%s", i, (char*)k);

		m0_be_op_init(&op);
		m0_be_btree_delete(tree, tx, &op, &key);
		m0_be_op_wait(&op);
		M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
		rc = op.bo_u.u_btree.t_rc;
		m0_be_op_fini(&op);

		M0_UT_ASSERT(rc == 0);
	}

	M0_LOG(M0_INFO, "Make sure nothing is left...");
	for (i = 0; i < INSERT_COUNT; ++i) {
		sprintf(k, "%03d", i);

		m0_be_op_init(&op);
		m0_be_btree_delete(tree, tx, &op, &key);
		m0_be_op_wait(&op);
		M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
		rc = op.bo_u.u_btree.t_rc;
		m0_be_op_fini(&op);

		M0_UT_ASSERT(rc == -ENOENT);
	}

	M0_LOG(M0_INFO, "Insert back all...");
	for (i = 0; i < INSERT_COUNT; ++i) {
		sprintf(k, "%03d", i);
		sprintf(v, "%03d", i);

		m0_be_op_init(&op);
		m0_be_btree_insert(tree, tx, &op, &key, &val);
		m0_be_op_wait(&op);
		M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
		m0_be_op_fini(&op);
	}

	M0_LOG(M0_INFO, "Deleting [%03d, %03d)...", INSERT_COUNT/4,
						    INSERT_COUNT*3/4);
	for (i = INSERT_COUNT/4; i < INSERT_COUNT*3/4; ++i) {
		sprintf(k, "%03d", i);
		M0_LOG(M0_DEBUG, "delete key=%03d", i);

		m0_be_op_init(&op);
		m0_be_btree_delete(tree, tx, &op, &key);
		m0_be_op_wait(&op);
		M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
		m0_be_op_fini(&op);
	}
}

static struct m0_be_btree *create_tree(struct m0_be_ut_h *h)
{
	struct m0_be_allocator   *a = h->buh_allocator;
	struct m0_be_tx_credit    cred;
	struct m0_be_btree       *tree;
	struct m0_be_op           op;
	struct m0_be_tx          *tx;
	struct m0_buf             key;
	struct m0_buf             val;
	char                      k[INSERT_SIZE];
	char                      v[INSERT_SIZE];
	int                       rc;
	int                       i;

	M0_ENTRY();
	/*
	 * Init BE, BE IO, credits
	 */

	M0_ALLOC_PTR(tx);
	M0_UT_ASSERT(tx != NULL);

	m0_be_tx_credit_init(&cred);

	/*
	 * Init transaction and its credits
	 */
	m0_be_ut_h_tx_init(tx, h);

	{ /* XXX: should calculate these credits not for dummy tree,
	   but for allocated below. This needs at least two transactions. */
		struct m0_be_btree t = { .bb_seg = &h->buh_seg };
		m0_be_btree_create_credit(&t, 1, &cred);
		m0_be_btree_insert_credit(&t, INSERT_COUNT, INSERT_SIZE,
					INSERT_SIZE, &cred);
	}

	m0_be_allocator_credit(a, M0_BAO_ALLOC, sizeof *tree, 0, &cred);

	m0_sm_group_lock(&ut__txs_sm_group);

	m0_be_tx_prep(tx, &cred);

	M0_LOG(M0_INFO, "Transaction open...");
	m0_be_tx_open(tx);
	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_ACTIVE), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_LOG(M0_INFO, "Transaction has reached M0_BTS_ACTIVE state.");

	/* start */

	m0_be_op_init(&op);
	tree = m0_be_alloc(a, tx, &op, sizeof *tree, 0);
	m0_be_op_wait(&op);
	M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	m0_be_op_fini(&op);

	M0_SET0(tree); /* XXX Why do we need this?  --vvv */
	m0_be_btree_init(tree, &h->buh_seg, &kv_ops);

	m0_be_op_init(&op);
	m0_be_btree_create(tree, tx, &op);
	m0_be_op_wait(&op);
	M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	m0_be_op_fini(&op);

	m0_buf_init(&key, k, sizeof k);
	m0_buf_init(&val, v, sizeof v);
	M0_LOG(M0_INFO, "Inserting...");
	/* insert */
	for (i = 0; i < INSERT_COUNT/2; ++i) {
		sprintf(k, "%03d", i);
		sprintf(v, "%03d", i);

		m0_be_op_init(&op);
		m0_be_btree_insert(tree, tx, &op, &key, &val);
		m0_be_op_wait(&op);
		M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
		m0_be_op_fini(&op);
	}

	M0_LOG(M0_INFO, "Inserting inplace...");
	/* insert inplace */
	for (i = INSERT_COUNT/2; i < INSERT_COUNT; ++i) {
		struct m0_be_btree_anchor anchor;

		sprintf(k, "%03d", i);

		anchor.ba_value.b_nob = sizeof v;
		m0_be_op_init(&op);
		m0_be_btree_insert_inplace(tree, tx, &op, &key, &anchor);
		m0_be_op_wait(&op);
		M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
		m0_be_op_fini(&op);

		/* update value */
		sprintf(anchor.ba_value.b_addr, "%03d", i);

		m0_be_btree_release(tree, tx, &anchor);
	}
	btree_dbg_print(tree);

	btree_delete(tree, tx);

	M0_LOG(M0_INFO, "Updating...");
	sprintf(k, "%03d", INSERT_COUNT - 1);
	sprintf(v, "XYZ");

	m0_be_op_init(&op);
	m0_be_btree_update(tree, tx, &op, &key, &val);
	m0_be_op_wait(&op);
	M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	m0_be_op_fini(&op);

	M0_LOG(M0_INFO, "Transaction close...");
	m0_be_tx_close(tx); /* Make things persistent. */

	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_PLACED), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_LOG(M0_INFO, "Transaction has reached M0_BTS_PLACED state.");

	/* XXX TODO: m0_be_tx_stable(tx) */

	m0_sm_group_unlock(&ut__txs_sm_group);

	btree_dbg_print(tree);

	M0_LEAVE();
	return tree;
}

static void destroy_tree(struct m0_be_btree *tree, struct m0_be_ut_h *h)
{
	struct m0_be_allocator   *a = h->buh_allocator;
	struct m0_be_tx_credit    cred;
	struct m0_be_op           op;
	struct m0_be_tx          *tx;
	int                       rc;

	M0_ENTRY();

	M0_ALLOC_PTR(tx);
	M0_UT_ASSERT(tx != NULL);

	m0_be_ut_h_tx_init(tx, h);

	m0_be_tx_credit_init(&cred);
	m0_be_btree_destroy_credit(tree, 1, &cred);
	m0_be_allocator_credit(a, M0_BAO_FREE, sizeof *tree, 0, &cred);

	m0_sm_group_lock(&ut__txs_sm_group);

	m0_be_tx_prep(tx, &cred);
	M0_LOG(M0_INFO, "Transaction open...");
	m0_be_tx_open(tx);
	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_ACTIVE), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_LOG(M0_INFO, "Transaction has reached M0_BTS_ACTIVE state.");

	M0_LOG(M0_INFO, "Btree %p destroy...", tree);
	m0_be_op_init(&op);
	m0_be_btree_destroy(tree, tx, &op);
	m0_be_op_wait(&op);
	M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	m0_be_op_fini(&op);

	m0_be_op_init(&op);
	m0_be_free(a, tx, &op, tree);
	m0_be_op_wait(&op);
	M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	m0_be_op_fini(&op);

	M0_LOG(M0_INFO, "Transaction close...");
	m0_be_tx_close(tx); /* Make things persistent. */

	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_PLACED), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_LOG(M0_INFO, "Transaction has reached M0_BTS_PLACED state.");

	/* XXX TODO: m0_be_tx_stable(tx) */

	m0_sm_group_unlock(&ut__txs_sm_group);

	btree_dbg_print(tree);

	M0_LEAVE();
}

static void cursor_test(struct m0_be_btree *tree)
{
	struct m0_be_btree_cursor cursor;
	struct m0_buf		  start;
	struct m0_buf		  key;
	struct m0_buf		  val;
	char                      sbuf[INSERT_SIZE];
	int                       v;
	int                       i;
	int                       rc;

	start = M0_BUF_INIT(sizeof sbuf, sbuf);

	m0_be_btree_cursor_init(&cursor, tree);

	sprintf(sbuf, "%03d", INSERT_COUNT/2);
	rc = m0_be_btree_cursor_get_sync(&cursor, &start, true);
	M0_UT_ASSERT(rc == 0);

	m0_be_btree_cursor_kv_get(&cursor, &key, &val);

	for (i = 0; i < INSERT_COUNT/4; ++i) {
		v = atoi(key.b_addr);
		M0_LOG(M0_DEBUG, "i=%i k=%d", i, v);
		M0_UT_ASSERT(v == i + INSERT_COUNT*3/4);

		m0_be_op_init(&cursor.bc_op);
		m0_be_btree_cursor_next(&cursor);
		m0_be_op_wait(&cursor.bc_op);
		M0_UT_ASSERT(m0_be_op_state(&cursor.bc_op) == M0_BOS_SUCCESS);
		rc = cursor.bc_op.bo_u.u_btree.t_rc;
		m0_be_op_fini(&cursor.bc_op);
		if (i < INSERT_COUNT/4 - 1)
			M0_UT_ASSERT(rc == 0);

		m0_be_btree_cursor_kv_get(&cursor, &key, &val);
	}

	M0_UT_ASSERT(key.b_addr == NULL);
	M0_UT_ASSERT(rc == -ENOENT);

	sprintf(sbuf, "%03d", INSERT_COUNT/4 - 1);
	rc = m0_be_btree_cursor_get_sync(&cursor, &start, false);
	M0_UT_ASSERT(rc == 0);

	m0_be_btree_cursor_kv_get(&cursor, &key, &val);

	for (i = INSERT_COUNT/4 - 1; i >= 0; --i) {
		v = atoi(key.b_addr);
		M0_LOG(M0_DEBUG, "i=%i k=%d", i, v);
		M0_UT_ASSERT(v == i);

		m0_be_op_init(&cursor.bc_op);
		m0_be_btree_cursor_prev(&cursor);
		m0_be_op_wait(&cursor.bc_op);
		M0_UT_ASSERT(m0_be_op_state(&cursor.bc_op) == M0_BOS_SUCCESS);
		rc = cursor.bc_op.bo_u.u_btree.t_rc;
		m0_be_op_fini(&cursor.bc_op);
		if (i > 0)
			M0_UT_ASSERT(rc == 0);

		m0_be_btree_cursor_kv_get(&cursor, &key, &val);
	}

	M0_UT_ASSERT(key.b_addr == NULL);
	M0_UT_ASSERT(rc == -ENOENT);

	sprintf(sbuf, "%03d", INSERT_COUNT);
	rc = m0_be_btree_cursor_get_sync(&cursor, &start, true);
	M0_UT_ASSERT(rc == -ENOENT);

	rc = m0_be_btree_cursor_last_sync(&cursor);
	M0_UT_ASSERT(rc == 0);
	m0_be_btree_cursor_kv_get(&cursor, &key, NULL);
	sprintf(sbuf, "%03d", INSERT_COUNT -1);
	M0_UT_ASSERT(strcmp(key.b_addr, sbuf) == 0);

	rc = m0_be_btree_cursor_first_sync(&cursor);
	M0_UT_ASSERT(rc == 0);
	m0_be_btree_cursor_kv_get(&cursor, &key, &val);
	M0_UT_ASSERT(strcmp(key.b_addr, "000") == 0 &&
		     strcmp(val.b_addr, key.b_addr) == 0);

	m0_be_btree_cursor_fini(&cursor);
}

static void check(struct m0_be_btree *tree, struct m0_be_ut_h *h)
{
	struct m0_buf             key;
	struct m0_buf             val;
	struct m0_be_op           op;
	char k[INSERT_SIZE];
	char v[INSERT_SIZE];
	int i;

	m0_be_btree_init(tree, &h->buh_seg, &kv_ops);

	m0_buf_init(&key, k, ARRAY_SIZE(k));
	m0_buf_init(&val, v, ARRAY_SIZE(v));

	/* lookup */
	for (i = 0; i < INSERT_COUNT; ++i) {
		sprintf(k, "%03d", i);

		m0_be_op_init(&op);
		m0_be_btree_lookup(tree, &op, &key, &val);
		m0_be_op_wait(&op);
		M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
		m0_be_op_fini(&op);

		if (INSERT_COUNT/4 <= i && i < INSERT_COUNT*3/4)
			M0_UT_ASSERT(op.bo_u.u_btree.t_rc == -ENOENT);
		else if (i == INSERT_COUNT - 1)
			M0_UT_ASSERT(strcmp(v, "XYZ") == 0);
		else
			M0_UT_ASSERT(strcmp(v, k) == 0);
	}

	/* lookup inplace */
	for (i = 0; i < INSERT_COUNT; ++i) {
		struct m0_be_btree_anchor anchor;

		sprintf(k, "%03d", i);

		m0_be_op_init(&op);
		m0_be_btree_lookup_inplace(tree, &op, &key, &anchor);
		m0_be_op_wait(&op);
		M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
		m0_be_op_fini(&op);

		val = anchor.ba_value;
		if (INSERT_COUNT/4 <= i && i < INSERT_COUNT*3/4)
			M0_UT_ASSERT(op.bo_u.u_btree.t_rc == -ENOENT);
		else if (i == INSERT_COUNT - 1)
			M0_UT_ASSERT(strcmp(val.b_addr, "XYZ") == 0);
		else
			M0_UT_ASSERT(strcmp(val.b_addr, k) == 0);

		m0_be_btree_release(tree, NULL, &anchor);
	}

	m0_be_op_init(&op);
	m0_be_btree_minkey(tree, &op, &key);
	m0_be_op_wait(&op);
	M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	m0_be_op_fini(&op);
	M0_UT_ASSERT(strcmp(key.b_addr, "000") == 0);

	m0_be_op_init(&op);
	m0_be_btree_maxkey(tree, &op, &key);
	m0_be_op_wait(&op);
	M0_UT_ASSERT(m0_be_op_state(&op) == M0_BOS_SUCCESS);
	m0_be_op_fini(&op);
	sprintf(k, "%03d", INSERT_COUNT - 1);
	M0_UT_ASSERT(strcmp(key.b_addr, k) == 0);

	cursor_test(tree);
	btree_dbg_print(tree);
	m0_be_btree_fini(tree);
}

#undef M0_TRACE_SUBSYSTEM
