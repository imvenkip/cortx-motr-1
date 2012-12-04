/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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

#pragma once

#ifndef __MERO_DB_DB_H__
#define __MERO_DB_DB_H__

#include "addb/addb.h"
#include "lib/tlist.h"
#include "lib/adt.h"           /* m0_buf */

/**
   @defgroup db Data-base interfaces.

   This file defines interfaces for access to a simple indexing mechanism,
   similar to data-base tables with primary index. Currently this interface is
   implemented on top of Oracle db5 (see Sleepycat's Berkeley DB) and details of
   this implementation leak into data-structures. In the future, additional
   implementations will be added, specifically a simple memory-only
   implementation for Linux kernel, and the separation between generic and
   implementation-specific state will be exacted.

   Main data-types introduced here are:

   @li data-base environment (m0_dbenv), where tables are located in and to
   which transactions are confined;

   @li data-base table (m0_table): a container for records indexed by key;

   @li transaction (m0_db_tx): a group of operations over tables that is atomic
   and isolated (in the standard data-base sense of these words);

   Auxiliary data-types are:

   @li table cursor (m0_db_cursor) used to iterate over table records, and

   @li transaction waiter (m0_db_tx_waiter) used to get notifications of (or to
   wait until) transaction state changes.

   @{
 */

struct m0_dbenv;
struct m0_table;
struct m0_table_ops;
struct m0_db_rec;
struct m0_db_tx;
struct m0_buf;

#ifdef __KERNEL__
#include "db/linux_kernel/db_impl.h"
#else
#include "db/db_impl.h"
#endif

/** Data-base environment.

    m0_dbenv represents a collection of related tables (m0_table). Transactions
    (m0_db_tx) cannot cross data-base environment boundary.
 */
struct m0_dbenv {
	struct m0_dbenv_impl d_i;
	/** ADDB context for events related to this environment. */
	struct m0_addb_ctx   d_addb;
};

/**
   Initialize data-base environment.

   @param name environment name, which is a directory where data-base files are
               stored. This directory is created if absent.
   @param flags environment flags. Currently db5 flags are used.

   @see http://www.oracle.com/technology/documentation/berkeley-db/db/api_reference/C/envopen.html
 */
int m0_dbenv_init(struct m0_dbenv *env, const char *name, uint64_t flags);

/**
   Finalize the data-base environment and release all associated resources.
 */
void m0_dbenv_fini(struct m0_dbenv *env);

/**
   When this call returns, results of all operations against the environment
   that completed before this call started are guaranteed to be persistent.
 */
M0_INTERNAL int m0_dbenv_sync(struct m0_dbenv *env);

/**
    Data-base table.

    A m0_table is (notionally) a container of (key, record) pairs. A new pair
    can be inserted, an existing pair can be deleted, given its key, and a
    record with a given key can be looked up.

    m0_table operations are scalable and efficient (O(log(N)), where N is number
    of pairs in the table). It is implemented over B-tree.

    @note currently, interface enforces key uniqueness although underlying
    implementation supports duplicates.

    @note confusingly, db5 refers to a table as "a data-base".
 */
struct m0_table {
	/** an environment this table is in. */
	struct m0_dbenv           *t_env;
	struct m0_table_impl       t_i;
	/**
	    operations vector.

	    Table operations are needed to integrate table "container"
	    abstraction with the underlying B-tree:

	    @li they define size of keys and records, so that B-tree knows how
	    much memory copy to or from user-supplied buffers;

	    @li they define conversions between in-memory and in-db formats of
	    keys and records;

	    @li they define key comparison function. B-tree requires keys to be
	    totally ordered. Particular ordering affects (key, record) pair
	    placement in the tree and might have serious performance
	    implications.
	 */
	const struct m0_table_ops *t_ops;
	/** an ADDB context for events related to this table. */
	struct m0_addb_ctx         t_addb;
};

/**
   Initialize a table.

   @param name - table name. In db5 this is a name of the file where table is
   stored.

   @param flags - table flags. Currently db5 flags are used.

   @see http://www.oracle.com/technology/documentation/berkeley-db/db/api_reference/C/dbopen.html
 */
M0_INTERNAL int m0_table_init(struct m0_table *table, struct m0_dbenv *env,
			      const char *name, uint64_t flags,
			      const struct m0_table_ops *ops);

/**
    Finalize the table and release all resources associated with it.
 */
M0_INTERNAL void m0_table_fini(struct m0_table *table);

/**
   How a memory buffer (for a key or a record) in a pair is allocated and who
   owns it.

   @see m0_db_buf
 */
enum m0_db_buf_type {
	DBT_ZERO,
	DBT_COPYOUT,
	DBT_ALLOC,
	DBT_INPLACE,
	DBT_NR
};

struct m0_db_buf {
	enum m0_db_buf_type   db_type;
	bool                  db_static;
	struct m0_buf         db_buf;
	struct m0_db_buf_impl db_i;
};

/**
   Pair of buffers for data-base operations.

   m0_db_pair is a descriptor of buffers where user supplied key and record are
   stored in and where data-base supplied key and record are retrieved to.

   m0_db_pair also describes the method of memory buffer allocation (and their
   ownership) used for exchanging data with the underlying data-base.
 */
struct m0_db_pair {
	struct m0_table  *dp_table;
	struct m0_db_buf  dp_key;
	struct m0_db_buf  dp_rec;
};

M0_INTERNAL void m0_db_pair_fini(struct m0_db_pair *pair);

/**
   Initialise a pair and set buffers to the given values.

 */
M0_INTERNAL void m0_db_pair_setup(struct m0_db_pair *pair,
				  struct m0_table *table, void *keybuf,
				  uint32_t keysize, void *recbuf,
				  uint32_t recsize);

/**
   Finalize the record returned by m0_table_lookup().
 */
M0_INTERNAL void m0_db_pair_release(struct m0_db_pair *pair);

enum {
	TO_KEY,
	TO_REC,
	TO_NR
};

/**
   Table operations vector.

   @see m0_table
 */
struct m0_table_ops {
	struct {
		/**
		    Maximal size of key or record (as determined by the index in
		    m0_table_ops::to).
		 */
		uint32_t max_size;
		/**
		   Convert in-memory key or record representation to in-db one.

		   @note not currently used.
		 */
		void   (*pack)(struct m0_table *table, uint32_t *size,
			       const void *src, void **dst);
		/**
		   Convert in-db key or record representation to in-memory one.

		   @note not currently used.
		 */
		void   (*open)(struct m0_table *table, uint32_t size,
			       const void *src, void **dst);
	} to[TO_NR];
	/**
	   Key comparison function.

	   Should return -ve, 0 or +ve value depending on how key0 and key1
	   compare in key ordering.
	 */
	int (*key_cmp)(struct m0_table *table,
		       const void *key0, const void *key1);
};

/**
   Data-base transaction.

   A context for a group of data-base updates that are atomic w.r.t. concurrent
   data-base updates and failures.
 */
struct m0_db_tx {
	/** An environment this transaction operates in. */
	struct m0_dbenv     *dt_env;
	/** A list of waiters (m0_db_tx_waiter). */
	struct m0_tl         dt_waiters;
	struct m0_db_tx_impl dt_i;
	/** An ADDB context for events related to this transaction. */
	struct m0_addb_ctx   dt_addb;
};

/**
   Starts a new transaction.

   @param flags - transaction flags. Currently db5 flags are used.

   @see http://www.oracle.com/technology/documentation/berkeley-db/db/api_reference/C/txnbegin.html
 */
M0_INTERNAL int m0_db_tx_init(struct m0_db_tx *tx, struct m0_dbenv *env,
			      uint64_t flags);

/**
   Commits the transaction.

   Commit is _not_ durable: log is not forced out. Transaction is invalid after
   this function returns.
 */
M0_INTERNAL int m0_db_tx_commit(struct m0_db_tx *tx);

/**
   Aborts the transaction.

   Transaction is invalid after this returns.
 */
M0_INTERNAL int m0_db_tx_abort(struct m0_db_tx *tx);

/**
   An anchor to wait for transaction state change.

   Liveness.

   Once m0_db_tx_waiter::tw_commit() has been called, it is guaranteed that
   m0_db_tx_waiter::tw_persistent() would eventually be called.

   The implementation calls m0_db_tx_waiter::tw_done() as the last call-back and
   won't touch the waiter afterwards. It is up to the caller to free the waiter
   data-structure (e.g., this can be done inside of m0_db_tx_waiter::tw_done()).
 */
struct m0_db_tx_waiter {
	/** Called when the transaction is committed */
	void                      (*tw_commit)(struct m0_db_tx_waiter *w);
	/** Called when the transaction is aborted */
	void                      (*tw_abort) (struct m0_db_tx_waiter *w);
	/** Called when a committed transaction becomes persistent. */
	void                      (*tw_persistent)(struct m0_db_tx_waiter *w);
	/** Called when no further call-backs will be coming. */
	void                      (*tw_done)(struct m0_db_tx_waiter *w);
	/** Linkage into a list of all waiters for data-base environment. */
	struct m0_tlink             tw_env;
	/** Linkage into a list of all waiters for a given transaction. */
	struct m0_tlink             tw_tx;
	struct m0_db_tx_waiter_impl tw_i;
	uint64_t                    tw_magix;
};

M0_TL_DESCR_DECLARE(txw, M0_EXTERN);
M0_TL_DEFINE(txw, static inline, struct m0_db_tx_waiter);

/**
   Adds a waiter for a transaction.

   Waiters call-backs will be called when the transaction changes its state
   appropriately.
 */
M0_INTERNAL void m0_db_tx_waiter_add(struct m0_db_tx *tx,
				     struct m0_db_tx_waiter *w);

/**
   Inserts (key, rec) pair into table as part of transaction tx.
 */
M0_INTERNAL int m0_table_insert(struct m0_db_tx *tx, struct m0_db_pair *pair);

/**
   Updates (key, rec) pair into table as part of transaction tx.
 */
M0_INTERNAL int m0_table_update(struct m0_db_tx *tx, struct m0_db_pair *pair);


/**
   Looks up a record with a given key in the table and returns it.

   Returned record must be finalized with a call to m0_db_pair_fini().

   @note lookup does require transaction (for locking context at least).

   @note no alignment guarantees on returned record.
 */
M0_INTERNAL int m0_table_lookup(struct m0_db_tx *tx, struct m0_db_pair *pair);

/**
   Delete a record with the key in the table as part of transaction tx.
 */
M0_INTERNAL int m0_table_delete(struct m0_db_tx *tx, struct m0_db_pair *pair);

/**
   Data-base cursor.

   A cursor can be positioned at a given (key, rec) in a table, moved around
   (m0_db_cursor_next(), m0_db_cursor_prev()) and used to update the table
   (m0_db_cursor_set(), m0_db_cursor_add(), m0_db_cursor_del()).
 */
struct m0_db_cursor {
        uint32_t                 c_flags;
	struct m0_table         *c_table;
	struct m0_db_tx         *c_tx;
	struct m0_db_cursor_impl c_i;
};

/**
 * Cursor flags
 */
enum m0_db_cursor_flags {
        M0_DB_CURSOR_READ_UNCOMMITTED  = 1 << 0,
        M0_DB_CURSOR_READ_COMMITTED    = 1 << 1,
        M0_DB_CURSOR_RMW               = 1 << 2,
};

/**
   Initialise a cursor.

   The cursor is not initially positioned anywhere. All operations with the
   cursor will be done in the context of a given transaction.
 */
M0_INTERNAL int m0_db_cursor_init(struct m0_db_cursor *cursor,
				  struct m0_table *table, struct m0_db_tx *tx,
				  uint32_t flags);

/**
   Release the resources associated with the cursor.
 */
M0_INTERNAL void m0_db_cursor_fini(struct m0_db_cursor *cursor);

/**
   Position the cursor at the (key, rec) pair with the least key not less than
   the key of a given pair.
 */
M0_INTERNAL int m0_db_cursor_get(struct m0_db_cursor *cursor,
				 struct m0_db_pair *pair);
/** Move cursor to the next key */
M0_INTERNAL int m0_db_cursor_next(struct m0_db_cursor *cursor,
				  struct m0_db_pair *pair);
/** Move cursor to the previous key */
M0_INTERNAL int m0_db_cursor_prev(struct m0_db_cursor *cursor,
				  struct m0_db_pair *pair);
/** Move cursor to the first key in the table */
M0_INTERNAL int m0_db_cursor_first(struct m0_db_cursor *cursor,
				   struct m0_db_pair *pair);
/** Move cursor to the last key in the table */
M0_INTERNAL int m0_db_cursor_last(struct m0_db_cursor *cursor,
				  struct m0_db_pair *pair);
/** Change the key and record of the current cursor pair.  */
M0_INTERNAL int m0_db_cursor_set(struct m0_db_cursor *cursor,
				 struct m0_db_pair *pair);
/** Add new pair to the table and position the cursor on it. */
M0_INTERNAL int m0_db_cursor_add(struct m0_db_cursor *cursor,
				 struct m0_db_pair *pair);
/**
    Delete the current pair from the table. For the purpose of following
    m0_db_cursor_next() and m0_db_cursor_prev() calls, the cursor remains
    positioned over the deleted pair.

    Following calls to m0_db_cursor_del() and m0_db_cursor_set() in the same
    cursor position will fail.
 */
M0_INTERNAL int m0_db_cursor_del(struct m0_db_cursor *cursor);

M0_INTERNAL int m0_db_init(void);
M0_INTERNAL void m0_db_fini(void);

/** @} end of db group */

/* __MERO_DB_REC_H__ */
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
