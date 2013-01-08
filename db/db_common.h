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

#ifndef __MERO_DB_DB_COMMON_H__
#define __MERO_DB_DB_COMMON_H__

#include "db/db.h"

/**
   @addtogroup db

   @{
 */

M0_INTERNAL void m0_dbenv_common_init(struct m0_dbenv *env);
M0_INTERNAL void m0_dbenv_common_fini(struct m0_dbenv *env);
M0_INTERNAL void m0_table_common_init(struct m0_table *table,
				      struct m0_dbenv *env,
				      const struct m0_table_ops *ops);
M0_INTERNAL void m0_table_common_fini(struct m0_table *table);
M0_INTERNAL bool m0_db_buf_invariant(const struct m0_db_buf *buf);
M0_INTERNAL void m0_db_buf_init(struct m0_db_buf *buf,
				enum m0_db_buf_type btype, void *area,
				uint32_t size);
M0_INTERNAL void m0_db_buf_fini(struct m0_db_buf *buf);
M0_INTERNAL void m0_db_buf_steal(struct m0_db_buf *buf);
M0_INTERNAL bool m0_db_pair_invariant(const struct m0_db_pair *p);
M0_INTERNAL void m0_db_pair_setup(struct m0_db_pair *pair,
				  struct m0_table *table, void *keybuf,
				  uint32_t keysize, void *recbuf,
				  uint32_t recsize);
M0_INTERNAL void m0_db_pair_fini(struct m0_db_pair *pair);
M0_INTERNAL void m0_db_pair_release(struct m0_db_pair *pair);
M0_INTERNAL void m0_db_common_tx_init(struct m0_db_tx *tx,
				      struct m0_dbenv *env);
M0_INTERNAL void m0_db_common_tx_fini(struct m0_db_tx *tx);

M0_INTERNAL int m0_db_tx_is_active(const struct m0_db_tx *tx);

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
