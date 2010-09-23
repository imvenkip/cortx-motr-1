/* -*- C -*- */

#include "lib/adt.h"   /* c2_buf */
#include "lib/misc.h"  /* C2_SET0 */
#include "lib/cdefs.h" /* C2_EXPORTED */
#include "lib/arith.h" /* C2_3WAY() */
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

static bool ktable_invariant_locked(struct c2_table *t, 
				    struct c2_table_impl *ti);
static bool ktable_invariant(struct c2_table *t);

int c2_dbenv_init(struct c2_dbenv *env, const char *name, uint64_t flags)
{
	c2_dbenv_common_init(env);
	return 0;
}
C2_EXPORTED(c2_dbenv_init);

void c2_dbenv_fini(struct c2_dbenv *env)
{
	c2_dbenv_common_fini(env);
}
C2_EXPORTED(c2_dbenv_fini);

int c2_dbenv_sync(struct c2_dbenv *env)
{
	return 0;
}
C2_EXPORTED(c2_dbenv_sync);

int c2_table_init(struct c2_table *table, struct c2_dbenv *env, 
		  const char *name, uint64_t flags, 
		  const struct c2_table_ops *ops)
{
	c2_table_common_init(table, env, ops);
	c2_list_init(&table->t_i.tk_pair);
	c2_mutex_init(&table->t_i.tk_lock);
	return 0;
}
C2_EXPORTED(c2_table_init);

static struct c2_db_kpair *kpair_extract(void *link)
{
	return container_of(link, struct c2_db_kpair, dk_linkage);
}

void c2_table_fini(struct c2_table *table)
{
	struct c2_db_kpair *kpair;

	C2_ASSERT(ktable_invariant(table));

	while (!c2_list_is_empty(&table->t_i.tk_pair)) {
		kpair = kpair_extract(&table->t_i.tk_pair.l_head);
		c2_list_del(&kpair->dk_linkage);
		c2_free(kpair);
	}
	c2_list_fini(&table->t_i.tk_pair);
	c2_mutex_fini(&table->t_i.tk_lock);
	c2_table_common_fini(table);
}
C2_EXPORTED(c2_table_fini);

int c2_db_tx_init(struct c2_db_tx *tx, struct c2_dbenv *env, uint64_t flags)
{
	c2_db_common_tx_init(tx, env);
	c2_list_init(&tx->dt_waiters);
	return 0;
}
C2_EXPORTED(c2_db_tx_init);

int c2_db_tx_commit(struct c2_db_tx *tx)
{
	struct c2_db_tx_waiter *w;
	struct c2_dbenv        *env;

	env = tx->dt_env;
	while (!c2_list_is_empty(&tx->dt_waiters)) {
		w = container_of(tx->dt_waiters.l_head, struct c2_db_tx_waiter,
				 tw_tx);
		c2_list_del(&w->tw_tx);
		w->tw_commit(w);
		w->tw_done(w);
	}
	c2_db_common_tx_fini(tx);
	return 0;
}
C2_EXPORTED(c2_db_tx_commit);

int c2_db_tx_abort(struct c2_db_tx *tx)
{
	C2_IMPOSSIBLE("Aborting transaction in kernel space.");
}
C2_EXPORTED(c2_db_tx_abort);

void c2_db_tx_waiter_add(struct c2_db_tx *tx, struct c2_db_tx_waiter *w)
{
	c2_list_add(&tx->dt_waiters, &w->tw_tx);
}
C2_EXPORTED(c2_db_tx_waiter_add);

static int key_cmp(struct c2_table *t,
		   const struct c2_buf *k0, const struct c2_buf *k1)
{
	if (t->t_ops->key_cmp != NULL)
		return t->t_ops->key_cmp(t, k0->b_addr, k1->b_addr);
	else
		return C2_3WAY(memcmp(k0->b_addr, k1->b_addr,
				      min_check(k0->b_nob, k1->b_nob)), 0);
}

static struct c2_db_kpair *ktable_lookup(struct c2_db_pair *pair, int *out)
{
	struct c2_db_kpair   *scan;
	struct c2_table      *t;
	struct c2_table_impl *ti;

	t  = pair->dp_table;
	ti = &t->t_i;
	/*
	 * Note that this should return only 0 or -ENOENT. The code below
	 * depends on this.
	 */
	C2_PRE(c2_mutex_is_locked(&ti->tk_lock));

	*out = -ENOENT;
	c2_list_for_each_entry(&ti->tk_pair, scan, struct c2_db_kpair,
			       dk_linkage) {
		switch (key_cmp(t, &scan->dk_key, &pair->dp_key.db_buf)) {
		case -1:
			continue;
		case 0:
			*out = 0;
			/* fall through */
		default:
			break;
		}
	}
	return scan;
}

static struct c2_db_kpair *kpair_alloc(struct c2_db_pair *pair)
{
	struct c2_db_kpair *kpair;
	struct c2_buf      *key;
	struct c2_buf      *rec;

	key = &pair->dp_key.db_buf;
	rec = &pair->dp_rec.db_buf;

	C2_CASSERT((sizeof *kpair & 07) == 0);
	C2_PRE((key->b_nob & 07) == 0);
	C2_PRE((rec->b_nob & 07) == 0);

	kpair = c2_alloc(sizeof *kpair + key->b_nob + rec->b_nob);
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

static int kbuf_copyout(struct c2_buf *kbuf, struct c2_db_buf *dbbuf)
{
	struct c2_buf *ubuf;

	ubuf = &dbbuf->db_buf;
	if (dbbuf->db_type == DBT_ALLOC) {
		C2_ASSERT(ubuf->b_addr == NULL);
		ubuf->b_addr = c2_alloc(kbuf->b_nob);
		if (ubuf->b_addr == NULL)
			return -ENOMEM;
		ubuf->b_nob = kbuf->b_nob;
	}
	C2_ASSERT(ubuf->b_addr != NULL);
	if (ubuf->b_nob < kbuf->b_nob)
		return -ENOBUFS;
	memcpy(ubuf->b_addr, kbuf->b_addr, kbuf->b_nob);
	return 0;
}

static int kpair_copyout(struct c2_db_kpair *kpair, struct c2_db_pair *pair)
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

static void pair_lock(struct c2_db_pair *pair)
{
	c2_mutex_lock(&pair->dp_table->t_i.tk_lock);
}

static void pair_unlock(struct c2_db_pair *pair)
{
	c2_mutex_unlock(&pair->dp_table->t_i.tk_lock);
}

static struct c2_list *pair_list(struct c2_db_pair *pair)
{
	return &pair->dp_table->t_i.tk_pair;
}

int c2_table_update(struct c2_db_tx *tx, struct c2_db_pair *pair)
{
	struct c2_db_kpair *replacement;
	struct c2_db_kpair *kpair;
	int                 result;

	C2_ASSERT(ktable_invariant(pair->dp_table));

	replacement = kpair_alloc(pair);
	if (replacement != NULL) {
		pair_lock(pair);
		kpair = ktable_lookup(pair, &result);
		if (result == 0) {
			c2_list_add_after(&kpair->dk_linkage, 
					  &replacement->dk_linkage);
			c2_list_del(&kpair->dk_linkage);
			c2_free(kpair);
		} else
			c2_free(replacement);
		pair_unlock(pair);
	} else
		result = -ENOMEM;

	C2_ASSERT(ktable_invariant(pair->dp_table));
	return result;
}
C2_EXPORTED(c2_table_update);

int table_insert(struct c2_db_pair *pair, struct c2_db_kpair **kpair_out)
{
	struct c2_db_kpair *newkp;
	struct c2_db_kpair *kpair;
	int                 result;

	C2_ASSERT(ktable_invariant(pair->dp_table));

	newkp = kpair_alloc(pair);
	if (newkp != NULL) {
		int out;

		pair_lock(pair);
		kpair = ktable_lookup(pair, &out);
		if (out == -ENOENT) {
			c2_list_add_before(&kpair->dk_linkage, 
					   &newkp->dk_linkage);
			*kpair_out = newkp;
			result = 0;
		} else {
			c2_free(newkp);
			result = -EEXIST;
		}
		pair_unlock(pair);
	} else
		result = -ENOMEM;
	C2_ASSERT(ktable_invariant(pair->dp_table));
	return result;
}

int c2_table_insert(struct c2_db_tx *tx, struct c2_db_pair *pair)
{
	struct c2_db_kpair *dummy;

	return table_insert(pair, &dummy);
}
C2_EXPORTED(c2_table_insert);

int c2_table_lookup(struct c2_db_tx *tx, struct c2_db_pair *pair)
{
	int                 result;
	struct c2_db_kpair *kpair;

	C2_ASSERT(ktable_invariant(pair->dp_table));

	pair_lock(pair);
	kpair = ktable_lookup(pair, &result);
	if (result == 0)
		kpair_copyout(kpair, pair);
	pair_unlock(pair);
	return result;
}
C2_EXPORTED(c2_table_lookup);

int c2_table_delete(struct c2_db_tx *tx, struct c2_db_pair *pair)
{
	int                 result;
	struct c2_db_kpair *kpair;

	C2_ASSERT(ktable_invariant(pair->dp_table));

	pair_lock(pair);
	kpair = ktable_lookup(pair, &result);
	if (result == 0) {
		c2_list_del(&kpair->dk_linkage);
		c2_free(kpair);
	}
	pair_unlock(pair);

	C2_ASSERT(ktable_invariant(pair->dp_table));

	return result;
}
C2_EXPORTED(c2_table_delete);

int c2_db_cursor_init(struct c2_db_cursor *cursor, struct c2_table *table,
		      struct c2_db_tx *tx)
{
	C2_SET0(cursor);
	return 0;
}
C2_EXPORTED(c2_db_cursor_init);

void c2_db_cursor_fini(struct c2_db_cursor *cursor)
{
}
C2_EXPORTED(c2_db_cursor_fini);

int c2_db_cursor_get(struct c2_db_cursor *cursor, struct c2_db_pair *pair)
{
	int result;

	C2_PRE(cursor->c_table == pair->dp_table);
	C2_ASSERT(ktable_invariant(pair->dp_table));

	c2_mutex_lock(&cursor->c_table->t_i.tk_lock);
	cursor->c_i.ck_current = ktable_lookup(pair, &result);
	if (result == 0)
		kpair_copyout(cursor->c_i.ck_current, pair);
	c2_mutex_unlock(&cursor->c_table->t_i.tk_lock);
	return result;
}
C2_EXPORTED(c2_db_cursor_get);

int c2_db_cursor_next(struct c2_db_cursor *cursor, struct c2_db_pair *pair)
{
	struct c2_db_cursor_impl *ci;
	int                       result;

	C2_PRE(cursor->c_table == pair->dp_table);
	C2_ASSERT(ktable_invariant(pair->dp_table));

	ci = &cursor->c_i;

	if (ci->ck_current == NULL)
		result = -EINVAL;
	else {
		pair_lock(pair);
		if (&ci->ck_current->dk_linkage == pair_list(pair)->l_tail)
			result = -ENOENT;
		else {
			ci->ck_current = kpair_extract
				(ci->ck_current->dk_linkage.ll_next);
			result = kpair_copyout(ci->ck_current, pair);
		}
		pair_unlock(pair);
	}
	return result;
}
C2_EXPORTED(c2_db_cursor_next);

int c2_db_cursor_prev(struct c2_db_cursor *cursor, struct c2_db_pair *pair)
{
	struct c2_db_cursor_impl *ci;
	int                       result;

	C2_PRE(cursor->c_table == pair->dp_table);
	C2_ASSERT(ktable_invariant(pair->dp_table));

	ci = &cursor->c_i;

	if (ci->ck_current == NULL)
		result = -EINVAL;
	else {
		pair_lock(pair);
		if (&ci->ck_current->dk_linkage == pair_list(pair)->l_head)
			result = -ENOENT;
		else {
			ci->ck_current = kpair_extract
				(ci->ck_current->dk_linkage.ll_prev);
			result = kpair_copyout(ci->ck_current, pair);
		}
		pair_unlock(pair);
	}
	return result;
}
C2_EXPORTED(c2_db_cursor_prev);

int c2_db_cursor_first(struct c2_db_cursor *cursor, struct c2_db_pair *pair)
{
	struct c2_db_cursor_impl *ci;
	struct c2_list           *pl;
	int                       result;

	C2_PRE(cursor->c_table == pair->dp_table);
	C2_ASSERT(ktable_invariant(pair->dp_table));

	ci = &cursor->c_i;
	pl = pair_list(pair);

	pair_lock(pair);
	if (c2_list_is_empty(pl))
		result = -ENOENT;
	else {
		ci->ck_current = kpair_extract(pl->l_head);
		kpair_copyout(ci->ck_current, pair);
		result = 0;
	}
	pair_unlock(pair);
	return result;
}
C2_EXPORTED(c2_db_cursor_first);

int c2_db_cursor_last(struct c2_db_cursor *cursor, struct c2_db_pair *pair)
{
	struct c2_db_cursor_impl *ci;
	struct c2_list           *pl;
	int                       result;

	C2_PRE(cursor->c_table == pair->dp_table);
	C2_ASSERT(ktable_invariant(pair->dp_table));

	ci = &cursor->c_i;
	pl = pair_list(pair);

	pair_lock(pair);
	if (c2_list_is_empty(pl))
		result = -ENOENT;
	else {
		ci->ck_current = kpair_extract(pl->l_tail);
		kpair_copyout(ci->ck_current, pair);
		result = 0;
	}
	pair_unlock(pair);
	return result;
}
C2_EXPORTED(c2_db_cursor_last);

int c2_db_cursor_set(struct c2_db_cursor *cursor, struct c2_db_pair *pair)
{
	struct c2_db_cursor_impl *ci;
	struct c2_db_kpair       *newkp;
	struct c2_db_kpair       *cur;
	struct c2_db_pair         replacement;
	int                       result;

	C2_PRE(cursor->c_table == pair->dp_table);
	C2_ASSERT(ktable_invariant(pair->dp_table));

	ci = &cursor->c_i;

	cur = ci->ck_current;
	if (cur == NULL)
		return -EINVAL;

	/* XXX accessing kpair without table mutex. */
	c2_db_pair_setup(&replacement, pair->dp_table,
			 cur->dk_key.b_addr, cur->dk_key.b_nob,
			 pair->dp_rec.db_buf.b_addr, pair->dp_rec.db_buf.b_nob);

	newkp = kpair_alloc(&replacement);
	if (newkp != NULL) {
		pair_lock(pair);
		c2_list_add_after(&cur->dk_linkage, &newkp->dk_linkage);
		c2_list_del(&cur->dk_linkage);
		c2_free(cur);
		ci->ck_current = newkp;
		pair_unlock(pair);
		result = 0;
	} else
		result = -ENOMEM;

	C2_ASSERT(ktable_invariant(pair->dp_table));

	return result;
}
C2_EXPORTED(c2_db_cursor_set);

int c2_db_cursor_add(struct c2_db_cursor *cursor, struct c2_db_pair *pair)
{
	C2_PRE(cursor->c_table == pair->dp_table);

	return table_insert(pair, &cursor->c_i.ck_current);
}
C2_EXPORTED(c2_db_cursor_add);

int c2_db_cursor_del(struct c2_db_cursor *cursor)
{
	struct c2_db_cursor_impl *ci;
	struct c2_table_impl     *ti;
	int                       result;

	C2_ASSERT(ktable_invariant(cursor->c_table));

	ci = &cursor->c_i;
	ti = &cursor->c_table->t_i;

	if (ci->ck_current != NULL) {
		c2_mutex_lock(&ti->tk_lock);
		c2_list_del(&ci->ck_current->dk_linkage);
		c2_free(ci->ck_current);
		ci->ck_current = NULL;
		c2_mutex_unlock(&ti->tk_lock);
		result = 0;
	} else
		result = -EINVAL;
	C2_ASSERT(ktable_invariant(cursor->c_table));
	return result;
}
C2_EXPORTED(c2_db_cursor_del);

void c2_db_buf_impl_init(struct c2_db_buf *buf)
{
}

void c2_db_buf_impl_fini(struct c2_db_buf *buf)
{
}

bool c2_db_buf_impl_invariant(const struct c2_db_buf *buf)
{
	return true;
}

static bool ktable_invariant_locked(struct c2_table *t, 
				    struct c2_table_impl *ti)
{
	struct c2_db_kpair   *scan;
	struct c2_db_kpair   *prev;

	ti = &t->t_i;

	if (!c2_list_invariant(&ti->tk_pair))
		return false;

	prev = NULL;
	c2_list_for_each_entry(&ti->tk_pair, scan, struct c2_db_kpair,
			       dk_linkage) {
		if (scan->dk_key.b_addr != scan + 1)
			return false;
		if (scan->dk_rec.b_addr != 
		    scan->dk_key.b_addr + scan->dk_key.b_nob)
			return false;

		if (prev != NULL && 
		    key_cmp(t, &prev->dk_key, &scan->dk_key) != +1)
			return false;
		prev = scan;
	}
	return true;
}

static bool ktable_invariant(struct c2_table *t)
{
	bool                  result;
	struct c2_table_impl *ti;
	
	ti = &t->t_i;

	c2_mutex_lock(&ti->tk_lock);
	result = ktable_invariant_locked(t, ti);
	c2_mutex_unlock(&ti->tk_lock);
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
