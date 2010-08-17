/* -*- C -*- */

#include <stdlib.h>       /* system */

#include "lib/types.h"
#include "lib/ut.h"
#include "lib/ub.h"
#include "db/db.h"

const char db_name[] = "ut-db";

static void test_db_create(void) 
{
	struct c2_dbenv db;
	int             result;

	result = c2_dbenv_init(&db, db_name, 0);
	C2_UT_ASSERT(result == 0);
	c2_dbenv_fini(&db);
}

static const struct c2_table_ops test_table_ops = {
	.to = {
		[TO_KEY] = { .max_size = 8 },
		[TO_REC] = { .max_size = 8 }
	},
	.key_cmp = NULL
};

static int db_reset(void)
{
	char *cmd;
	int   rc;

	rc = asprintf(&cmd, "rm -fr \"%s\"", db_name);
	C2_ASSERT(rc > 0);

	rc = system(cmd);
	C2_ASSERT(rc == 0);
	free(cmd);
	return 0;
}

static void test_table_create(void) 
{
	struct c2_dbenv db;
	struct c2_table table;
	int             result;

	result = c2_dbenv_init(&db, db_name, 0);
	C2_UT_ASSERT(result == 0);

	result = c2_table_init(&table, &db, "test-table", 0, &test_table_ops);
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

	result = c2_table_init(&table, &db, "test-table", 0, &test_table_ops);
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

	result = c2_table_init(&table, &db, "test-table", 0, &test_table_ops);
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

	result = c2_table_init(&table, &db, "test-table", 0, &test_table_ops);
	C2_UT_ASSERT(result == 0);

	result = c2_db_tx_init(&tx, &db, 0);
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

	result = c2_table_init(&table, &db, "test-table", 0, &test_table_ops);
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

	result = c2_table_init(&table, &db, "test-table", 0, &test_table_ops);
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

	result = c2_table_init(&table, &db, "test-table", 0, &test_table_ops);
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
		{ NULL, NULL }
	}
};

/*
 * UB
 */

enum {
	UB_ITER = 10000
};

static struct c2_dbenv   ub_db;
static struct c2_table   ub_table;
static struct c2_db_tx   ub_tx;
static struct c2_db_pair ub_pair;
static uint64_t key;
static uint64_t rec;

static void ub_init(void)
{
	int result;

	db_reset();

	result = c2_dbenv_init(&ub_db, db_name, 0);
	C2_ASSERT(result == 0);

	result = c2_table_init(&ub_table, &ub_db, "test-table", 0, 
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

static void ub_insert(int i)
{
	int      result;

	key = i;
	rec = i*i;

	result = c2_table_insert(&ub_tx, &ub_pair);
	C2_ASSERT(result == 0);
}

static void ub_lookup(int i)
{
	int       result;

	key = i;
	result = c2_table_lookup(&ub_tx, &ub_pair);
	C2_ASSERT(result == 0);
	C2_ASSERT(rec == i*i);
	c2_db_pair_release(&ub_pair);
}

static void ub_delete(int i)
{
	int      result;

	key = i;

	result = c2_table_delete(&ub_tx, &ub_pair);
	C2_ASSERT(result == 0);
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
