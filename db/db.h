/* -*- C -*- */

#ifndef __COLIBRI_DB_DB_H__
#define __COLIBRI_DB_DB_H__

#include_next <db.h>

#include "lib/types.h"
#include "addb/addb.h"

/**
   @defgroup db Data-base interfaces.

   This file defines interfaces for access to a simple indexing mechanism,
   similar to data-base tables with primary index. Currently this interface is
   implemented on top of Oracle db5 (nee Sleepycat's Berkeley DB) and details of
   this implementation leak into data-structures. In the future, additional
   implementations will be added, specifically a simple memory-only
   implementation for Linux kernel, and the separation between generic and
   implementation-specific state will be exacted.

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
struct c2_dbenv {
	/** db5 private handle */
	DB_ENV            *d_env;
	/** ADDB context for events related to this environment. */
	struct c2_addb_ctx d_addb;
	/** File stream where error messages for this dbenv are sent to. */
	FILE              *d_errlog;
	/** File stream where informational messages for this dbenv are sent
	    to. */
	FILE              *d_msglog;
};

/**
   Initialize data-base environment.

   @param name environment name, which is a directory where data-base files are
               stored. This directory is created if absent.
   @param flags environment flags. Currently db5 flags are used.

   @see http://www.oracle.com/technology/documentation/berkeley-db/db/api_reference/C/envopen.html
 */
int  c2_dbenv_init(struct c2_dbenv *env, const char *name, uint64_t flags);

/**
   Finalize the data-base environment and release all associated resources.
 */
void c2_dbenv_fini(struct c2_dbenv *env);

/**
    Data-base table.

    A c2_table is (notionally) a container of (key, record) pairs. A new pair
    can be inserted, an existing pair can be deleted, given its key, and a
    record with a given key can be looked up.

    c2_table operations are scalable and efficient (O(log(N)), where N is number
    of pairs in the table). It is implemented over B-tree.

    @note currently, interface enforces key uniqueness although underlying
    implementation supports duplicates.

    @note confusingly, db5 refers to a table as "a data-base".
 */
struct c2_table {
	/** an environment this table is in. */
	struct c2_dbenv           *t_env;
	/** db5 private table handle. */
	DB                        *t_db;
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
	const struct c2_table_ops *t_ops;
	/** an ADDB context for events related to this table. */
	struct c2_addb_ctx         t_addb;
};

/**
   Initialize a table.

   @param name - table name. In db5 this is a name of the file where table is
   stored.

   @param flags - table flags. Currently db5 flags are used.

   @see http://www.oracle.com/technology/documentation/berkeley-db/db/api_reference/C/dbopen.html
 */
int  c2_table_init(struct c2_table *table, struct c2_dbenv *env, 
		   const char *name, uint64_t flags, 
		   const struct c2_table_ops *ops);

/**
    Finalize the table and release all resources associated with it.
 */
void c2_table_fini(struct c2_table *table);

enum c2_db_pair_flags {
	DPF_ALLOCATED = 1 << 0
};

/**
   Pair of buffers for data-base operations.

   c2_db_pair is a descriptor of buffers where user supplied key and record are
   stored in and where data-base supplied key and record are retrieved to.
 */
struct c2_db_pair {
	struct c2_table *dp_table;
	void            *dp_keybuf;
	void            *dp_recbuf;
	uint32_t         dp_key_size;
	uint32_t         dp_rec_size;
	DBT              dp_key;
	DBT              dp_rec;
	uint32_t         dp_flags;
};

//int  c2_db_pair_init(struct c2_db_pair *pair, const struct c2_table *table);
void c2_db_pair_fini(struct c2_db_pair *pair);

/**
   Initialise a pair and allocated buffers of maximal size indicated by
   table->t_ops->to[]->max_size.

   Buffers will be freed by c2_db_pair_fini().
 */
int  c2_db_pair_alloc(struct c2_db_pair *pair, struct c2_table *table);

/**
   Initialise a pair and set buffers to the given values.
 */
void c2_db_pair_setup(struct c2_db_pair *pair, struct c2_table *table,
		      void *keybuf, uint32_t keysize, 
		      void *recbuf, uint32_t recsize);

/**
   Finalize the record returned by c2_table_lookup().
 */
void c2_db_pair_release(struct c2_db_pair *pair);

enum {
	TO_KEY,
	TO_REC,
	TO_NR
};

/**
   Table operations vector. 

   @see c2_table
 */
struct c2_table_ops {
	struct {
		/** 
		    Maximal size of key or record (as determined by the index in
		    c2_table_ops::to).
		 */
		uint32_t max_size;
		/**
		   Convert in-memory key or record representation to in-db one.

		   @note not currently used.
		 */
		void   (*pack)(struct c2_table *table, uint32_t *size,
			       const void *src, void **dst);
		/**
		   Convert in-db key or record representation to in-memory one.

		   @note not currently used.
		 */
		void   (*open)(struct c2_table *table, uint32_t size,
			       const void *src, void **dst);
	} to[TO_NR];
	/**
	   Key comparison function. 

	   Should return -ve, 0 or +ve value depending on how key0 and key1
	   compare in key ordering.
	 */
	int (*key_cmp)(struct c2_table *table, 
		       const void *key0, const void *key1);
};

/**
   Data-base transaction.

   A context for a group of data-base updates that are atomic w.r.t. concurrent
   data-base updates and failures.
 */
struct c2_db_tx {
	/** An environment this transaction operates in. */
	struct c2_dbenv   *dt_env;
	/** A db5 private transaction handle. */
	DB_TXN            *dt_txn;
	/** An ADDB context for events related to this transaction. */
	struct c2_addb_ctx dt_addb;
};

/**
   Starts a new transaction.

   @param flags - transaction flags. Currently db5 flags are used.

   @see http://www.oracle.com/technology/documentation/berkeley-db/db/api_reference/C/txnbegin.html
 */
int c2_db_tx_init  (struct c2_db_tx *tx, struct c2_dbenv *env, uint64_t flags);

/**
   Commits the transaction.

   Commit is _not_ durable: log is not forced out. Transaction is invalid after
   this function returns.
 */
int c2_db_tx_commit(struct c2_db_tx *tx);

/**
   Aborts the transaction.

   Transaction is invalid after this returns.
 */
int c2_db_tx_abort (struct c2_db_tx *tx);

/**
   Inserts (key, rec) pair into table as part of transaction tx.
 */
int c2_table_insert(struct c2_db_tx *tx, struct c2_db_pair *pair);

/**
   Updates (key, rec) pair into table as part of transaction tx.
 */
int c2_table_update(struct c2_db_tx *tx, struct c2_db_pair *pair);


/**
   Looks up a record with a given key in the table and returns it.

   Returned record must be finalized with a call to c2_db_pair_fini().

   @note lookup does require transaction (for locking context at least).

   @note no alignment guarantees on returned record.
 */
int c2_table_lookup(struct c2_db_tx *tx, struct c2_db_pair *pair);

/**
   Delete a record with the key in the table as part of transaction tx.
 */
int c2_table_delete(struct c2_db_tx *tx, struct c2_db_pair *pair);

/**
   Data-base cursor.

   A cursor can be positioned at a given (key, rec) in a table, moved around
   (c2_db_cursor_next(), c2_db_cursor_prev()) and used to update the table
   (c2_db_cursor_set(), c2_db_cursor_add(), c2_db_cursor_del()).
 */
struct c2_db_cursor {
	struct c2_table *c_table;
	struct c2_db_tx *c_tx;
	DBC             *c_dbc;
};

/**
   Initialise a cursor.

   The cursor is not initially positioned anywhere. All operations with the
   cursor will be done in the context of a given transaction.
 */
int  c2_db_cursor_init(struct c2_db_cursor *cursor, struct c2_table *table,
		       struct c2_db_tx *tx);

/**
   Release the resources associated with the cursor.
 */
void c2_db_cursor_fini(struct c2_db_cursor *cursor);

/**
   Position the cursor at the (key, rec) pair with the least key not less than
   the key of a given pair.
 */
int c2_db_cursor_get (struct c2_db_cursor *cursor, struct c2_db_pair *pair);
/** Move cursor to the next key */
int c2_db_cursor_next(struct c2_db_cursor *cursor, struct c2_db_pair *pair);
/** Move cursor to the previous key */
int c2_db_cursor_prev(struct c2_db_cursor *cursor, struct c2_db_pair *pair);
/** Change the key and record of the current cursor pair.  */
int c2_db_cursor_set (struct c2_db_cursor *cursor, struct c2_db_pair *pair);
/** Add new pair to the table and position the cursor on it. */
int c2_db_cursor_add (struct c2_db_cursor *cursor, struct c2_db_pair *pair);
/**
    Delete the current pair from the table. For the purpose of following
    c2_db_cursor_next() and c2_db_cursor_prev() calls, the cursor remains
    positioned over the deleted pair.

    Following calls to c2_db_cursor_del() and c2_db_cursor_set() in the same
    cursor position will fail.
 */
int c2_db_cursor_del (struct c2_db_cursor *cursor);

int  c2_db_init(void);
void c2_db_fini(void);

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
