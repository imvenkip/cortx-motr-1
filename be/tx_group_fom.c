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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>,
 *                  Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>,
 *                  Maxim Medved <maxim_medved@xyratex.com>
 * Original creation date: 17-Jun-2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_BE
#include "lib/trace.h"

#include "be/tx_group_fom.h"

#include "lib/misc.h"       /* M0_BITS */
#include "lib/memory.h"     /* m0_free */
#include "lib/errno.h"      /* ENOMEM */
#include "reqh/reqh.h"      /* m0_reqh_state_get */

#include "be/tx_group.h"
#include "be/tx_service.h"
#include "be/log.h"

/**
 * @addtogroup be
 * @{
 */

static struct m0_be_tx_group_fom *fom2tx_group_fom(const struct m0_fom *fom);
static void tx_group_fom_fini(struct m0_fom *fom);
static void be_op_reset(struct m0_be_op *op);

/* ------------------------------------------------------------------
 * State definitions
 * ------------------------------------------------------------------ */

/**
 * Phases of tx_group fom.
 *
 * @verbatim
 *  ,-------- INIT
 *  |   [0]    |              [0] Initialisation failed: at this point only
 *  |          |                  m0_be_group_ondisk_init() can fail.
 *  |          v [1]          [1] Engine adds tx group to list of opened
 *  | ,------ OPEN <------.       groups.
 *  | |        |          |
 *  | |        v [2]      |   [2] Gets awoken by tx_group_close() or tx_group
 *  | |      LOGGING      |       timeout is reached.
 *  | |        | [3]      |   [3] Initiate log IO
 *  | |        v [4]      |   [4] Log IO completes.
 *  | |      PLACING      |
 *  | |        | [5]      |   [5] Initiate in-place IO.
 *  | |        v [6]      |   [6] In-place IO completes.
 *  | |      PLACED       |
 *  | |        | [7]      |   [7] Removes all tx from the tx_group.
 *  | |        v          |
 *  | |    STABILIZING    |
 *  | |        | [8]      |   [8] Gets awoken by m0_be_tx_group_stable().
 *  | |        v          |
 *  | |      STABLE ------'
 *  | |
 *  | | [9]                   [9] tgf_stopping flag is set by
 *  | `----> STOPPING ----.       m0_be_tx_group_stop().
 *  |                     |
 *  |                     v
 *  `----> FAILED ----> FINISH
 *
 * [0], [3], [5], [7] -- internal actions;
 * [1], [2], [8], [9] -- external engine events;
 * [4], [6]           -- external IO events.
 * @endverbatim
 */
enum tx_group_fom_state {
	TGS_INIT   = M0_FOM_PHASE_INIT,
	TGS_FINISH = M0_FOM_PHASE_FINISH,
	/**
	 * tx_group gets populated with transactions, that are added with
	 * tx_group_add().
	 */
	TGS_OPEN   = M0_FOM_PHASE_NR,
	/** Log stobio is in progress. */
	TGS_LOGGING,
	/** In-place (segment) stobio is in progress. */
	TGS_PLACING,
	TGS_PLACED,
	/** Waiting for transactions to stabilize. */
	TGS_STABILIZING,
	/**
	 * m0_be_tx_stable() has been called for all transactions of the
	 * tx_group.
	 */
	TGS_STABLE,
	TGS_STOPPING,
	TGS_FAILED,
	TGS_NR
};

static struct m0_sm_state_descr tx_group_fom_states[TGS_NR] = {
#define _S(name, flags, allowed)  \
	[name] = {                    \
		.sd_flags   = flags,  \
		.sd_name    = #name,  \
		.sd_allowed = allowed \
	}

	_S(TGS_INIT,   M0_SDF_INITIAL, M0_BITS(TGS_OPEN, TGS_FAILED)),
	_S(TGS_FINISH, M0_SDF_TERMINAL, 0),
	_S(TGS_FAILED, M0_SDF_FAILURE, M0_BITS(TGS_FINISH)),
	_S(TGS_STOPPING,    0, M0_BITS(TGS_FINISH)),
	_S(TGS_OPEN,        0, M0_BITS(TGS_LOGGING, TGS_STOPPING)),
	_S(TGS_LOGGING,     0, M0_BITS(TGS_PLACING)),
	_S(TGS_PLACING,     0, M0_BITS(TGS_PLACED)),
	_S(TGS_PLACED,      0, M0_BITS(TGS_STABILIZING)),
	_S(TGS_STABILIZING, 0, M0_BITS(TGS_STABLE)),
	_S(TGS_STABLE,      0, M0_BITS(TGS_OPEN)),
#undef _S
};

static struct m0_sm_conf tx_group_fom_conf = {
	.scf_name      = "phases of m0_be_tx_group_fom",
	.scf_nr_states = ARRAY_SIZE(tx_group_fom_states),
	.scf_state     = tx_group_fom_states,
};

static int tx_group_fom_tick(struct m0_fom *fom)
{
	enum tx_group_fom_state	   phase = m0_fom_phase(fom);
	struct m0_be_tx_group_fom *m	 = fom2tx_group_fom(fom);
	struct m0_be_tx_group     *gr	 = m->tgf_group;
	struct m0_be_op           *op	 = &m->tgf_op;
	int                        rc;

	M0_ENTRY("m0_be_tx_group_fom phase %s", m0_fom_phase_name(fom, phase));

	switch (phase) {
	case TGS_INIT:
		rc = m0_be_tx_group__allocate(gr);
		m0_fom_phase_move(fom, rc, rc == 0 ? TGS_OPEN : TGS_FAILED);
		m0_semaphore_up(&m->tgf_start_sem);
		return M0_FSO_WAIT;
	case TGS_OPEN:
		if (m->tgf_stopping) {
			m0_fom_phase_set(fom, TGS_STOPPING);
			return M0_FSO_AGAIN;
		}
		return M0_FSO_WAIT;
	case TGS_LOGGING:
		be_op_reset(op);
		m0_be_tx_group__log(gr, op);
		return m0_be_op_tick_ret(op, fom, TGS_PLACING);
	case TGS_PLACING:
		m0_be_tx_group__tx_state_post(gr, M0_BTS_LOGGED, false);
		be_op_reset(op);
		m0_be_tx_group__place(gr, op);
		return m0_be_op_tick_ret(op, fom, TGS_PLACED);
	case TGS_PLACED:
		m0_be_tx_group__tx_state_post(gr, M0_BTS_PLACED, true);
		m0_fom_phase_set(fom, TGS_STABILIZING);
		return M0_FSO_AGAIN;
	case TGS_STABILIZING:
		if (m->tgf_stable) {
			m0_fom_phase_set(fom, TGS_STABLE);
			return M0_FSO_AGAIN;
		}
		return M0_FSO_WAIT;
	case TGS_STABLE:
		m0_be_tx_group_discard(gr);
		m0_be_tx_group_reset(gr);
		m0_be_tx_group_open(gr);
		m0_fom_phase_set(fom, TGS_OPEN);
		return M0_FSO_AGAIN;
	case TGS_STOPPING:
		m0_fom_phase_set(fom, TGS_FINISH);
		m0_be_tx_group__deallocate(gr);
		return M0_FSO_WAIT;
	case TGS_FAILED:
		m0_fom_phase_set(fom, TGS_FINISH);
		return M0_FSO_WAIT;
	default:
		M0_IMPOSSIBLE("XXX");
	}

	M0_LEAVE();
	return M0_FSO_WAIT;
}

static void tx_group_fom_fini(struct m0_fom *fom)
{
	struct m0_be_tx_group_fom *m = fom2tx_group_fom(fom);

	m0_fom_fini(fom);
	m0_semaphore_up(&m->tgf_finish_sem);
}

static size_t tx_group_fom_locality(const struct m0_fom *fom)
{
	return 0; /* XXX TODO: reconsider */
}

static void
tx_group_fom_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc M0_UNUSED)
{
	/* XXX */
	fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
}

static const struct m0_fom_ops tx_group_fom_ops = {
	.fo_fini          = tx_group_fom_fini,
	.fo_tick          = tx_group_fom_tick,
	.fo_home_locality = tx_group_fom_locality,
	.fo_addb_init     = tx_group_fom_addb_init
};

static struct m0_fom_type tx_group_fom_type;

static const struct m0_fom_type_ops tx_group_fom_type_ops = {
	.fto_create = NULL
};

static struct m0_be_tx_group_fom *fom2tx_group_fom(const struct m0_fom *fom)
{
	/* XXX TODO bob_of() */
	return container_of(fom, struct m0_be_tx_group_fom, tgf_gen);
}

static void be_tx_group_fom_log(struct m0_be_tx_group_fom *m)
{
	M0_LOG(M0_DEBUG, "m=%p, tx_nr=%zu", m,
	       m0_be_tx_group_size(m->tgf_group));

	m0_fom_phase_set(&m->tgf_gen, TGS_LOGGING);
	m0_fom_wakeup(&m->tgf_gen);
}

static void be_tx_group_fom_handle(struct m0_sm_group *gr,
				   struct m0_sm_ast   *ast)
{
	struct m0_be_tx_group_fom *m =
		container_of(ast, struct m0_be_tx_group_fom, tgf_ast_handle);

	M0_ENTRY("m=%p", m);

	/*
	 * There are 2 possible scenarios when this function is called:
	 *  - tgf_ast_timeout only enqueued
	 *  - tgf_to is armed; therefore, tgf_ast_timeout isn't enqueued
	 */
	m0_fom_timeout_cancel(&m->tgf_to);
	m0_sm_ast_cancel(gr, &m->tgf_ast_timeout);
	be_tx_group_fom_log(m);

	M0_LEAVE();
}

/*
 * Wakes up tx_group_fom iff it is waiting.
 * It is possible that multiple fom wakeup asts are posted through different
 * code paths. Thus we avoid waking up of already running FOM.
 */
static void be_tx_group_fom_iff_waiting_wakeup(struct m0_fom *fom)
{
	if (m0_fom_is_waiting(fom))
		m0_fom_wakeup(fom);
}

static void be_tx_group_fom_stable(struct m0_sm_group *_, struct m0_sm_ast *ast)
{
	struct m0_be_tx_group_fom *m =
		container_of(ast, struct m0_be_tx_group_fom, tgf_ast_stable);

	m->tgf_stable = true;
	be_tx_group_fom_iff_waiting_wakeup(&m->tgf_gen);
}

static void be_tx_group_fom_stop(struct m0_sm_group *gr, struct m0_sm_ast *ast)
{
	struct m0_be_tx_group_fom *m =
		container_of(ast, struct m0_be_tx_group_fom, tgf_ast_stop);

	m->tgf_stopping = true;
	be_tx_group_fom_iff_waiting_wakeup(&m->tgf_gen);
}

static void be_tx_group_fom_timeout_cb(struct m0_fom_callback *cb)
{
	struct m0_be_tx_group_fom *m  =
		container_of(cb->fc_fom, struct m0_be_tx_group_fom, tgf_gen);
	struct m0_be_tx_group     *gr = m->tgf_group;

	M0_ENTRY("m=%p", m);

	m0_be_tx_group_postclose(gr);
	be_tx_group_fom_log(m);

	m0_sm_ast_cancel(&m->tgf_gen.fo_loc->fl_group, &m->tgf_ast_handle);

	M0_LEAVE();
}

static void be_tx_group_fom_handle_delayed(struct m0_sm_group *gr,
					   struct m0_sm_ast   *ast)
{
	int                        rc;
	struct m0_be_tx_group_fom *m =
		container_of(ast, struct m0_be_tx_group_fom, tgf_ast_timeout);

	M0_ENTRY();

	rc = m0_fom_timeout_arm(&m->tgf_to, &m->tgf_gen,
				be_tx_group_fom_timeout_cb,
				m->tgf_close_abs_timeout);
	/*
	 * XXX tx_group_fom shouldn't fail at this point. It would mean that
	 *     all txs are failed too.
	 */
	M0_ASSERT_INFO(rc == 0, "rc = %d", rc);

	M0_LEAVE();
}

M0_INTERNAL void m0_be_tx_group_fom_init(struct m0_be_tx_group_fom *m,
					 struct m0_be_tx_group *gr,
					 struct m0_reqh *reqh)
{
	M0_ENTRY();

	m0_fom_init(&m->tgf_gen, &tx_group_fom_type,
		    &tx_group_fom_ops, NULL, NULL, reqh, &m0_be_txs_stype);

	m->tgf_group	= gr;
	m->tgf_reqh	= reqh;
	m->tgf_stable	= false;
	m->tgf_stopping = false;

	m0_fom_timeout_init(&m->tgf_to);
	m->tgf_close_abs_timeout = M0_TIME_NEVER;

#define _AST(handler) (struct m0_sm_ast){ .sa_cb = (handler) }
	m->tgf_ast_handle  = _AST(be_tx_group_fom_handle);
	m->tgf_ast_stable  = _AST(be_tx_group_fom_stable);
	m->tgf_ast_stop    = _AST(be_tx_group_fom_stop);
	m->tgf_ast_timeout = _AST(be_tx_group_fom_handle_delayed);
#undef _AST

	m0_fom_type_init(&tx_group_fom_type, &tx_group_fom_type_ops,
			 &m0_be_txs_stype, &tx_group_fom_conf);
	m0_semaphore_init(&m->tgf_start_sem, 0);
	m0_semaphore_init(&m->tgf_finish_sem, 0);
	m0_be_op_init(&m->tgf_op);
	/* XXX */
	m0_be_op_state_set(&m->tgf_op, M0_BOS_ACTIVE);
	m0_be_op_state_set(&m->tgf_op, M0_BOS_SUCCESS);

	M0_LEAVE();
}

M0_INTERNAL void m0_be_tx_group_fom_fini(struct m0_be_tx_group_fom *m)
{
	M0_PRE(m0_fom_phase(&m->tgf_gen) == TGS_FINISH);

	m0_be_op_fini(&m->tgf_op);
	m0_semaphore_fini(&m->tgf_start_sem);
	m0_semaphore_fini(&m->tgf_finish_sem);
	m0_fom_timeout_fini(&m->tgf_to);
}

M0_INTERNAL void m0_be_tx_group_fom_reset(struct m0_be_tx_group_fom *m)
{
	m->tgf_stable            = false;
	m->tgf_close_abs_timeout = M0_TIME_NEVER;
	m0_fom_timeout_fini(&m->tgf_to);
	m0_fom_timeout_init(&m->tgf_to);
}

static void be_tx_group_fom_ast_post(struct m0_be_tx_group_fom *gf,
				     struct m0_sm_ast *ast)
{
	m0_sm_ast_post(&gf->tgf_gen.fo_loc->fl_group, ast);
}

M0_INTERNAL int m0_be_tx_group_fom_start(struct m0_be_tx_group_fom *gf)
{
	int            rc;
	struct m0_fom *fom = &gf->tgf_gen;

	m0_fom_queue(fom, gf->tgf_reqh);
	m0_semaphore_down(&gf->tgf_start_sem);
	M0_ASSERT(M0_IN(m0_fom_phase(fom), (TGS_OPEN, TGS_FAILED)));
	rc = m0_fom_rc(fom);
	if (m0_fom_phase(fom) == TGS_FAILED) {
		M0_ASSERT(rc != 0);
		m0_fom_wakeup(fom);
		m0_semaphore_down(&gf->tgf_finish_sem);
	}

	return rc;
}

M0_INTERNAL void m0_be_tx_group_fom_stop(struct m0_be_tx_group_fom *gf)
{
	be_tx_group_fom_ast_post(gf, &gf->tgf_ast_stop);
	m0_semaphore_down(&gf->tgf_finish_sem);
}

M0_INTERNAL void m0_be_tx_group_fom_handle(struct m0_be_tx_group_fom *m,
					   m0_time_t abs_timeout)
{
	M0_PRE(ergo(abs_timeout != M0_TIME_IMMEDIATELY,
		    m->tgf_close_abs_timeout == M0_TIME_NEVER));

	if (abs_timeout == M0_TIME_IMMEDIATELY) {
		be_tx_group_fom_ast_post(m, &m->tgf_ast_handle);
	} else {
		m->tgf_close_abs_timeout = abs_timeout;
		be_tx_group_fom_ast_post(m, &m->tgf_ast_timeout);
	}
}

M0_INTERNAL void m0_be_tx_group_fom_stable(struct m0_be_tx_group_fom *gf)
{
	be_tx_group_fom_ast_post(gf, &gf->tgf_ast_stable);
}

static void be_op_reset(struct m0_be_op *op)
{
	m0_be_op_fini(op);
	m0_be_op_init(op);
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
