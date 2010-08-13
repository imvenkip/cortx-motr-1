/* -*- C -*- */

#include <stdarg.h>    /* memset */
#include <string.h>    /* memset */
#include <stdlib.h>    /* free */
#include <sys/stat.h>  /* mkdir */
#include <stdio.h>     /* asprintf, fopen, fclose */

#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"

#include "db/db.h"

/**
   @addtogroup db
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

static int  key_compare  (DB *db, const DBT *dbt1, const DBT *dbt2);
static void dbt_setup_arg(const struct c2_table *table, int idx, 
			  DBT *dbt, void *buf);
static void dbt_setup_ret(const struct c2_table *table, int idx, DBT *dbt);

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
		else
			return db_error;
	}
}

static int db_call_tail(struct c2_addb_ctx *ctx, int rc, const char *method)
{
	if (rc != 0) {
		rc = dberr_conv(rc);
		C2_ADDB_ADD(ctx, &db_loc, c2_addb_func_fail, method, rc);
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
	rc = (dbenv)->d_env->method((dbenv)->d_env, __VA_ARGS__);	\
	db_call_tail(&(dbenv)->d_addb, rc, #method);			\
})

/**
   Calls table method (open, get, put, etc.), converts the result to errno and
   emits addb event if necessary.

   @note c2_table_lookup() calls ->get() directly.
 */
#define TABLE_CALL(table, method, ...)				\
({								\
	int rc;							\
								\
	rc = (table)->t_db->method((table)->t_db, __VA_ARGS__);	\
	db_call_tail(&(table)->t_addb, rc, #method);		\
})

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
		/*
		 * XXX todo
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
			DB_READ_UNCOMMITTED;

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
	if (table->t_db != NULL)
		TABLE_CALL(table, close, 0);
	c2_addb_ctx_fini(&table->t_addb);
}

int c2_db_tx_init(struct c2_db_tx *tx, struct c2_dbenv *env, uint64_t flags)
{
	int result;
	DB_TXN *txn;

	if (flags == 0)
		flags = DB_READ_UNCOMMITTED|DB_TXN_NOSYNC;

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

int c2_db_tx_commit(struct c2_db_tx *tx)
{
	return dberr_conv(tx->dt_txn->commit(tx->dt_txn, DB_TXN_NOSYNC));
}

int c2_db_tx_abort(struct c2_db_tx *tx)
{
	return dberr_conv(tx->dt_txn->abort(tx->dt_txn));
}

int c2_table_insert(struct c2_db_tx *tx, struct c2_table *table, void *key, 
		    void *rec)
{
	DBT key_dbt;
	DBT rec_dbt;

	dbt_setup_arg(table, TO_KEY, &key_dbt, key);
	dbt_setup_arg(table, TO_REC, &rec_dbt, rec);
	return TABLE_CALL(table, put, tx->dt_txn, 
			  &key_dbt, &rec_dbt, DB_NOOVERWRITE);
}

int c2_table_lookup(struct c2_db_tx *tx, struct c2_table *table, void *key, 
		    void **rec)
{
	DBT key_dbt;
	DBT rec_dbt;
	int result;

	dbt_setup_arg(table, TO_KEY, &key_dbt, key);
	dbt_setup_ret(table, TO_REC, &rec_dbt);

	/*
	 * TABLE_CALL() is not used here, because DB_NOTFOUND is a "valid"
	 * return value for which no addb noise should be made.
	 */
	result = table->t_db->get(table->t_db, tx->dt_txn, 
				  &key_dbt, &rec_dbt, 0);
	if (result == DB_NOTFOUND)
		result = -ENOENT;
	else if (result == 0)
		*rec = rec_dbt.data;
	else
		result = db_call_tail(&table->t_addb, result, "get");
	return result;
}

int c2_table_delete(struct c2_db_tx *tx, struct c2_table *table, void *key)
{
	DBT key_dbt;

	dbt_setup_arg(table, TO_KEY, &key_dbt, key);
	return TABLE_CALL(table, del, tx->dt_txn, &key_dbt, 0);
}

void c2_db_rec_fini(const struct c2_table *table, void *rec)
{
	free(rec);
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

static void dbt_setup_arg(const struct c2_table *table, int idx, 
			  DBT *dbt, void *buf)
{
	memset(dbt, 0, sizeof *dbt);
	/*
	 * XXX use table->t_ops.to[].{pack,open}().
	 */
	dbt->data = buf;
	dbt->size = table->t_ops->to[idx].size;
}

static void dbt_setup_ret(const struct c2_table *table, int idx, DBT *dbt)
{
	memset(dbt, 0, sizeof *dbt);
	dbt->flags = DB_DBT_MALLOC;
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
