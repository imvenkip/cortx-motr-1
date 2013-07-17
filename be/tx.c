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
 * Original creation date: 29-May-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "lib/errno.h"
#include "lib/misc.h"          /* m0_forall */
#include "lib/cdefs.h"         /* ARRAY_SIZE */
#include "lib/memory.h"
#include "lib/types.h"
#include "lib/ext.h"           /* m0_ext */

#include "be/be.h"
#include "be/tx.h"
#include "fop/fom.h"

/**
 * @addtogroup be
 *
 * @{
 */

static int placed_st_in(struct m0_sm *mach);
static int   done_st_in(struct m0_sm *mach);

M0_TL_DESCR_DEFINE(eng, "m0_be_tx_engine::te_txs[]", M0_INTERNAL,
		   struct m0_be_tx, t_engine_linkage, t_magic,
		   M0_BE_TX_MAGIC, M0_BE_TX_ENGINE_MAGIC);

M0_TL_DEFINE(eng, M0_INTERNAL, struct m0_be_tx);

static bool tx_state_invariant(const struct m0_sm *mach)
{
	return m0_be__tx_invariant(
		container_of(mach, const struct m0_be_tx, t_sm));
}

static struct m0_sm_state_descr tx_states[M0_BTS_NR] = {
#define _S(name, flags, allowed)                    \
	[name] = {                                  \
		.sd_flags     = flags,              \
		.sd_name      = #name,              \
		.sd_invariant = tx_state_invariant, \
		.sd_allowed   = allowed             \
	}

	_S(M0_BTS_FAILED,  M0_SDF_FAILURE, 0),
	_S(M0_BTS_PREPARE, M0_SDF_INITIAL, M0_BITS(M0_BTS_ACTIVE,
						   M0_BTS_OPENING,
						   M0_BTS_FAILED)),
	_S(M0_BTS_OPENING, 0, M0_BITS(M0_BTS_ACTIVE, M0_BTS_FAILED)),
	_S(M0_BTS_ACTIVE,  0, M0_BITS(M0_BTS_CLOSED)),
	_S(M0_BTS_CLOSED,  0, M0_BITS(M0_BTS_GROUPED)),
	_S(M0_BTS_GROUPED, 0, M0_BITS(M0_BTS_PLACED)),
	[M0_BTS_PLACED] = {
		.sd_name      = "M0_BTS_PLACED",
		.sd_in        = placed_st_in,
		.sd_invariant = tx_state_invariant,
		.sd_allowed   = M0_BITS(M0_BTS_DONE)
	},
	[M0_BTS_DONE] = {
		.sd_flags     = M0_SDF_TERMINAL,
		.sd_name      = "M0_BTS_DONE",
		.sd_in        = done_st_in,
		.sd_invariant = tx_state_invariant,
		.sd_allowed   = 0
	}
#undef _S
};

static const struct m0_sm_conf tx_sm_conf = {
	.scf_name      = "m0_be_tx::t_sm",
	.scf_nr_states = M0_BTS_NR,
	.scf_state     = tx_states
};

static struct m0_be_tx_engine *tx_engine(const struct m0_be_tx *tx);
static void        tx_fail             (struct m0_be_tx *tx, int err);
static void        tx_engine_got_space (struct m0_be_tx_engine *eng);
static void        tx_engine_lock      (struct m0_be_tx_engine *eng);
static void        tx_engine_unlock    (struct m0_be_tx_engine *eng);
static bool        tx_is_locked        (const struct m0_be_tx *tx);

M0_INTERNAL void m0_be_tx_engine_init(struct m0_be_tx_engine *engine)
{
	int rc;

	m0_be_log_init(&engine->te_log);
	rc = m0_be_log_create(&engine->te_log, 1ULL << 28);
	/* XXX */
	M0_ASSERT(rc == 0);
	m0_forall(i, ARRAY_SIZE(engine->te_txs),
		  (eng_tlist_init(&engine->te_txs[i]), true));
	m0_rwlock_init(&engine->te_lock);
	tx_group_init(&engine->te_group, m0_be_log_stob(&engine->te_log));
	/*
	log_init(&engine->te_log_X, 1ULL << 28, 1ULL << 25, 200000);
	*/

	M0_POST(m0_be__tx_engine_invariant(engine));
}

M0_INTERNAL void m0_be_tx_engine_fini(struct m0_be_tx_engine *engine)
{
	M0_PRE(m0_be__tx_engine_invariant(engine));

	tx_group_fini(&engine->te_group);
	m0_rwlock_fini(&engine->te_lock);
	m0_forall(i, ARRAY_SIZE(engine->te_txs),
		  (eng_tlist_fini(&engine->te_txs[i]), true));
	m0_be_log_destroy(&engine->te_log);
	m0_be_log_fini(&engine->te_log);
}

M0_INTERNAL void m0_be_tx_init(struct m0_be_tx    *tx,
			       uint64_t            tid,
			       struct m0_be       *be,
			       struct m0_sm_group *sm_group,
			       m0_be_tx_cb_t       persistent,
			       m0_be_tx_cb_t       discarded,
			       bool                is_part_of_global_tx,
			       void              (*filler)(struct m0_be_tx *tx,
							   void *payload),
			       void               *datum)
{
	m0_sm_init(&tx->t_sm, &tx_sm_conf, M0_BTS_PREPARE, sm_group);

	tx->t_id = tid;
	tx->t_be = be;
	eng_tlink_init_at(tx, &tx_engine(tx)->te_txs[M0_BTS_PREPARE]);
	grp_tlink_init(tx);

	m0_be_tx_credit_init(&tx->t_prepared);
	tx->t_persistent  = persistent;
	tx->t_discarded   = discarded;
	tx->t_glob_stable = !is_part_of_global_tx;
	tx->t_filler      = filler;
	tx->t_datum       = datum;

	M0_POST(m0_be__tx_invariant(tx));
}

M0_INTERNAL void m0_be_tx_fini(struct m0_be_tx *tx)
{
	M0_PRE(m0_be__tx_invariant(tx));

	m0_be_reg_area_fini(&tx->t_reg_area);
	eng_tlink_del_fini(tx);
	m0_sm_fini(&tx->t_sm);
}

M0_INTERNAL void
m0_be_tx_prep(struct m0_be_tx *tx, const struct m0_be_tx_credit *credit)
{
	M0_ENTRY();
	M0_PRE(m0_be__tx_invariant(tx));
	M0_PRE(m0_be__tx_state(tx) == M0_BTS_PREPARE);
	M0_PRE(tx_is_locked(tx));

	m0_be_tx_credit_add(&tx->t_prepared, credit);

	M0_POST(m0_be__tx_invariant(tx));
	M0_LEAVE();
}

M0_INTERNAL void m0_be_tx_open(struct m0_be_tx *tx)
{
	struct m0_be_tx_engine *eng = tx_engine(tx);
	int			rc;

	M0_ENTRY();
	M0_PRE(m0_be__tx_invariant(tx));
	M0_PRE(m0_be__tx_state(tx) == M0_BTS_PREPARE);
	M0_PRE(tx_is_locked(tx));

	rc = m0_be_reg_area_init(&tx->t_reg_area, &tx->t_prepared, true);
	if (rc == 0) {
		rc = m0_be_log_reserve_tx(&eng->te_log, &tx->t_prepared);
		if (rc == 0) {
			m0_be__tx_state_set(tx, M0_BTS_ACTIVE);
		} else {
			m0_be_reg_area_fini(&tx->t_reg_area);
		}
	}
	if (rc != 0)
		tx_fail(tx, rc);

	M0_POST(m0_be__tx_invariant(tx));
	M0_LEAVE();
}

#if 0 /* XXX Nikita's code */
M0_INTERNAL void
m0_be_tx_capture(struct m0_be_tx *tx, const struct m0_be_reg *reg)
{
	struct m0_be_tx_credit *pos = &tx->t_pos;
	m0_bindex_t             idx = pos->tc_reg_nr;
	struct m0_be_reg_d     *new = &tx->t_reg_d_area[idx];
	struct m0_be_reg_d     *old;
	struct m0_be_reg_d     *prev = NULL;

	M0_PRE(m0_be__tx_invariant(tx));
	M0_PRE(m0_be__tx_state(tx) == M0_BTS_ACTIVE);
	M0_PRE(tx_is_locked(tx));

	new->rd_tx  = tx;
	new->rd_idx = idx;
	new->rd_buf = tx->t_reg_area + pos->tc_reg_size;
	new->rd_reg = *reg;

	M0_LOG(M0_DEBUG, "capture %p: [%p, %p)", new, new->rd_reg.br_addr,
	       new->rd_reg.br_addr + new->rd_reg.br_size);
	m0_be_tx_credit_mod(&tx->t_captured, new, +1);
	/*
	 * A transaction keeps a tree of captured regions in tx->t_tree. Key
	 * comparison function in this address is tx_reg_cmp(), which pretends
	 * that overlapping regions are equal. As a result, the tree contains
	 * disjoint regions, which are sorted by their starting address.
	 *
	 * The loop below iterates over all already captured regions
	 * intersecting with the new one.
	 */
	while (new->rd_reg.br_size > 0) {
		struct m0_ext               enew;
		struct m0_ext               eold;
		struct m0_ext               intersection;
		struct m0_be_regdtree_node *oldnode;

		oldnode = tsearch(new, &tx->t_root, &tx_reg_cmp);
		M0_ASSERT(oldnode != NULL);
		old = oldnode->bn_reg_d;
		M0_ASSERT(old != prev); /* check that we are not stuck */

		/*
		 * tsearch() returns either an existing tree element with the
		 * key equal to the key of the new element (i.e., an
		 * intersecting region), or the new element. In the latter case,
		 * "new" is inserted in the tree.
		 */
		if (old == new) {
			/* No intersection. */
			memcpy(new->rd_buf,
			       new->rd_reg.br_addr, new->rd_reg.br_size);
			m0_be_tx_credit_mod(&tx->t_used, new, +1);
			m0_be_tx_credit_mod(&tx->t_pos,  new, +1);
			break;
		}
		tx_reg_ext(old, &eold);
		tx_reg_ext(new, &enew);
		if (m0_ext_is_partof(&enew, &eold)) {
			/* New completely covers old. */
			m0_be_tx_credit_mod(&tx->t_used, old, -1);
			tdelete(old, &tx->t_root, &tx_reg_cmp);
			M0_SET0(old);
		} else {
			/* New and old regions partially overlap. */
			m0_bcount_t oleft;
			m0_bcount_t nleft;
			m0_bcount_t nright;
			m0_bcount_t common;

			/*
			 * First, replace common intersection of new and old
			 * with the new data.
			 */
			m0_ext_intersection(&eold, &enew, &intersection);
			M0_ASSERT(!m0_ext_is_empty(&intersection));
			oleft  = intersection.e_start - eold.e_start;
			nleft  = intersection.e_start - enew.e_start;
			nright = enew.e_end - intersection.e_end;
			common = m0_ext_length(&intersection);
			/*
			 * Following cases are possible:
			 *
			 *    nleft > 0, nright == 0, oleft == 0
			 *
			 *                  nleft    common
			 *    new:        [--------+--------]
			 *    old:                 [--------+----]
			 *
			 *
			 *    nleft == 0, nright > 0
			 *
			 *                           common   nright
			 *    new:                 [--------+--------]
			 *    old:    [------------+--------]
			 *                oleft
			 *
			 *
			 *    nleft == 0 && nright == 0
			 *
			 *                           common
			 *    new:                 [--------]
			 *    old:    [------------+--------+--------]
			 *                oleft
			 */
			memcpy(old->rd_buf + oleft, new->rd_reg.br_addr + nleft,
			       common);
			if (nleft == 0 && nright == 0)
				/* old completely covers new. */
				break;
			/*
			 * New can extend beyond intersection only in one
			 * direction, because otherwise it would completely
			 * cover old and this was checked above.
			 */
			M0_ASSERT((nleft > 0) != (nright > 0));
			/*
			 * Shrink the new region and repeat the loop to check
			 * for intersection with another existing region.
			 */
			new->rd_reg.br_size -= common;
			if (nright > 0)
				new->rd_reg.br_addr += common;
		}
		prev = old;
	}
	M0_POST(m0_be__tx_invariant(tx));
}
#else /* XXX */
static void cap_uncap(struct m0_be_tx *tx, const struct m0_be_reg *reg,
		      void (*op)(struct m0_be_reg_area *ra,
				 const struct m0_be_reg_d *rd))
{
	M0_PRE(m0_be__tx_invariant(tx));
	M0_PRE(m0_be__tx_state(tx) == M0_BTS_ACTIVE);
	M0_PRE(tx_is_locked(tx));

	op(&tx->t_reg_area,
	   &((const struct m0_be_reg_d){ .rd_tx = tx, .rd_reg = *reg }));
}

M0_INTERNAL void
m0_be_tx_capture(struct m0_be_tx *tx, const struct m0_be_reg *reg)
{
	cap_uncap(tx, reg, m0_be_reg_area_capture);
}

M0_INTERNAL void
m0_be_tx_uncapture(struct m0_be_tx *tx, const struct m0_be_reg *reg)
{
	cap_uncap(tx, reg, m0_be_reg_area_uncapture);
}
#endif /* XXX */

M0_INTERNAL void m0_be_tx_close(struct m0_be_tx *tx)
{
	struct m0_be_tx_engine *eng = tx_engine(tx);

	M0_ENTRY();
	M0_PRE(m0_be__tx_invariant(tx));
	M0_PRE(m0_be__tx_state(tx) == M0_BTS_ACTIVE);
	M0_PRE(tx_is_locked(tx));

	tx_engine_lock(eng);
	m0_be__tx_state_set(tx, M0_BTS_CLOSED);
	tx_engine_got_space(eng);
	tx_engine_unlock(eng);

	M0_LEAVE();
}

M0_INTERNAL int
m0_be_tx_timedwait(struct m0_be_tx *tx, int state, m0_time_t timeout)
{
	M0_ENTRY();
	M0_PRE(tx_is_locked(tx));

	m0_sm_timedwait(&tx->t_sm, state, timeout);
	M0_RETURN(tx->t_sm.sm_rc);
}

M0_INTERNAL void m0_be_tx_stable(struct m0_be_tx *tx)
{
	enum m0_be_tx_state state = m0_be__tx_state(tx);

	M0_ENTRY();
	M0_PRE(m0_be__tx_invariant(tx));
	M0_PRE(tx_is_locked(tx));
	M0_PRE(M0_IN(state, (M0_BTS_PLACED, M0_BTS_GROUPED)));
	M0_PRE(!tx->t_glob_stable);

	tx->t_glob_stable = true;
	if (state == M0_BTS_PLACED) {
		m0_be__tx_state_set(tx, M0_BTS_DONE);
		m0_fom_wakeup(tx_engine(tx)->te_fom);
	}
	M0_LEAVE();
}

static void tx_engine_got_space(struct m0_be_tx_engine *eng)
{
	struct m0_be_tx *tx;
	int              rc;

	M0_PRE(m0_be__tx_engine_invariant(eng));

	m0_tl_for(eng, &eng->te_txs[M0_BTS_OPENING], tx) {
		rc = m0_be_log_reserve_tx(&tx_engine(tx)->te_log,
					  &tx->t_prepared);
		if (rc == 0)
			m0_be__tx_state_set(tx, M0_BTS_ACTIVE);
		else
			break;
	} m0_tl_endfor;

	M0_POST(m0_be__tx_engine_invariant(eng));
}

M0_INTERNAL enum m0_be_tx_state m0_be__tx_state(const struct m0_be_tx *tx)
{
	return tx->t_sm.sm_state;
}

M0_INTERNAL void
m0_be__tx_state_set(struct m0_be_tx *tx, enum m0_be_tx_state state)
{
	M0_ENTRY("%d --> %d", m0_be__tx_state(tx), state);
	M0_PRE(m0_be__tx_invariant(tx));
	M0_PRE(tx_is_locked(tx));
	M0_PRE(m0_be__tx_state(tx) != state);

	m0_sm_state_set(&tx->t_sm, state);
	/*
	 * XXX TODO: M0_PRE() that tx_engine is locked exclusively.
	 * We need to introduce some kind of m0_rwlock_write_locked() for
	 * this purpose.
	 */
	/* Link the transaction. */
	eng_tlist_move(&tx_engine(tx)->te_txs[state], tx);

	M0_POST(m0_be__tx_invariant(tx));
	M0_LEAVE();
}

static void tx_fail(struct m0_be_tx *tx, int err)
{
	M0_PRE(m0_be__tx_invariant(tx));

	m0_sm_fail(&tx->t_sm, M0_BTS_FAILED, err);
	eng_tlist_del(tx);

	M0_POST(m0_be__tx_invariant(tx));
}

static void _tx_grouped(struct m0_sm_group *_, struct m0_sm_ast *ast)
{
	m0_be__tx_state_set(container_of(ast, struct m0_be_tx, t_ast),
			    M0_BTS_GROUPED);
	m0_ref_put(ast->sa_datum);
}

static void _tx_placed(struct m0_sm_group *_, struct m0_sm_ast *ast)
{
	m0_be__tx_state_set(container_of(ast, struct m0_be_tx, t_ast),
			    M0_BTS_PLACED);
}

M0_INTERNAL void m0_be__tx_state_post(struct m0_be_tx    *tx,
				      enum m0_be_tx_state to,
				      struct m0_ref      *ref)
{
	/*
	 * tx_group's fom and tx's sm may belong different sm_groups (e.g.,
	 * they may be processed by different localities).
	 *
	 *             locality
	 *             --------
	 *             sm_group     sm_group    sm_group
	 *                | |            |         | |
	 *                | |            |         | |
	 *      tx_group  | |            |         | |
	 *      --------  | |            |         | |
	 *           fom -' |            |         | |
	 *                  |  tx    tx  |     tx  | |  tx
	 *                  |  --    --  |     --  | |  --
	 *                  `- sm    sm -'     sm -' `- sm
	 *
	 * ->fo_tick() of tx_group's fom shall not assume that sm_group of
	 * tx's sm is locked. In order to advance tx's sm, ->fo_tick()
	 * implementation should post an AST to tx's sm_group.
	 */
	M0_PRE(M0_IN(to, (M0_BTS_GROUPED, M0_BTS_PLACED)));
	M0_PRE((to == M0_BTS_PLACED) == (ref == NULL));

	tx->t_ast.sa_cb = to == M0_BTS_GROUPED ? _tx_grouped : _tx_placed;
	tx->t_ast.sa_datum = ref;
	m0_sm_ast_post(tx->t_sm.sm_grp, &tx->t_ast);
}

static struct m0_be_tx_engine *tx_engine(const struct m0_be_tx *tx)
{
	return &tx->t_be->b_tx_engine;
}

static void tx_engine_lock(struct m0_be_tx_engine *eng)
{
	m0_rwlock_write_lock(&eng->te_lock);
}

static void tx_engine_unlock(struct m0_be_tx_engine *eng)
{
	m0_rwlock_write_unlock(&eng->te_lock);
}

M0_INTERNAL bool
m0_be__tx_engine_invariant(const struct m0_be_tx_engine *engine)
{
	struct m0_be_tx *prev = NULL;

	return true || /* XXX RESTOREME */
		m0_forall(i, M0_BTS_NR,
			  m0_tl_forall(eng, t, &engine->te_txs[i],
				       m0_be__tx_invariant(t) &&
				       ergo(prev != NULL && prev->t_lsn != 0,
					    t->t_lsn != 0 &&
					    prev->t_lsn > t->t_lsn) &&
				       (prev = t, true)));
}

M0_INTERNAL bool m0_be__tx_invariant(const struct m0_be_tx *tx)
{
	const enum m0_be_tx_state state = m0_be__tx_state(tx);

	return true || ( /* XXX RESTOREME */
		state < M0_BTS_NR &&
		eng_tlist_contains(&tx_engine(tx)->te_txs[state], tx) &&
		(tx->t_lsn == 0) == (state < M0_BTS_GROUPED) &&
		m0_be_reg_area__invariant(&tx->t_reg_area) &&
		(tx->t_group != NULL) == (state >= M0_BTS_GROUPED) &&
		(tx->t_leader == (
			tx->t_group != NULL &&
			tx == grp_tlist_head(&tx->t_group->tg_txs))) &&
		(tx->t_group != NULL) == grp_tlist_contains(
			&tx->t_group->tg_txs, tx));
}

static bool tx_is_locked(const struct m0_be_tx *tx)
{
	return m0_mutex_is_locked(&tx->t_sm.sm_grp->s_lock);
}

static struct m0_be_tx *sm_to_tx(struct m0_sm *mach)
{
	return container_of(mach, struct m0_be_tx, t_sm); /* XXX bob_of() */
}

static int placed_st_in(struct m0_sm *mach)
{
	M0_ENTRY("t_glob_stable=%d", !!sm_to_tx(mach)->t_glob_stable);
	M0_LEAVE();
	/* XXX FIXME: Don't intermix "external" and "chained" styles of state
	 * transitions. This makes code hard to read. (Reported by Nikita.) */
	return sm_to_tx(mach)->t_glob_stable ? M0_BTS_DONE : -1;
}

static int done_st_in(struct m0_sm *mach)
{
	const struct m0_be_tx *tx = sm_to_tx(mach);
	M0_ENTRY();

	if (tx->t_discarded != NULL)
		tx->t_discarded(tx);
	M0_LEAVE();
	return -1;
}

M0_INTERNAL struct m0_be_reg_area *m0_be_tx__reg_area(struct m0_be_tx *tx)
{
	return &tx->t_reg_area;
}

/** @} end of be group */
#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
