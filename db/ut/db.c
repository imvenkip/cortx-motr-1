/* -*- C -*- */

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

	result = c2_dbenv_init(&db, db_name, 0);
	C2_UT_ASSERT(result == 0);

	result = c2_table_init(&table, &db, test_table, 0, &test_table_ops);
	C2_UT_ASSERT(result == 0);

	result = c2_db_tx_init(&tx, &db, 0);
	C2_UT_ASSERT(result == 0);

	key = 42;
	c2_db_pair_setup(&cons, &table, &key, sizeof key, &rec, sizeof rec);
	result = c2_table_lookup(&tx, &cons);
	C2_UT_ASSERT(result == -ENOENT);

	c2_db_pair_fini(&cons);
	result = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(result == 0);

	c2_table_fini(&table);
	c2_dbenv_fini(&db);
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

	result = c2_dbenv_init(&db, db_name, 0);
	C2_UT_ASSERT(result == 0);

	result = c2_table_init(&table, &db, test_table, 0, &test_table_ops);
	C2_UT_ASSERT(result == 0);

	result = c2_db_tx_init(&tx, &db, 0);
	C2_UT_ASSERT(result == 0);

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

	result = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(result == 0);

	c2_table_fini(&table);
	c2_dbenv_fini(&db);

	/* and look up again */

	result = c2_dbenv_init(&db, db_name, 0);
	C2_UT_ASSERT(result == 0);

	result = c2_table_init(&table, &db, test_table, 0, &test_table_ops);
	C2_UT_ASSERT(result == 0);

	result = c2_db_tx_init(&tx, &db, 0);
	C2_UT_ASSERT(result == 0);

	c2_db_pair_setup(&cons1, &table, &key, sizeof key, 
			 &rec_out, sizeof rec_out);
	result = c2_table_lookup(&tx, &cons1);
	C2_UT_ASSERT(result == 0);
	C2_UT_ASSERT(rec_out == rec);

	c2_db_pair_fini(&cons1);
	
	result = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(result == 0);

	c2_table_fini(&table);
	c2_dbenv_fini(&db);
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

	result = c2_dbenv_init(&db, db_name, 0);
	C2_UT_ASSERT(result == 0);

	result = c2_table_init(&table, &db, test_table, 0, &test_table_ops);
	C2_UT_ASSERT(result == 0);

	result = c2_db_tx_init(&tx, &db, 0);
	C2_UT_ASSERT(result == 0);

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

	result = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(result == 0);

	c2_table_fini(&table);
	c2_dbenv_fini(&db);
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

	result = c2_dbenv_init(&db, db_name, 0);
	C2_UT_ASSERT(result == 0);

	result = c2_table_init(&table, &db, test_table, 0, &test_table_ops);
	C2_UT_ASSERT(result == 0);

	result = c2_db_tx_init(&tx, &db, 0);
	C2_UT_ASSERT(result == 0);

	key = 44;
	rec = 18;

	c2_db_pair_setup(&cons, &table, &key, sizeof key, &rec, sizeof rec);

	result = c2_table_insert(&tx, &cons);
	C2_UT_ASSERT(result == 0);

	c2_db_pair_fini(&cons);
	c2_db_tx_abort(&tx);

	c2_table_fini(&table);
	c2_dbenv_fini(&db);

	result = c2_dbenv_init(&db, db_name, 0);
	C2_UT_ASSERT(result == 0);

	result = c2_table_init(&table, &db, test_table, 0, &test_table_ops);
	C2_UT_ASSERT(result == 0);

	result = c2_db_tx_init(&tx, &db, 0);
	C2_UT_ASSERT(result == 0);

	c2_db_pair_setup(&cons, &table, &key, sizeof key, &rec, sizeof rec);
	result = c2_table_lookup(&tx, &cons);
	C2_UT_ASSERT(result == -ENOENT);
	
	c2_db_pair_fini(&cons);
	result = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(result == 0);

	c2_table_fini(&table);
	c2_dbenv_fini(&db);
}

static void test_waiter(void) 
{
	struct c2_dbenv   db;
	struct c2_db_tx   tx;
	struct c2_table   table;
	struct c2_db_pair cons;
	int               result;
	uint64_t          key;
	uint64_t          rec;
	int               wflag;

	struct c2_db_tx_waiter wait;

	result = c2_dbenv_init(&db, db_name, 0);
	C2_UT_ASSERT(result == 0);

	result = c2_table_init(&table, &db, test_table, 0, &test_table_ops);
	C2_UT_ASSERT(result == 0);

	result = c2_db_tx_init(&tx, &db, 0);
	C2_UT_ASSERT(result == 0);

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
	c2_db_tx_abort(&tx);

	C2_UT_ASSERT(wflag == 2);

	c2_table_fini(&table);
	c2_dbenv_fini(&db);

	result = c2_dbenv_init(&db, db_name, 0);
	C2_UT_ASSERT(result == 0);

	result = c2_table_init(&table, &db, test_table, 0, &test_table_ops);
	C2_UT_ASSERT(result == 0);

	result = c2_db_tx_init(&tx, &db, 0);
	C2_UT_ASSERT(result == 0);

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
	result = c2_db_tx_commit(&tx);
	C2_UT_ASSERT(result == 0);

	c2_table_fini(&table);
	c2_dbenv_fini(&db);
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
	int result;

	result = c2_db_cursor_init(&ub_cur, &ub_table, &ub_tx);
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
	int result;

	result = c2_db_cursor_init(&ub_cur, &ub_table, &ub_tx);
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
