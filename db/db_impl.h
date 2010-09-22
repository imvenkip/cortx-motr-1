/* -*- C -*- */

#ifndef __COLIBRI_DB_DB_IMPL_H__
#define __COLIBRI_DB_DB_IMPL_H__

#include_next <db.h>

#include "lib/types.h"
#include "lib/thread.h"
#include "lib/list.h"
#include "lib/mutex.h"

/**
   @addtogroup db Data-base interfaces.

   @{
 */

struct c2_dbenv;
struct c2_table;
struct c2_table_ops;
struct c2_db_rec;
struct c2_db_tx;

/** Data-base environment.

    c2_dbenv represents a collection of related tables (c2_table). Transactions
    (c2_db_tx) cannot cross data-base environment boundary.
 */
struct c2_dbenv_impl {
	/** db5 private handle */
	DB_ENV            *d_env;
	/** File stream where error messages for this dbenv are sent to. */
	FILE              *d_errlog;
	/** File stream where informational messages for this dbenv are sent
	    to. */
	FILE              *d_msglog;
	/** Log cursor used to determine the current LSN. */
	DB_LOGC           *d_logc;
	/** Lock protecting waiters list. */
	struct c2_mutex    d_lock;
	/** A list of waiters (c2_db_tx_waiter). */
	struct c2_list     d_waiters;
	/** Thread for asynchronous environment related work. */
	struct c2_thread   d_thread;
	/** True iff the environment is being shut down. */
	bool               d_shutdown;
};

struct c2_table_impl {
	/** db5 private table handle. */
	DB                        *t_db;
};

struct c2_db_pair_impl {
	DBT                   dp_key;
	DBT                   dp_rec;
};

struct c2_db_tx_impl {
	/** A db5 private transaction handle. */
	DB_TXN            *dt_txn;
};

struct c2_db_tx_waiter_impl {
	/** An lsn from the transaction this wait is for. */
	DB_LSN              tw_lsn;
};

struct c2_db_cursor_impl {
	DBC             *c_dbc;
};

/** @} end of db group */

/* __COLIBRI_DB_REC_H__ */
#endif

/* 
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
