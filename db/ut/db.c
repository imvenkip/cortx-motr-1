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

#include "lib/arith.h"    /* C2_3WAY */
#include "lib/types.h"
#include "lib/ut.h"
#include "lib/ub.h"
#include "db/db.h"

static const char db_name[] = "ut-db";
static const char test_table[] = "test-table";

static void test_db_create(void)
{
	struct c2_dbenv db;
	int             result;

	result = c2_dbenv_init(&db, db_name, 0);
	C2_UT_ASSERT(result == 0);
	c2_dbenv_fini(&db);
}

static int test_key_cmp(struct c2_table *table,
			const void *key0, const void *key1)
{
	const uint64_t *u0 = key0;
	const uint64_t *u1 = key1;

	return C2_3WAY(*u0, *u1);
}

static const struct c2_table_ops test_table_ops = {
	.to = {
		[TO_KEY] = { .max_size = 8 },
		[TO_REC] = { .max_size = 8 }
	},
	.key_cmp = test_key_cmp
};

static int db_reset(void)
{
        return c2_ut_db_reset(db_name);
}

static void dbut_init(const char *db_name,
                      const char *test_table,
                      struct c2_dbenv *db,
                      struct c2_table *table,
                      struct c2_db_tx *tx)
{
        int result;

	result = c2_dbenv_init(db, db_name, 0);
        C2_UT_ASSERT(result == 0);

        result = c2_table_init(table, db, test_table, 0, &test_table_ops);
        C2_UT_ASSERT(result == 0);

        result = c2_db_tx_init(tx, db, 0);
        C2_UT_ASSERT(result == 0);
}

static void dbut_fini(struct c2_dbenv *db,
                      struct c2_table *table,
                      struct c2_db_tx *tx,
                      int (*tx_end)(struct c2_db_tx *))
{
        int result;

	result = tx_end(tx);
	C2_UT_ASSERT(result == 0);

	c2_table_fini(table);
	c2_dbenv_fini(db);
}

static void test_table_create(void)
{
	struct c2_dbenv db;
	struct c2_table table;
	int             result;

	result = c2_dbenv_init(&db, db_name, 0);
	C2_UT_ASSERT(result == 0);

	result = c2_table_init(&table, &db, test_table, 0, &test_table_ops);
	C2_UT_ASSERT(result == 0);

	c2_table_fini(&table);
	c2_dbenv_fini(&db);
}

static void test_lookup(void)
{
	struct c2_dbenv   db;
	struct c2_db_tx   tx;
	struct c2_table   table;
	struct c2_db_pair cons;
	int               result;
	uint64_t          key;
	uint64_t          rec;

        dbut_init(db_name, test_table, &db, &table, &tx);

	key = 42;
	c2_db_pair_setup(&cons, &table, &key, sizeof key, &rec, sizeof rec);
	result = c2_table_lookup(&tx, &cons);
	C2_UT_ASSERT(result == -ENOENT);

	c2_db_pair_fini(&cons);
        dbut_fini(&db, &table, &tx, &c2_db_tx_commit);
}

static void test_insert(void)
{
	struct c2_dbenv   db;
	struct c2_db_tx   tx;
	struct c2_table   table;
	struct c2_db_pair cons;
	struct c2_db_pair cons1;
	int               result;
	uint64_t          key;
	uint64_t          rec;
	uint64_t          rec_out;

        dbut_init(db_name, test_table, &db, &table, &tx);

	key = 42;
	rec = 16;
	c2_db_pair_setup(&cons, &table, &key, sizeof key, &rec, sizeof rec);

	result = c2_table_insert(&tx, &cons);
	C2_UT_ASSERT(result == 0);

	c2_db_pair_setup(&cons1, &table, &key, sizeof key,
			 &rec_out, sizeof rec_out);

	result = c2_table_lookup(&tx, &cons1);
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(rec_out == rec);

	c2_db_pair_fini(&cons1);
	c2_db_pair_fini(&cons);
        dbut_fini(&db, &table, &tx, &c2_db_tx_commit);

	/* and look up again */

        dbut_init(db_name, test_table, &db, &table, &tx);

	c2_db_pair_setup(&cons1, &table, &key, sizeof key,
			 &rec_out, sizeof rec_out);
	result = c2_table_lookup(&tx, &cons1);
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(rec_out == rec);

	c2_db_pair_fini(&cons1);
        dbut_fini(&db, &table, &tx, &c2_db_tx_commit);
}

static void test_delete(void)
{
	struct c2_dbenv   db;
	struct c2_db_tx   tx;
	struct c2_table   table;
	struct c2_db_pair cons;
	struct c2_db_pair cons1;
	int               result;
	uint64_t          key;
	uint64_t          rec;
	uint64_t          rec_out;

        dbut_init(db_name, test_table, &db, &table, &tx);

	key = 43;
	rec = 17;

	c2_db_pair_setup(&cons, &table, &key, sizeof key, &rec, sizeof rec);
	c2_db_pair_setup(&cons1, &table, &key, sizeof key,
			 &rec_out, sizeof rec_out);

	result = c2_table_insert(&tx, &cons);
	C2_UT_ASSERT(result == 0);

	result = c2_table_lookup(&tx, &cons1);
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(rec_out == rec);

	c2_db_pair_release(&cons1);
	c2_db_pair_release(&cons);

	result = c2_table_delete(&tx, &cons);
	C2_UT_ASSERT(result == 0);
	result = c2_table_lookup(&tx, &cons1);
	C2_UT_ASSERT(result == -ENOENT);

	c2_db_pair_fini(&cons1);
	c2_db_pair_fini(&cons);

        dbut_fini(&db, &table, &tx, &c2_db_tx_commit);
}

static void test_abort(void)
{
	struct c2_dbenv   db;
	struct c2_db_tx   tx;
	struct c2_table   table;
	struct c2_db_pair cons;
	int               result;
	uint64_t          key;
	uint64_t          rec;

        dbut_init(db_name, test_table, &db, &table, &tx);

	key = 44;
	rec = 18;
	c2_db_pair_setup(&cons, &table, &key, sizeof key, &rec, sizeof rec);

	result = c2_table_insert(&tx, &cons);
	C2_UT_ASSERT(result == 0);

	c2_db_pair_fini(&cons);
        dbut_fini(&db, &table, &tx, &c2_db_tx_abort);

        dbut_init(db_name, test_table, &db, &table, &tx);

	c2_db_pair_setup(&cons, &table, &key, sizeof key, &rec, sizeof rec);
	result = c2_table_lookup(&tx, &cons);
	C2_UT_ASSERT(result == -ENOENT);

	c2_db_pair_fini(&cons);
        dbut_fini(&db, &table, &tx, &c2_db_tx_commit);
}

static void test_waiter(void)
{
	struct c2_dbenv        db;
	struct c2_db_tx        tx;
	struct c2_table        table;
	struct c2_db_pair      cons;
	int                    result;
	uint64_t               key;
	uint64_t               rec;
	int                    wflag;
	struct c2_db_tx_waiter wait;

        dbut_init(db_name, test_table, &db, &table, &tx);

	key = 45;
	rec = 19;
	c2_db_pair_setup(&cons, &table, &key, sizeof key, &rec, sizeof rec);

	result = c2_table_insert(&tx, &cons);
	C2_UT_ASSERT(result == 0);

	wflag = 0;
	wait.tw_abort = LAMBDA(void, (struct c2_db_tx_waiter *w) {
			C2_UT_ASSERT(w == &wait);
			wflag = 1;
		});
	wait.tw_commit = LAMBDA(void, (struct c2_db_tx_waiter *w) {
			C2_UT_ASSERT(false);
		});
	wait.tw_persistent = LAMBDA(void, (struct c2_db_tx_waiter *w) {
			C2_UT_ASSERT(false);
		});
	wait.tw_done = LAMBDA(void, (struct c2_db_tx_waiter *w) {
			C2_UT_ASSERT(wflag == 1);
			wflag = 2;
		});
	c2_db_tx_waiter_add(&tx, &wait);

	c2_db_pair_fini(&cons);
        dbut_fini(&db, &table, &tx, &c2_db_tx_abort);

	C2_UT_ASSERT(wflag == 2);

        dbut_init(db_name, test_table, &db, &table, &tx);

	c2_db_pair_setup(&cons, &table, &key, sizeof key, &rec, sizeof rec);
	result = c2_table_insert(&tx, &cons);
	C2_UT_ASSERT(result == 0);

	wflag = 0;
	wait.tw_abort = LAMBDA(void, (struct c2_db_tx_waiter *w) {
			C2_UT_ASSERT(false);
		});
	wait.tw_commit = LAMBDA(void, (struct c2_db_tx_waiter *w) {
			C2_UT_ASSERT(w == &wait);
			wflag = 1;
		});
	wait.tw_persistent = LAMBDA(void, (struct c2_db_tx_waiter *w) {
			C2_UT_ASSERT(wflag == 1);
			wflag = 2;
		});
	wait.tw_done = LAMBDA(void, (struct c2_db_tx_waiter *w) {
			C2_UT_ASSERT(wflag == 2);
			wflag = 3;
		});
	c2_db_tx_waiter_add(&tx, &wait);

	c2_db_pair_fini(&cons);
        dbut_fini(&db, &table, &tx, &c2_db_tx_commit);
	C2_UT_ASSERT(wflag == 3);
}

const struct c2_test_suite db_ut = {
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
	struct c2_dbenv     db;
	struct c2_db_tx     tx1;
	struct c2_db_tx     tx2;
	struct c2_table     table1;
	struct c2_table     table2;
	struct c2_db_pair   pair1;
	struct c2_db_pair   pair2;
	struct c2_db_cursor cursor1;
	struct c2_db_cursor cursor2;
	int                 result;
	uint64_t            key;
	uint64_t            rec;

        dbut_init(db_name, test_table, &db, &table1, &tx1);

	key = 11;
	rec = 16;
	c2_db_pair_setup(&pair1, &table1, &key, sizeof key, &rec, sizeof rec);

	result = c2_table_insert(&tx1, &pair1);
	C2_UT_ASSERT(result == 0);

	c2_db_pair_fini(&pair1);
        dbut_fini(&db, &table1, &tx1, &c2_db_tx_commit);

        /* Get readonly cursor */
        dbut_init(db_name, test_table, &db, &table1, &tx1);

	result = c2_db_cursor_init(&cursor1, &table1, &tx1, 0);
	C2_UT_ASSERT(result == 0);

	key = 11;
	c2_db_pair_setup(&pair1, &table1, &key, sizeof key, &rec, sizeof rec);
	result = c2_db_cursor_get(&cursor1, &pair1);
	C2_UT_ASSERT(result == 0);

        /*
         * Now initialise read only cursor on same table and in same trasaction
         * where table already having read-only cursor.
         */
	result = c2_table_init(&table2, &db, test_table, 0, &test_table_ops);
	C2_UT_ASSERT(result == 0);

        /* lock will not blocks */
	result = c2_db_tx_init(&tx2, &db, DB_TXN_NOWAIT);
	C2_UT_ASSERT(result == 0);

        result = c2_db_cursor_init(&cursor2, &table2, &tx2, 0);
	C2_UT_ASSERT(result == 0);

	key = 11;
	c2_db_pair_setup(&pair2, &table2, &key, sizeof key, &rec, sizeof rec);
	result = c2_db_cursor_get(&cursor2, &pair2);
	C2_UT_ASSERT(result == 0);

        c2_db_cursor_fini(&cursor2);
	c2_db_pair_fini(&pair2);

	c2_db_cursor_fini(&cursor1);
	c2_db_pair_fini(&pair1);

	result = c2_db_tx_commit(&tx2);
	C2_UT_ASSERT(result == 0);
	c2_table_fini(&table2);

        dbut_fini(&db, &table1, &tx1, &c2_db_tx_commit);
}

/*
 * This test is negative test case.
 * First transaction initialise cursor in RW mode.
 * Second transaction tries to get cursor record
 * for RW which is already locked by first transaction.
 */
static void test_cursor_flags_rmw(void)
{
	struct c2_dbenv     db;
	struct c2_db_tx     tx1;
	struct c2_db_tx     tx2;
	struct c2_table     table1;
	struct c2_table     table2;
	struct c2_db_pair   pair1;
	struct c2_db_pair   pair2;
	struct c2_db_cursor cursor1;
	struct c2_db_cursor cursor2;
	int                 result;
	uint64_t            key;
	uint64_t            rec;

        /* Insert some records */
        dbut_init(db_name, test_table, &db, &table1, &tx1);

	key = 22;
	rec = 16;
	c2_db_pair_setup(&pair1, &table1, &key, sizeof key, &rec, sizeof rec);

	result = c2_table_insert(&tx1, &pair1);
	C2_UT_ASSERT(result == 0);

	c2_db_pair_fini(&pair1);
        dbut_fini(&db, &table1, &tx1, &c2_db_tx_commit);

        dbut_init(db_name, test_table, &db, &table1, &tx1);

	result = c2_db_cursor_init(&cursor1, &table1, &tx1, C2_DB_CURSOR_RMW);
	C2_UT_ASSERT(result == 0);

	key = 22;
	c2_db_pair_setup(&pair1, &table1, &key, sizeof key, &rec, sizeof rec);

        result = c2_db_cursor_get(&cursor1, &pair1);
	C2_UT_ASSERT(result == 0);

        /*
         * Now initialise read/modify/write cursor on same table and
         * in same trasaction where table already having read-only cursor.
         */
	result = c2_table_init(&table2, &db, test_table, 0, &test_table_ops);
	C2_UT_ASSERT(result == 0);

	/* lock will not blocks */
	result = c2_db_tx_init(&tx2, &db, DB_TXN_NOWAIT);
	C2_UT_ASSERT(result == 0);

	result = c2_db_cursor_init(&cursor2, &table2, &tx2, C2_DB_CURSOR_RMW);
	C2_UT_ASSERT(result == 0);

	key = 22;
	c2_db_pair_setup(&pair2, &table2, &key, sizeof key, &rec, sizeof rec);

	/*
	 * This call should fail since record is locked by
	 * transaction tx1 for RMW
	 */
	result = c2_db_cursor_get(&cursor2, &pair2);
	C2_UT_ASSERT(result != 0);

	c2_db_cursor_fini(&cursor2);
	c2_db_pair_fini(&pair2);

	result = c2_db_tx_commit(&tx2);
	C2_UT_ASSERT(result == 0);
	c2_table_fini(&table2);

	c2_db_cursor_fini(&cursor1);
	c2_db_pair_fini(&pair1);

        dbut_fini(&db, &table1, &tx1, &c2_db_tx_commit);
}

const struct c2_test_suite db_cursor_ut = {
	.ts_name = "db-cursor-ut",
	.ts_init = db_reset,
	.ts_fini = db_reset,
	.ts_tests = {
		{ "cursor_flag_read_only", test_cursor_flags_read_only },
		{ "cursor_flag_rmw", test_cursor_flags_rmw },
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

static struct c2_dbenv     ub_db;
static struct c2_table     ub_table;
static struct c2_db_tx     ub_tx;
static struct c2_db_pair   ub_pair;
static struct c2_db_cursor ub_cur;
static uint64_t key;
static uint64_t rec;

static void ub_init(void)
{
	int result;

	db_reset();

	result = c2_dbenv_init(&ub_db, db_name, 0);
	C2_ASSERT(result == 0);

	result = c2_table_init(&ub_table, &ub_db, test_table, 0,
			       &test_table_ops);
	C2_ASSERT(result == 0);

	result = c2_db_tx_init(&ub_tx, &ub_db, 0);
	C2_ASSERT(result == 0);

	c2_db_pair_setup(&ub_pair, &ub_table,
			 &key, sizeof key, &rec, sizeof rec);
}

static void ub_fini(void)
{
	int result;

	result = c2_db_tx_commit(&ub_tx);
	C2_ASSERT(result == 0);

	c2_db_pair_fini(&ub_pair);
	c2_table_fini(&ub_table);
	c2_dbenv_fini(&ub_db);
	db_reset();
}

static void checkpoint()
{
	int result;

	result = c2_db_tx_commit(&ub_tx);
	C2_ASSERT(result == 0);

	result = c2_db_tx_init(&ub_tx, &ub_db, 0);
	C2_ASSERT(result == 0);
}

static void ub_insert(int i)
{
	int      result;

	key = i;
	rec = key*key;

	result = c2_table_insert(&ub_tx, &ub_pair);
	C2_ASSERT(result == 0);

	if (i%1000)
		checkpoint();
}

static void ub_lookup(int i)
{
	int       result;

	key = i;
	result = c2_table_lookup(&ub_tx, &ub_pair);
	C2_ASSERT(result == 0);
	C2_ASSERT(rec == key*key);
	c2_db_pair_release(&ub_pair);

	if (i%1000)
		checkpoint();
}

static void ub_delete(int i)
{
	int      result;

	key = i;

	result = c2_table_delete(&ub_tx, &ub_pair);
	C2_ASSERT(result == 0);

	if (i%1000)
		checkpoint();
}

static void ub_iterate_init(void)
{
	int      result;

	result = c2_db_cursor_init(&ub_cur, &ub_table, &ub_tx, 0);
	C2_ASSERT(result == 0);
	key = 0;
	result = c2_db_cursor_get(&ub_cur, &ub_pair);
	C2_ASSERT(rec == 0*0);
}

static void ub_iterate(int i)
{
	int result;

	result = c2_db_cursor_next(&ub_cur, &ub_pair);
	C2_ASSERT((result ==       0) == (i != UB_ITER - 1));
	C2_ASSERT((result == -ENOENT) == (i == UB_ITER - 1));
	C2_ASSERT(ergo(result == 0, key == i + 1));
	C2_ASSERT(ergo(result == 0, rec == key * key));
}

static void ub_iterate_fini(void)
{
	c2_db_cursor_fini(&ub_cur);
}

static void ub_iterate_back_init(void)
{
	int      result;

	result = c2_db_cursor_init(&ub_cur, &ub_table, &ub_tx, 0);
	C2_ASSERT(result == 0);
	key = UB_ITER - 1;
	result = c2_db_cursor_get(&ub_cur, &ub_pair);
	C2_ASSERT(key == UB_ITER - 1);
	C2_ASSERT(rec == key * key);
}

static void ub_iterate_back(int i)
{
	int result;

	result = c2_db_cursor_prev(&ub_cur, &ub_pair);
	C2_ASSERT((result ==       0) == (i != UB_ITER - 1));
	C2_ASSERT((result == -ENOENT) == (i == UB_ITER - 1));
	C2_ASSERT(ergo(result == 0, key == UB_ITER - 2 - i));
	C2_ASSERT(ergo(result == 0, rec == key * key));
}

static void ub_iterate_back_fini(void)
{
	c2_db_cursor_fini(&ub_cur);
}

struct c2_ub_set c2_db_ub = {
	.us_name = "db-ub",
	.us_init = ub_init,
	.us_fini = ub_fini,
	.us_run  = {
		{ .ut_name = "insert",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_insert },

		{ .ut_name = "lookup",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_lookup },

		{ .ut_name  = "iterate",
		  .ut_iter  = UB_ITER_TX,
		  .ut_init  = ub_iterate_init,
		  .ut_round = ub_iterate,
		  .ut_fini  = ub_iterate_fini },

		{ .ut_name  = "iterate-back",
		  .ut_iter  = UB_ITER_TX,
		  .ut_init  = ub_iterate_back_init,
		  .ut_round = ub_iterate_back,
		  .ut_fini  = ub_iterate_back_fini },

		{ .ut_name = "delete",
		  .ut_iter = UB_ITER,
		  .ut_round = ub_delete },

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
