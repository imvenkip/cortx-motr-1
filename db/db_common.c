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

#include "lib/adt.h"   /* m0_buf */
#include "lib/misc.h"  /* M0_SET0 */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"

#include "mero/magic.h"
#include "db/db.h"

/**
   @addtogroup db

   <b>db code common for all implementations.</b>

   @{
 */

const struct m0_addb_loc db_loc = {
	.al_name = "db"
};

const struct m0_addb_ctx_type db_env_ctx_type = {
	.act_name = "db-env"
};

const struct m0_addb_ctx_type db_table_ctx_type = {
	.act_name = "db-table"
};

const struct m0_addb_ctx_type db_tx_ctx_type = {
	.act_name = "db-tx"
};

M0_TL_DESCR_DEFINE(txw, "tx waiters", M0_INTERNAL, struct m0_db_tx_waiter,
		   tw_tx, tw_magix, M0_DB_TX_WAITER_MAGIC,
		   0xd1550c1ab1ea11ce /* dissociable alice  */);


M0_INTERNAL void m0_db_buf_impl_init(struct m0_db_buf *buf);
M0_INTERNAL void m0_db_buf_impl_fini(struct m0_db_buf *buf);
M0_INTERNAL bool m0_db_buf_impl_invariant(const struct m0_db_buf *buf);

M0_INTERNAL void m0_dbenv_common_init(struct m0_dbenv *env)
{
	M0_SET0(env);
	m0_addb_ctx_init(&env->d_addb, &db_env_ctx_type, &m0_addb_global_ctx);
}

M0_INTERNAL void m0_dbenv_common_fini(struct m0_dbenv *env)
{
	m0_addb_ctx_fini(&env->d_addb);
}

M0_INTERNAL void m0_table_common_init(struct m0_table *table,
				      struct m0_dbenv *env,
				      const struct m0_table_ops *ops)
{
	table->t_env = env;
	table->t_ops = ops;
	m0_addb_ctx_init(&table->t_addb, &db_table_ctx_type, &env->d_addb);
}

M0_INTERNAL void m0_table_common_fini(struct m0_table *table)
{
	m0_addb_ctx_fini(&table->t_addb);
}

M0_INTERNAL bool m0_db_buf_invariant(const struct m0_db_buf *buf)
{
	return
		DBT_ZERO < buf->db_type && buf->db_type < DBT_NR &&
		/* in-place buffers are not yet supported */
		buf->db_type != DBT_INPLACE &&
		(buf->db_buf.b_addr != NULL) == (buf->db_buf.b_nob > 0) &&
		ergo(buf->db_static, buf->db_buf.b_nob > 0) &&
		m0_db_buf_impl_invariant(buf);
}

M0_INTERNAL void m0_db_buf_init(struct m0_db_buf *buf,
				enum m0_db_buf_type btype, void *area,
				uint32_t size)
{
	buf->db_type = btype;
	buf->db_buf.b_addr = area;
	buf->db_buf.b_nob  = size;
	m0_db_buf_impl_init(buf);
	M0_ASSERT(m0_db_buf_invariant(buf));
}

M0_INTERNAL void m0_db_buf_fini(struct m0_db_buf *buf)
{
	M0_ASSERT(m0_db_buf_invariant(buf));
	m0_db_buf_impl_fini(buf);
	if (!buf->db_static) {
		m0_free(buf->db_buf.b_addr);
		buf->db_buf.b_addr = NULL;
	}
}

M0_INTERNAL void m0_db_buf_steal(struct m0_db_buf *buf)
{
	M0_PRE(buf->db_type == DBT_ALLOC);
	buf->db_buf.b_addr = NULL;
	buf->db_buf.b_nob  = 0;
}

M0_INTERNAL bool m0_db_pair_invariant(const struct m0_db_pair *p)
{
	return
		p->dp_table != NULL &&
		m0_db_buf_invariant(&p->dp_key) &&
		m0_db_buf_invariant(&p->dp_rec);
}

M0_INTERNAL void m0_db_pair_setup(struct m0_db_pair *pair,
				  struct m0_table *table, void *keybuf,
				  uint32_t keysize, void *recbuf,
				  uint32_t recsize)
{
	M0_PRE((keybuf != NULL) == (keysize > 0));
	M0_PRE((recbuf != NULL) == (recsize > 0));

	M0_SET0(pair);
	pair->dp_table = table;

	if (keybuf != NULL) {
		m0_db_buf_init(&pair->dp_key, DBT_COPYOUT, keybuf, keysize);
		pair->dp_key.db_static = true;
	} else
		m0_db_buf_init(&pair->dp_key, DBT_ALLOC, NULL, 0);

	if (recbuf != NULL) {
		m0_db_buf_init(&pair->dp_rec, DBT_COPYOUT, recbuf, recsize);
		pair->dp_rec.db_static = true;
	} else
		m0_db_buf_init(&pair->dp_rec, DBT_ALLOC, NULL, 0);
	M0_POST(m0_db_pair_invariant(pair));
}

M0_INTERNAL void m0_db_pair_fini(struct m0_db_pair *pair)
{
	M0_PRE(m0_db_pair_invariant(pair));
	m0_db_buf_fini(&pair->dp_rec);
	m0_db_buf_fini(&pair->dp_key);
	M0_SET0(pair);
}

M0_INTERNAL void m0_db_pair_release(struct m0_db_pair *pair)
{
}

M0_INTERNAL int m0_db_tx_is_active(const struct m0_db_tx *tx)
{
        return tx->dt_env != NULL;
}

M0_INTERNAL void m0_db_common_tx_init(struct m0_db_tx *tx, struct m0_dbenv *env)
{
	tx->dt_env = env;
	txw_tlist_init(&tx->dt_waiters);
	m0_addb_ctx_init(&tx->dt_addb, &db_tx_ctx_type, &env->d_addb);
}

M0_INTERNAL void m0_db_common_tx_fini(struct m0_db_tx *tx)
{
	m0_addb_ctx_fini(&tx->dt_addb);
	txw_tlist_fini(&tx->dt_waiters);
	tx->dt_env = NULL;
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
