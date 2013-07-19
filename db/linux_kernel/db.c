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
 * Original creation date: 09/23/2010
 */

#include "lib/misc.h"  /* M0_SET0 */
#include "lib/cdefs.h" /* M0_EXPORTED */
#include "lib/arith.h" /* M0_3WAY() */
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"

#include "db/db.h"
#include "db/db_common.h"

/**
   @addtogroup db

   <b>Linux kernel implementation.</b>

   @{
 */

M0_TL_DESCR_DEFINE(pair, "(key, rec) pairs",
		   static, struct m0_db_kpair, dk_linkage, dk_magix,
		   0x50c1a112ede1ffe1 /* socialized eiffel */,
		   0x0551f1edca5cade5 /* ossified cascades */);
M0_TL_DEFINE(pair, static, struct m0_db_kpair);

static bool ktable_invariant_locked(struct m0_table *t,
				    struct m0_table_impl *ti);
static bool ktable_invariant(struct m0_table *t);

M0_INTERNAL int m0_dbenv_init(struct m0_dbenv *env, const char *name,
			      uint64_t flags)
{
	m0_dbenv_common_init(env);
	return 0;
}
M0_EXPORTED(m0_dbenv_init);

M0_INTERNAL void m0_dbenv_fini(struct m0_dbenv *env)
{
	m0_dbenv_common_fini(env);
}
M0_EXPORTED(m0_dbenv_fini);

M0_INTERNAL int m0_dbenv_sync(struct m0_dbenv *env)
{
	return 0;
}

M0_INTERNAL int m0_table_init(struct m0_table *table, struct m0_dbenv *env,
			      const char *name, uint64_t flags,
			      const struct m0_table_ops *ops)
{
	m0_table_common_init(table, env, ops);
	pair_tlist_init(&table->t_i.tk_pair);
	m0_mutex_init(&table->t_i.tk_lock);
	return 0;
}

M0_INTERNAL void m0_table_fini(struct m0_table *table)
{
	struct m0_db_kpair *kpair;

	M0_ASSERT(ktable_invariant(table));

	m0_tl_for(pair, &table->t_i.tk_pair, kpair) {
		pair_tlink_del_fini(kpair);
		m0_free(kpair);
	} m0_tl_endfor;
	pair_tlist_fini(&table->t_i.tk_pair);
	m0_mutex_fini(&table->t_i.tk_lock);
	m0_table_common_fini(table);
}

M0_INTERNAL int m0_db_tx_init(struct m0_db_tx *tx, struct m0_dbenv *env,
			      uint64_t flags)
{
	m0_db_common_tx_init(tx, env);
	txw_tlist_init(&tx->dt_waiters);
	return 0;
}

M0_INTERNAL int m0_db_tx_commit(struct m0_db_tx *tx)
{
	struct m0_db_tx_waiter *w;
	struct m0_dbenv        *env;

	env = tx->dt_env;
	m0_tl_teardown(txw, &tx->dt_waiters, w) {
		w->tw_commit(w);
		w->tw_done(w);
	}
	m0_db_common_tx_fini(tx);
	return 0;
}

M0_INTERNAL int m0_db_tx_abort(struct m0_db_tx *tx)
{
	M0_IMPOSSIBLE("Aborting transaction in kernel space.");
	return -ENOSYS;
}

M0_INTERNAL void m0_db_tx_waiter_add(struct m0_db_tx *tx,
				     struct m0_db_tx_waiter *w)
{
	txw_tlist_add(&tx->dt_waiters, w);
}

static int key_cmp(struct m0_table *t,
		   const struct m0_buf *k0, const struct m0_buf *k1)
{
	if (t->t_ops->key_cmp != NULL)
		return t->t_ops->key_cmp(t, k0->b_addr, k1->b_addr);
	else
		return M0_3WAY(memcmp(k0->b_addr, k1->b_addr,
				      min_check(k0->b_nob, k1->b_nob)), 0);
}

static struct m0_db_kpair *ktable_lookup(struct m0_db_pair *pair, int *out)
{
	struct m0_db_kpair   *scan;
	struct m0_table      *t;
	struct m0_table_impl *ti;

	t  = pair->dp_table;
	ti = &t->t_i;
	/*
	 * Note that this should return only 0 or -ENOENT. The code below
	 * depends on this.
	 */
	M0_PRE(m0_mutex_is_locked(&ti->tk_lock));

	*out = -ENOENT;
	m0_tl_for(pair, &ti->tk_pair, scan) {
		switch (key_cmp(t, &scan->dk_key, &pair->dp_key.db_buf)) {
		case -1:
			continue;
		case 0:
			*out = 0;
			/* fall through */
		default:
			break;
		}
	} m0_tl_endfor;
	return scan;
}

static struct m0_db_kpair *kpair_alloc(struct m0_db_pair *pair)
{
	struct m0_db_kpair *kpair;
	struct m0_buf      *key;
	struct m0_buf      *rec;

	key = &pair->dp_key.db_buf;
	rec = &pair->dp_rec.db_buf;

	M0_CASSERT((sizeof *kpair & 07) == 0);
	M0_PRE((key->b_nob & 07) == 0);
	M0_PRE((rec->b_nob & 07) == 0);

	kpair = m0_alloc(sizeof *kpair + key->b_nob + rec->b_nob);
	if (kpair != NULL) {
		kpair->dk_key.b_nob = key->b_nob;
		kpair->dk_rec.b_nob = rec->b_nob;
		kpair->dk_key.b_addr = kpair + 1;
		kpair->dk_rec.b_addr = kpair->dk_key.b_addr + key->b_nob;
		memcpy(kpair->dk_key.b_addr, key->b_addr, key->b_nob);
		memcpy(kpair->dk_rec.b_addr, rec->b_addr, rec->b_nob);
	}
	return kpair;
}

static int kbuf_copyout(struct m0_buf *kbuf, struct m0_db_buf *dbbuf)
{
	struct m0_buf *ubuf;

	ubuf = &dbbuf->db_buf;
	if (dbbuf->db_type == DBT_ALLOC) {
		M0_ASSERT(ubuf->b_addr == NULL);
		ubuf->b_addr = m0_alloc(kbuf->b_nob);
		if (ubuf->b_addr == NULL)
			return -ENOMEM;
		ubuf->b_nob = kbuf->b_nob;
	}
	M0_ASSERT(ubuf->b_addr != NULL);
	if (ubuf->b_nob < kbuf->b_nob)
		return -ENOBUFS;
	memcpy(ubuf->b_addr, kbuf->b_addr, kbuf->b_nob);
	return 0;
}

static int kpair_copyout(struct m0_db_kpair *kpair, struct m0_db_pair *pair)
{
	int result;

	/*
	 * XXX this might return an error after modifying the pair.
	 */

	result = kbuf_copyout(&kpair->dk_key, &pair->dp_key);
	if (result == 0)
		result = kbuf_copyout(&kpair->dk_rec, &pair->dp_rec);
	return result;
}

static void pair_lock(struct m0_db_pair *pair)
{
	m0_mutex_lock(&pair->dp_table->t_i.tk_lock);
}

static void pair_unlock(struct m0_db_pair *pair)
{
	m0_mutex_unlock(&pair->dp_table->t_i.tk_lock);
}

static struct m0_tl *pair_list(struct m0_db_pair *pair)
{
	return &pair->dp_table->t_i.tk_pair;
}

M0_INTERNAL int m0_table_update(struct m0_db_tx *tx, struct m0_db_pair *pair)
{
	struct m0_db_kpair *replacement;
	struct m0_db_kpair *kpair;
	int                 result;

	M0_ASSERT(ktable_invariant(pair->dp_table));

	replacement = kpair_alloc(pair);
	if (replacement != NULL) {
		pair_lock(pair);
		kpair = ktable_lookup(pair, &result);
		if (result == 0) {
			pair_tlist_add_after(kpair, replacement);
			pair_tlink_del_fini(kpair);
			m0_free(kpair);
		} else
			m0_free(replacement);
		pair_unlock(pair);
	} else
		result = -ENOMEM;

	M0_ASSERT(ktable_invariant(pair->dp_table));
	return result;
}

M0_INTERNAL int table_insert(struct m0_db_pair *pair,
			     struct m0_db_kpair **kpair_out)
{
	struct m0_db_kpair *newkp;
	struct m0_db_kpair *kpair;
	int                 result;

	M0_ASSERT(ktable_invariant(pair->dp_table));

	newkp = kpair_alloc(pair);
	if (newkp != NULL) {
		int out;

		pair_lock(pair);
		kpair = ktable_lookup(pair, &out);
		if (out == -ENOENT) {
			pair_tlist_add_before(kpair, newkp);
			*kpair_out = newkp;
			result = 0;
		} else {
			m0_free(newkp);
			result = -EEXIST;
		}
		pair_unlock(pair);
	} else
		result = -ENOMEM;
	M0_ASSERT(ktable_invariant(pair->dp_table));
	return result;
}

M0_INTERNAL int m0_table_insert(struct m0_db_tx *tx, struct m0_db_pair *pair)
{
	struct m0_db_kpair *dummy;

	return table_insert(pair, &dummy);
}

M0_INTERNAL int m0_table_lookup(struct m0_db_tx *tx, struct m0_db_pair *pair)
{
	int                 result;
	struct m0_db_kpair *kpair;

	M0_ASSERT(ktable_invariant(pair->dp_table));

	pair_lock(pair);
	kpair = ktable_lookup(pair, &result);
	if (result == 0)
		kpair_copyout(kpair, pair);
	pair_unlock(pair);
	return result;
}

M0_INTERNAL int m0_table_delete(struct m0_db_tx *tx, struct m0_db_pair *pair)
{
	int                 result;
	struct m0_db_kpair *kpair;

	M0_ASSERT(ktable_invariant(pair->dp_table));

	pair_lock(pair);
	kpair = ktable_lookup(pair, &result);
	if (result == 0) {
		pair_tlink_del_fini(kpair);
		m0_free(kpair);
	}
	pair_unlock(pair);

	M0_ASSERT(ktable_invariant(pair->dp_table));

	return result;
}

M0_INTERNAL int m0_db_cursor_init(struct m0_db_cursor *cursor,
				  struct m0_table *table, struct m0_db_tx *tx,
				  uint32_t flags)
{
	M0_SET0(cursor);
	return 0;
}

M0_INTERNAL void m0_db_cursor_fini(struct m0_db_cursor *cursor)
{
}

M0_INTERNAL int m0_db_cursor_get(struct m0_db_cursor *cursor,
				 struct m0_db_pair *pair)
{
	int result;

	M0_PRE(cursor->c_table == pair->dp_table);
	M0_ASSERT(ktable_invariant(pair->dp_table));

	m0_mutex_lock(&cursor->c_table->t_i.tk_lock);
	cursor->c_i.ck_current = ktable_lookup(pair, &result);
	if (result == 0)
		kpair_copyout(cursor->c_i.ck_current, pair);
	m0_mutex_unlock(&cursor->c_table->t_i.tk_lock);
	return result;
}

M0_INTERNAL int m0_db_cursor_next(struct m0_db_cursor *cursor,
				  struct m0_db_pair *pair)
{
	struct m0_db_cursor_impl *ci;
	int                       result;
	struct m0_db_kpair       *next;

	M0_PRE(cursor->c_table == pair->dp_table);
	M0_ASSERT(ktable_invariant(pair->dp_table));

	ci = &cursor->c_i;

	if (ci->ck_current == NULL)
		result = -EINVAL;
	else {
		pair_lock(pair);
		next = pair_tlist_next(pair_list(pair), ci->ck_current);
		if (next == NULL)
			result = -ENOENT;
		else {
			ci->ck_current = next;
			result = kpair_copyout(ci->ck_current, pair);
		}
		pair_unlock(pair);
	}
	return result;
}

M0_INTERNAL int m0_db_cursor_prev(struct m0_db_cursor *cursor,
				  struct m0_db_pair *pair)
{
	struct m0_db_cursor_impl *ci;
	int                       result;
	struct m0_db_kpair       *prev;

	M0_PRE(cursor->c_table == pair->dp_table);
	M0_ASSERT(ktable_invariant(pair->dp_table));

	ci = &cursor->c_i;

	if (ci->ck_current == NULL)
		result = -EINVAL;
	else {
		pair_lock(pair);
		prev = pair_tlist_prev(pair_list(pair), ci->ck_current);
		if (prev == NULL)
			result = -ENOENT;
		else {
			ci->ck_current = prev;
			result = kpair_copyout(ci->ck_current, pair);
		}
		pair_unlock(pair);
	}
	return result;
}

M0_INTERNAL int m0_db_cursor_first(struct m0_db_cursor *cursor,
				   struct m0_db_pair *pair)
{
	struct m0_db_cursor_impl *ci;
	struct m0_tl             *pl;
	int                       result;
	struct m0_db_kpair       *first;

	M0_PRE(cursor->c_table == pair->dp_table);
	M0_ASSERT(ktable_invariant(pair->dp_table));

	ci = &cursor->c_i;
	pl = pair_list(pair);

	pair_lock(pair);
	first = pair_tlist_head(pl);
	if (first == NULL)
		result = -ENOENT;
	else {
		ci->ck_current = first;
		kpair_copyout(ci->ck_current, pair);
		result = 0;
	}
	pair_unlock(pair);
	return result;
}

M0_INTERNAL int m0_db_cursor_last(struct m0_db_cursor *cursor,
				  struct m0_db_pair *pair)
{
	struct m0_db_cursor_impl *ci;
	struct m0_tl             *pl;
	int                       result;
	struct m0_db_kpair       *last;

	M0_PRE(cursor->c_table == pair->dp_table);
	M0_ASSERT(ktable_invariant(pair->dp_table));

	ci = &cursor->c_i;
	pl = pair_list(pair);

	pair_lock(pair);
	last = pair_tlist_tail(pl);
	if (last == NULL)
		result = -ENOENT;
	else {
		ci->ck_current = last;
		kpair_copyout(ci->ck_current, pair);
		result = 0;
	}
	pair_unlock(pair);
	return result;
}

M0_INTERNAL int m0_db_cursor_set(struct m0_db_cursor *cursor,
				 struct m0_db_pair *pair)
{
	struct m0_db_cursor_impl *ci;
	struct m0_db_kpair       *newkp;
	struct m0_db_kpair       *cur;
	struct m0_db_pair         replacement;
	int                       result;

	M0_PRE(cursor->c_table == pair->dp_table);
	M0_ASSERT(ktable_invariant(pair->dp_table));

	ci = &cursor->c_i;

	cur = ci->ck_current;
	if (cur == NULL)
		return -EINVAL;

	/* XXX accessing kpair without table mutex. */
	m0_db_pair_setup(&replacement, pair->dp_table,
			 cur->dk_key.b_addr, cur->dk_key.b_nob,
			 pair->dp_rec.db_buf.b_addr, pair->dp_rec.db_buf.b_nob);

	newkp = kpair_alloc(&replacement);
	if (newkp != NULL) {
		pair_lock(pair);
		pair_tlist_add_after(cur, newkp);
		pair_tlink_del_fini(cur);
		m0_free(cur);
		ci->ck_current = newkp;
		pair_unlock(pair);
		result = 0;
	} else
		result = -ENOMEM;

	M0_ASSERT(ktable_invariant(pair->dp_table));

	return result;
}

M0_INTERNAL int m0_db_cursor_add(struct m0_db_cursor *cursor,
				 struct m0_db_pair *pair)
{
	M0_PRE(cursor->c_table == pair->dp_table);

	return table_insert(pair, &cursor->c_i.ck_current);
}

M0_INTERNAL int m0_db_cursor_del(struct m0_db_cursor *cursor)
{
	struct m0_db_cursor_impl *ci;
	struct m0_table_impl     *ti;
	int                       result;

	M0_ASSERT(ktable_invariant(cursor->c_table));

	ci = &cursor->c_i;
	ti = &cursor->c_table->t_i;

	if (ci->ck_current != NULL) {
		m0_mutex_lock(&ti->tk_lock);
		pair_tlink_del_fini(ci->ck_current);
		m0_free(ci->ck_current);
		ci->ck_current = NULL;
		m0_mutex_unlock(&ti->tk_lock);
		result = 0;
	} else
		result = -EINVAL;
	M0_ASSERT(ktable_invariant(cursor->c_table));
	return result;
}

M0_INTERNAL void m0_db_buf_impl_init(struct m0_db_buf *buf)
{
}

M0_INTERNAL void m0_db_buf_impl_fini(struct m0_db_buf *buf)
{
}

M0_INTERNAL bool m0_db_buf_impl_invariant(const struct m0_db_buf *buf)
{
	return true;
}

static bool ktable_invariant_locked(struct m0_table *t,
				    struct m0_table_impl *ti)
{
	struct m0_tl *tkp = &t->t_i.tk_pair;

	return
		M0_CHECK_EX(m0_tlist_invariant(&pair_tl, tkp)) &&
		m0_tl_forall(pair, scan, tkp,
			     scan->dk_key.b_addr == scan + 1 &&
			     scan->dk_rec.b_addr ==
			     scan->dk_key.b_addr + scan->dk_key.b_nob &&
			     ergo(pair_tlist_prev(tkp, scan) != NULL,
				  key_cmp(t, &pair_tlist_prev(tkp, scan)->dk_key,
					  &scan->dk_key) == -1));
}

static bool ktable_invariant(struct m0_table *t)
{
	bool                  result;
	struct m0_table_impl *ti;

	ti = &t->t_i;

	m0_mutex_lock(&ti->tk_lock);
	result = ktable_invariant_locked(t, ti);
	m0_mutex_unlock(&ti->tk_lock);
	return result;
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
