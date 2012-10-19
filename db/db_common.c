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

#include "lib/adt.h"   /* c2_buf */
#include "lib/misc.h"  /* C2_SET0 */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"

#include "colibri/magic.h"
#include "db/db.h"

/**
   @addtogroup db

   <b>db code common for all implementations.</b>

   @{
 */

const struct c2_addb_loc db_loc = {
	.al_name = "db"
};

const struct c2_addb_ctx_type db_env_ctx_type = {
	.act_name = "db-env"
};

const struct c2_addb_ctx_type db_table_ctx_type = {
	.act_name = "db-table"
};

const struct c2_addb_ctx_type db_tx_ctx_type = {
	.act_name = "db-tx"
};

C2_TL_DESCR_DEFINE(txw,
		   "tx waiters", , struct c2_db_tx_waiter, tw_tx, tw_magix,
		   C2_DB_TX_WAITER_MAGIC,
		   0xd1550c1ab1ea11ce /* dissociable alice  */);


extern void c2_db_buf_impl_init(struct c2_db_buf *buf);
extern void c2_db_buf_impl_fini(struct c2_db_buf *buf);
extern bool c2_db_buf_impl_invariant(const struct c2_db_buf *buf);

void c2_dbenv_common_init(struct c2_dbenv *env)
{
	C2_SET0(env);
	c2_addb_ctx_init(&env->d_addb, &db_env_ctx_type, &c2_addb_global_ctx);
}

void c2_dbenv_common_fini(struct c2_dbenv *env)
{
	c2_addb_ctx_fini(&env->d_addb);
}

void c2_table_common_init(struct c2_table *table, struct c2_dbenv *env,
			  const struct c2_table_ops *ops)
{
	table->t_env = env;
	table->t_ops = ops;
	c2_addb_ctx_init(&table->t_addb, &db_table_ctx_type, &env->d_addb);
}

void c2_table_common_fini(struct c2_table *table)
{
	c2_addb_ctx_fini(&table->t_addb);
}

bool c2_db_buf_invariant(const struct c2_db_buf *buf)
{
	return
		DBT_ZERO < buf->db_type && buf->db_type < DBT_NR &&
		/* in-place buffers are not yet supported */
		buf->db_type != DBT_INPLACE &&
		(buf->db_buf.b_addr != NULL) == (buf->db_buf.b_nob > 0) &&
		ergo(buf->db_static, buf->db_buf.b_nob > 0) &&
		c2_db_buf_impl_invariant(buf);
}

void c2_db_buf_init(struct c2_db_buf *buf, enum c2_db_buf_type btype,
		    void *area, uint32_t size)
{
	buf->db_type = btype;
	buf->db_buf.b_addr = area;
	buf->db_buf.b_nob  = size;
	c2_db_buf_impl_init(buf);
	C2_ASSERT(c2_db_buf_invariant(buf));
}

void c2_db_buf_fini(struct c2_db_buf *buf)
{
	C2_ASSERT(c2_db_buf_invariant(buf));
	c2_db_buf_impl_fini(buf);
	if (!buf->db_static) {
		c2_free(buf->db_buf.b_addr);
		buf->db_buf.b_addr = NULL;
	}
}

void c2_db_buf_steal(struct c2_db_buf *buf)
{
	C2_PRE(buf->db_type == DBT_ALLOC);
	buf->db_buf.b_addr = NULL;
	buf->db_buf.b_nob  = 0;
}

bool c2_db_pair_invariant(const struct c2_db_pair *p)
{
	return
		p->dp_table != NULL &&
		c2_db_buf_invariant(&p->dp_key) &&
		c2_db_buf_invariant(&p->dp_rec);
}

void c2_db_pair_setup(struct c2_db_pair *pair, struct c2_table *table,
		      void *keybuf, uint32_t keysize,
		      void *recbuf, uint32_t recsize)
{
	C2_PRE((keybuf != NULL) == (keysize > 0));
	C2_PRE((recbuf != NULL) == (recsize > 0));

	C2_SET0(pair);
	pair->dp_table = table;

	if (keybuf != NULL) {
		c2_db_buf_init(&pair->dp_key, DBT_COPYOUT, keybuf, keysize);
		pair->dp_key.db_static = true;
	} else
		c2_db_buf_init(&pair->dp_key, DBT_ALLOC, NULL, 0);

	if (recbuf != NULL) {
		c2_db_buf_init(&pair->dp_rec, DBT_COPYOUT, recbuf, recsize);
		pair->dp_rec.db_static = true;
	} else
		c2_db_buf_init(&pair->dp_rec, DBT_ALLOC, NULL, 0);
	C2_POST(c2_db_pair_invariant(pair));
}

void c2_db_pair_fini(struct c2_db_pair *pair)
{
	C2_PRE(c2_db_pair_invariant(pair));
	c2_db_buf_fini(&pair->dp_rec);
	c2_db_buf_fini(&pair->dp_key);
	C2_SET0(pair);
}

void c2_db_pair_release(struct c2_db_pair *pair)
{
}

int c2_db_tx_is_active(const struct c2_db_tx *tx)
{
        return tx->dt_env != NULL;
}

void c2_db_common_tx_init(struct c2_db_tx *tx, struct c2_dbenv *env)
{
	tx->dt_env = env;
	txw_tlist_init(&tx->dt_waiters);
	c2_addb_ctx_init(&tx->dt_addb, &db_tx_ctx_type, &env->d_addb);
}

void c2_db_common_tx_fini(struct c2_db_tx *tx)
{
	c2_addb_ctx_fini(&tx->dt_addb);
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
