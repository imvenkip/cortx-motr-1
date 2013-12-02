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

#include <stddef.h>		/* ptrdiff_t */

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

static bool be_tx_state_invariant(const struct m0_sm *mach)
{
	return m0_be_tx__invariant(
		container_of(mach, const struct m0_be_tx, t_sm));
}

static bool be_tx_is_locked(const struct m0_be_tx *tx);

#define BE_TX_LOCKED_AT_STATE(tx, states)				\
({									\
	const struct m0_be_tx *__tx = (tx);				\
									\
	_0C(be_tx_is_locked(__tx)) && m0_be_tx__invariant(__tx) &&	\
		_0C(M0_IN(m0_be_tx_state(__tx), states));		\
})

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

static void be_tx_state_move(struct m0_be_tx *tx,
			     enum m0_be_tx_state state,
			     int rc);

M0_INTERNAL void m0_be_tx_init(struct m0_be_tx     *tx,
			       uint64_t             tid,
			       struct m0_be_domain *dom,
			       struct m0_sm_group  *sm_group,
			       m0_be_tx_cb_t        persistent,
			       m0_be_tx_cb_t        discarded,
			       void               (*filler)(struct m0_be_tx *tx,
							    void *payload),
			       void                *datum)
{
	enum m0_be_tx_state state;

	*tx = (struct m0_be_tx) {
		.t_id         = tid,
		.t_engine     = m0_be_domain_engine(dom),
		.t_persistent = persistent,
		.t_discarded  = discarded,
		.t_filler     = filler,
		.t_datum      = datum
	};

	m0_sm_init(&tx->t_sm, &be_tx_sm_conf, M0_BTS_PREPARE, sm_group);

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

	M0_POST(BE_TX_LOCKED_AT_STATE(tx, (M0_BTS_PREPARE)));
}

M0_INTERNAL void m0_be_tx_fini(struct m0_be_tx *tx)
{
	enum m0_be_tx_state state;

	M0_PRE(BE_TX_LOCKED_AT_STATE(tx, (M0_BTS_DONE, M0_BTS_FAILED)));
	M0_PRE(tx->t_ref == 0);

	m0_be_engine__tx_fini(tx->t_engine, tx);

	for (state = 0; state < ARRAY_SIZE(be_tx_ast_offset); ++state) {
		if (be_tx_ast_offset[state] != 0)
			m0_sm_ast_cancel(tx->t_sm.sm_grp, be_tx_ast(tx, state));
	}
	/*
	 * Note: m0_sm_fini() will call be_tx_state_invariant(), so
	 * m0_be_tx::t_reg_area should be finalized after m0_be_tx::t_sm.
	 */
	m0_sm_fini(&tx->t_sm);
	m0_be_reg_area_fini(&tx->t_reg_area);
}

M0_INTERNAL void
m0_be_tx_prep(struct m0_be_tx *tx, const struct m0_be_tx_credit *credit)
{
	M0_ENTRY();
	M0_PRE(BE_TX_LOCKED_AT_STATE(tx, (M0_BTS_PREPARE)));

	m0_be_tx_credit_add(&tx->t_prepared, credit);

	M0_POST(BE_TX_LOCKED_AT_STATE(tx, (M0_BTS_PREPARE)));
	M0_LEAVE();
}

M0_INTERNAL void m0_be_tx_open(struct m0_be_tx *tx)
{
	int rc;

	M0_ENTRY();
	M0_PRE(BE_TX_LOCKED_AT_STATE(tx, (M0_BTS_PREPARE)));

	rc = m0_be_tx_credit_eq(&tx->t_prepared,
				&m0_be_tx_credit_invalid) ? -EINVAL : 0;
	rc = rc ?: m0_be_reg_area_init(&tx->t_reg_area, &tx->t_prepared, true);

	if (rc == -EINVAL) {
		M0_LOG(M0_DEBUG, "tx = %p: tx credit is invalid", tx);
	} else if (rc == -ENOMEM) {
		M0_LOG(M0_DEBUG, "tx = %p: there is not enough memory "
		       "to allocate using prepared credit "BETXCR_F,
		       tx, BETXCR_P(&tx->t_prepared));
	}
	be_tx_state_move(tx, rc == 0 ? M0_BTS_OPENING : M0_BTS_FAILED, rc);

	M0_POST(BE_TX_LOCKED_AT_STATE(tx, (M0_BTS_OPENING, M0_BTS_FAILED)));
	M0_LEAVE();
}

M0_INTERNAL void
m0_be_tx_capture(struct m0_be_tx *tx, const struct m0_be_reg *reg)
{
	M0_PRE(BE_TX_LOCKED_AT_STATE(tx, (M0_BTS_ACTIVE)));
	M0_PRE(m0_be_reg__invariant(reg));

	m0_be_reg_area_capture(&tx->t_reg_area, &M0_BE_REG_D(*reg, NULL));
}

M0_INTERNAL void
m0_be_tx_uncapture(struct m0_be_tx *tx, const struct m0_be_reg *reg)
{
	M0_PRE(BE_TX_LOCKED_AT_STATE(tx, (M0_BTS_ACTIVE)));
	M0_PRE(m0_be_reg__invariant(reg));

	m0_be_reg_area_uncapture(&tx->t_reg_area, &M0_BE_REG_D(*reg, NULL));
}

M0_INTERNAL void m0_be_tx_close(struct m0_be_tx *tx)
{
	M0_ENTRY();
	M0_PRE(BE_TX_LOCKED_AT_STATE(tx, (M0_BTS_ACTIVE)));

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
m0_be_tx_timedwait(struct m0_be_tx *tx, uint64_t states, m0_time_t deadline)
{
	M0_ENTRY();
	M0_PRE(be_tx_is_locked(tx));

	m0_sm_timedwait(&tx->t_sm, states, deadline);
	M0_RETURN(tx->t_sm.sm_rc);
}

M0_INTERNAL enum m0_be_tx_state m0_be_tx_state(const struct m0_be_tx *tx)
{
	return tx->t_sm.sm_state;
}

M0_INTERNAL const char *m0_be_tx_state_name(enum m0_be_tx_state state)
{
	return m0_sm_conf_state_name(&be_tx_sm_conf, state);
}

static void be_tx_state_move_ast(struct m0_be_tx *tx, enum m0_be_tx_state state)
{
	enum m0_be_tx_state tx_state = m0_be_tx_state(tx);

	M0_LOG(M0_DEBUG, "ast: tx = %p, %s -> %s",
	       tx, m0_be_tx_state_name(tx_state), m0_be_tx_state_name(state));

	if (tx_state < M0_BTS_CLOSED || state == tx_state + 1) {
		/*
		 * If we have state transition to M0_BTS_FAILED here
		 * then transaction exceeds engine tx size limit.
		 */
		be_tx_state_move(tx, state,
				 state == M0_BTS_FAILED ? -E2BIG : 0);
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
		 m0_be_tx_state_name(m0_be_tx_state(tx)),
		 m0_be_tx_state_name(state), rc);

	M0_PRE(m0_be_tx__invariant(tx));
	M0_PRE(be_tx_is_locked(tx));

	if (rc != 0)
		M0_LOG(M0_ERROR, "%s -> %s: transaction failure: err=%d",
			m0_be_tx_state_name(m0_be_tx_state(tx)),
			m0_be_tx_state_name(state), rc);

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
	       tx, m0_be_tx_state_name(state));

	m0_sm_ast_post(tx->t_sm.sm_grp, be_tx_ast(tx, state));
}

M0_INTERNAL bool m0_be_tx__invariant(const struct m0_be_tx *tx)
{
	return _0C(m0_be_tx_state(tx) < M0_BTS_NR) &&
	       m0_be_reg_area__invariant(&tx->t_reg_area);
}

static bool be_tx_is_locked(const struct m0_be_tx *tx)
{
	return m0_mutex_is_locked(&tx->t_sm.sm_grp->s_lock);
}

M0_INTERNAL struct m0_be_reg_area *m0_be_tx__reg_area(struct m0_be_tx *tx)
{
	return &tx->t_reg_area;
}

M0_INTERNAL int m0_be_tx_open_sync(struct m0_be_tx *tx)
{
	enum m0_be_tx_state state;
	int		    rc;

	m0_be_tx_open(tx);
	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_ACTIVE, M0_BTS_FAILED),
				M0_TIME_NEVER);

	state = m0_be_tx_state(tx);
	M0_ASSERT_INFO(equi(rc == 0, state == M0_BTS_ACTIVE) &&
		       equi(rc != 0, state == M0_BTS_FAILED),
		       "rc = %d, tx = %p, m0_be_tx_state(tx) = %s",
		       rc, tx, m0_be_tx_state_name(state));
	return rc;
}

M0_INTERNAL void m0_be_tx_close_sync(struct m0_be_tx *tx)
{
	int rc;

	m0_be_tx_close(tx);
	rc = m0_be_tx_timedwait(tx, M0_BITS(M0_BTS_DONE), M0_TIME_NEVER);
	M0_ASSERT_INFO(rc == 0, "Transaction can't fail after m0_be_tx_open(): "
		       "rc = %d, tx = %p", rc, tx);
}

#undef BE_TX_LOCKED_AT_STATE

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
