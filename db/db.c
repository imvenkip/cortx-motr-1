/* -*- C -*- */
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
 * Original author: Nikita Danilov <nikita_danilov@xyratex.com>
 * Original creation date: 08/13/2010
 */

/*
 * Define the ADDB types in this file.
 */
#undef M0_ADDB_CT_CREATE_DEFINITION
#define M0_ADDB_CT_CREATE_DEFINITION
#undef M0_ADDB_RT_CREATE_DEFINITION
#define M0_ADDB_RT_CREATE_DEFINITION
#include "db/db_addb.h"

#include <stdarg.h>
#include <stdlib.h>    /* free */
#include <sys/stat.h>  /* mkdir */
#include <stdio.h>     /* asprintf, fopen, fclose */

#include "lib/adt.h"   /* m0_buf */
#include "lib/misc.h"  /* M0_SET0 */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"

#include "mero/magic.h"
#include "db/db.h"
#include "db/db_common.h"
#include "db/extmap_seg_xc.h"

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
   started by m0_dbenv_init() once a second learns (by calling
   DBENV->log_stat()) what is the last persistent LSN and signals waiters for
   all transactions with LSNs less than or equal to the last persistent LSN.

   This solution leaves much to be desired: (i) it smells of a hack, (ii)
   get_lsn() is perhaps too expensive to be called on each transaction
   completion, (iii) it relies on undocumented internal structure of LSN, see
   dbenv_thread() for details.

   Alternatively, commit call-back can be added to db5.

   @see http://www.oracle.com/technology/documentation/berkeley-db/db/
api_reference/C/index.html

   @{
 */

struct m0_addb_ctx m0_db_mod_ctx;

static int key_compare(DB *db, const DBT *dbt1, const DBT *dbt2);
static int get_lsn(struct m0_dbenv *env, DB_LSN *lsn);
static void dbenv_thread(struct m0_dbenv *env);

M0_TL_DESCR_DEFINE(enw, "env waiters", static, struct m0_db_tx_waiter,
		   tw_env, tw_magix,
		   M0_DB_TX_WAITER_MAGIC, M0_DB_TX_WAITER_HEAD_MAGIC);
M0_TL_DEFINE(enw, static, struct m0_db_tx_waiter);


/**
   Convert db5 specific error code into generic errno.

   This function is idempotent: dberr_conv(dberr_conv(x)) == dberr_conv(x) for
   all x.

   @see http://www.oracle.com/technology/documentation/berkeley-db/db/
programmer_reference/program_errorret.html
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
	case DB_RUNRECOVERY:
		return -EUCLEAN;
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
static int db_call_tail(struct m0_addb_ctx *ctx, int rc, const char *name,
			int *tolerate)
{
	if (rc != 0) {
		rc = dberr_conv(rc);
		for (; *tolerate != 0 && *tolerate != rc; tolerate++)
			;
		/** @todo:  Use the ctx field in db_call_tail. Will require a
		    context chain to be established through transactions and
		    table objects from the dbenv object.
		    Dbenv creation will have to pass in an external context.
		 */
		if (*tolerate == 0)
			M0_ADDB_FUNC_FAIL(&m0_addb_gmc,
					  M0_DB_ADDB_LOC_CALL_TAIL, rc,
					  &m0_db_mod_ctx);
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

   @note m0_table_lookup() calls ->get() directly.
 */
#define TABLE_CALL(table, method, ...)					\
({									\
	int rc;								\
	struct m0_table *t = (table);					\
									\
	m0_mutex_lock(&t->t_i.t_lock);					\
	rc = t->t_i.t_db->method(t->t_i.t_db , ## __VA_ARGS__);		\
	m0_mutex_unlock(&t->t_i.t_lock);				\
	db_call_tail(&t->t_addb, rc, #method, table_tol_ ## method);	\
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
	struct m0_db_cursor *c = (cur);					\
	struct m0_table     *t = c->c_table;				\
									\
	m0_mutex_lock(&t->t_i.t_lock);					\
	rc = c->c_i.c_dbc->method(c->c_i.c_dbc , ## __VA_ARGS__);	\
	m0_mutex_unlock(&t->t_i.t_lock);				\
	db_call_tail(&t->t_addb, rc, "dbc::" #method,			\
		cursor_tol_ ## method);					\
})

/**
   Helper function: opens a file with a name constructed from printf(3)-like
   format and arguments.
 */
static __attribute__((format(printf, 3, 4))) int
openvar(FILE ** file, const char *mode, const char *fmt, ...)
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
	M0_IMPOSSIBLE("realloc called.");
	return 0;
}

/**
   Major part of m0_dbenv_init().
 */
static int dbenv_setup(struct m0_dbenv *env, const char *name, uint64_t flags)
{
	int                   result;
	DB_ENV               *de;
	struct m0_dbenv_impl *di;

	M0_SET0(env);

	di = &env->d_i;

	m0_dbenv_common_init(env);
	m0_mutex_init(&di->d_lock);
	enw_tlist_init(&di->d_waiters);
	m0_cond_init(&di->d_shutdown_cond, &di->d_lock);
	/*
	 * XXX translate flags from m0 to db5.
	 */
	if (flags == 0)
		flags = DB_CREATE|DB_THREAD|DB_INIT_LOG|DB_INIT_MPOOL|
			DB_INIT_TXN|DB_RECOVER|DB_REGISTER;

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
		de->set_errpfx(de, "m0");
		result = DBENV_CALL(env, set_verbose, DB_VERB_DEADLOCK, 1);
		M0_ASSERT(result == 0);
		result = DBENV_CALL(env, set_verbose, DB_VERB_WAITSFOR, 1);
		M0_ASSERT(result == 0);
		result = DBENV_CALL(env, set_verbose, DB_VERB_RECOVERY, 1);
		M0_ASSERT(result == 0);
		result = DBENV_CALL(env, set_flags, DB_TXN_NOSYNC, 1);
		M0_ASSERT(result == 0);
		result = DBENV_CALL(env, set_lk_detect, DB_LOCK_DEFAULT);
		M0_ASSERT(result == 0);
		result = DBENV_CALL(env, set_alloc, m0_alloc, never, m0_free);
		M0_ASSERT(result == 0);

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
		 * DBENV_CALL(env, set_app_dispatch, m0_fol_dispatch);
		 *
		 * start ->memp_trickle() thread.
		 */
		if (result == 0) {
			result = DBENV_CALL(env, open, name, flags, 0700);
			if (result == 0) {
				result = M0_THREAD_INIT(&di->d_thread,
							struct m0_dbenv *, NULL,
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

int m0_dbenv_init(struct m0_dbenv *env, const char *name, uint64_t flags)
{
	int result;

	result = dbenv_setup(env, name, flags);
	if (result != 0)
		m0_dbenv_fini(env);
	return result;
}

void m0_dbenv_fini(struct m0_dbenv *env)
{
	struct m0_dbenv_impl *di;

	di = &env->d_i;
	if (di->d_env != NULL) {
		DBENV_CALL(env, memp_sync, NULL);
		DBENV_CALL(env, log_flush, NULL);
	}
	if (di->d_thread.t_state == TS_RUNNING) {
		m0_mutex_lock(&di->d_lock);
		di->d_shutdown = true;
		m0_cond_signal(&di->d_shutdown_cond);
		m0_mutex_unlock(&di->d_lock);
		m0_thread_join(&di->d_thread);
		m0_thread_fini(&di->d_thread);
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
	m0_cond_fini(&di->d_shutdown_cond);
	enw_tlist_fini(&di->d_waiters);
	m0_mutex_fini(&di->d_lock);
	m0_dbenv_common_fini(env);
}

M0_INTERNAL int m0_dbenv_sync(struct m0_dbenv *env)
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

M0_INTERNAL int m0_table_init(struct m0_table *table, struct m0_dbenv *env,
			      const char *name, uint64_t flags,
			      const struct m0_table_ops *ops)
{
	int result;
	DB *db;

	m0_mutex_init(&table->t_i.t_lock);
	m0_table_common_init(table, env, ops);
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
		m0_table_fini(table);
	return result;
}

M0_INTERNAL void m0_table_fini(struct m0_table *table)
{
	if (table->t_i.t_db != NULL) {
		TABLE_CALL(table, sync, 0);
		TABLE_CALL(table, close, 0);
		table->t_i.t_db = NULL;
	}
	m0_table_common_fini(table);
	m0_mutex_fini(&table->t_i.t_lock);
}

M0_INTERNAL void m0_db_buf_impl_init(struct m0_db_buf *buf)
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
		M0_IMPOSSIBLE("Wrong buffer type.");
	}
}

M0_INTERNAL void m0_db_buf_impl_fini(struct m0_db_buf *buf)
{
}

M0_INTERNAL bool m0_db_buf_impl_invariant(const struct m0_db_buf *buf)
{
	return
                buf->db_i.db_dbt.data == buf->db_buf.b_addr &&
                (buf->db_type == DBT_ALLOC) ?
                buf->db_i.db_dbt.size == buf->db_buf.b_nob :
                buf->db_i.db_dbt.ulen == buf->db_buf.b_nob;
}

M0_INTERNAL int m0_db_tx_init(struct m0_db_tx *tx, struct m0_dbenv *env,
			      uint64_t flags)
{
	int result;
	DB_TXN *txn;

	m0_db_common_tx_init(tx, env);
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

static void waiter_fini(struct m0_db_tx_waiter *w)
{
	enw_tlink_del_fini(w);
	w->tw_done(w);
}

static int tx_fini_pre(struct m0_db_tx *tx, bool commit)
{
	struct m0_db_tx_waiter *w;
	struct m0_dbenv        *env;
	int                     result;
	DB_LSN                  lsn;

	env = tx->dt_env;
	if (commit && !txw_tlist_is_empty(&tx->dt_waiters)) {
		result = get_lsn(env, &lsn);
		if (result != 0)
			return result;
	}
	m0_tl_for(txw, &tx->dt_waiters, w) {
		txw_tlist_del(w);
		if (!commit) {
			w->tw_abort(w);
			m0_mutex_lock(&env->d_i.d_lock);
			waiter_fini(w);
			m0_mutex_unlock(&env->d_i.d_lock);
		} else {
			w->tw_commit(w);
			w->tw_i.tw_lsn = lsn;
		}
	} m0_tl_endfor;
	return 0;
}

M0_INTERNAL void tx_fini(struct m0_db_tx *tx)
{
	m0_db_common_tx_fini(tx);
}

static int tx_tol_commit[] = { 0 };
static int tx_tol_abort[] = { 0 };

M0_INTERNAL int m0_db_tx_commit(struct m0_db_tx *tx)
{
	int result;

	result = tx_fini_pre(tx, true);
	if (result == 0)
		result = TX_CALL(tx, commit, DB_TXN_NOSYNC);
	tx_fini(tx);
	return result;
}

M0_INTERNAL int m0_db_tx_abort(struct m0_db_tx *tx)
{
	int result;

	tx_fini_pre(tx, false);
	result = TX_CALL(tx, abort);
	tx_fini(tx);
	return result;
}

M0_INTERNAL void m0_db_tx_waiter_add(struct m0_db_tx *tx,
				     struct m0_db_tx_waiter *w)
{
	struct m0_dbenv *env;

	env = tx->dt_env;

	m0_mutex_lock(&env->d_i.d_lock);
	enw_tlink_init_at(w, &env->d_i.d_waiters);
	m0_mutex_unlock(&env->d_i.d_lock);

	txw_tlink_init_at(w, &tx->dt_waiters);
}

static DBT *pair_key(struct m0_db_pair *pair)
{
	return &pair->dp_key.db_i.db_dbt;
}

static DBT *pair_rec(struct m0_db_pair *pair)
{
	return &pair->dp_rec.db_i.db_dbt;
}

static void pair_prep(struct m0_db_pair *pair)
{
	DBT *key;
	DBT *rec;

	key = pair_key(pair);
	rec = pair_rec(pair);

	M0_PRE(m0_db_pair_invariant(pair));

	if (pair->dp_key.db_type == DBT_ALLOC) {
		m0_free(key->data);
		key->data = NULL;
	}
	if (pair->dp_rec.db_type == DBT_ALLOC) {
		m0_free(rec->data);
		rec->data = NULL;
	}
}

static void db_buf_done(struct m0_db_buf *buf)
{
	if (buf->db_type == DBT_ALLOC) {
		buf->db_buf.b_addr = buf->db_i.db_dbt.data;
		buf->db_buf.b_nob  = buf->db_i.db_dbt.size;
	}
}

static void pair_done(struct m0_db_pair *pair)
{
	db_buf_done(&pair->dp_key);
	db_buf_done(&pair->dp_rec);
	M0_POST(m0_db_pair_invariant(pair));
}

#define WITH_PAIR(pair, action)			\
({						\
	struct m0_db_pair *__pair = (pair);	\
	int                __result;		\
						\
	pair_prep(__pair);			\
	__result = (action);			\
	pair_done(__pair);			\
	__result;				\
});

M0_INTERNAL int m0_table_update(struct m0_db_tx *tx, struct m0_db_pair *pair)
{
	return WITH_PAIR(pair, TABLE_CALL(pair->dp_table, put, tx->dt_i.dt_txn,
					  pair_key(pair), pair_rec(pair), 0));
}

M0_INTERNAL int m0_table_insert(struct m0_db_tx *tx, struct m0_db_pair *pair)
{
	return WITH_PAIR(pair, TABLE_CALL(pair->dp_table, put, tx->dt_i.dt_txn,
					  pair_key(pair), pair_rec(pair),
					  DB_NOOVERWRITE));
}

M0_INTERNAL int m0_table_lookup(struct m0_db_tx *tx, struct m0_db_pair *pair)
{
	/*
	 * Possible optimization: if pair's DBT flags are 0, ->get() would
	 *                        return with DBT->data pointing directly to the
	 *                        in-db data. Returned pointer is valid until
	 *                        _any_ call against the same DB handle is made
	 *                        by any thread.
	 *
	 *                        This gives a 0-copy lookup: embed a mutex in
	 *                        m0_table, lock it in m0_table_lookup() and
	 *                        release in m0_db_rec_fini().
	 *
	 *                        DBT_INPLACE buffer type is reserved for this
	 *                        purpose.
	 */
	return WITH_PAIR(pair, TABLE_CALL(pair->dp_table, get, tx->dt_i.dt_txn,
					  pair_key(pair), pair_rec(pair),
					  0));
}

M0_INTERNAL int m0_table_delete(struct m0_db_tx *tx, struct m0_db_pair *pair)
{
	return WITH_PAIR(pair, TABLE_CALL(pair->dp_table, del, tx->dt_i.dt_txn,
					  pair_key(pair), 0));
}

M0_INTERNAL int m0_db_cursor_init(struct m0_db_cursor *cursor,
				  struct m0_table *table, struct m0_db_tx *tx,
				  uint32_t flags)
{
	cursor->c_flags = 0;
	cursor->c_table = table;
	cursor->c_tx    = tx;
	return TABLE_CALL(table, cursor, tx->dt_i.dt_txn,
			  &cursor->c_i.c_dbc, 0);
}

static int cursor_tol_close[] = { 0 };
static int cursor_tol_get[] = { -ENOENT, 0 };
static int cursor_tol_put[] = { 0 };
static int cursor_tol_del[] = { 0 };

M0_INTERNAL void m0_db_cursor_fini(struct m0_db_cursor *cursor)
{
	CURSOR_CALL(cursor, close);
}

static int cursor_get(struct m0_db_cursor *cursor, struct m0_db_pair *pair,
		      uint32_t flags)
{
	return WITH_PAIR(pair, CURSOR_CALL(cursor, get, pair_key(pair),
					   pair_rec(pair),
                                           cursor->c_flags | flags));
}

M0_INTERNAL int m0_db_cursor_get(struct m0_db_cursor *cursor,
				 struct m0_db_pair *pair)
{
	return cursor_get(cursor, pair, DB_SET_RANGE);
}

M0_INTERNAL int m0_db_cursor_next(struct m0_db_cursor *cursor,
				  struct m0_db_pair *pair)
{
	return cursor_get(cursor, pair, DB_NEXT);
}

M0_INTERNAL int m0_db_cursor_prev(struct m0_db_cursor *cursor,
				  struct m0_db_pair *pair)
{
	return cursor_get(cursor, pair, DB_PREV);
}

M0_INTERNAL int m0_db_cursor_first(struct m0_db_cursor *cursor,
				   struct m0_db_pair *pair)
{
	return cursor_get(cursor, pair, DB_FIRST);
}

M0_INTERNAL int m0_db_cursor_last(struct m0_db_cursor *cursor,
				  struct m0_db_pair *pair)
{
	return cursor_get(cursor, pair, DB_LAST);
}

M0_INTERNAL int m0_db_cursor_set(struct m0_db_cursor *cursor,
				 struct m0_db_pair *pair)
{
	return WITH_PAIR(pair, CURSOR_CALL(cursor, put, pair_key(pair),
					   pair_rec(pair), DB_CURRENT));
}

M0_INTERNAL int m0_db_cursor_add(struct m0_db_cursor *cursor,
				 struct m0_db_pair *pair)
{
	return WITH_PAIR(pair, CURSOR_CALL(cursor, put, pair_key(pair),
					   pair_rec(pair), DB_KEYFIRST));
}

M0_INTERNAL int m0_db_cursor_del(struct m0_db_cursor *cursor)
{
	return CURSOR_CALL(cursor, del, 0);
}

M0_INTERNAL int m0_db_init(void)
{
	m0_addb_ctx_type_register(&m0_addb_ct_db_mod);
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_db_mod_ctx,
			 &m0_addb_ct_db_mod, &m0_addb_proc_ctx);
	m0_xc_extmap_seg_init();
	return 0;
}

M0_INTERNAL void m0_db_fini(void)
{
        m0_addb_ctx_fini(&m0_db_mod_ctx);
	m0_xc_extmap_seg_fini();
}

static int key_compare(DB *db, const DBT *dbt0, const DBT *dbt1)
{
	struct m0_table *table;

	table = db->app_private;
	M0_ASSERT(table->t_i.t_db == db);
	M0_ASSERT(table->t_ops->key_cmp != NULL);
	return table->t_ops->key_cmp(table, dbt0->data, dbt1->data);
}

/**
   Returns the last LSN used by the data-base environment.
 */
static int get_lsn(struct m0_dbenv *env, DB_LSN *lsn)
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
static void dbenv_thread(struct m0_dbenv *env)
{
	bool                  last;
	DB_LSN                next;
	DB_LOG_STAT          *st;
	struct m0_dbenv_impl *di;

	di = &env->d_i;

	M0_SET0(&next);
	last = false;
	do {
		int                     rc;
		int                     nr_pages;
		struct m0_db_tx_waiter *w;

		DBENV_CALL(env, memp_trickle, 10, &nr_pages);
		rc = DBENV_CALL(env, log_stat, &st, 0);
		m0_mutex_lock(&di->d_lock);
		last = di->d_shutdown;
		if (rc == 0) {
			/*
			 * Reconstruct LSN from DB_LOG_STAT fields. This has
			 * been reverse engineered from the db5 sources.
			 */
			next.file   = st->st_disk_file;
			next.offset = st->st_disk_offset;
			m0_free(st);
			m0_tl_for(enw, &di->d_waiters, w) {
				if (log_compare(&w->tw_i.tw_lsn, &next) <= 0) {
					w->tw_persistent(w);
					waiter_fini(w);
				}
			} m0_tl_endfor;
		}
		m0_cond_timedwait(&di->d_shutdown_cond, m0_time_from_now(1, 0));
		m0_mutex_unlock(&di->d_lock);
	} while (!last);
	M0_ASSERT(enw_tlist_is_empty(&env->d_i.d_waiters));
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
