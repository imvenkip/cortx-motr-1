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

#include "be/tx.h"
#include "be/tx_internal.h"

#include "lib/errno.h"		/* ENOMEM */
#include "lib/misc.h"		/* M0_BITS */
#include "lib/arith.h"		/* M0_CNT_INC */

#include "be/op.h"		/* m0_be_op */
#include "be/domain.h"		/* m0_be_domain_engine */
#include "be/engine.h"		/* m0_be_engine__tx_state_set */

/**
 * @addtogroup be
 *
 * @{
 */

#if 0
M0_TL_DESCR_DEFINE(eng, "m0_be_tx_engine::te_txs[]", M0_INTERNAL,
		   struct m0_be_tx, t_engine_linkage, t_magic,
		   M0_BE_TX_MAGIC, M0_BE_TX_ENGINE_MAGIC);

M0_TL_DEFINE(eng, M0_INTERNAL, struct m0_be_tx);
#endif

static bool be_tx_state_invariant(const struct m0_sm *mach)
{
	return m0_be_tx__invariant(
		container_of(mach, const struct m0_be_tx, t_sm));
}

static const ptrdiff_t be_tx_ast_offset[M0_BTS_NR] = {
	[M0_BTS_ACTIVE] = offsetof(struct m0_be_tx, t_ast_active),
	[M0_BTS_FAILED] = offsetof(struct m0_be_tx, t_ast_failed),
	[M0_BTS_GROUPED] = offsetof(struct m0_be_tx, t_ast_grouped),
	[M0_BTS_LOGGED] = offsetof(struct m0_be_tx, t_ast_logged),
	[M0_BTS_PLACED] = offsetof(struct m0_be_tx, t_ast_placed),
	[M0_BTS_DONE] = offsetof(struct m0_be_tx, t_ast_done)
};

static void be_tx_state_move_ast(struct m0_be_tx *tx,
				 enum m0_be_tx_state state);

static void be_tx_ast_cb(struct m0_sm_group *sm_group, struct m0_sm_ast *ast)
{
	enum m0_be_tx_state state = (enum m0_be_tx_state)ast->sa_datum;
	struct m0_be_tx    *tx    = ((void *)ast) - be_tx_ast_offset[state];

	M0_PRE(be_tx_ast_offset[state] != 0);
	be_tx_state_move_ast(tx, state);
}

static struct m0_sm_ast *be_tx_ast(struct m0_be_tx *tx, enum m0_be_tx_state state)
{
	M0_PRE(be_tx_ast_offset[state] != 0);
	return ((void *)tx) + be_tx_ast_offset[state];
}

/* be sure to change be_tx_state_move_ast if change be_tx_states */
static struct m0_sm_state_descr be_tx_states[M0_BTS_NR] = {
	[M0_BTS_PREPARE] = {
		.sd_flags = M0_SDF_INITIAL,
		.sd_name = "prepare",
		.sd_invariant = be_tx_state_invariant,
		.sd_allowed = M0_BITS(M0_BTS_OPENING, M0_BTS_FAILED),
	},
	[M0_BTS_OPENING] = {
		.sd_name = "opening",
		.sd_invariant = be_tx_state_invariant,
		.sd_allowed = M0_BITS(M0_BTS_ACTIVE, M0_BTS_FAILED),
	},
	[M0_BTS_FAILED] = {
		.sd_flags =  M0_SDF_TERMINAL | M0_SDF_FAILURE,
		.sd_name = "failed",
		.sd_invariant = be_tx_state_invariant,
		.sd_allowed = 0,
	},
	[M0_BTS_ACTIVE] = {
		.sd_name = "active",
		.sd_invariant = be_tx_state_invariant,
		.sd_allowed = M0_BITS(M0_BTS_CLOSED),
	},
	[M0_BTS_CLOSED] = {
		.sd_name = "closed",
		.sd_invariant = be_tx_state_invariant,
		.sd_allowed = M0_BITS(M0_BTS_GROUPED),
	},
	[M0_BTS_GROUPED] = {
		.sd_name = "grouped",
		.sd_invariant = be_tx_state_invariant,
		.sd_allowed = M0_BITS(M0_BTS_LOGGED),
	},
	[M0_BTS_LOGGED] = {
		.sd_name = "logged",
		.sd_invariant = be_tx_state_invariant,
		.sd_allowed = M0_BITS(M0_BTS_PLACED),
	},
	[M0_BTS_PLACED] = {
		.sd_name = "placed",
		.sd_invariant = be_tx_state_invariant,
		.sd_allowed = M0_BITS(M0_BTS_DONE),
	},
	[M0_BTS_DONE] = {
		.sd_flags = M0_SDF_TERMINAL,
		.sd_name = "done",
		.sd_invariant = be_tx_state_invariant,
		.sd_allowed = 0,
	},
};

static const struct m0_sm_conf be_tx_sm_conf = {
	.scf_name      = "m0_be_tx::t_sm",
	.scf_nr_states = M0_BTS_NR,
	.scf_state     = be_tx_states
};

static bool be_tx_is_locked(const struct m0_be_tx *tx);
static void be_tx_state_move(struct m0_be_tx *tx,
			     enum m0_be_tx_state state,
			     int rc);

M0_INTERNAL void m0_be_tx_init(struct m0_be_tx    *tx,
			       uint64_t            tid,
			       struct m0_be_domain    *dom,
			       struct m0_sm_group *sm_group,
			       m0_be_tx_cb_t       persistent,
			       m0_be_tx_cb_t       discarded,
			       bool                is_part_of_global_tx,
			       void              (*filler)(struct m0_be_tx *tx,
							   void *payload),
			       void               *datum)
{
	enum m0_be_tx_state state;

	/* XXX REFACTORME */
	*tx = (struct m0_be_tx) {
	};

	m0_sm_init(&tx->t_sm, &be_tx_sm_conf, M0_BTS_PREPARE, sm_group);

	tx->t_id		 = tid;
	tx->t_engine		 = m0_be_domain_engine(dom);

	m0_be_tx_credit_init(&tx->t_prepared);
	tx->t_persistent	 = persistent;
	tx->t_discarded		 = discarded;
	tx->t_filler		 = filler;
	tx->t_datum		 = datum;

	for (state = 0; state < ARRAY_SIZE(be_tx_ast_offset); ++state) {
		if (be_tx_ast_offset[state] != 0) {
			*be_tx_ast(tx, state) = (struct m0_sm_ast) {
				.sa_cb	  = be_tx_ast_cb,
				.sa_datum = (void *) state,
			};
		}
	}

	m0_be_engine__tx_init(tx->t_engine, tx, M0_BTS_PREPARE);
	m0_be_tx_get(tx);

	M0_POST(m0_be_tx__invariant(tx));
}

M0_INTERNAL void m0_be_tx_fini(struct m0_be_tx *tx)
{
	enum m0_be_tx_state state;

	/* XXX reorder M0_PRE to check be_tx_is_locked() first */
	M0_PRE(m0_be_tx__invariant(tx));
	M0_PRE(M0_IN(m0_be_tx_state(tx), (M0_BTS_DONE, M0_BTS_FAILED)));
	M0_PRE(be_tx_is_locked(tx));
	M0_PRE(tx->t_ref == 0);

	m0_be_engine__tx_fini(tx->t_engine, tx);

	for (state = 0; state < ARRAY_SIZE(be_tx_ast_offset); ++state) {
		if (be_tx_ast_offset[state] != 0)
			m0_sm_ast_cancel(tx->t_sm.sm_grp, be_tx_ast(tx, state));
	}
	m0_be_reg_area_fini(&tx->t_reg_area);
	m0_sm_fini(&tx->t_sm);
}

M0_INTERNAL void
m0_be_tx_prep(struct m0_be_tx *tx, const struct m0_be_tx_credit *credit)
{
	M0_ENTRY();
	M0_PRE(m0_be_tx__invariant(tx));
	M0_PRE(m0_be_tx_state(tx) == M0_BTS_PREPARE);
	M0_PRE(be_tx_is_locked(tx));

	m0_be_tx_credit_add(&tx->t_prepared, credit);

	M0_POST(m0_be_tx__invariant(tx));
	M0_LEAVE();
}

M0_INTERNAL void m0_be_tx_open(struct m0_be_tx *tx)
{
	int rc;

	M0_ENTRY();
	M0_PRE(m0_be_tx__invariant(tx));
	M0_PRE(m0_be_tx_state(tx) == M0_BTS_PREPARE);
	M0_PRE(be_tx_is_locked(tx));

	rc = m0_be_reg_area_init(&tx->t_reg_area, &tx->t_prepared, true);

	be_tx_state_move(tx, rc == 0 ? M0_BTS_OPENING : M0_BTS_FAILED, rc);

	M0_POST(m0_be_tx__invariant(tx));
	M0_LEAVE();
}

static void cap_uncap(struct m0_be_tx *tx, const struct m0_be_reg *reg,
		      void (*op)(struct m0_be_reg_area *ra,
				 const struct m0_be_reg_d *rd))
{
	M0_PRE(m0_be_tx__invariant(tx));
	M0_PRE(m0_be_tx_state(tx) == M0_BTS_ACTIVE);
	M0_PRE(be_tx_is_locked(tx));

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

M0_INTERNAL void m0_be_tx_close(struct m0_be_tx *tx)
{
	M0_ENTRY();
	M0_PRE(m0_be_tx__invariant(tx));
	M0_PRE(m0_be_tx_state(tx) == M0_BTS_ACTIVE);
	M0_PRE(be_tx_is_locked(tx));

	be_tx_state_move(tx, M0_BTS_CLOSED, 0);

	M0_LEAVE();
}

M0_INTERNAL void m0_be_tx_get(struct m0_be_tx *tx)
{
	M0_ENTRY();
	M0_PRE(be_tx_is_locked(tx));

	M0_CNT_INC(tx->t_ref);
}

M0_INTERNAL void m0_be_tx_put(struct m0_be_tx *tx)
{
	M0_ENTRY();
	M0_PRE(be_tx_is_locked(tx));

	M0_CNT_DEC(tx->t_ref);
	if (tx->t_ref == 0 && m0_be_tx_state(tx) != M0_BTS_FAILED)
		m0_be_tx__state_post(tx, M0_BTS_DONE);
}

M0_INTERNAL int
m0_be_tx_timedwait(struct m0_be_tx *tx, int states, m0_time_t timeout)
{
	M0_ENTRY();
	M0_PRE(be_tx_is_locked(tx));

	m0_sm_timedwait(&tx->t_sm, states, timeout);
	M0_RETURN(tx->t_sm.sm_rc);
}

#if 0
M0_INTERNAL void m0_be_tx_stable(struct m0_be_tx *tx)
{
	enum m0_be_tx_state state = m0_be_tx_state(tx);

	M0_ENTRY();
	M0_PRE(m0_be_tx__invariant(tx));
	M0_PRE(be_tx_is_locked(tx));
	M0_PRE(M0_IN(state, (M0_BTS_PLACED, M0_BTS_GROUPED)));
	M0_PRE(!tx->t_glob_stable);

	tx->t_glob_stable = true;
	if (state == M0_BTS_PLACED) {
		be_tx_state_move(tx, M0_BTS_DONE, 0);
		m0_fom_wakeup(tx_engine(tx)->te_fom);
	}
	M0_LEAVE();
}
#endif

M0_INTERNAL enum m0_be_tx_state m0_be_tx_state(const struct m0_be_tx *tx)
{
	return tx->t_sm.sm_state;
}

M0_INTERNAL const char *m0_be_tx_state_name(const struct m0_be_tx *tx,
					    enum m0_be_tx_state state)
{
	return m0_sm_state_name(&tx->t_sm, state);
}

static void be_tx_state_move_ast(struct m0_be_tx *tx, enum m0_be_tx_state state)
{
	enum m0_be_tx_state tx_state = m0_be_tx_state(tx);

	M0_LOG(M0_DEBUG, "ast: tx = %p, %s -> %s",
	       tx, m0_be_tx_state_name(tx, tx_state),
	       m0_be_tx_state_name(tx, state));

	if (tx_state < M0_BTS_CLOSED || state == tx_state + 1) {
		be_tx_state_move(tx, state,
				 state == M0_BTS_FAILED ? -ENOMEM : 0);
	} else {
		while (tx_state < state)
			be_tx_state_move(tx, ++tx_state, 0);
	}
}

static void be_tx_state_move(struct m0_be_tx *tx,
			     enum m0_be_tx_state state,
			     int rc)
{
	M0_ENTRY("tx %p: %s --> %s, rc = %d", tx,
		 m0_be_tx_state_name(tx, m0_be_tx_state(tx)),
		 m0_be_tx_state_name(tx, state), rc);

	M0_PRE(m0_be_tx__invariant(tx));
	M0_PRE(be_tx_is_locked(tx));

	if (rc != 0)
		M0_LOG(M0_ERROR, "transaction failure: err=%d", rc);

	if (state == M0_BTS_LOGGED && tx->t_persistent != NULL)
		tx->t_persistent(tx);
	if (state == M0_BTS_DONE && tx->t_discarded != NULL)
		tx->t_discarded(tx);

	m0_sm_move(&tx->t_sm, rc, state);
	m0_be_engine__tx_state_set(tx->t_engine, tx, state);

	if (state == M0_BTS_PLACED || state == M0_BTS_FAILED)
		m0_be_tx_put(tx);

	M0_POST(m0_be_tx__invariant(tx));
	M0_LEAVE();
}

M0_INTERNAL void m0_be_tx__state_post(struct m0_be_tx *tx,
				      enum m0_be_tx_state state)
{
	/* XXX move to group_fom doc */
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
	M0_PRE(M0_IN(state, (M0_BTS_ACTIVE, M0_BTS_FAILED, M0_BTS_GROUPED,
			     M0_BTS_LOGGED, M0_BTS_PLACED, M0_BTS_DONE)));
	M0_LOG(M0_DEBUG, "tx = %p, state = %s",
	       tx, m0_be_tx_state_name(tx, state));

	m0_sm_ast_post(tx->t_sm.sm_grp, be_tx_ast(tx, state));
}

M0_INTERNAL bool m0_be_tx__invariant(const struct m0_be_tx *tx)
{
	M0_PRE(be_tx_is_locked(tx));
	/* const enum m0_be_tx_state state = m0_be_tx_state(tx); */

	return true; /* XXX RESTOREME */
#if 0
		(state < M0_BTS_NR &&
		eng_tlist_contains(&tx_engine(tx)->te_txs[state], tx) &&
		(tx->t_lsn == 0) == (state < M0_BTS_GROUPED) &&
		m0_be_reg_area__invariant(&tx->t_reg_area) &&
		(tx->t_group != NULL) == (state >= M0_BTS_GROUPED) &&
		(tx->t_leader == (
			tx->t_group != NULL &&
			tx == grp_tlist_head(&tx->t_group->tg_txs))) &&
		(tx->t_group != NULL) == grp_tlist_contains(
			&tx->t_group->tg_txs, tx));
#endif
}

static bool be_tx_is_locked(const struct m0_be_tx *tx)
{
	return m0_mutex_is_locked(&tx->t_sm.sm_grp->s_lock);
}

#if 0
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
#endif

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
