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

#include <stdarg.h>
#include <stdlib.h>    /* free */
#include <sys/stat.h>  /* mkdir */
#include <stdio.h>     /* asprintf, fopen, fclose */

#include "lib/adt.h"   /* c2_buf */
#include "lib/misc.h"  /* C2_SET0 */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"

#include "colibri/magic.h"
#include "db/db.h"
#include "db/db_common.h"

/**
   @addtogroup db

   <b>db5 based implementation.</b>

   Should be mostly self-evident.

   An implementation emits ADDB events all db5 errors.

   The natural way to implement transaction waiter would be to use a "commit
   call-back" that gets called whenever a transaction or a group of transactions
   becomes persistent. Alas, db5 provides no such call-back. Instead, when a
   transaction is closed, the last LSN used by the data-base environment is
   obtained (get_lsn()). A special per-environment thread (dbenv_thread()),
   started by c2_dbenv_init() once a second learns (by calling
   DBENV->log_stat()) what is the last persistent LSN and signals waiters for
   all transactions with LSNs less than or equal to the last persistent LSN.

   This solution leaves much to be desired: (i) it smells of a hack, (ii)
   get_lsn() is perhaps too expensive to be called on each transaction
   completion, (iii) it relies on undocumented internal structure of LSN, see
   dbenv_thread() for details.

   Alternatively, commit call-back can be added to db5.

   @see http://www.oracle.com/technology/documentation/berkeley-db/db/api_reference/C/index.html

   @{
 */

static int key_compare(DB *db, const DBT *dbt1, const DBT *dbt2);
static int get_lsn(struct c2_dbenv *env, DB_LSN *lsn);
static void dbenv_thread(struct c2_dbenv *env);

C2_TL_DESCR_DEFINE(enw, "env waiters", static, struct c2_db_tx_waiter,
		   tw_env, tw_magix,
		   C2_DB_TX_WAITER_MAGIC, C2_DB_TX_WAITER_HEAD_MAGIC);
C2_TL_DEFINE(enw, static, struct c2_db_tx_waiter);


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
   A helper to DBENV_CALL(), TABLE_CALL(), TX_CALL() and CURSOR_CALL(): converts
   db5 error code into errno and emits an ADDB event in a given context, if
   necessary.
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
	rc = (dbenv)->d_i.d_env->method((dbenv)->d_i.d_env , ## __VA_ARGS__); \
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
	rc = (table)->t_i.t_db->method((table)->t_i.t_db , ## __VA_ARGS__); \
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
	rc = (tx)->dt_i.dt_txn->method((tx)->dt_i.dt_txn , ## __VA_ARGS__); \
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
	rc = (cur)->c_i.c_dbc->method((cur)->c_i.c_dbc , ## __VA_ARGS__); \
	db_call_tail(&(cur)->c_table->t_addb, rc, "dbc::" #method,	\
		cursor_tol_ ## method);					\
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
static int dbenv_tol_log_cursor[] = { 0 };
static int dbenv_tol_log_stat[] = { 0 };
static int dbenv_tol_memp_trickle[] = { 0 };
static int dbenv_tol_set_alloc[] = { 0 };
static int dbenv_tol_txn_checkpoint[] = { 0 };

static void *never(void *ptr, size_t size)
{
	C2_IMPOSSIBLE("realloc called.");
}

/**
   Major part of c2_dbenv_init().
 */
static int dbenv_setup(struct c2_dbenv *env, const char *name, uint64_t flags)
{
	int                   result;
	DB_ENV               *de;
	struct c2_dbenv_impl *di;

	C2_SET0(env);

	di = &env->d_i;

	c2_dbenv_common_init(env);
	c2_mutex_init(&di->d_lock);
	enw_tlist_init(&di->d_waiters);
	c2_cond_init(&di->d_shutdown_cond);
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

	result = openvar(&di->d_errlog, "a", "%s.errlog", name);
	if (result != 0)
		return result;

	result = openvar(&di->d_msglog, "a", "%s.msglog", name);
	if (result != 0)
		return result;

	result = db_env_create(&di->d_env, 0);
	if (result == 0) {
		de = di->d_env;
		de->app_private = env;
		de->set_msgfile(de, di->d_msglog);
		de->set_errfile(de, di->d_errlog);
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
		result = DBENV_CALL(env, set_alloc, c2_alloc, never, c2_free);
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
		if (result == 0) {
			result = DBENV_CALL(env, open, name, flags, 0700);
			if (result == 0) {
				result = C2_THREAD_INIT(&di->d_thread,
							struct c2_dbenv *, NULL,
							&dbenv_thread, env,
							"dbenv_thread");
				if (result == 0)
					DBENV_CALL(env, log_cursor,
						   &di->d_logc, 0);
			}
		}
	} else
		di->d_env = NULL;
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
	struct c2_dbenv_impl *di;

	di = &env->d_i;
	if (di->d_env != NULL) {
		DBENV_CALL(env, memp_sync, NULL);
		DBENV_CALL(env, log_flush, NULL);
	}
	if (di->d_thread.t_state == TS_RUNNING) {
		c2_mutex_lock(&di->d_lock);
		di->d_shutdown = true;
		c2_cond_signal(&di->d_shutdown_cond, &di->d_lock);
		c2_mutex_unlock(&di->d_lock);
		c2_thread_join(&di->d_thread);
		c2_thread_fini(&di->d_thread);
	}
	if (di->d_logc != NULL) {
		di->d_logc->close(di->d_logc, 0);
		di->d_logc = NULL;
	}
	if (di->d_env != NULL) {
		DBENV_CALL(env, close, 0);
		di->d_env = NULL;
	}
	if (di->d_msglog != NULL) {
		fclose(di->d_msglog);
		di->d_msglog = NULL;
	}
	if (di->d_errlog != NULL) {
		fclose(di->d_errlog);
		di->d_errlog = NULL;
	}
	c2_cond_fini(&di->d_shutdown_cond);
	enw_tlist_fini(&di->d_waiters);
	c2_mutex_fini(&di->d_lock);
	c2_dbenv_common_fini(env);
}

int c2_dbenv_sync(struct c2_dbenv *env)
{
	return DBENV_CALL(env, txn_checkpoint, 0, 0, 0);
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

	c2_table_common_init(table, env, ops);
	if (flags == 0)
		flags = DB_AUTO_COMMIT|DB_CREATE|DB_THREAD|DB_TXN_NOSYNC|
			/*
			 * Both a data-base and a transaction
			 * must use "read uncommitted" to avoid
			 * dead-locks.
			 */
			0/*DB_READ_UNCOMMITTED*/;

	result = db_create(&table->t_i.t_db, env->d_i.d_env, 0);
	if (result == 0) {
		db = table->t_i.t_db;
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
		table->t_i.t_db = NULL;
	result = dberr_conv(result);
	if (result != 0)
		c2_table_fini(table);
	return result;
}

void c2_table_fini(struct c2_table *table)
{
	if (table->t_i.t_db != NULL) {
		TABLE_CALL(table, sync, 0);
		TABLE_CALL(table, close, 0);
		table->t_i.t_db = NULL;
	}
	c2_table_common_fini(table);
}

void c2_db_buf_impl_init(struct c2_db_buf *buf)
{
	DBT *dbt;

	dbt = &buf->db_i.db_dbt;
	dbt->data = buf->db_buf.b_addr;
	dbt->ulen = dbt->size = buf->db_buf.b_nob;

	switch (buf->db_type) {
	case DBT_ALLOC:
		dbt->flags = DB_DBT_MALLOC;
		break;
	case DBT_COPYOUT:
		dbt->flags = DB_DBT_USERMEM;
		break;
	default:
		C2_IMPOSSIBLE("Wrong buffer type.");
	}
}

void c2_db_buf_impl_fini(struct c2_db_buf *buf)
{
}

bool c2_db_buf_impl_invariant(const struct c2_db_buf *buf)
{
	return
                buf->db_i.db_dbt.data == buf->db_buf.b_addr &&
                (buf->db_type == DBT_ALLOC) ?
                buf->db_i.db_dbt.size == buf->db_buf.b_nob :
                buf->db_i.db_dbt.ulen == buf->db_buf.b_nob;
}

int c2_db_tx_init(struct c2_db_tx *tx, struct c2_dbenv *env, uint64_t flags)
{
	int result;
	DB_TXN *txn;

	c2_db_common_tx_init(tx, env);
	if (flags == 0)
		flags = 0/*DB_READ_UNCOMMITTED*/|DB_TXN_NOSYNC;

	result = DBENV_CALL(env, txn_begin, NULL, &tx->dt_i.dt_txn, flags);
	if (result == 0) {
		txn = tx->dt_i.dt_txn;
		/*
		 * Hack alert: DB_TXN has no application private field similar
		 * to {DBENV,DB}->app_private. Hijack xml_private.
		 */
		txn->xml_internal = tx;
	} else {
		tx->dt_i.dt_txn = NULL;
		tx->dt_env = NULL;
        }
	result = dberr_conv(result);
	return result;
}

static void waiter_fini(struct c2_db_tx_waiter *w)
{
	enw_tlink_del_fini(w);
	w->tw_done(w);
}

static int tx_fini_pre(struct c2_db_tx *tx, bool commit)
{
	struct c2_db_tx_waiter *w;
	struct c2_dbenv        *env;
	int                     result;
	DB_LSN                  lsn;

	env = tx->dt_env;
	if (commit && !txw_tlist_is_empty(&tx->dt_waiters)) {
		result = get_lsn(env, &lsn);
		if (result != 0)
			return result;
	}
	c2_tl_for(txw, &tx->dt_waiters, w) {
		txw_tlist_del(w);
		if (!commit) {
			w->tw_abort(w);
			c2_mutex_lock(&env->d_i.d_lock);
			waiter_fini(w);
			c2_mutex_unlock(&env->d_i.d_lock);
		} else {
			w->tw_commit(w);
			w->tw_i.tw_lsn = lsn;
		}
	} c2_tl_endfor;
	return 0;
}

void tx_fini(struct c2_db_tx *tx)
{
	c2_db_common_tx_fini(tx);
}

static int tx_tol_commit[] = { 0 };
static int tx_tol_abort[] = { 0 };

int c2_db_tx_commit(struct c2_db_tx *tx)
{
	int result;

	result = tx_fini_pre(tx, true);
	if (result == 0)
		result = TX_CALL(tx, commit, DB_TXN_NOSYNC);
	tx_fini(tx);
	return result;
}

int c2_db_tx_abort(struct c2_db_tx *tx)
{
	int result;

	tx_fini_pre(tx, false);
	result = TX_CALL(tx, abort);
	tx_fini(tx);
	return result;
}

void c2_db_tx_waiter_add(struct c2_db_tx *tx, struct c2_db_tx_waiter *w)
{
	struct c2_dbenv *env;

	env = tx->dt_env;

	c2_mutex_lock(&env->d_i.d_lock);
	enw_tlink_init_at(w, &env->d_i.d_waiters);
	c2_mutex_unlock(&env->d_i.d_lock);

	txw_tlink_init_at(w, &tx->dt_waiters);
}

static DBT *pair_key(struct c2_db_pair *pair)
{
	return &pair->dp_key.db_i.db_dbt;
}

static DBT *pair_rec(struct c2_db_pair *pair)
{
	return &pair->dp_rec.db_i.db_dbt;
}

static void pair_prep(struct c2_db_pair *pair)
{
	DBT *key;
	DBT *rec;

	key = pair_key(pair);
	rec = pair_rec(pair);

	C2_PRE(c2_db_pair_invariant(pair));

	if (pair->dp_key.db_type == DBT_ALLOC) {
		c2_free(key->data);
		key->data = NULL;
	}
	if (pair->dp_rec.db_type == DBT_ALLOC) {
		c2_free(rec->data);
		rec->data = NULL;
	}
}

static void db_buf_done(struct c2_db_buf *buf)
{
	if (buf->db_type == DBT_ALLOC) {
		buf->db_buf.b_addr = buf->db_i.db_dbt.data;
		buf->db_buf.b_nob  = buf->db_i.db_dbt.size;
	}
}

static void pair_done(struct c2_db_pair *pair)
{
	db_buf_done(&pair->dp_key);
	db_buf_done(&pair->dp_rec);
	C2_POST(c2_db_pair_invariant(pair));
}

#define WITH_PAIR(pair, action)			\
({						\
	struct c2_db_pair *__pair = (pair);	\
	int                __result;		\
						\
	pair_prep(__pair);			\
	__result = (action);			\
	pair_done(__pair);			\
	__result;				\
});

int c2_table_update(struct c2_db_tx *tx, struct c2_db_pair *pair)
{
	return WITH_PAIR(pair, TABLE_CALL(pair->dp_table, put, tx->dt_i.dt_txn,
					  pair_key(pair), pair_rec(pair), 0));
}

int c2_table_insert(struct c2_db_tx *tx, struct c2_db_pair *pair)
{
	return WITH_PAIR(pair, TABLE_CALL(pair->dp_table, put, tx->dt_i.dt_txn,
					  pair_key(pair), pair_rec(pair),
					  DB_NOOVERWRITE));
}

int c2_table_lookup(struct c2_db_tx *tx, struct c2_db_pair *pair)
{
	/*
	 * Possible optimization: if pair's DBT flags are 0, ->get() would
	 *                        return with DBT->data pointing directly to the
	 *                        in-db data. Returned pointer is valid until
	 *                        _any_ call against the same DB handle is made
	 *                        by any thread.
	 *
	 *                        This gives a 0-copy lookup: embed a mutex in
	 *                        c2_table, lock it in c2_table_lookup() and
	 *                        release in c2_db_rec_fini().
	 *
	 *                        DBT_INPLACE buffer type is reserved for this
	 *                        purpose.
	 */
	return WITH_PAIR(pair, TABLE_CALL(pair->dp_table, get, tx->dt_i.dt_txn,
					  pair_key(pair), pair_rec(pair),
					  0));
}

int c2_table_delete(struct c2_db_tx *tx, struct c2_db_pair *pair)
{
	return WITH_PAIR(pair, TABLE_CALL(pair->dp_table, del, tx->dt_i.dt_txn,
					  pair_key(pair), 0));
}

int c2_db_cursor_init(struct c2_db_cursor *cursor, struct c2_table *table,
		      struct c2_db_tx *tx, uint32_t flags)
{
	cursor->c_flags = 0;
        if (flags & C2_DB_CURSOR_READ_COMMITTED)
                cursor->c_flags |= DB_READ_COMMITTED;
        else if (flags & C2_DB_CURSOR_READ_UNCOMMITTED)
                cursor->c_flags |= DB_READ_UNCOMMITTED;
        else if (flags & C2_DB_CURSOR_RMW)
                cursor->c_flags |= DB_RMW;

	cursor->c_table = table;
	cursor->c_tx    = tx;
	return TABLE_CALL(table, cursor, tx->dt_i.dt_txn,
			  &cursor->c_i.c_dbc, 0);
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
	return WITH_PAIR(pair, CURSOR_CALL(cursor, get, pair_key(pair),
					   pair_rec(pair),
                                           cursor->c_flags | flags));
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

int c2_db_cursor_first(struct c2_db_cursor *cursor, struct c2_db_pair *pair)
{
	return cursor_get(cursor, pair, DB_FIRST);
}

int c2_db_cursor_last(struct c2_db_cursor *cursor, struct c2_db_pair *pair)
{
	return cursor_get(cursor, pair, DB_LAST);
}

int c2_db_cursor_set(struct c2_db_cursor *cursor, struct c2_db_pair *pair)
{
	return WITH_PAIR(pair, CURSOR_CALL(cursor, put, pair_key(pair),
					   pair_rec(pair), DB_CURRENT));
}

int c2_db_cursor_add(struct c2_db_cursor *cursor, struct c2_db_pair *pair)
{
	return WITH_PAIR(pair, CURSOR_CALL(cursor, put, pair_key(pair),
					   pair_rec(pair), DB_KEYFIRST));
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
	C2_ASSERT(table->t_i.t_db == db);
	C2_ASSERT(table->t_ops->key_cmp != NULL);
	return table->t_ops->key_cmp(table, dbt0->data, dbt1->data);
}

/**
   Returns the last LSN used by the data-base environment.
 */
static int get_lsn(struct c2_dbenv *env, DB_LSN *lsn)
{
	int         rc;
	DBT         nonce;
	/* dummy buffer to copy last log record to. */
	static char dummy[20000];

	nonce.data  = dummy;
	nonce.flags = DB_DBT_USERMEM;
	nonce.ulen  = sizeof dummy;

	rc = env->d_i.d_logc->get(env->d_i.d_logc, lsn, &nonce, DB_LAST);
	return db_call_tail(&env->d_addb, rc, "logc::get", NULL);
}

/**
   Per data-base environment thread.

   This thread loops until the environment shutdown starts, doing the following:

   @li de-stage some dirty pages, to guarantee some amount of free memory in the
   pool and to make write-back smoother;

   @li determine what is the last persistent LSN and signal transacaction
   waiters accordingly;

   @li sleep for some time.
 */
static void dbenv_thread(struct c2_dbenv *env)
{
	bool                  last;
	DB_LSN                next;
	DB_LOG_STAT          *st;
	struct c2_dbenv_impl *di;

	di = &env->d_i;

	C2_SET0(&next);
	last = false;
	do {
		int                     rc;
		int                     nr_pages;
		struct c2_db_tx_waiter *w;
		c2_time_t               deadline;
		c2_time_t               delay;

		DBENV_CALL(env, memp_trickle, 10, &nr_pages);
		rc = DBENV_CALL(env, log_stat, &st, 0);
		c2_mutex_lock(&di->d_lock);
		last = di->d_shutdown;
		if (rc == 0) {
			/*
			 * Reconstruct LSN from DB_LOG_STAT fields. This has
			 * been reverse engineered from the db5 sources.
			 */
			next.file   = st->st_disk_file;
			next.offset = st->st_disk_offset;
			c2_free(st);
			c2_tl_for(enw, &di->d_waiters, w) {
				if (log_compare(&w->tw_i.tw_lsn, &next) <= 0) {
					w->tw_persistent(w);
					waiter_fini(w);
				}
			} c2_tl_endfor;
		}
		deadline = c2_time_now();
		c2_time_set(&delay, 1, 0);
		deadline = c2_time_add(deadline, delay);
		c2_cond_timedwait(&di->d_shutdown_cond, &di->d_lock, deadline);
		c2_mutex_unlock(&di->d_lock);
	} while (!last);
	C2_ASSERT(enw_tlist_is_empty(&env->d_i.d_waiters));
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
