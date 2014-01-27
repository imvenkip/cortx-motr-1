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

#define REQH_EMU 1

#if REQH_EMU
#if 0
m0_fom_phase - ok
m0_fom_phase_set - ok
m0_be_op_tick_ret - problem
tx_group_fom_fini == fo_fini == m0_fom_fini - problem
m0_fom_wakeup - problem
m0_reqh_state_get - problem - solved
m0_fom_init - problem
m0_fom_type_init - ok
m0_fom_queue - problem
#endif
#define m0_reqh_state_get(reqh) M0_REQH_ST_NORMAL
#define m0_fom_init reqh_emu_fom_init
#define m0_fom_fini reqh_emu_fom_fini
#define m0_fom_queue reqh_emu_fom_queue
#define m0_fom_wakeup reqh_emu_fom_wakeup
#define m0_be_op_tick_ret reqh_emu_op_tick_ret
#endif

#if REQH_EMU

struct reqh_emu_fom {
	struct m0_sm_group     *re_grp;
	struct m0_semaphore	re_stop_sem;
	struct m0_semaphore	re_fom_wakeup;
	struct m0_thread	re_thread;
	struct m0_fom	       *re_gen;
	struct m0_fom_locality	re_fom_loc;
	struct m0_clink		re_op_wait_clink;
	struct m0_be_op	       *re_op_current;
};

static struct reqh_emu_fom *fom2reqh_emu_fom(struct m0_fom *m)
{
	return (struct reqh_emu_fom *)m->fo_fop;
}

static void reqh_emu_loc_handler_thread(struct reqh_emu_fom *re)
{
	struct m0_sm_group *grp = re->re_grp;
	struct m0_fom	   *fom = re->re_gen;
	int		    rc;

	while (!m0_semaphore_trydown(&re->re_stop_sem)) {
		m0_chan_wait(&grp->s_clink);
		m0_sm_group_lock(grp);

		M0_ASSERT(m0_fom_invariant(fom));

		/* see fom_exec */
		if (m0_semaphore_trydown(&re->re_fom_wakeup)) {
			do {
				M0_ASSERT(m0_fom_phase(fom)
					  != M0_FOM_PHASE_FINISH);
				rc = fom->fo_ops->fo_tick(fom);
			} while (rc == M0_FSO_AGAIN);
			M0_ASSERT(rc == M0_FSO_WAIT);
			if (m0_fom_phase(fom) == M0_FOM_PHASE_FINISH) {
				m0_sm_group_unlock(grp);
				break;
			}
		}

		m0_sm_group_unlock(grp);
	}
}

static struct m0_sm_conf tx_group_fom_conf;

M0_INTERNAL void reqh_emu_fom_wakeup(struct m0_fom *fom)
{
	struct reqh_emu_fom *re = fom2reqh_emu_fom(fom);

	m0_semaphore_up(&re->re_fom_wakeup);
	m0_clink_signal(&re->re_grp->s_clink);
}

static bool reqh_emu_fom_wakeup_cb(struct m0_clink *clink)
{
	enum m0_be_op_state  state;
	struct reqh_emu_fom *re;

	re = container_of(clink, struct reqh_emu_fom, re_op_wait_clink);
	state = m0_be_op_state(re->re_op_current);
	if (M0_IN(state, (M0_BOS_SUCCESS, M0_BOS_FAILURE))) {
		m0_clink_del(clink);
		reqh_emu_fom_wakeup(re->re_gen);
		re->re_op_current = NULL;
	}
	return true;
}

/* XXX copied from fop/fom.c to pass m0_fom_invariant() */
M0_TL_DESCR_DEFINE(runq_emu, "runq fom", static, struct m0_fom, fo_linkage,
		   fo_magic, M0_FOM_MAGIC, M0_FOM_RUNQ_MAGIC);
M0_TL_DEFINE(runq_emu, static, struct m0_fom);
M0_TL_DESCR_DEFINE(wail_emu, "wail fom", static, struct m0_fom, fo_linkage,
		   fo_magic, M0_FOM_MAGIC, M0_FOM_WAIL_MAGIC);
M0_TL_DEFINE(wail_emu, static, struct m0_fom);

void reqh_emu_fom_init(struct m0_fom *fom, struct m0_fom_type *fom_type,
		 const struct m0_fom_ops *ops, struct m0_fop *fop,
		 struct m0_fop *reply, struct m0_reqh *reqh,
		 const struct m0_reqh_service_type *stype)
{
	struct reqh_emu_fom *re;

	M0_ALLOC_PTR(re);
	M0_ASSERT(re != NULL);

	*fom = (struct m0_fom){
		.fo_fop  = (void *)re,
		.fo_ops  = ops,
		.fo_loc  = &re->re_fom_loc,
		.fo_type = fom_type,
	};

	re->re_gen = fom;
	re->re_grp = &re->re_fom_loc.fl_group;
	m0_clink_init(&re->re_op_wait_clink, &reqh_emu_fom_wakeup_cb);
	m0_sm_group_init(re->re_grp);
	m0_semaphore_init(&re->re_stop_sem, 0);
	m0_semaphore_init(&re->re_fom_wakeup, 0);
	m0_sm_init(&fom->fo_sm_phase, &tx_group_fom_conf,
		   M0_FOM_PHASE_INIT, re->re_grp);
	fom->fo_sm_state.sm_grp = re->re_grp;
	fom->fo_sm_state.sm_state = M0_FOS_WAITING;
	fom->fo_cb.fc_state = M0_FCS_DONE;
	runq_emu_tlink_init(fom);
	runq_emu_tlist_init(&fom->fo_loc->fl_runq);
	wail_emu_tlist_init(&fom->fo_loc->fl_wail);
	wail_emu_tlist_add_tail(&fom->fo_loc->fl_wail, fom);
}

void reqh_emu_fom_fini(struct m0_fom *fom)
{
	struct reqh_emu_fom *re = fom2reqh_emu_fom(fom);
	int		     rc;

	m0_semaphore_up(&re->re_stop_sem);
	m0_clink_signal(&re->re_grp->s_clink);

	rc = m0_thread_join(&re->re_thread);
	M0_ASSERT(rc == 0);
	m0_thread_fini(&re->re_thread);

	wail_emu_tlist_del(fom);
	wail_emu_tlist_fini(&fom->fo_loc->fl_wail);
	runq_emu_tlist_fini(&fom->fo_loc->fl_runq);
	runq_emu_tlink_fini(fom);

	m0_sm_group_lock(re->re_grp);
	m0_sm_fini(&fom->fo_sm_phase);
	m0_sm_group_unlock(re->re_grp);

	m0_semaphore_fini(&re->re_stop_sem);
	m0_semaphore_fini(&re->re_fom_wakeup);
	m0_sm_group_fini(re->re_grp);
	m0_clink_fini(&re->re_op_wait_clink);

	m0_free(re);
}

M0_INTERNAL void reqh_emu_fom_queue(struct m0_fom *fom, struct m0_reqh *reqh)
{
	struct reqh_emu_fom *re = fom2reqh_emu_fom(fom);
	int		     rc;

	rc = M0_THREAD_INIT(&re->re_thread, struct reqh_emu_fom *, NULL,
			    &reqh_emu_loc_handler_thread, re,
			    "%preqh_emu_fom", re);
	M0_ASSERT(rc == 0);
	reqh_emu_fom_wakeup(fom);
}

M0_INTERNAL int
reqh_emu_op_tick_ret(struct m0_be_op *op, struct m0_fom *fom, int next_state)
{
	enum m0_fom_phase_outcome  ret = M0_FSO_AGAIN;
	struct reqh_emu_fom	  *re = fom2reqh_emu_fom(fom);

	m0_sm_group_lock(op->bo_sm.sm_grp);
	M0_PRE(M0_IN(op->bo_sm.sm_state,
		     (M0_BOS_ACTIVE, M0_BOS_SUCCESS, M0_BOS_FAILURE)));

	if (M0_IN(op->bo_sm.sm_state, (M0_BOS_INIT, M0_BOS_ACTIVE))) {
		ret = M0_FSO_WAIT;
		M0_ASSERT(re->re_op_current == NULL);
		re->re_op_current = op;
		m0_mb();
		m0_clink_add(&op->bo_sm.sm_chan, &re->re_op_wait_clink);
	}
	m0_sm_group_unlock(op->bo_sm.sm_grp);

	m0_fom_phase_set(fom, next_state);
	return ret;
}
#endif

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
 *             INIT
 *              |
 *              v [0]          [0] Engine adds tx group to list of opened
 *   ,-------- OPEN <------.       groups.
 *   |          |          |
 *   |          v [1]      |   [1] Gets awoken by tx_group_close() or tx_group
 *   |        LOGGING      |       timeout is reached.
 *   |          | [2]      |   [2] Initiate log IO
 *   |          v [3]      |   [3] Log IO completes.
 *   |        PLACING      |
 *   |          | [4]      |   [4] Initiate in-place IO.
 *   |          v [5]      |   [5] In-place IO completes.
 *   |        PLACED       |
 *   |          | [6]      |   [6] Removes all tx from the tx_group.
 *   |          v          |
 *   |      STABILIZING    |
 *   |          | [7]      |   [7] Gets awoken by m0_be_tx_group_stable().
 *   |          v          |
 *   |        STABLE ------'
 *   | [8]                     [8] tgf_stopping flag is set by
 *   |                             m0_be_tx_group_stop().
 *   `-----> STOPPING
 *              |
 *              v
 *            FINISH
 *
 * [2], [4], [6]      -- internal actions;
 * [0], [1], [7], [8] -- external engine events;
 * [3], [5]           -- external IO events.
 * @endverbatim
 *
 * Note the absence of "FAILED" state -- a m0_be_tx_group_fom is not allowed to
 * fail.
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
	TGS_NR
};

static struct m0_sm_state_descr tx_group_fom_states[TGS_NR] = {
#define _S(name, flags, allowed)  \
	[name] = {                    \
		.sd_flags   = flags,  \
		.sd_name    = #name,  \
		.sd_allowed = allowed \
	}

	_S(TGS_INIT,   M0_SDF_INITIAL, M0_BITS(TGS_OPEN)),
	_S(TGS_FINISH, M0_SDF_TERMINAL, 0),
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

	M0_ENTRY("m0_be_tx_group_fom phase %s", m0_fom_phase_name(fom, phase));

	switch (phase) {
	case TGS_INIT:
		m0_fom_phase_set(fom, TGS_OPEN);
		m0_semaphore_up(&m->tgf_started);
		return M0_FSO_AGAIN;
	case TGS_OPEN:
		if (m->tgf_stopping) {
			m0_fom_phase_set(fom, TGS_STOPPING);
			return M0_FSO_AGAIN;
		}
		break;
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
		m0_semaphore_up(&m->tgf_stopped);
		return M0_FSO_WAIT;
	default:
		M0_IMPOSSIBLE("XXX");
	}

	M0_LEAVE();
	return M0_FSO_WAIT;
}

static void tx_group_fom_fini(struct m0_fom *fom)
{
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

	m->tgf_group = (struct m0_be_tx_group *)ast->sa_datum;
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

static void be_tx_group_fom_stable(struct m0_sm_group *_, struct m0_sm_ast *ast)
{
	struct m0_be_tx_group_fom *m =
		container_of(ast, struct m0_be_tx_group_fom, tgf_ast_stable);

	m->tgf_stable = true;
	m0_fom_wakeup(&m->tgf_gen);
}

static void be_tx_group_fom_stop(struct m0_sm_group *gr, struct m0_sm_ast *ast)
{
	struct m0_be_tx_group_fom *m =
		container_of(ast, struct m0_be_tx_group_fom, tgf_ast_stop);

	m->tgf_stopping = true;
	m0_fom_wakeup(&m->tgf_gen);
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

	m->tgf_group = (struct m0_be_tx_group *)ast->sa_datum;
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

M0_INTERNAL void
m0_be_tx_group_fom_init(struct m0_be_tx_group_fom *m, struct m0_reqh *reqh)
{
	M0_ENTRY();

	m0_fom_init(&m->tgf_gen, &tx_group_fom_type,
		    &tx_group_fom_ops, NULL, NULL, reqh, &m0_be_txs_stype);

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
	m0_semaphore_init(&m->tgf_started, 0);
	m0_semaphore_init(&m->tgf_stopped, 0);
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
	m0_semaphore_fini(&m->tgf_started);
	m0_semaphore_fini(&m->tgf_stopped);
	m0_fom_timeout_fini(&m->tgf_to);
	m0_fom_fini(&m->tgf_gen);
}

M0_INTERNAL void m0_be_tx_group_fom_reset(struct m0_be_tx_group_fom *m)
{
	m->tgf_stable            = false;
	m->tgf_group             = NULL;
	m->tgf_close_abs_timeout = M0_TIME_NEVER;
	m0_fom_timeout_fini(&m->tgf_to);
	m0_fom_timeout_init(&m->tgf_to);
}

static void be_tx_group_fom_ast_post(struct m0_be_tx_group_fom *gf,
				     struct m0_sm_ast *ast)
{
	m0_sm_ast_post(&gf->tgf_gen.fo_loc->fl_group, ast);
}

M0_INTERNAL void m0_be_tx_group_fom_start(struct m0_be_tx_group_fom *gf)
{
	m0_fom_queue(&gf->tgf_gen, gf->tgf_reqh);
	m0_semaphore_down(&gf->tgf_started);
}

M0_INTERNAL void m0_be_tx_group_fom_stop(struct m0_be_tx_group_fom *gf)
{
	be_tx_group_fom_ast_post(gf, &gf->tgf_ast_stop);
	m0_semaphore_down(&gf->tgf_stopped);
}

M0_INTERNAL void m0_be_tx_group_fom_handle(struct m0_be_tx_group_fom *m,
					   struct m0_be_tx_group *gr,
					   m0_time_t abs_timeout)
{
	if (abs_timeout == M0_TIME_IMMEDIATELY) {
		m->tgf_ast_handle.sa_datum = (void *)gr;
		be_tx_group_fom_ast_post(m, &m->tgf_ast_handle);
	} else {
		m->tgf_ast_timeout.sa_datum = (void *)gr;
		m->tgf_close_abs_timeout    = abs_timeout;
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

#if REQH_EMU
#undef m0_reqh_state_get
#undef m0_fom_init
#undef m0_fom_fini
#undef m0_fom_queue
#undef m0_fom_wakeup
#undef m0_be_op_tick_ret
#undef REQH_EMU
#endif

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
