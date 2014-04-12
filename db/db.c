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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "lib/buf.h"   /* m0_buf */
#include "lib/misc.h"  /* M0_SET0 */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/finject.h"
#include "lib/string.h"		/* m0_strdup */

#include "module/instance.h"	/* m0 */
#include "mero/magic.h"
#include "db/db.h"
#include "db/db_common.h"
#include "reqh/reqh.h"
#include "be/domain.h"
#include "db/extmap_xc.h"
#include "ut/stob.h"		/* m0_ut_stob_linux_get_by_key */

#include "be/ut/helper.h"
#include "be/btree.h"
#include "be/seg.h"
#include "be/tx.h"

/**
   @addtogroup db
   @{
 */

struct m0_addb_ctx m0_db_mod_ctx;

#if 0
static struct m0_sm_group  __sm_group;
static struct m0_be_domain __dom;
static struct m0_be_ut_seg __seg;
#endif

struct m0_stob;

enum {
	SEG_MAP_SIZE_MAX = 0x1000,
	SEG_SIZE	 = M0_BE_DB_SEGMENT_SIZE,
};

struct seg_map_item {
	char		    *smi_name;
	uint64_t	     smi_stob_id;
	void		    *smi_seg_addr;
	struct m0_be_ut_seg *smi_ut_seg;
	bool		     smi_reset;
};

static struct seg_map_item seg_map[SEG_MAP_SIZE_MAX];
static int		   seg_map_size = 0;
static struct m0_mutex	   seg_map_lock_;

static bool seg_map_tryload_item(FILE *f, int *state)
{
        uint64_t  stob_id;
        char      name[256];
        void     *seg_addr;
        int       rc;

        rc = fscanf(f, "%s %"SCNu64" %p\n", name, &stob_id, &seg_addr);
        if (rc != EOF) {
                M0_ASSERT_INFO(rc == 3, "invalid format: rc = %d", rc);
                seg_map_add(name, stob_id, seg_addr, NULL);
        }
        return rc != EOF;
}

static void seg_map_lock(void)
{
	m0_mutex_lock(&seg_map_lock_);
}

static void seg_map_unlock(void)
{
	m0_mutex_unlock(&seg_map_lock_);
}

static void seg_map_add(const char *name,
			uint64_t stob_id,
			void *seg_addr,
			struct m0_be_ut_seg *ut_seg)
{
	seg_map_lock();
	seg_map[seg_map_size] = (struct seg_map_item) {
		.smi_name     = m0_strdup(name),
		.smi_stob_id  = stob_id,
		.smi_seg_addr = seg_addr,
		.smi_ut_seg   = ut_seg,
		.smi_reset    = false,
	};
	++seg_map_size;
	seg_map_unlock();
}

static struct seg_map_item *seg_map_lookup(const char *name)
{
	struct seg_map_item *smi = NULL;
	int                  i;

	seg_map_lock();
	for (i = 0; i < seg_map_size; ++i) {
		if (m0_streq(seg_map[i].smi_name, name)) {
			smi = &seg_map[i];
			break;
		}
	}
	seg_map_unlock();
	return smi;
}

static struct seg_map_item *seg_map_lookup_ut_seg(struct m0_be_ut_seg *ut_seg)
{
	struct seg_map_item *smi = NULL;
	int                  i;

	seg_map_lock();
	for (i = 0; i < seg_map_size; ++i) {
		if (seg_map[i].smi_ut_seg == ut_seg) {
			smi = &seg_map[i];
			break;
		}
	}
	seg_map_unlock();
	return smi;
}

static void dbenv_seg_init(struct m0_be_ut_seg *ut_seg,
			   struct m0_be_ut_backend *ut_be,
			   const char *name)
{
	struct seg_map_item *smi = seg_map_lookup(name);
	struct m0_be_seg    *seg;
	struct m0_sm_group  *grp;
	struct m0_stob	    *stob;
	int		     rc;

	if (smi == NULL || smi->smi_reset) {
		m0_be_ut_seg_init(ut_seg, ut_be, SEG_SIZE);
		m0_be_ut_seg_allocator_init(ut_seg, ut_be);
		grp = m0_be_ut_backend_sm_group_lookup(ut_be);
		m0_sm_group_lock(grp);
		rc = m0_be_seg_dict_create_grp(&ut_seg->bus_seg, grp);
		m0_sm_group_unlock(grp);
		M0_ASSERT(rc == 0);
		if (smi == NULL) {
			seg_map_add(name,
				    m0_stob_key_get(ut_seg->bus_seg.bs_stob),
				    ut_seg->bus_seg.bs_addr, ut_seg);
		}
	} else {
		seg = &ut_seg->bus_seg;

		stob = m0_ut_stob_linux_get_by_key(smi->smi_stob_id);
		m0_be_seg_init(seg, stob, &ut_be->but_dom);
		rc = m0_be_seg_open(&ut_seg->bus_seg);
		M0_ASSERT(rc == 0);
		m0_be_allocator_init(m0_be_seg_allocator(seg), seg);
		m0_be_seg_dict_init(seg);

		ut_seg->bus_copy = NULL;
	}
	smi = seg_map_lookup(name);
	smi->smi_reset = false;
	smi->smi_ut_seg = ut_seg;
}

static void dbenv_seg_fini(struct m0_be_ut_seg *ut_seg)
{
	struct seg_map_item *smi = seg_map_lookup_ut_seg(ut_seg);
	struct m0_stob	    *stob = ut_seg->bus_seg.bs_stob;
	// int		     rc;

	M0_ASSERT(smi != NULL);
	smi->smi_ut_seg = NULL;

	m0_free(ut_seg->bus_copy);

	/* XXX workaround for bug in btree capturing */
	m0_be_reg__write(&M0_BE_REG_SEG(&ut_seg->bus_seg));
	m0_be_seg_close(&ut_seg->bus_seg);
	m0_be_seg_fini(&ut_seg->bus_seg);

	m0_ut_stob_put(stob, false);
}

static void dbenv_seg_reset(const char *name)
{
	struct seg_map_item *smi = seg_map_lookup(name);

	if (smi != NULL) {
		M0_ASSERT(smi->smi_ut_seg == NULL);
		smi->smi_reset = true;
	}
}

int m0_dbenv_init(struct m0_dbenv *env, const char *name,
		  uint64_t flags)
{
	struct m0_dbenv_impl *di = &env->d_i;

	if (m0_get()->i_dbenv != NULL) {
		m0_get()->i_dbenv_save = m0_get()->i_dbenv;
		m0_get()->i_dbenv = NULL;
	} else if (m0_get()->i_dbenv_save == NULL) {
		   m0_get()->i_dbenv = env;
	}
	di->d_dom = &di->d_ut_be.but_dom;
	di->d_seg = &di->d_ut_seg.bus_seg;
	m0_be_ut_backend_init(&di->d_ut_be);
	m0_be_ut_backend_new_grp_lock_state_set(&di->d_ut_be, true);
	dbenv_seg_init(&di->d_ut_seg, &di->d_ut_be, name);
	return 0;
}

void m0_dbenv_fini(struct m0_dbenv *env)
{
	struct m0_dbenv_impl *di = &env->d_i;

	if (m0_get()->i_dbenv == env || m0_get()->i_dbenv_save == env) {
		m0_get()->i_dbenv = NULL;
		m0_get()->i_dbenv_save = NULL;
	}
	dbenv_seg_fini(&di->d_ut_seg);
	m0_be_ut_backend_fini(&di->d_ut_be);
}

M0_INTERNAL int m0_dbenv_sync(struct m0_dbenv *env)
{
	return -1;
}

void m0_dbenv_reset(const char *name)
{
	dbenv_seg_reset(name);
}

static struct m0_be_btree_kv_ops table_ops_default = {
	.ko_ksize = NULL,
	.ko_vsize = NULL,
	.ko_compare = NULL,
};

enum {
	BE_BTREE_KSIZE_MAX	  = 0x504 - 4,
	BE_BTREE_VSIZE_MAX	  = 0x504 - 4,
	BE_BTREE_KVSIZE_THRESHOLD = 0x10000,
};

static void db_tx_lock(struct m0_db_tx *tx_)
{
	if (!tx_->dt_i.dt_is_locked)
		m0_sm_group_lock(tx_->dt_i.dt_txn->t_sm.sm_grp);
}

static void db_tx_unlock(struct m0_db_tx *tx_)
{
	if (!tx_->dt_i.dt_is_locked)
		m0_sm_group_unlock(tx_->dt_i.dt_txn->t_sm.sm_grp);
}

M0_INTERNAL int m0_table_init(struct m0_table *table, struct m0_dbenv *env,
			      const char *name, uint64_t flags,
			      const struct m0_table_ops *tops_)
{
	struct m0_be_btree_kv_ops *ops;
        struct m0_be_tx	      *tx;
        struct m0_db_tx	       tx_;
        struct m0_be_seg      *seg = env->d_i.d_seg;
	struct m0_be_btree    *tree;
	struct m0_table_ops   *tops;
        void		      *p;
        int		       rc;

	M0_ENTRY();
	M0_ASSERT(tops_ != NULL);

	M0_ALLOC_PTR(ops);
	M0_ASSERT(ops != NULL);
	M0_ALLOC_PTR(tops);	/* XXX memory leak here */
	M0_ASSERT(tops != NULL);
	*tops = *tops_;

	tops->to[TO_KEY].max_size =
		(tops->to[TO_KEY].max_size > BE_BTREE_KVSIZE_THRESHOLD ?
		 BE_BTREE_KSIZE_MAX : tops->to[TO_KEY].max_size) + 4;
	tops->to[TO_REC].max_size =
		(tops->to[TO_REC].max_size > BE_BTREE_KVSIZE_THRESHOLD ?
		 BE_BTREE_VSIZE_MAX : tops->to[TO_REC].max_size) + 4;
#if 0
	tops->to[TO_KEY].max_size = min_check(tops->to[TO_KEY].max_size,
					      (uint32_t)BE_BTREE_KSIZE_MAX) + 4;
	tops->to[TO_REC].max_size = min_check(tops->to[TO_REC].max_size,
					      (uint32_t)BE_BTREE_VSIZE_MAX) + 4;
#endif
	M0_CASSERT(sizeof(int) == 4);

	*ops = table_ops_default;
	ops->ko_table = table;
	ops->ko_table_ops = tops;
	tops->ops = ops;
	table->t_ops = tops;

	/* save for m0_free() */
	table->t_i.i_btree_ops = ops;
	table->t_i.i_table_ops = tops;

        rc = m0_be_seg_dict_lookup(seg, name, &p);
        if (rc == 0) {
		tree = (struct m0_be_btree*)p;
		m0_be_btree_init(tree, seg, ops);
                table->t_i.i_tree = p;
                return M0_RC(0);
        }

	rc = m0_db_tx_init(&tx_, env, 0);
	M0_ASSERT(rc == 0);
	db_tx_lock(&tx_);
	tx = tx_.dt_i.dt_txn;

	M0_BE_ALLOC_PTR_SYNC(tree, seg, tx);
	table->t_i.i_tree = tree;

        m0_be_btree_init(tree, seg, ops);
	M0_BE_OP_SYNC(op, m0_be_btree_create(tree, tx, &op));

	rc = m0_be_seg_dict_insert(seg, tx, name, tree);
	M0_ASSERT(rc == 0);

	db_tx_unlock(&tx_);
	m0_db_tx_commit(&tx_);


        return M0_RC(rc);
}

M0_INTERNAL void m0_table_fini(struct m0_table *table)
{
	m0_be_btree_fini(table->t_i.i_tree);
	m0_free(table->t_i.i_btree_ops);
	m0_free(table->t_i.i_table_ops);
}

static void db_buf_copy_to_impl(struct m0_db_buf *buf)
{
        struct m0_buf *dbt = &buf->db_i.db_dbt;
	uint32_t       size = buf->db_buf.b_nob;

	M0_PRE(size + 4 <= dbt->b_nob);

	memset(dbt->b_addr, 0, dbt->b_nob);
	memcpy(dbt->b_addr, buf->db_buf.b_addr, size);
	memcpy(dbt->b_addr + dbt->b_nob - sizeof size, &size, sizeof(size));
}

static void db_buf_copy_from_impl(struct m0_db_buf *buf)
{
        struct m0_buf *dbt = &buf->db_i.db_dbt;
	struct m0_buf *out = &buf->db_buf;
	uint32_t       size;
	int	       rc;

	if (out->b_nob > 0) {
		memcpy(&buf->db_buf.b_nob, dbt->b_addr + dbt->b_nob - 4, 4);
		M0_ASSERT(buf->db_buf.b_nob <= dbt->b_nob - 4);
		memcpy(buf->db_buf.b_addr, dbt->b_addr, buf->db_buf.b_nob);
	} else {
		memcpy(&size, dbt->b_addr + dbt->b_nob - 4, 4);
		if (size > 0) {
			rc = m0_buf_copy(out, dbt);
			M0_ASSERT(rc == 0);
			out->b_nob = size;
		}
	}
}

M0_INTERNAL void m0_db_buf_impl_init(struct m0_db_buf *buf, uint32_t size_max)
{
        struct m0_buf *dbt = &buf->db_i.db_dbt;

#if 0
        dbt->b_addr = buf->db_buf.b_addr;
        dbt->b_nob  = buf->db_buf.b_nob;
#else
	m0_buf_init(dbt, m0_alloc(size_max), size_max);
	db_buf_copy_to_impl(buf);
#endif
}

M0_INTERNAL void m0_db_buf_impl_fini(struct m0_db_buf *buf)
{
        struct m0_buf *dbt = &buf->db_i.db_dbt;

	m0_free(dbt->b_addr);
}

M0_INTERNAL bool m0_db_buf_impl_invariant(const struct m0_db_buf *buf)
{
#if 0
        const struct m0_buf *dbt = &buf->db_i.db_dbt;

        return memcmp(dbt->b_addr, buf->db_buf.b_addr, buf->db_buf.b_nob) == 0 &&
		memcmp(dbt->b_addr + dbt->b_nob - 4, &buf->db_buf.b_nob, 4) == 0;
#else
	return true;
#endif
}

M0_INTERNAL int m0_db_tx_init(struct m0_db_tx *tx_, struct m0_dbenv *env,
			      uint64_t flags)
{
	struct m0_be_tx_credit	enough = M0_BE_TX_CREDIT(1 << 17, 1 << 22);
	struct m0_sm_group     *grp;
        struct m0_be_tx        *tx;
        int			rc;


	M0_ENTRY();
	M0_PRE(flags == 0);

        tx = &tx_->dt_i.dt_tx;
	tx_->dt_i.dt_txn = tx;
	tx_->dt_i.dt_ut_be = &env->d_i.d_ut_be;
	tx_->dt_i.dt_is_locked = false;

	grp = m0_be_ut_backend_sm_group_lookup(&env->d_i.d_ut_be);

	m0_sm_group_lock(grp);
	m0_be_tx_init(tx, 0, env->d_i.d_dom, grp, NULL, NULL, NULL, NULL);
        m0_be_tx_prep(tx, &enough);
        rc = m0_be_tx_open_sync(tx);
        if (rc != 0)
                m0_be_tx_fini(tx);
	m0_sm_group_unlock(grp);

        return M0_RC(rc);
}

M0_INTERNAL int m0_db_tx_commit(struct m0_db_tx *tx_)
{
	struct m0_db_tx_impl *ti = &tx_->dt_i;
        struct m0_be_tx	     *tx = ti->dt_txn;
	struct m0_sm_group   *grp = tx->t_sm.sm_grp;

	M0_ENTRY();


	m0_sm_group_lock(grp);
        m0_be_tx_close_sync(tx);
        m0_be_tx_fini(tx);
	m0_sm_group_unlock(grp);

        return M0_RC(0);
}

M0_INTERNAL int m0_db_tx_abort(struct m0_db_tx *tx)
{
	M0_IMPOSSIBLE("");
	return -1;
}

/* --------------------------------------------------------------------------
 * Table operations
 * ------------------------------------------------------------------------ */

static void db_pair_copy_from_impl(struct m0_db_pair *pair)
{
	db_buf_copy_from_impl(&pair->dp_key);
	db_buf_copy_from_impl(&pair->dp_rec);
}

static void db_pair_copy_to_impl(struct m0_db_pair *pair)
{
	db_buf_copy_to_impl(&pair->dp_key);
	db_buf_copy_to_impl(&pair->dp_rec);
}

M0_INTERNAL int m0_table_update(struct m0_db_tx *tx_, struct m0_db_pair *pair)
{
        struct m0_be_btree *tree = pair->dp_table->t_i.i_tree;
        struct m0_buf      *key  = &pair->dp_key.db_i.db_dbt;
        struct m0_buf      *val  = &pair->dp_rec.db_i.db_dbt;
        struct m0_be_tx    *tx   = tx_->dt_i.dt_txn;
	int                 rc;

	db_pair_copy_to_impl(pair);
	db_tx_lock(tx_);
        rc = M0_BE_OP_SYNC_RET(op, m0_be_btree_update(tree, tx, &op, key, val),
			       bo_u.u_btree.t_rc);
	db_tx_unlock(tx_);
	db_pair_copy_from_impl(pair);

	/* to preserve db5 semantic */
	if (rc != 0)
		rc = m0_table_insert(tx_, pair);
        return rc;
}

M0_INTERNAL int m0_table_insert(struct m0_db_tx *tx_, struct m0_db_pair *pair)
{
        struct m0_be_btree *tree = pair->dp_table->t_i.i_tree;
        struct m0_buf      *key  = &pair->dp_key.db_i.db_dbt;
        struct m0_buf      *val  = &pair->dp_rec.db_i.db_dbt;
        struct m0_be_tx    *tx   = tx_->dt_i.dt_txn;
	int                 rc;

	db_pair_copy_to_impl(pair);
	db_tx_lock(tx_);
        rc = M0_BE_OP_SYNC_RET(op, m0_be_btree_insert(tree, tx, &op, key, val),
			       bo_u.u_btree.t_rc);
	db_tx_unlock(tx_);
	db_pair_copy_from_impl(pair);
        return rc;
}

M0_INTERNAL int m0_table_lookup(struct m0_db_tx *tx, struct m0_db_pair *pair)
{
        struct m0_be_btree *tree = pair->dp_table->t_i.i_tree;
        struct m0_buf      *key  = &pair->dp_key.db_i.db_dbt;
        struct m0_buf      *val  = &pair->dp_rec.db_i.db_dbt;
	int rc;

	db_pair_copy_to_impl(pair);
	rc = M0_BE_OP_SYNC_RET(op, m0_be_btree_lookup(tree, &op, key, val),
                                 bo_u.u_btree.t_rc);
	db_pair_copy_from_impl(pair);
	return rc;
}

M0_INTERNAL int m0_table_delete(struct m0_db_tx *tx_, struct m0_db_pair *pair)
{
        struct m0_be_tx    *tx   = tx_->dt_i.dt_txn;
        struct m0_be_btree *tree = pair->dp_table->t_i.i_tree;
        struct m0_buf      *key  = &pair->dp_key.db_i.db_dbt;
	int		    rc;

	db_pair_copy_to_impl(pair);
	db_tx_lock(tx_);
        rc = M0_BE_OP_SYNC_RET(op, m0_be_btree_delete(tree, tx, &op, key),
                                 bo_u.u_btree.t_rc);
	db_tx_unlock(tx_);
	db_pair_copy_from_impl(pair);
	return rc;
}

/* --------------------------------------------------------------------------
 * Cursors
 * ------------------------------------------------------------------------ */

M0_INTERNAL int m0_db_cursor_init(struct m0_db_cursor *cursor_,
                                  struct m0_table *table, struct m0_db_tx *tx,
                                  uint32_t flags)
{
        struct m0_be_btree_cursor *cursor;

	cursor_->c_tx = tx;
        M0_ALLOC_PTR(cursor_->c_i.c_i);
        cursor = cursor_->c_i.c_i;
	cursor_->c_i.c_after_delete = false;
        if (cursor == NULL)
                return -ENOMEM;

        cursor_->c_flags = flags;
        cursor_->c_table = table;
        m0_be_btree_cursor_init(cursor, table->t_i.i_tree);
        return 0;
}

M0_INTERNAL void m0_db_cursor_fini(struct m0_db_cursor *cursor_)
{
        struct m0_be_btree_cursor *cursor = cursor_->c_i.c_i;

        m0_be_btree_cursor_fini(cursor);
        m0_free(cursor);
}

static void cursor_get(struct m0_be_btree_cursor *cursor,
		       struct m0_db_pair *pair,
		       int rc)
{
	struct m0_buf key;
	struct m0_buf rec;

	if (rc != 0)
		return;

        m0_be_btree_cursor_kv_get(cursor, &key, &rec);
	M0_ASSERT(pair->dp_key.db_i.db_dbt.b_nob <= key.b_nob);
	M0_ASSERT(pair->dp_rec.db_i.db_dbt.b_nob <= rec.b_nob);

#if 0
	if (pair->dp_key.db_type == DBT_ALLOC)
		m0_buf_copy(&pair->dp_key.db_i.db_dbt, &key);
	else
#endif
		memcpy(pair->dp_key.db_i.db_dbt.b_addr, key.b_addr, key.b_nob);

#if 0
	if (pair->dp_rec.db_type == DBT_ALLOC) {
		if (m0_buf_is_set(&rec))
			m0_buf_copy(&pair->dp_rec.db_i.db_dbt, &rec);
	} else
#endif
		memcpy(pair->dp_rec.db_i.db_dbt.b_addr, rec.b_addr, rec.b_nob);

#if 0
	pair->dp_key.db_buf = pair->dp_key.db_i.db_dbt;
	pair->dp_rec.db_buf = pair->dp_rec.db_i.db_dbt;
#else
	db_pair_copy_from_impl(pair);
#endif
}

static
void cursor_set(struct m0_be_btree_cursor *cursor, struct m0_db_pair *pair)
{
	db_buf_copy_to_impl(&pair->dp_key);
	db_buf_copy_to_impl(&pair->dp_rec);
}

M0_INTERNAL int m0_db_cursor_get(struct m0_db_cursor *cursor_,
                                 struct m0_db_pair *pair)
{
        struct m0_be_btree_cursor *cursor = cursor_->c_i.c_i;
        int rc;

	cursor_set(cursor, pair);
        rc = m0_be_btree_cursor_get_sync(cursor,
					 &pair->dp_key.db_i.db_dbt,
					 true);
	cursor_get(cursor, pair, rc);
        return rc;
}

M0_INTERNAL int m0_db_cursor_next(struct m0_db_cursor *cursor_,
                                  struct m0_db_pair *pair)
{
        struct m0_be_btree_cursor *cursor = cursor_->c_i.c_i;
        int rc;

	cursor_set(cursor, pair);
	rc = m0_db_cursor_get(cursor_, pair);
	if (rc != 0)
		return rc;

	/* This strange looking construction is needed to position cursor
	   exactly after deleteted element when m0_db_cursor_next is called
	   after m0_db_cursor_del. Some algorithms may need this. */
	if (cursor_->c_i.c_after_delete) {
		cursor_->c_i.c_after_delete = false;
		return rc;
	}

        m0_be_op_init(&cursor->bc_op);
        m0_be_btree_cursor_next(cursor);
        rc = m0_be_op_wait(&cursor->bc_op);
        M0_ASSERT(rc == 0);

	rc = cursor->bc_op.bo_u.u_btree.t_rc;
	cursor_get(cursor, pair, rc);
        return rc;
}

M0_INTERNAL int m0_db_cursor_prev(struct m0_db_cursor *cursor_,
                                  struct m0_db_pair *pair)
{
        struct m0_be_btree_cursor *cursor = cursor_->c_i.c_i;
        int rc;

	cursor_set(cursor, pair);
	rc = m0_db_cursor_get(cursor_, pair);
	if (rc != 0)
		return rc;

        m0_be_op_init(&cursor->bc_op);
        m0_be_btree_cursor_prev(cursor);
        rc = m0_be_op_wait(&cursor->bc_op);
        M0_ASSERT(rc == 0);

	rc = cursor->bc_op.bo_u.u_btree.t_rc;
	cursor_get(cursor, pair, rc);
        return rc;
}

M0_INTERNAL int m0_db_cursor_first(struct m0_db_cursor *cursor_,
                                   struct m0_db_pair *pair)
{
        struct m0_be_btree_cursor *cursor = cursor_->c_i.c_i;
        int rc;

	cursor_set(cursor, pair);
	rc = m0_be_btree_cursor_first_sync(cursor);

	if (rc == 0)
		cursor_get(cursor, pair, 0);

        return rc;
}

M0_INTERNAL int m0_db_cursor_last(struct m0_db_cursor *cursor_,
                                  struct m0_db_pair *pair)
{
        struct m0_be_btree_cursor *cursor = cursor_->c_i.c_i;
        int rc;

	cursor_set(cursor, pair);
	rc = m0_be_btree_cursor_last_sync(cursor);

	if (rc == 0)
		cursor_get(cursor, pair, 0);

        return rc;
}

M0_INTERNAL int m0_db_cursor_set(struct m0_db_cursor *cursor_,
                                 struct m0_db_pair *pair)
{
#if 0
        M0_BE_OP_SYNC(op, m0_be_btree_update(pair->dp_table->t_i.i_tree,
                                             cursor_->c_tx->dt_i.dt_txn,
                                             &op,
                                             &pair->dp_key.db_i.db_dbt,
                                             &pair->dp_rec.db_i.db_dbt));
#else
	int rc;
	struct m0_db_tx *tx_ = cursor_->c_tx;
        struct m0_be_tx *tx  = tx_->dt_i.dt_txn;
	struct m0_buf    key;
	struct m0_buf   *val;
	struct m0_be_btree *tree = cursor_->c_table->t_i.i_tree;

	m0_be_btree_cursor_kv_get(cursor_->c_i.c_i, &key, NULL);

	db_buf_copy_to_impl(&pair->dp_rec);
	val = &pair->dp_rec.db_i.db_dbt;

	db_tx_lock(tx_);
        rc = M0_BE_OP_SYNC_RET(op, m0_be_btree_update(tree, tx, &op, &key, val),
			       bo_u.u_btree.t_rc);
	db_tx_unlock(tx_);
#endif
        return rc;
}

M0_INTERNAL int m0_db_cursor_add(struct m0_db_cursor *cursor_,
                                 struct m0_db_pair *pair)
{
#if 0
        M0_BE_OP_SYNC(op,
                      m0_be_btree_insert(pair->dp_table->t_i.i_tree,
                                         cursor_->c_tx->dt_i.dt_txn,
                                         &op,
                                         &pair->dp_key.db_i.db_dbt,
                                         &pair->dp_rec.db_i.db_dbt));
#else
	int rc = m0_table_insert(cursor_->c_tx, pair);
	M0_ASSERT(rc == 0);	/* XXX */
#endif
        return m0_db_cursor_get(cursor_, pair);
}

M0_INTERNAL int m0_db_cursor_del(struct m0_db_cursor *cursor_)
{
        struct m0_be_btree_cursor *cursor = cursor_->c_i.c_i;
        struct m0_buf key;
        struct m0_buf val;

        m0_be_btree_cursor_kv_get(cursor, &key, &val);

#if 0
        M0_BE_OP_SYNC(op,
                      m0_be_btree_delete(cursor->bc_tree,
                                         cursor_->c_tx->dt_i.dt_txn,
                                         &op, &key));
#else
	struct m0_db_pair pair;
	m0_db_pair_setup(&pair, cursor_->c_table, key.b_addr, key.b_nob - 4,
			 val.b_addr, val.b_nob - 4);
	int rc = m0_table_delete(cursor_->c_tx, &pair);
	M0_ASSERT(rc == 0);	/* XXX */
	m0_db_pair_fini(&pair);
#endif

	cursor_->c_i.c_after_delete = true;
	return cursor->bc_op.bo_u.u_btree.t_rc;
}

M0_INTERNAL int m0_db_init(void)
{
	m0_addb_ctx_type_register(&m0_addb_ct_db_mod);
	M0_ADDB_CTX_INIT(&m0_addb_gmc, &m0_db_mod_ctx,
			 &m0_addb_ct_db_mod, &m0_addb_proc_ctx);
	m0_xc_extmap_init();
	m0_mutex_init(&seg_map_lock_);
	return 0;
}

M0_INTERNAL void m0_db_fini(void)
{
	int i;

	for (i = 0; i < seg_map_size; ++i)
		m0_free(seg_map[i].smi_name);
	m0_mutex_fini(&seg_map_lock_);
        m0_addb_ctx_fini(&m0_db_mod_ctx);
	m0_xc_extmap_fini();
}

/* ------------------------------------------------------------------------
 * reqh part
 * ---------------------------------------------------------------------- */

#if 0
static int ast_init(void);
static void ast_fini(void);

M0_INTERNAL int m0_db_start(struct m0_reqh *reqh)
{
	int rc;
	struct m0_be_domain_cfg cfg = {
		.bc_engine = {
			.bec_group_nr = 1,
			.bec_log_size = 1 << 27,
			.bec_tx_size_max = M0_BE_TX_CREDIT_INIT(1 << 20,
								1 << 26),
			.bec_group_size_max = M0_BE_TX_CREDIT_INIT(1 << 21,
								   1 << 27),
			.bec_group_tx_max = 20
		}
	};

	cfg.bc_engine.bec_group_fom_reqh = reqh;

	rc = ast_init();
	M0_ASSERT(rc == 0);
	rc = m0_be_domain_init(&__dom, &cfg);
	M0_ASSERT(rc == 0);
	m0_be_ut_seg_init(&__seg, &__dom, 1 << 27);

	m0_sm_group_lock(&__sm_group);
	XXX_m0_be_ut_seg_allocator_init(&__seg, &__sm_group, &__dom);
	rc = m0_be_seg_dict_create_grp(&__seg.bus_seg, &__sm_group);
	M0_ASSERT(rc == 0);
	m0_sm_group_unlock(&__sm_group);

	return rc;
}

M0_INTERNAL void m0_db_stop(void)
{
	m0_be_ut_seg_fini(&__seg);
	m0_be_domain_fini(&__dom);
	ast_fini();
}

#include "ut/ast_thread.c" /* XXX */
static int ast_init(void)
{
	m0_sm_group_init(&__sm_group);
	return m0_ut_ast_thread_start(&__sm_group);
}

static void ast_fini(void)
{
	m0_ut_ast_thread_stop();
	m0_sm_group_fini(&__sm_group);
}
#endif

#undef M0_TRACE_SUBSYSTEM

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
