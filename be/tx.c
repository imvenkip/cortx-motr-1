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
#include "be/domain.h"
#include "fop/fom.h"
#include "be/engine.h"

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

static int done_st_in(struct m0_sm *mach);


static bool tx_state_invariant(const struct m0_sm *mach)
{
	return m0_be_tx__invariant(
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
	_S(M0_BTS_PREPARE, M0_SDF_INITIAL, M0_BITS(M0_BTS_OPENING,
						   M0_BTS_FAILED)),
	_S(M0_BTS_OPENING, 0, M0_BITS(M0_BTS_ACTIVE, M0_BTS_FAILED)),
	_S(M0_BTS_ACTIVE,  0, M0_BITS(M0_BTS_CLOSED)),
	_S(M0_BTS_CLOSED,  0, M0_BITS(M0_BTS_GROUPED)),
	_S(M0_BTS_GROUPED,  0, M0_BITS(M0_BTS_LOGGED)),
	_S(M0_BTS_LOGGED, 0, M0_BITS(M0_BTS_PLACED)),
	_S(M0_BTS_PLACED, 0, M0_BITS(M0_BTS_DONE)),
#undef _S
	[M0_BTS_DONE] = {
		.sd_flags     = M0_SDF_TERMINAL,
		.sd_name      = "M0_BTS_DONE",
		.sd_in        = done_st_in,
		.sd_invariant = tx_state_invariant,
		.sd_allowed   = 0
	}
};

static const struct m0_sm_conf tx_sm_conf = {
	.scf_name      = "m0_be_tx::t_sm",
	.scf_nr_states = M0_BTS_NR,
	.scf_state     = tx_states
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
	m0_sm_init(&tx->t_sm, &tx_sm_conf, M0_BTS_PREPARE, sm_group);

	tx->t_id		 = tid;
	tx->t_engine		 = m0_be_domain_engine(dom);

	m0_be_tx_credit_init(&tx->t_prepared);
	tx->t_reg_area_allocated = false;
	tx->t_persistent	 = persistent;
	tx->t_discarded		 = discarded;
	tx->t_glob_stable	 = !is_part_of_global_tx; /* XXX */
	tx->t_filler		 = filler;
	tx->t_datum		 = datum;

	m0_be_engine__tx_init(tx->t_engine, tx, M0_BTS_PREPARE);

	M0_POST(m0_be_tx__invariant(tx));
}

M0_INTERNAL void m0_be_tx_fini(struct m0_be_tx *tx)
{
	M0_PRE(m0_be_tx__invariant(tx));

	m0_be_engine__tx_fini(tx->t_engine, tx);

	if (tx->t_reg_area_allocated)
		m0_be_reg_area_fini(&tx->t_reg_area);
	m0_sm_fini(&tx->t_sm);
}

M0_INTERNAL void
m0_be_tx_prep(struct m0_be_tx *tx, const struct m0_be_tx_credit *credit)
{
	M0_ENTRY();
	M0_PRE(m0_be_tx__invariant(tx));
	M0_PRE(m0_be_tx__state(tx) == M0_BTS_PREPARE);
	M0_PRE(be_tx_is_locked(tx));

	m0_be_tx_credit_add(&tx->t_prepared, credit);

	M0_POST(m0_be_tx__invariant(tx));
	M0_LEAVE();
}

M0_INTERNAL int m0_be_tx_open(struct m0_be_tx *tx)
{
	int rc;

	M0_ENTRY();
	M0_PRE(m0_be_tx__invariant(tx));
	M0_PRE(m0_be_tx__state(tx) == M0_BTS_PREPARE);
	M0_PRE(be_tx_is_locked(tx));

	rc = m0_be_reg_area_init(&tx->t_reg_area, &tx->t_prepared, true);
	tx->t_reg_area_allocated = rc == 0;

	be_tx_state_move(tx, rc == 0 ? M0_BTS_OPENING : M0_BTS_FAILED, rc);

	M0_POST(m0_be_tx__invariant(tx));
	M0_RETURN(rc);
}

static void cap_uncap(struct m0_be_tx *tx, const struct m0_be_reg *reg,
		      void (*op)(struct m0_be_reg_area *ra,
				 const struct m0_be_reg_d *rd))
{
	M0_PRE(m0_be_tx__invariant(tx));
	M0_PRE(m0_be_tx__state(tx) == M0_BTS_ACTIVE);
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
	M0_PRE(m0_be_tx__state(tx) == M0_BTS_ACTIVE);
	M0_PRE(be_tx_is_locked(tx));

	be_tx_state_move(tx, M0_BTS_CLOSED, 0);

	M0_LEAVE();
}

M0_INTERNAL int
m0_be_tx_timedwait(struct m0_be_tx *tx, int state, m0_time_t timeout)
{
	M0_ENTRY();
	M0_PRE(be_tx_is_locked(tx));

	m0_sm_timedwait(&tx->t_sm, state, timeout);
	M0_RETURN(tx->t_sm.sm_rc);
}

#if 0
M0_INTERNAL void m0_be_tx_stable(struct m0_be_tx *tx)
{
	enum m0_be_tx_state state = m0_be_tx__state(tx);

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

M0_INTERNAL enum m0_be_tx_state m0_be_tx__state(const struct m0_be_tx *tx)
{
	return tx->t_sm.sm_state;
}

M0_INTERNAL const char *m0_be_tx_state_name(const struct m0_be_tx *tx,
					    enum m0_be_tx_state state)
{
	return m0_sm_state_name(&tx->t_sm, state);
}

static void be_tx_state_move(struct m0_be_tx *tx,
			     enum m0_be_tx_state state,
			     int rc)
{
	M0_ENTRY("%d --> %d", m0_be_tx__state(tx), state);
	M0_PRE(m0_be_tx__invariant(tx));
	M0_PRE(be_tx_is_locked(tx));

	if (rc != 0)
		M0_LOG(M0_ERROR, "transaction failure: err=%d", rc);

	m0_sm_move(&tx->t_sm, state, rc);
	m0_be_engine__tx_state_set(tx->t_engine, tx, state);

	M0_POST(m0_be_tx__invariant(tx));
	M0_LEAVE();
}

static void be_tx_ast_cb(struct m0_sm_group *sm_group, struct m0_sm_ast *ast)
{
	struct m0_be_tx	   *tx;
	enum m0_be_tx_state state = (enum m0_be_tx_state) ast->sa_datum;

	switch (state) {
	case M0_BTS_ACTIVE:
		tx = container_of(ast, struct m0_be_tx, t_ast_active);
		break;
	case M0_BTS_GROUPED:
		tx = container_of(ast, struct m0_be_tx, t_ast_grouped);
		break;
	case M0_BTS_PLACED:
		tx = container_of(ast, struct m0_be_tx, t_ast_placed);
		break;
	default:
		M0_IMPOSSIBLE("be/tx: invalid state in ast callback");
	}
	be_tx_state_move(tx, state, 0);
}

M0_INTERNAL void m0_be_tx__state_post(struct m0_be_tx *tx,
				      enum m0_be_tx_state state)
{
	struct m0_sm_ast *ast;
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
	M0_PRE(M0_IN(state, (M0_BTS_ACTIVE, M0_BTS_GROUPED, M0_BTS_PLACED)));

	switch (state) {
	case M0_BTS_ACTIVE:
		ast = &tx->t_ast_active;
		break;
	case M0_BTS_GROUPED:
		ast = &tx->t_ast_active;
		break;
	case M0_BTS_PLACED:
		ast = &tx->t_ast_active;
		break;
	default:
		M0_IMPOSSIBLE("be/tx: can't post ast for state");
	}
	ast->sa_cb = be_tx_ast_cb;
	ast->sa_datum = (void *) state;
	m0_sm_ast_post(tx->t_sm.sm_grp, ast);
}

M0_INTERNAL bool m0_be_tx__invariant(const struct m0_be_tx *tx)
{
#if 0
	const enum m0_be_tx_state state = m0_be_tx__state(tx);
#endif

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

static struct m0_be_tx *sm_to_tx(struct m0_sm *mach)
{
	return container_of(mach, struct m0_be_tx, t_sm); /* XXX bob_of() */
}

#if 0
static int placed_st_in(struct m0_sm *mach)
{
	M0_ENTRY("t_glob_stable=%d", !!sm_to_tx(mach)->t_glob_stable);
	M0_LEAVE();
	/* XXX FIXME: Don't intermix "external" and "chained" styles of state
	 * transitions. This makes code hard to read. (Reported by Nikita.) */
	return sm_to_tx(mach)->t_glob_stable ? M0_BTS_DONE : -1;
}
#endif

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
