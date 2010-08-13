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
   implementation for Linux kernel, and separation between generic and
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
		    Size of key or record (as determined by the index in
		    c2_table_ops::to). 

		    Currently only fixed size keys and records are supported. In
		    the future, methods will be added to this operation vector
		    to determine key or record size.
		 */
		uint32_t size;
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
	int    (*key_cmp)(struct c2_table *table,
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
   Commit the transaction.

   Commit is _not_ durable: log is not forced out. Transaction is invalid after
   this returns.
 */
int c2_db_tx_commit(struct c2_db_tx *tx);

/**
   Commit the transaction.

   Transaction is invalid after this returns.
 */
int c2_db_tx_abort (struct c2_db_tx *tx);

/**
   Insert (key, rec) pair into table as part of transaction tx.
 */
int c2_table_insert(struct c2_db_tx *tx, struct c2_table *table, void *key, 
		    void *rec);
/**
   Look up and return record with a given key in the table and returns it.

   Returned record must be finalized with a call to c2_db_rec_fini().

   @note lookup does require transaction (for locking context at least).

   @note no alignment guarantees on returned record.
 */
int c2_table_lookup(struct c2_db_tx *tx, struct c2_table *table, void *key, 
		    void **rec);
/**
   Delete a record with the key in the table as part of transaction tx.
 */
int c2_table_delete(struct c2_db_tx *tx, struct c2_table *table, void *key);

/**
   Finalize the record returned by c2_table_lookup().
 */
void c2_db_rec_fini(const struct c2_table *table, void *rec);

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
