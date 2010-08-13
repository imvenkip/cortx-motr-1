/* -*- C -*- */

#ifndef __COLIBRI_DB_DB_H__
#define __COLIBRI_DB_DB_H__

#include_next <db.h>

#include "lib/types.h"
#include "addb/addb.h"

/**
   @defgroup db Data-base interfaces.

   Data-base access interfaces based on Oracle db5.

   @{
 */

struct c2_dbenv;
struct c2_table;
struct c2_table_ops;
struct c2_db_rec;
struct c2_db_tx;

struct c2_dbenv {
	DB_ENV            *d_env;
	struct c2_addb_ctx d_addb;

	/** File stream where error messages for this dbenv are sent to. */
	FILE              *d_errlog;
	/** File stream where informational messages for this dbenv are sent
	    to. */
	FILE              *d_msglog;
};

int  c2_dbenv_init(struct c2_dbenv *env, const char *name, uint64_t flags);
void c2_dbenv_fini(struct c2_dbenv *env);

struct c2_table {
	struct c2_dbenv           *t_env;
	DB                        *t_db;
	const struct c2_table_ops *t_ops;
	struct c2_addb_ctx         t_addb;
};

int  c2_table_init(struct c2_table *table, struct c2_dbenv *env, 
		   const char *name, uint64_t flags, 
		   const struct c2_table_ops *ops);
void c2_table_fini(struct c2_table *table);

enum {
	TO_KEY,
	TO_REC,
	TO_NR
};

struct c2_table_ops {
	struct {
		uint32_t size;
		void   (*pack)(struct c2_table *table, uint32_t *size,
			       const void *src, void **dst);
		void   (*open)(struct c2_table *table, uint32_t size,
			       const void *src, void **dst);
	} to[TO_NR];
	int    (*key_cmp)(struct c2_table *table,
			  const void *key0, const void *key1);
};

struct c2_db_tx {
	struct c2_dbenv *dt_env;
	DB_TXN          *dt_txn;
};

int c2_db_tx_init  (struct c2_db_tx *tx, struct c2_dbenv *env, uint64_t flags);
int c2_db_tx_commit(struct c2_db_tx *tx);
int c2_db_tx_abort (struct c2_db_tx *tx);

int c2_table_insert(struct c2_db_tx *tx, struct c2_table *table, void *key, 
		    void *rec);
int c2_table_lookup(struct c2_db_tx *tx, struct c2_table *table, void *key, 
		    void **rec);
int c2_table_delete(struct c2_db_tx *tx, struct c2_table *table, void *key);

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
