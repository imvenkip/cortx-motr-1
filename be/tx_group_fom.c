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
 * XXX UPDATEME
 *
 * @verbatim
 *                 INIT
 *                  |
 *                  v [0]          [0] Send "Add itself to a tx_group" AST
 *   ,------------ OPEN <------.       to all M0_BTS_CLOSED transactions
 *   |              |          |       of the tx_engine.
 *   |              | [1]      |   [1] Gets awoken by tx_group_close().
 *   |              |          |
 *   |              v [2]      |   [2] Initiate 1st log IO (tx_group and,
 *   |            LOGGING      |       when the log is wrapped, log header).
 *   |              | [3]      |   [3] 1st log IO completes.
 *   |              |          |
 *   |              v [4]      |   [4] Initiate 2nd log IO (commit block).
 *   |           COMMITTING    |
 *   |              | [5]      |   [5] 2nd log IO completes. Invoke
 *   |              |          |       m0_be_tx::t_persistent() for each
 *   |              |          |       transaction of the tx_group.
 *   |              v [6]      |   [6] Initiate in-place IO.
 *   |            PLACING      |
 *   |              | [7]      |   [7] In-place IO completes.
 *   |              |          |
 *   |              v    [8]   |   [8] Gets awoken by m0_be_tx_stable().
 *   |      ,---- PLACED ---.  |       The tx_group contains transactions
 *   |      |       | ^     |  |       that are not M0_BTS_DONE.
 *   |      |   [9] | |     |  |   [9] Gets awoken by m0_be_tx_stable().
 *   |      |       | `-----'  |       All transactions of the tx_group
 *   |      |       v          |       are M0_BTS_DONE.
 *   |      |     STABLE ------'
 *   |      |
 *   | [10] |                     [10] XXX TODO: m0_backend_fini()?
 *   |      |
 *   `------`---> FINISH
 *
 * [0], [2], [4] and [6]   -- m0_sm_state_descr::sd_in();
 * [1], [3], [5], [7]-[10] -- external events.
 * XXX s/PLACED/STABILIZING/
 * @endverbatim
 *
 * Note the absence of "FAILED" state --- a tx_group is not allowed to fail.
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

static void be_tx_group_stable(struct m0_sm_group *_, struct m0_sm_ast *ast)
{
	struct m0_be_tx_group_fom *m =
		container_of(ast, struct m0_be_tx_group_fom, tgf_ast_stable);

	m->tgf_stable = true;
	m0_fom_wakeup(&m->tgf_gen);
}

static void be_tx_group_move(struct m0_sm_group *_, struct m0_sm_ast *ast)
{
	struct m0_be_tx_group_fom *m =
		container_of(ast, struct m0_be_tx_group_fom, tgf_ast_move);
	enum tx_group_fom_state state = (enum tx_group_fom_state)ast->sa_datum;

	M0_PRE(M0_IN(state, (TGS_LOGGING, TGS_STOPPING)));

	if (state == TGS_STOPPING) {
		m->tgf_stopping = true;
	} else {
		m0_fom_phase_set(&m->tgf_gen, state);
	}
	m0_fom_wakeup(&m->tgf_gen);
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

	m->tgf_ast_stable = (struct m0_sm_ast){ .sa_cb = be_tx_group_stable };
	m->tgf_ast_move	  = (struct m0_sm_ast){ .sa_cb = be_tx_group_move   };

	m0_fom_type_init(&tx_group_fom_type, &tx_group_fom_type_ops,
			 &m0_be_txs_stype, &tx_group_fom_conf);
	m0_semaphore_init(&m->tgf_started, 0);
	m0_semaphore_init(&m->tgf_stopped, 0);
	m0_be_op_init(&m->tgf_op);
	/* XXX */
	m0_be_op_state_set(&m->tgf_op, M0_BOS_ACTIVE);
	m0_be_op_state_set(&m->tgf_op, M0_BOS_SUCCESS);
}

M0_INTERNAL void m0_be_tx_group_fom_fini(struct m0_be_tx_group_fom *m)
{
	m0_be_op_fini(&m->tgf_op);
	m0_semaphore_fini(&m->tgf_started);
	m0_semaphore_fini(&m->tgf_stopped);
	m0_fom_fini(&m->tgf_gen);
}

M0_INTERNAL void m0_be_tx_group_fom_reset(struct m0_be_tx_group_fom *m)
{
	m->tgf_stable = false;
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
	gf->tgf_ast_move.sa_datum = (void *) TGS_STOPPING;
	be_tx_group_fom_ast_post(gf, &gf->tgf_ast_move);
	m0_semaphore_down(&gf->tgf_stopped);
}

M0_INTERNAL void m0_be_tx_group_fom_handle(struct m0_be_tx_group_fom *gf,
					   struct m0_be_tx_group *gr)
{
	gf->tgf_group = gr;
	gf->tgf_ast_move.sa_datum = (void *) TGS_LOGGING;
	be_tx_group_fom_ast_post(gf, &gf->tgf_ast_move);
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
