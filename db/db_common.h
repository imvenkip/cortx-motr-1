/* -*- C -*- */

#ifndef __COLIBRI_DB_DB_COMMON_H__
#define __COLIBRI_DB_DB_COMMON_H__

#include "db/db.h"

/**
   @addtogroup db

   @{
 */

void c2_dbenv_common_init(struct c2_dbenv *env);
void c2_dbenv_common_fini(struct c2_dbenv *env);
void c2_table_common_init(struct c2_table *table, struct c2_dbenv *env,
			  const struct c2_table_ops *ops);
void c2_table_common_fini(struct c2_table *table);
bool c2_db_buf_invariant (const struct c2_db_buf *buf);
void c2_db_buf_init      (struct c2_db_buf *buf, enum c2_db_buf_type btype,
			  void *area, uint32_t size);
void c2_db_buf_fini      (struct c2_db_buf *buf);
void c2_db_buf_steal     (struct c2_db_buf *buf);
bool c2_db_pair_invariant(const struct c2_db_pair *p);
void c2_db_pair_setup    (struct c2_db_pair *pair, struct c2_table *table,
			  void *keybuf, uint32_t keysize, 
			  void *recbuf, uint32_t recsize);
void c2_db_pair_fini     (struct c2_db_pair *pair);
void c2_db_pair_release  (struct c2_db_pair *pair);
void c2_db_common_tx_init(struct c2_db_tx *tx, struct c2_dbenv *env);
void c2_db_common_tx_fini(struct c2_db_tx *tx);

extern const struct c2_addb_loc      db_loc;
extern const struct c2_addb_ctx_type db_env_ctx_type;
extern const struct c2_addb_ctx_type db_table_ctx_type;
extern const struct c2_addb_ctx_type db_tx_ctx_type;

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
