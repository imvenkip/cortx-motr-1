/* -*- C -*- */

#include <stdarg.h>
#include <stdlib.h>    /* free */
#include <sys/stat.h>  /* mkdir */
#include <stdio.h>     /* asprintf, fopen, fclose */

#include "lib/misc.h"  /* C2_SET0 */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"

#include "db/db.h"

/**
   @addtogroup db

   <b>db5 based implementation.</b>

   Should be mostly self-evident.

   An implementation emits ADDB events all db5 errors.

   @see http://www.oracle.com/technology/documentation/berkeley-db/db/api_reference/C/index.html

   @{
 */

static const struct c2_addb_loc db_loc = {
	.al_name = "db"
};

static const struct c2_addb_ctx_type db_env_ctx_type = {
	.act_name = "db-env"
};

static const struct c2_addb_ctx_type db_table_ctx_type = {
	.act_name = "db-table"
};

static const struct c2_addb_ctx_type db_tx_ctx_type = {
	.act_name = "db-tx"
};

static int key_compare(DB *db, const DBT *dbt1, const DBT *dbt2);

/**
   Convert db5 specific error code into generic errno.

   This function is idempotent: dberr_conv(dberr_conv(x)) == dberr_conv(x) for
   all x.

   @see http://www.oracle.com/technology/documentation/berkeley-db/db/programmer_reference/program_errorret.html
 */
static int dberr_conv(int db_error)
{
	/*
	 * This translation is incomplete. Add more cases if you see spurious
	 * EINVAL's. See <db.h> (DB5 header) for error constants.
	 */
	switch (db_error) {
	case DB_NOTFOUND:
		return -ENOENT;
	case DB_LOCK_DEADLOCK:
		return -EDEADLK;
	case DB_KEYEXIST:
		return -EEXIST;
	case DB_KEYEMPTY:
		return -ENOENT;
	case DB_LOCK_NOTGRANTED:
		return -ENOLCK;
	case DB_BUFFER_SMALL:
		return -ENOBUFS;
	default:
		/* As per <db.h>:
		 *
		 *         We don't want our error returns to conflict with
		 *         other packages where possible, so pick a base error
		 *         value that's hopefully not common.  We document that
		 *         we own the error name space from -30,800 to -30,999.
		 */
		if (-30999 <= db_error && db_error <= -30800)
			return -EINVAL;
		else if (db_error > 0)
			/* errno */
			return -db_error;
		else
			return db_error;
	}
}

/**
   A helper to DBENV_CALL(), TABLE_CALL() and TX_CALL(): converts db5 error code
   into errno and emits an ADDB event in a given context, if necessary.
 */
static int db_call_tail(struct c2_addb_ctx *ctx, int rc, const char *name,
			int *tolerate)
{
	if (rc != 0) {
		rc = dberr_conv(rc);
		for (; *tolerate != 0 && *tolerate != rc; tolerate++)
			;
		if (*tolerate == 0)
			C2_ADDB_ADD(ctx, &db_loc, c2_addb_func_fail, name, rc);
	}
	return rc;
}

/**
   Calls dbenv method (open, set_*, get_*, tx_*, etc.), converts the result to
   errno and emits addb event if necessary.
 */
#define DBENV_CALL(dbenv, method, ...)					\
({									\
	int rc;								\
									\
	rc = (dbenv)->d_env->method((dbenv)->d_env , ## __VA_ARGS__);	\
	db_call_tail(&(dbenv)->d_addb, rc, #method, dbenv_tol_ ## method); \
})

/**
   Calls table method (open, get, put, etc.), converts the result to errno and
   emits addb event if necessary.

   @note c2_table_lookup() calls ->get() directly.
 */
#define TABLE_CALL(table, method, ...)					\
({									\
	int rc;								\
									\
	rc = (table)->t_db->method((table)->t_db , ## __VA_ARGS__);	\
	db_call_tail(&(table)->t_addb, rc, #method, table_tol_ ## method); \
})

/**
   Calls transaction method (commit, abort, etc.), converts the result to errno
   and emits addb event if necessary.
 */
#define TX_CALL(tx, method, ...)					\
({									\
	int rc;								\
									\
	rc = (tx)->dt_txn->method((tx)->dt_txn , ## __VA_ARGS__);	\
	db_call_tail(&(tx)->dt_addb, rc, #method, tx_tol_ ## method);	\
})

/**
   Calls cursor method (get, set, close, etc.), converts the result to errno
   and emits addb event in the context of cursor table, if necessary.
 */
#define CURSOR_CALL(cur, method, ...)					\
({									\
	int rc;								\
									\
	rc = (cur)->c_dbc->method((cur)->c_dbc , ## __VA_ARGS__);	\
	db_call_tail(&(cur)->c_table->t_addb, rc, "dbc::" #method,	\
		cursor_tol_ ## method);				\
})

/**
   Helper function: opens a file with a name constructed from printf(3)-like
   format and arguments.
 */
static __attribute__((format(printf, 3, 4))) int
openvar(FILE **file, const char *mode, const char *fmt, ...)
{
	char   *name;
	int     nob;
	va_list args;
	int     result;

	va_start(args, fmt);
	nob = vasprintf(&name, fmt, args);
	if (nob >= 0) {
		*file = fopen(name, mode);
		free(name);
		result = file != NULL ? 0 : -errno;
	} else {
		*file = NULL;
		result = -ENOMEM;
	}
	va_end(args);
	return result;
}

static int dbenv_tol_set_verbose[] = { 0 };
static int dbenv_tol_set_flags[] = { 0 };
static int dbenv_tol_open[] = { 0 };
static int dbenv_tol_close[] = { 0 };
static int dbenv_tol_txn_begin[] = { 0 };
static int dbenv_tol_set_lk_detect[] = { 0 };
static int dbenv_tol_memp_sync[] = { 0 };
static int dbenv_tol_log_flush[] = { 0 };

/**
   Major part of c2_dbenv_init().
 */
static int dbenv_setup(struct c2_dbenv *env, const char *name, uint64_t flags)
{
	int result;
	DB_ENV *de;

	/*
	 * XXX translate flags from c2 to db5.
	 */
	if (flags == 0)
		flags = DB_CREATE|DB_THREAD|DB_INIT_LOG|DB_INIT_MPOOL|
			DB_INIT_TXN|DB_INIT_LOCK|DB_RECOVER;

	if (flags & DB_CREATE)
		/* try to create home directory, don't bother to check the
		   result, ->open() below would fail anyway. */
		mkdir(name, 0700);

	/*
	 * Redirect db environment message stream and error streams to
	 * appropriately named files. Alternatively, DB_ENV->set_msgcall() and
	 * DB_ENV->set_errcall() can be used to intercept individual messages
	 * (at the data-base environment level, corresponding
	 * DB->set_{msg,err}call() calls can be used for table-level granularity
	 * interception) and to emit ADDB events for them.
	 */

	result = openvar(&env->d_errlog, "a", "%s.errlog", name);
	if (result != 0)
		return result;

	result = openvar(&env->d_msglog, "a", "%s.msglog", name);
	if (result != 0)
		return result;

	c2_addb_ctx_init(&env->d_addb, &db_env_ctx_type, &c2_addb_global_ctx);
	result = db_env_create(&env->d_env, 0);
	if (result == 0) {
		de = env->d_env;
		de->app_private = env;
		de->set_msgfile(de, env->d_msglog);
		de->set_errfile(de, env->d_errlog);
		de->set_errpfx(de, "c2");
		result = DBENV_CALL(env, set_verbose, DB_VERB_DEADLOCK, 1);
		C2_ASSERT(result == 0);
		result = DBENV_CALL(env, set_verbose, DB_VERB_WAITSFOR, 1);
		C2_ASSERT(result == 0);
		result = DBENV_CALL(env, set_verbose, DB_VERB_RECOVERY, 1);
		C2_ASSERT(result == 0);
		result = DBENV_CALL(env, set_flags, DB_TXN_NOSYNC, 1);
		C2_ASSERT(result == 0);
		result = DBENV_CALL(env, set_lk_detect, DB_LOCK_DEFAULT);
		C2_ASSERT(result == 0);
		/*
		 * XXX todo
		 *
		 * db5 has a plethora of data-base flags and options that could
		 * be setup here. One goal of db/db.h interface is to insulate
		 * its user from all these complexities. Sensible defaults must
		 * be selected.
		 *
		 * DBENV_CALL(env, set_thread_count, ...);
		 * DBENV_CALL(env, set_cachesize, ...);
		 * DBENV_CALL(env, set_bsize, ...);
		 * DBENV_CALL(env, set_app_dispatch, c2_fol_dispatch);
		 *
		 * start ->memp_trickle() thread.
		 */
		if (result == 0)
			result = DBENV_CALL(env, open, name, flags, 0700);
	} else
		env->d_env = NULL;
	return dberr_conv(result);
}

int c2_dbenv_init(struct c2_dbenv *env, const char *name, uint64_t flags)
{
	int result;

	result = dbenv_setup(env, name, flags);
	if (result != 0)
		c2_dbenv_fini(env);
	return result;
}

void c2_dbenv_fini(struct c2_dbenv *env)
{
	if (env->d_env != NULL) {
		DBENV_CALL(env, memp_sync, NULL);
		DBENV_CALL(env, log_flush, NULL);
		DBENV_CALL(env, close, 0);
		env->d_env = NULL;
	}
	if (env->d_msglog != NULL) {
		fclose(env->d_msglog);
		env->d_msglog = NULL;
	}
	if (env->d_errlog != NULL) {
		fclose(env->d_errlog);
		env->d_errlog = NULL;
	}
	c2_addb_ctx_fini(&env->d_addb);
}

static int table_tol_set_lorder[] = { 0 };
static int table_tol_open[] = { 0 };
static int table_tol_close[] = { 0 };
static int table_tol_set_bt_compare[] = { 0 };
static int table_tol_put[] = { 0 };
static int table_tol_get[] = { -ENOENT, 0 };
static int table_tol_del[] = { 0 };
static int table_tol_cursor[] = { 0 };
static int table_tol_sync[] = { 0 };

int c2_table_init(struct c2_table *table, struct c2_dbenv *env, 
		  const char *name, uint64_t flags, 
		  const struct c2_table_ops *ops)
{
	int result;
	DB *db;

	if (flags == 0)
		flags = DB_AUTO_COMMIT|DB_CREATE|DB_THREAD|DB_TXN_NOSYNC|
			/*
			 * Both a data-base and a transaction
			 * must use "read uncommitted" to avoid
			 * dead-locks.
			 */
			0/*DB_READ_UNCOMMITTED*/;

	table->t_env = env;
	table->t_ops = ops;
	c2_addb_ctx_init(&table->t_addb, &db_table_ctx_type, &env->d_addb);
	result = db_create(&table->t_db, env->d_env, 0);
	if (result == 0) {
		db = table->t_db;
		db->app_private = table;
		/* Our lord is little-endian. */
		result = TABLE_CALL(table, set_lorder, 1234);
		if (result == 0) {
			if (ops->key_cmp != NULL)
				result = TABLE_CALL(table, set_bt_compare,
						    &key_compare);
		}
		if (result == 0)
			result = TABLE_CALL(table, open, NULL, name, 
					    NULL, DB_BTREE, flags, 0700);
	} else
		table->t_db = NULL;
	result = dberr_conv(result);
	if (result != 0)
		c2_table_fini(table);
	return result;
}

void c2_table_fini(struct c2_table *table)
{
	if (table->t_db != NULL) {
		TABLE_CALL(table, sync, 0);
		TABLE_CALL(table, close, 0);
	}
	c2_addb_ctx_fini(&table->t_addb);
}

void pair_init(struct c2_db_pair *pair, struct c2_table *table)
{
	pair->dp_table = table;

	pair->dp_key.data  = pair->dp_keybuf;
	pair->dp_rec.data  = pair->dp_recbuf;
	pair->dp_key.size  = pair->dp_key.ulen;
	pair->dp_rec.size  = pair->dp_rec.ulen;
	pair->dp_key.flags = pair->dp_rec.flags = DB_DBT_USERMEM;
}

int c2_db_pair_alloc(struct c2_db_pair *pair, struct c2_table *table)
{
	C2_SET0(pair);

	pair->dp_key.ulen = table->t_ops->to[TO_KEY].max_size;
	pair->dp_rec.ulen = table->t_ops->to[TO_REC].max_size;

	pair->dp_keybuf = c2_alloc(pair->dp_key.ulen);
	pair->dp_recbuf = c2_alloc(pair->dp_rec.ulen);

	pair->dp_flags = DPF_ALLOCATED;
	pair_init(pair, table);

	if (pair->dp_keybuf != NULL && pair->dp_recbuf != NULL)
		return 0;
	else {
		c2_db_pair_fini(pair);
		return -ENOMEM;
	}
}

void c2_db_pair_setup(struct c2_db_pair *pair, struct c2_table *table,
		      void *keybuf, uint32_t keysize, 
		      void *recbuf, uint32_t recsize)
{
	C2_SET0(pair);

	pair->dp_key.ulen = keysize;
	pair->dp_rec.ulen = recsize;

	pair->dp_keybuf = keybuf;
	pair->dp_recbuf = recbuf;

	pair_init(pair, table);
}

void c2_db_pair_fini(struct c2_db_pair *pair)
{
	if (pair->dp_flags & DPF_ALLOCATED) {
		c2_free(pair->dp_keybuf);
		c2_free(pair->dp_recbuf);
	}
	C2_SET0(pair);
}

void c2_db_pair_release(struct c2_db_pair *pair)
{
}

int c2_db_tx_init(struct c2_db_tx *tx, struct c2_dbenv *env, uint64_t flags)
{
	int result;
	DB_TXN *txn;

	if (flags == 0)
		flags = 0/*DB_READ_UNCOMMITTED*/|DB_TXN_NOSYNC;

	c2_addb_ctx_init(&tx->dt_addb, &db_tx_ctx_type, &env->d_addb);
	result = DBENV_CALL(env, txn_begin, NULL, &tx->dt_txn, flags);
	if (result == 0) {
		txn = tx->dt_txn;
		/*
		 * Hack alert: DB_TXN has no application private field similar
		 * to {DBENV,DB}->app_private. Hijack xml_private.
		 */
		txn->xml_internal = tx;
	} else
		tx->dt_txn = NULL;
	result = dberr_conv(result);
	return result;
}

void tx_fini(struct c2_db_tx *tx)
{
	c2_addb_ctx_fini(&tx->dt_addb);
}

static int tx_tol_commit[] = { 0 };
static int tx_tol_abort[] = { 0 };

int c2_db_tx_commit(struct c2_db_tx *tx)
{
	int result;

	result = TX_CALL(tx, commit, DB_TXN_NOSYNC);
	tx_fini(tx);
	return result;
}

int c2_db_tx_abort(struct c2_db_tx *tx)
{
	int result;

	result = TX_CALL(tx, abort);
	tx_fini(tx);
	return result;
}

int c2_table_update(struct c2_db_tx *tx, struct c2_db_pair *pair)
{
	return TABLE_CALL(pair->dp_table, put, tx->dt_txn,
			  &pair->dp_key, &pair->dp_rec, 0);
}

int c2_table_insert(struct c2_db_tx *tx, struct c2_db_pair *pair)
{
	return TABLE_CALL(pair->dp_table, put, tx->dt_txn, 
			  &pair->dp_key, &pair->dp_rec, DB_NOOVERWRITE);
}

int c2_table_lookup(struct c2_db_tx *tx, struct c2_db_pair *pair)
{
	int result;

	/*
	 * Possible optimization: if pair DBT's flags are 0, ->get() would
	 *                        return with DBT->data pointing directly to the
	 *                        in-db data. Returned pointer is valid until
	 *                        _any_ call against the same DB handle is made
	 *                        by any thread.
	 *
	 *                        This gives a 0-copy lookup: embed a mutex in
	 *                        c2_table, lock it in c2_table_lookup() and
	 *                        release in c2_db_rec_fini().
	 */
	result = TABLE_CALL(pair->dp_table, get, 
			    tx->dt_txn, &pair->dp_key, &pair->dp_rec, DB_RMW);
	return result;
}

int c2_table_delete(struct c2_db_tx *tx, struct c2_db_pair *pair)
{
	return TABLE_CALL(pair->dp_table, del, tx->dt_txn, &pair->dp_key, 0);
}

int c2_db_cursor_init(struct c2_db_cursor *cursor, struct c2_table *table,
		      struct c2_db_tx *tx)
{
	cursor->c_table = table;
	cursor->c_tx    = tx;
	return TABLE_CALL(table, cursor, tx->dt_txn, &cursor->c_dbc, 0);
}

static int cursor_tol_close[] = { 0 };
static int cursor_tol_get[] = { -ENOENT, 0 };
static int cursor_tol_put[] = { 0 };
static int cursor_tol_del[] = { 0 };

void c2_db_cursor_fini(struct c2_db_cursor *cursor)
{
	CURSOR_CALL(cursor, close);
}

static int cursor_get(struct c2_db_cursor *cursor, struct c2_db_pair *pair,
		      uint32_t flags)
{
	return CURSOR_CALL(cursor, get, &pair->dp_key, &pair->dp_rec, 
			   flags|DB_RMW);
}

int c2_db_cursor_get(struct c2_db_cursor *cursor, struct c2_db_pair *pair)
{
	return cursor_get(cursor, pair, DB_SET_RANGE);
}

int c2_db_cursor_next(struct c2_db_cursor *cursor, struct c2_db_pair *pair)
{
	return cursor_get(cursor, pair, DB_NEXT);
}

int c2_db_cursor_prev(struct c2_db_cursor *cursor, struct c2_db_pair *pair)
{
	return cursor_get(cursor, pair, DB_PREV);
}

int c2_db_cursor_set(struct c2_db_cursor *cursor, struct c2_db_pair *pair)
{
	return CURSOR_CALL(cursor, put, 
			   &pair->dp_key, &pair->dp_rec, DB_CURRENT);
}

int c2_db_cursor_add(struct c2_db_cursor *cursor, struct c2_db_pair *pair)
{
	return CURSOR_CALL(cursor, put,
			   &pair->dp_key, &pair->dp_rec, DB_KEYFIRST);
}

int c2_db_cursor_del(struct c2_db_cursor *cursor)
{
	return CURSOR_CALL(cursor, del, 0);
}

int c2_db_init(void)
{
	return 0;
}

void c2_db_fini(void)
{
}

static int key_compare(DB *db, const DBT *dbt0, const DBT *dbt1)
{
	struct c2_table *table;

	table = db->app_private;
	C2_ASSERT(table->t_db == db);
	C2_ASSERT(table->t_ops->key_cmp != NULL);
	return table->t_ops->key_cmp(table, dbt0->data, dbt1->data);
}

/** @} end of db group */

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
