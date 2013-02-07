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

#include "lib/arith.h"    /* M0_3WAY */
#include "lib/types.h"
#include "lib/ut.h"
#include "lib/ub.h"
#include "db/db.h"

static const char db_name[] = "ut-db";
static const char test_table[] = "test-table";

static void test_db_create(void)
{
	struct m0_dbenv db;
	int             result;

	result = m0_dbenv_init(&db, db_name, 0);
	M0_UT_ASSERT(result == 0);
	m0_dbenv_fini(&db);
}

static int test_key_cmp(struct m0_table *table,
			const void *key0, const void *key1)
{
	const uint64_t *u0 = key0;
	const uint64_t *u1 = key1;

	return M0_3WAY(*u0, *u1);
}

static const struct m0_table_ops test_table_ops = {
	.to = {
		[TO_KEY] = { .max_size = 8 },
		[TO_REC] = { .max_size = 8 }
	},
	.key_cmp = test_key_cmp
};

static int db_reset(void)
{
        return m0_ut_db_reset(db_name);
}

static void dbut_init(const char *db_name,
                      const char *test_table,
                      struct m0_dbenv *db,
                      struct m0_table *table,
                      struct m0_db_tx *tx)
{
        int result;

	result = m0_dbenv_init(db, db_name, 0);
        M0_UT_ASSERT(result == 0);

        result = m0_table_init(table, db, test_table, 0, &test_table_ops);
        M0_UT_ASSERT(result == 0);

        result = m0_db_tx_init(tx, db, 0);
        M0_UT_ASSERT(result == 0);
}

static void dbut_fini(struct m0_dbenv *db,
                      struct m0_table *table,
                      struct m0_db_tx *tx,
                      int (*tx_end)(struct m0_db_tx *))
{
        int result;

	result = tx_end(tx);
	M0_UT_ASSERT(result == 0);

	m0_table_fini(table);
	m0_dbenv_fini(db);
}

static void test_table_create(void)
{
	struct m0_dbenv db;
	struct m0_table table;
	int             result;

	result = m0_dbenv_init(&db, db_name, 0);
	M0_UT_ASSERT(result == 0);

	result = m0_table_init(&table, &db, test_table, 0, &test_table_ops);
	M0_UT_ASSERT(result == 0);

	m0_table_fini(&table);
	m0_dbenv_fini(&db);
}

static void test_lookup(void)
{
	struct m0_dbenv   db;
	struct m0_db_tx   tx;
	struct m0_table   table;
	struct m0_db_pair cons;
	int               result;
	uint64_t          key;
	uint64_t          rec;

        dbut_init(db_name, test_table, &db, &table, &tx);

	key = 42;
	m0_db_pair_setup(&cons, &table, &key, sizeof key, &rec, sizeof rec);
	result = m0_table_lookup(&tx, &cons);
	M0_UT_ASSERT(result == -ENOENT);

	m0_db_pair_fini(&cons);
        dbut_fini(&db, &table, &tx, &m0_db_tx_commit);
}

static void test_insert(void)
{
	struct m0_dbenv   db;
	struct m0_db_tx   tx;
	struct m0_table   table;
	struct m0_db_pair cons;
	struct m0_db_pair cons1;
	int               result;
	uint64_t          key;
	uint64_t          rec;
	uint64_t          rec_out;

        dbut_init(db_name, test_table, &db, &table, &tx);

	key = 42;
	rec = 16;
	m0_db_pair_setup(&cons, &table, &key, sizeof key, &rec, sizeof rec);

	result = m0_table_insert(&tx, &cons);
	M0_UT_ASSERT(result == 0);

	m0_db_pair_setup(&cons1, &table, &key, sizeof key,
			 &rec_out, sizeof rec_out);

	result = m0_table_lookup(&tx, &cons1);
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(rec_out == rec);

	m0_db_pair_fini(&cons1);
	m0_db_pair_fini(&cons);
        dbut_fini(&db, &table, &tx, &m0_db_tx_commit);

	/* and look up again */

        dbut_init(db_name, test_table, &db, &table, &tx);

	m0_db_pair_setup(&cons1, &table, &key, sizeof key,
			 &rec_out, sizeof rec_out);
	result = m0_table_lookup(&tx, &cons1);
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(rec_out == rec);

	m0_db_pair_fini(&cons1);
        dbut_fini(&db, &table, &tx, &m0_db_tx_commit);
}

static void test_delete(void)
{
	struct m0_dbenv   db;
	struct m0_db_tx   tx;
	struct m0_table   table;
	struct m0_db_pair cons;
	struct m0_db_pair cons1;
	int               result;
	uint64_t          key;
	uint64_t          rec;
	uint64_t          rec_out;

        dbut_init(db_name, test_table, &db, &table, &tx);

	key = 43;
	rec = 17;

	m0_db_pair_setup(&cons, &table, &key, sizeof key, &rec, sizeof rec);
	m0_db_pair_setup(&cons1, &table, &key, sizeof key,
			 &rec_out, sizeof rec_out);

	result = m0_table_insert(&tx, &cons);
	M0_UT_ASSERT(result == 0);

	result = m0_table_lookup(&tx, &cons1);
	M0_UT_ASSERT(result == 0);
	M0_UT_ASSERT(rec_out == rec);

	m0_db_pair_release(&cons1);
	m0_db_pair_release(&cons);

	result = m0_table_delete(&tx, &cons);
	M0_UT_ASSERT(result == 0);
	result = m0_table_lookup(&tx, &cons1);
	M0_UT_ASSERT(result == -ENOENT);

	m0_db_pair_fini(&cons1);
	m0_db_pair_fini(&cons);

        dbut_fini(&db, &table, &tx, &m0_db_tx_commit);
}

static void test_abort(void)
{
	struct m0_dbenv   db;
	struct m0_db_tx   tx;
	struct m0_table   table;
	struct m0_db_pair cons;
	int               result;
	uint64_t          key;
	uint64_t          rec;

        dbut_init(db_name, test_table, &db, &table, &tx);

	key = 44;
	rec = 18;
	m0_db_pair_setup(&cons, &table, &key, sizeof key, &rec, sizeof rec);

	result = m0_table_insert(&tx, &cons);
	M0_UT_ASSERT(result == 0);

	m0_db_pair_fini(&cons);
        dbut_fini(&db, &table, &tx, &m0_db_tx_abort);

        dbut_init(db_name, test_table, &db, &table, &tx);

	m0_db_pair_setup(&cons, &table, &key, sizeof key, &rec, sizeof rec);
	result = m0_table_lookup(&tx, &cons);
	M0_UT_ASSERT(result == -ENOENT);

	m0_db_pair_fini(&cons);
        dbut_fini(&db, &table, &tx, &m0_db_tx_commit);
}

static void test_waiter(void)
{
	struct m0_dbenv        db;
	struct m0_db_tx        tx;
	struct m0_table        table;
	struct m0_db_pair      cons;
	int                    result;
	uint64_t               key;
	uint64_t               rec;
	int                    wflag;
	struct m0_db_tx_waiter wait;

        dbut_init(db_name, test_table, &db, &table, &tx);

	key = 45;
	rec = 19;
	m0_db_pair_setup(&cons, &table, &key, sizeof key, &rec, sizeof rec);

	result = m0_table_insert(&tx, &cons);
	M0_UT_ASSERT(result == 0);

	wflag = 0;
	wait.tw_abort = LAMBDA(void, (struct m0_db_tx_waiter *w) {
			M0_UT_ASSERT(w == &wait);
			wflag = 1;
		});
	wait.tw_commit = LAMBDA(void, (struct m0_db_tx_waiter *w) {
			M0_UT_ASSERT(false);
		});
	wait.tw_persistent = LAMBDA(void, (struct m0_db_tx_waiter *w) {
			M0_UT_ASSERT(false);
		});
	wait.tw_done = LAMBDA(void, (struct m0_db_tx_waiter *w) {
			M0_UT_ASSERT(wflag == 1);
			wflag = 2;
		});
	m0_db_tx_waiter_add(&tx, &wait);

	m0_db_pair_fini(&cons);
        dbut_fini(&db, &table, &tx, &m0_db_tx_abort);

	M0_UT_ASSERT(wflag == 2);

        dbut_init(db_name, test_table, &db, &table, &tx);

	m0_db_pair_setup(&cons, &table, &key, sizeof key, &rec, sizeof rec);
	result = m0_table_insert(&tx, &cons);
	M0_UT_ASSERT(result == 0);

	wflag = 0;
	wait.tw_abort = LAMBDA(void, (struct m0_db_tx_waiter *w) {
			M0_UT_ASSERT(false);
		});
	wait.tw_commit = LAMBDA(void, (struct m0_db_tx_waiter *w) {
			M0_UT_ASSERT(w == &wait);
			wflag = 1;
		});
	wait.tw_persistent = LAMBDA(void, (struct m0_db_tx_waiter *w) {
			M0_UT_ASSERT(wflag == 1);
			wflag = 2;
		});
	wait.tw_done = LAMBDA(void, (struct m0_db_tx_waiter *w) {
			M0_UT_ASSERT(wflag == 2);
			wflag = 3;
		});
	m0_db_tx_waiter_add(&tx, &wait);

	m0_db_pair_fini(&cons);
        dbut_fini(&db, &table, &tx, &m0_db_tx_commit);
	M0_UT_ASSERT(wflag == 3);
}

const struct m0_test_suite db_ut = {
	.ts_name = "libdb-ut",
	.ts_init = db_reset,
	.ts_fini = db_reset,
	.ts_tests = {
		{ "db-create", test_db_create },
		{ "table-create", test_table_create },
		{ "lookup", test_lookup },
		{ "insert", test_insert },
		{ "delete", test_delete },
		{ "abort", test_abort },
		{ "waiter", test_waiter },
		{ NULL, NULL }
	}
};

/*
 * Test Suite db-cursor-ut.
 */

/*
 * This test is positive test case.
 * First transaction initialise cursor in Read Only purpose.
 * Second transaction tries to get cursor record for Read
 * Only purpose which is already locked by first transaction.
 */
static void test_cursor_flags_read_only(void)
{
	struct m0_dbenv     db;
	struct m0_db_tx     tx1;
	struct m0_db_tx     tx2;
	struct m0_table     table1;
	struct m0_table     table2;
	struct m0_db_pair   pair1;
	struct m0_db_pair   pair2;
	struct m0_db_cursor cursor1;
	struct m0_db_cursor cursor2;
	int                 result;
	uint64_t            key;
	uint64_t            rec;

        dbut_init(db_name, test_table, &db, &table1, &tx1);

	key = 11;
	rec = 16;
	m0_db_pair_setup(&pair1, &table1, &key, sizeof key, &rec, sizeof rec);

	result = m0_table_insert(&tx1, &pair1);
	M0_UT_ASSERT(result == 0);

	m0_db_pair_fini(&pair1);
        dbut_fini(&db, &table1, &tx1, &m0_db_tx_commit);

        /* Get readonly cursor */
        dbut_init(db_name, test_table, &db, &table1, &tx1);

	result = m0_db_cursor_init(&cursor1, &table1, &tx1, 0);
	M0_UT_ASSERT(result == 0);

	key = 11;
	m0_db_pair_setup(&pair1, &table1, &key, sizeof key, &rec, sizeof rec);
	result = m0_db_cursor_get(&cursor1, &pair1);
	M0_UT_ASSERT(result == 0);

        /*
         * Now initialise read only cursor on same table and in same trasaction
         * where table already having read-only cursor.
         */
	result = m0_table_init(&table2, &db, test_table, 0, &test_table_ops);
	M0_UT_ASSERT(result == 0);

        /* lock will not blocks */
	result = m0_db_tx_init(&tx2, &db, DB_TXN_NOWAIT);
	M0_UT_ASSERT(result == 0);

        result = m0_db_cursor_init(&cursor2, &table2, &tx2, 0);
	M0_UT_ASSERT(result == 0);

	key = 11;
	m0_db_pair_setup(&pair2, &table2, &key, sizeof key, &rec, sizeof rec);
	result = m0_db_cursor_get(&cursor2, &pair2);
	M0_UT_ASSERT(result == 0);

        m0_db_cursor_fini(&cursor2);
	m0_db_pair_fini(&pair2);

	m0_db_cursor_fini(&cursor1);
	m0_db_pair_fini(&pair1);

	result = m0_db_tx_commit(&tx2);
	M0_UT_ASSERT(result == 0);
	m0_table_fini(&table2);

        dbut_fini(&db, &table1, &tx1, &m0_db_tx_commit);
}

const struct m0_test_suite db_cursor_ut = {
	.ts_name = "db-cursor-ut",
	.ts_init = db_reset,
	.ts_fini = db_reset,
	.ts_tests = {
		{ "cursor_flag_read_only", test_cursor_flags_read_only },
		{ NULL, NULL }
	}
};

/*
 * UB
 */

enum {
	UB_ITER = 200000,
	UB_ITER_TX = 10000
};

static struct m0_dbenv     ub_db;
static struct m0_table     ub_table;
static struct m0_db_tx     ub_tx;
static struct m0_db_pair   ub_pair;
static struct m0_db_cursor ub_cur;
static uint64_t key;
static uint64_t rec;

static void ub_init(void)
{
	int result;

	db_reset();

	result = m0_dbenv_init(&ub_db, db_name, 0);
	M0_ASSERT(result == 0);

	result = m0_table_init(&ub_table, &ub_db, test_table, 0,
			       &test_table_ops);
	M0_ASSERT(result == 0);

	result = m0_db_tx_init(&ub_tx, &ub_db, 0);
	M0_ASSERT(result == 0);

	m0_db_pair_setup(&ub_pair, &ub_table,
			 &key, sizeof key, &rec, sizeof rec);
}

static void ub_fini(void)
{
	int result;

	result = m0_db_tx_commit(&ub_tx);
	M0_ASSERT(result == 0);

	m0_db_pair_fini(&ub_pair);
	m0_table_fini(&ub_table);
	m0_dbenv_fini(&ub_db);
	db_reset();
}

static void checkpoint()
{
	int result;

	result = m0_db_tx_commit(&ub_tx);
	M0_ASSERT(result == 0);

	result = m0_db_tx_init(&ub_tx, &ub_db, 0);
	M0_ASSERT(result == 0);
}

static void ub_insert(int i)
{
	int      result;

	key = i;
	rec = key*key;

	result = m0_table_insert(&ub_tx, &ub_pair);
	M0_ASSERT(result == 0);

	if (i%1000)
		checkpoint();
}

static void ub_lookup(int i)
{
	int       result;

	key = i;
	result = m0_table_lookup(&ub_tx, &ub_pair);
	M0_ASSERT(result == 0);
	M0_ASSERT(rec == key*key);
	m0_db_pair_release(&ub_pair);

	if (i%1000)
		checkpoint();
}

static void ub_delete(int i)
{
	int      result;

	key = i;

	result = m0_table_delete(&ub_tx, &ub_pair);
	M0_ASSERT(result == 0);

	if (i%1000)
		checkpoint();
}

static void ub_iterate_init(void)
{
	int      result;

	result = m0_db_cursor_init(&ub_cur, &ub_table, &ub_tx, 0);
	M0_ASSERT(result == 0);
	key = 0;
	result = m0_db_cursor_get(&ub_cur, &ub_pair);
	M0_ASSERT(rec == 0*0);
}

static void ub_iterate(int i)
{
	int result;

	result = m0_db_cursor_next(&ub_cur, &ub_pair);
	M0_ASSERT((result ==       0) == (i != UB_ITER - 1));
	M0_ASSERT((result == -ENOENT) == (i == UB_ITER - 1));
	M0_ASSERT(ergo(result == 0, key == i + 1));
	M0_ASSERT(ergo(result == 0, rec == key * key));
}

static void ub_iterate_fini(void)
{
	m0_db_cursor_fini(&ub_cur);
}

static void ub_iterate_back_init(void)
{
	int      result;

	result = m0_db_cursor_init(&ub_cur, &ub_table, &ub_tx, 0);
	M0_ASSERT(result == 0);
	key = UB_ITER - 1;
	result = m0_db_cursor_get(&ub_cur, &ub_pair);
	M0_ASSERT(key == UB_ITER - 1);
	M0_ASSERT(rec == key * key);
}

static void ub_iterate_back(int i)
{
	int result;

	result = m0_db_cursor_prev(&ub_cur, &ub_pair);
	M0_ASSERT((result ==       0) == (i != UB_ITER - 1));
	M0_ASSERT((result == -ENOENT) == (i == UB_ITER - 1));
	M0_ASSERT(ergo(result == 0, key == UB_ITER - 2 - i));
	M0_ASSERT(ergo(result == 0, rec == key * key));
}

static void ub_iterate_back_fini(void)
{
	m0_db_cursor_fini(&ub_cur);
}

struct m0_ub_set m0_db_ub = {
	.us_name = "db-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = {
		{ .ub_name = "insert",
		  .ub_iter = UB_ITER,
		  .ub_round = ub_insert },

		{ .ub_name = "lookup",
		  .ub_iter = UB_ITER,
		  .ub_round = ub_lookup },

		{ .ub_name  = "iterate",
		  .ub_iter  = UB_ITER_TX,
		  .ub_init  = ub_iterate_init,
		  .ub_round = ub_iterate,
		  .ub_fini  = ub_iterate_fini },

		{ .ub_name  = "iterate-back",
		  .ub_iter  = UB_ITER_TX,
		  .ub_init  = ub_iterate_back_init,
		  .ub_round = ub_iterate_back,
		  .ub_fini  = ub_iterate_back_fini },

		{ .ub_name = "delete",
		  .ub_iter = UB_ITER,
		  .ub_round = ub_delete },

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
