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
 *                  Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
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

/**
 * @addtogroup be
 * @{
 */

static struct m0_be_tx_group_fom *tx_group_fom(const struct m0_fom *fom);
static void tx_group_fom_fini(struct m0_fom *fom);

/* ------------------------------------------------------------------
 * State definitions
 * ------------------------------------------------------------------ */

/**
 * Phases of tx_group fom.
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
enum tx_group_state {
	TGS_INIT   = M0_FOM_PHASE_INIT,
	TGS_FINISH = M0_FOM_PHASE_FINISH,
	/**
	 * tx_group gets populated with transactions, that are added with
	 * tx_group_add().
	 */
	TGS_OPEN   = M0_FOM_PHASE_NR,
	/**
	 * The first log stobio is in progress.
	 *
	 * Log representation of the tx_group is being written to the log.
	 * If the log is wrapping around (i.e., when a record is known to
	 * reach the end of the log), then the stobio will also include
	 * log header.
	 */
	TGS_LOGGING,
	/**
	 * The second log stobio is in progress.
	 *
	 * Commit block is being written to the log.
	 */
	TGS_COMMITTING,
	/** In-place (segment) stobio is in progress. */
	TGS_PLACING,
	TGS_STABILIZING,
	/**
	 * m0_be_tx_stable() has been called for all transactions of the
	 * tx_group.
	 */
	TGS_STABLE,
	TGS_NR
};

static int       open_tick(struct m0_fom *fom);
static int    logging_tick(struct m0_fom *fom);
static int committing_tick(struct m0_fom *fom);
static int    placing_tick(struct m0_fom *fom);
static int    placed_tick(struct m0_fom *fom);
static int    stable_tick(struct m0_fom *fom);

static struct m0_sm_state_descr _tx_group_states[TGS_NR] = {
#define _S(name, flags, allowed)  \
	[name] = {                    \
		.sd_flags   = flags,  \
		.sd_name    = #name,  \
		.sd_allowed = allowed \
	}

	_S(TGS_INIT,   M0_SDF_INITIAL, M0_BITS(TGS_OPEN)),
	_S(TGS_FINISH, M0_SDF_TERMINAL, 0),
	_S(TGS_OPEN,       0, M0_BITS(TGS_OPEN, TGS_LOGGING, TGS_FINISH)),
	_S(TGS_LOGGING,    0, M0_BITS(TGS_COMMITTING)),
	_S(TGS_COMMITTING, 0, M0_BITS(TGS_PLACING)),
	_S(TGS_PLACING,    0, M0_BITS(TGS_STABILIZING)),
	_S(TGS_STABILIZING,     0, M0_BITS(TGS_STABILIZING, TGS_STABLE, TGS_FINISH)),
	_S(TGS_STABLE,     0, M0_BITS(TGS_OPEN))
#undef _S
};

static struct m0_sm_conf _tx_group_conf = {
	.scf_name      = "phases of tx_group",
	.scf_nr_states = ARRAY_SIZE(_tx_group_states),
	.scf_state     = _tx_group_states
};

/* ------------------------------------------------------------------
 * FOM operations
 * ------------------------------------------------------------------ */

static int tx_group_tick(struct m0_fom *fom)
{
	M0_ENTRY("tx_group state=%u", m0_fom_phase(fom));

	switch (m0_fom_phase(fom)) {
	case TGS_INIT:
		m0_fom_phase_set(fom, TGS_OPEN);
		m0_semaphore_up(&tx_group_fom(fom)->tgf_started);
		return M0_FSO_AGAIN;
	case TGS_OPEN:
		return open_tick(fom);
	case TGS_LOGGING:
		return logging_tick(fom);
	case TGS_COMMITTING:
		return committing_tick(fom);
	case TGS_PLACING:
		return placing_tick(fom);
	case TGS_STABILIZING:
		return placed_tick(fom);
	case TGS_STABLE:
		return stable_tick(fom);
	case TGS_FINISH:
	default:
		M0_IMPOSSIBLE("XXX");
	}

	M0_LEAVE();
	return M0_FSO_WAIT;
}

static void tx_group_fom_fini(struct m0_fom *fom)
{
	M0_ENTRY();

	m0_fom_fini(fom);
	m0_semaphore_fini(&tx_group_fom(fom)->tgf_started);

	M0_LEAVE();
}

static size_t tx_group_locality(const struct m0_fom *fom)
{
	return 0; /* XXX TODO: reconsider */
}

static void
tx_group_addb_init(struct m0_fom *fom, struct m0_addb_mc *mc M0_UNUSED)
{
	fom->fo_addb_ctx.ac_magic = M0_ADDB_CTX_MAGIC;
}

static const struct m0_fom_ops tx_group_fom_ops = {
	.fo_fini          = tx_group_fom_fini,
	.fo_tick          = tx_group_tick,
	.fo_home_locality = tx_group_locality,
	.fo_addb_init     = tx_group_addb_init
};

static struct m0_fom_type tx_group_fom_type;

static const struct m0_fom_type_ops tx_group_fom_type_ops = {
	.fto_create = NULL
};

#if 0
static struct m0_fom *
tx_group_fom_create(struct m0_reqh *reqh, struct m0_be_tx_engine *engine)
{
	struct m0_be_tx_group_fom *m;
	struct m0_fom *fom;

	M0_ALLOC_PTR(m);
	if (m == NULL)
		return NULL;

	fom = &m->tgf_gen;
	m0_fom_init(fom, &tx_group_fom_type, &tx_group_fom_ops, NULL, NULL,
		    reqh, &m0_be_txs_stype);

	engine->te_fom = fom;
	m->tgf_engine = engine;
	m->tgf_stopping = false;
	m0_fom_timeout_init(&m->tgf_to);
	m0_semaphore_init(&m->tgf_started, 0);

	return fom;
}
#endif

#if 0
M0_INTERNAL int
m0_be_tx_engine_start(struct m0_be_tx_engine *engine, struct m0_reqh *reqh)
{
	struct m0_fom *fom;

	M0_ENTRY();
	M0_PRE(m0_reqh_state_get(reqh) == M0_REQH_ST_NORMAL);

	m0_fom_type_init(&tx_group_fom_type, &tx_group_fom_type_ops,
			 &m0_be_txs_stype, &_tx_group_conf);

	fom = tx_group_fom_create(reqh, engine);
	if (fom == NULL)
		M0_RETURN(-ENOMEM);

	m0_fom_queue(fom, reqh);
	m0_semaphore_down(&tx_group_fom(fom)->tgf_started);

	M0_RETURN(0);
}

M0_INTERNAL void m0_be_tx_engine_stop(struct m0_be_tx_engine *engine)
{
	struct m0_be_tx *tx;
	M0_ENTRY();

	tx_group_fom(engine->te_fom)->tgf_stopping = true;
	m0_tl_for(eng, &engine->te_txs[M0_BTS_PLACED], tx) {
		/* XXX FIXME: Transactions should leave tx_engine's lists
		 * by some other means (m0_be_tx::t_discarded() call?). */
		eng_tlist_del(tx);
	} m0_tl_endfor;

	M0_LEAVE();
}
#endif

/* ------------------------------------------------------------------
 * State transitions
 * ------------------------------------------------------------------ */

static struct m0_be_tx_group *tx_group(const struct m0_be_tx_group_fom *m);
#if 0
static void tx_group_populate(struct m0_be_tx_group *group,
			      struct m0_be_tx_engine *engine, bool *full);
#endif
static void fom_wake(struct m0_ref *ref);

static int open_tick(struct m0_fom *fom)
{
	struct m0_be_tx_group_fom    *m   = tx_group_fom(fom);
	/* struct m0_be_engine	*eng = m->tgf_engine; */
	struct m0_be_tx_group  *gr  = tx_group(m);
	/* struct m0_be_tx        *tx; */
	int                     rc;
	size_t			group_size;

	M0_ENTRY();
	M0_PRE(!m->tgf_full);

	/* tx_group_populate(gr, eng, &m->tgf_full); */

	group_size = m0_be_tx_group_size(gr);
	if (m->tgf_expired && group_size > 0) {

		/* tx_group_close(eng, gr); */
		m->tgf_expired = false;

		m0_ref_init(&m->tgf_nr_ungrouped, group_size,
			    fom_wake);
#if 0
		M0_LOG(M0_DEBUG, "Posting \"Get grouped!\" AST(s)");
		m0_be_tx_group__tx_state_post(gr, M0_BTS_GROUPED);
		m0_tl_for(grp, &gr->tg_txs, tx) {
			m0_be_tx__state_post(tx, M0_BTS_GROUPED,
					     &m->tgf_nr_ungrouped);
		} m0_tl_endfor;
#endif

		m0_fom_phase_set(fom, TGS_LOGGING);
	} else if (m->tgf_stopping && group_size == 0) {
		m0_fom_phase_set(fom, TGS_FINISH);
	} else {
		M0_LOG(M0_DEBUG, "Start timer");
		m->tgf_expired = true;
		m0_fom_timeout_init(&m->tgf_to);
		rc = m0_fom_timeout_wait_on(&m->tgf_to, fom,
					    m0_time_from_now(2, 0));
		M0_ASSERT(rc == 0);
	}
	M0_LEAVE();
	return M0_FSO_WAIT;
}

static int logging_tick(struct m0_fom *fom)
{
	struct m0_be_tx_group_fom   *m  = tx_group_fom(fom);
	/* struct m0_be_tx_group *gr = tx_group(m); */
	struct m0_be_op       *op = &m->tgf_op;

	M0_ENTRY();
	M0_PRE(m0_ref_read(&m->tgf_nr_ungrouped) == 0);

	m0_be_op_init(op);
	/*
	 * Launch the 1st log IO: tx_group and, when the log is wrapped,
	 * log header.
	 */
	/* m0_be_log_submit(&m->tgf_engine->te_log, op, gr); */ /* XXX */

	M0_LEAVE();
	return m0_be_op_tick_ret(op, fom, TGS_COMMITTING);
}

static int committing_tick(struct m0_fom *fom)
{
	struct m0_be_tx_group_fom   *m  = tx_group_fom(fom);
	/* struct m0_be_tx_group *gr = tx_group(m); */
	struct m0_be_op       *op = &m->tgf_op;

	/* XXX: on error transit to failure state, and finish system */
	M0_PRE(m0_be_op_state(op) == M0_BOS_SUCCESS);

	M0_ENTRY();

	m0_be_op_init(op);
	/*
	 * Launch the 2nd log IO: commit block.
	 */
	/* m0_be_log_commit(&m->tgf_engine->te_log, op, gr); */ /* XXX */

	M0_LEAVE();
	return m0_be_op_tick_ret(op, fom, TGS_PLACING);
}

static int placing_tick(struct m0_fom *fom)
{
	struct m0_be_tx_group_fom   *m  = tx_group_fom(fom);
	struct m0_be_tx_group *gr = tx_group(m);
	struct m0_be_op       *op = &m->tgf_op;

	M0_ENTRY();
	M0_PRE(!grp_tlist_is_empty(&gr->tg_txs));
	/* XXX TODO: M0_PRE() that tx is working with the same segment. */

	/* perform IO */
	m0_be_op_init(op);
	m0_be_io_launch(&gr->tg_od.go_io_seg, op);

	M0_LEAVE();
	return m0_be_op_tick_ret(op, fom, TGS_STABILIZING);
}

static int placed_tick(struct m0_fom *fom)
{
	/*
	struct m0_be_tx             *tx;
	const struct m0_be_tx_group *gr = tx_group(tx_group_fom(fom));
	*/

	M0_ENTRY();

	/*
	M0_LOG(M0_DEBUG, "Posting \"Get placed!\" AST(s)");
	m0_tl_for(grp, &gr->tg_txs, tx) {
		m0_be_tx__state_post(tx, M0_BTS_PLACED, NULL);
		if (tx->t_persistent != NULL)
			tx->t_persistent(tx);
		grp_tlist_del(tx);
	} m0_tl_endfor;
	*/

	m0_fom_phase_set(fom, TGS_STABLE);

	M0_LEAVE();
	return M0_FSO_AGAIN;
}

static int stable_tick(struct m0_fom *fom)
{
	const struct m0_be_tx_group *gr = tx_group(tx_group_fom(fom));

	M0_ENTRY();
	M0_PRE(grp_tlist_is_empty(&gr->tg_txs));

	m0_fom_phase_set(fom, TGS_OPEN);

	M0_LEAVE();
	return M0_FSO_AGAIN;
}

/* ------------------------------------------------------------------
 * Auxiliary functions
 * ------------------------------------------------------------------ */

static struct m0_be_tx_group_fom *tx_group_fom(const struct m0_fom *fom)
{
	/* XXX TODO bob_of() */
	return container_of(fom, struct m0_be_tx_group_fom, tgf_gen);
}

static void fom_wake(struct m0_ref *ref)
{
	struct m0_be_tx_group_fom *m = container_of(ref, struct m0_be_tx_group_fom,
					      tgf_nr_ungrouped);
	m0_fom_wakeup(&m->tgf_gen);
}

static struct m0_be_tx_group *tx_group(const struct m0_be_tx_group_fom *m)
{
	/* return &m->tgf_engine->te_group; */
	return NULL;
}

#if 0
static void tx_group_populate(struct m0_be_tx_group *group,
			      struct m0_be_tx_engine *engine, bool *full)
{
	struct m0_be_tx *tx;

	m0_rwlock_read_lock(&engine->te_lock);
	m0_tl_for(eng, &engine->te_txs[M0_BTS_CLOSED], tx) {
		if (false /* XXX TODO: credit of group + credit of tx >=
			   * max credit of group (threshold) */) {
			*full = true;
			break;
		}
		tx_group_add(engine, group, tx);
	} m0_tl_endfor;
	m0_rwlock_read_unlock(&engine->te_lock);
}
#endif

M0_INTERNAL void m0_be_tx_group_fom_init(struct m0_be_tx_group_fom *gf,
					 struct m0_reqh *reqh)
{
	M0_ENTRY();
	M0_PRE(m0_reqh_state_get(reqh) == M0_REQH_ST_NORMAL);

	m0_fom_init(&gf->tgf_gen, &tx_group_fom_type,
		    &tx_group_fom_ops, NULL, NULL, reqh, &m0_be_txs_stype);

	gf->tgf_reqh = reqh;
	gf->tgf_stopping = false;
	m0_fom_timeout_init(&gf->tgf_to);
	m0_semaphore_init(&gf->tgf_started, 0);
	m0_fom_type_init(&tx_group_fom_type, &tx_group_fom_type_ops,
			 &m0_be_txs_stype, &_tx_group_conf);

	m0_fom_queue(&gf->tgf_gen, gf->tgf_reqh);
	m0_semaphore_down(&gf->tgf_started);
}

M0_INTERNAL void m0_be_tx_group_fom_fini(struct m0_be_tx_group_fom *gf)
{
	gf->tgf_stopping = true;
	/* XXX */
}

M0_INTERNAL void m0_be_tx_group_fom_reset(struct m0_be_tx_group_fom *gf)
{
}

M0_INTERNAL void m0_be_tx_group_fom_process(struct m0_be_tx_group_fom *gf,
					    struct m0_be_tx_group *gr)
{
	gf->tgf_group = gr;
	/* send ast to wake up fom */
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
