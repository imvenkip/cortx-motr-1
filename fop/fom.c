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
 * Original author: Mandar Sawant <Mandar_Sawant@xyratex.com>
 * Original creation date: 05/04/2011
 */

#include "lib/misc.h"
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/locality.h"
#include "lib/processor.h"
#include "lib/time.h"
#include "lib/timer.h"
#include "lib/arith.h"
#include "lib/cdefs.h" /* ergo */
#include "db/db_common.h"
#include "addb/addb.h"
#include "mero/magic.h"
#include "fop/fop.h"
#include "fop/fom_long_lock.h"
#include "reqh/reqh.h"
#include "sm/sm.h"
#include "fop/fop_addb.h"
#include "rpc/rpc_machine.h"

/**
 * @addtogroup fom
 *
 * <b>Locality internals</b>
 *
 * A locality has 4 groups of threads associated with it:
 *
 *     - a handler thread: m0_fom_locality::fl_handler. This thread executes
 *       main handler loop (loc_handler_thread()), where it waits until there
 *       are foms in runqueue and executes their phase transitions. This thread
 *       keeps group lock (m0_fom_locality::fl_group::s_lock) all the
 *       time. As a result, if phase transitions do not block, group lock is
 *       rarely touched;
 *
 *     - blocked threads: threads that executed a m0_fom_block_enter(), but not
 *       yet a matching m0_fom_block_leave() call as part of phase transition;
 *
 *     - unblocking threads: threads that are executing a m0_fom_block_leave()
 *       call and trying to re-acquire the group lock to complete phase
 *       transition.
 *
 *     - idle threads: these are waiting on m0_fom_locality::fl_idle to
 *       become the new handler thread when the previous handler is blocked.
 *
 * Transitions between thread groups are as following:
 *
 *     - the handler thread and a pool of idle threads are created when the
 *       locality is initialised;
 *
 *     - as part of m0_fom_block_leave() call, the blocked thread increments
 *       m0_fom_locality::fl_unblocking counter and acquires the group
 *       lock. When the group lock is acquired, the thread makes itself the
 *       handler thread;
 *
 *     - on each iteration of the main handler thread loop, the handler checks
 *       m0_fom_locality::fl_unblocking. If the counter is greater than 0, the
 *       handler releases the group lock and makes itself idle;
 *
 *     - as part of m0_fom_block_enter() call, current handler thread releases
 *       the group lock and checks m0_fom_locality::fl_unblocking. If the
 *       counter is 0, the handler wakes up one of idle threads (creating one if
 *       none exists), making it the new handler thread.
 *
 * @verbatim
 *
 *      INIT----------------------->IDLE
 *            loc_thr_create()      | ^
 *             ^                    | |
 *             .                    | |
 *             .     signal(fl_idle)| |(unblocking > 0)
 *             .        ^           | |       ^
 *             .        .           | |       .
 *             .        .           | |       .
 *           m0_fom_block_enter()   V |       .
 *        +-----------------------HANDLER     .
 *        |                          ^        .
 *        V                          |        .
 *     BLOCKED                  UNBLOCKING    .
 *        |                          ^        .
 *        |                          |        .
 *        +--------------------------+        .
 *           m0_fom_block_leave()             .
 *                    .                       .
 *                    .........................
 *
 * @endverbatim
 *
 * In the diagram above, dotted arrow means that the state transition causes a
 * change making state transition for *other* thread possible.
 *
 * All threads are linked into m0_fom_locality::fl_threads. This list is used
 * mostly for debugging and invariant checking purposes, but also for thread
 * finalisation (loc_fini()).
 *
 * Thread state transitions, associated lists and counters are protected by
 * the group mutex.
 *
 * @{
 */

enum {
	LOC_HT_WAIT = 1,
	LOC_IDLE_NR = 1
};

/**
 * Locality thread states. Used for correctness checking.
 */
enum loc_thread_state {
	HANDLER = 1,
	BLOCKED,
	UNBLOCKING,
	IDLE
};

/**
 * Locality thread.
 *
 * Instances of this structure are allocated by loc_thr_create() and freed by
 * loc_thr_fini(). At any time m0_loc_thread::lt_linkage is in
 * m0_fom_locality::fl_threads list and m0_loc_thread::lt_clink is registered
 * with some channel.
 *
 * ->lt_linkage is protected by the locality's group lock. ->lt_state is updated
 * under the group lock, so it can be used by invariants. Other fields are only
 * accessed by the current thread and require no locking.
 */
struct m0_loc_thread {
	enum loc_thread_state   lt_state;
	struct m0_thread        lt_thread;
	struct m0_tlink         lt_linkage;
	struct m0_fom_locality *lt_loc;
	struct m0_clink         lt_clink;
	uint64_t                lt_magix;
};

M0_TL_DESCR_DEFINE(thr, "fom thread", static, struct m0_loc_thread, lt_linkage,
		   lt_magix, M0_FOM_THREAD_MAGIC, M0_FOM_THREAD_HEAD_MAGIC);
M0_TL_DEFINE(thr, static, struct m0_loc_thread);

M0_TL_DESCR_DEFINE(runq, "runq fom", static, struct m0_fom, fo_linkage,
		   fo_magic, M0_FOM_MAGIC, M0_FOM_RUNQ_MAGIC);
M0_TL_DEFINE(runq, static, struct m0_fom);

M0_TL_DESCR_DEFINE(wail, "wail fom", static, struct m0_fom, fo_linkage,
		   fo_magic, M0_FOM_MAGIC, M0_FOM_WAIL_MAGIC);
M0_TL_DEFINE(wail, static, struct m0_fom);

static bool fom_wait_time_is_out(const struct m0_fom_domain *dom,
                                 const struct m0_fom *fom);
static int loc_thr_create(struct m0_fom_locality *loc);

/**
 * Fom domain operations.
 * @todo Support fom timeout functionality.
 */
static struct m0_fom_domain_ops m0_fom_dom_ops = {
	.fdo_time_is_out = fom_wait_time_is_out
};

static void group_lock(struct m0_fom_locality *loc)
{
	m0_sm_group_lock(&loc->fl_group);
}

static void group_unlock(struct m0_fom_locality *loc)
{
	m0_sm_group_unlock(&loc->fl_group);
}

M0_INTERNAL bool m0_fom_group_is_locked(const struct m0_fom *fom)
{
	return m0_mutex_is_locked(&fom->fo_loc->fl_group.s_lock);
}

static bool is_in_runq(const struct m0_fom *fom)
{
	return runq_tlist_contains(&fom->fo_loc->fl_runq, fom);
}

static bool is_in_wail(const struct m0_fom *fom)
{
	return wail_tlist_contains(&fom->fo_loc->fl_wail, fom);
}

static bool thread_invariant(const struct m0_loc_thread *t)
{
	struct m0_fom_locality *loc = t->lt_loc;

	return
		M0_IN(t->lt_state, (HANDLER, BLOCKED, UNBLOCKING, IDLE)) &&
		(loc->fl_handler == t) == (t->lt_state == HANDLER) &&
		ergo(t->lt_state == UNBLOCKING,
		     m0_atomic64_get(&loc->fl_unblocking) > 0);
}

M0_INTERNAL bool m0_fom_domain_invariant(const struct m0_fom_domain *dom)
{
	return dom != NULL && dom->fd_localities != NULL &&
		dom->fd_ops != NULL;
}

M0_INTERNAL bool m0_locality_invariant(const struct m0_fom_locality *loc)
{
	return	loc != NULL && loc->fl_dom != NULL &&
		m0_mutex_is_locked(&loc->fl_group.s_lock) &&
		M0_CHECK_EX(m0_tlist_invariant(&runq_tl, &loc->fl_runq)) &&
		M0_CHECK_EX(m0_tlist_invariant(&wail_tl, &loc->fl_wail)) &&
		m0_tl_forall(thr, t, &loc->fl_threads,
			     t->lt_loc == loc && thread_invariant(t)) &&
		ergo(loc->fl_handler != NULL,
		     thr_tlist_contains(&loc->fl_threads, loc->fl_handler));

}

M0_INTERNAL struct m0_reqh *m0_fom_reqh(const struct m0_fom *fom)
{
	return fom->fo_loc->fl_dom->fd_reqh;
}

static inline enum m0_fom_state fom_state(const struct m0_fom *fom)
{
	return fom->fo_sm_state.sm_state;
}

static inline void fom_state_set(struct m0_fom *fom, enum m0_fom_state state)
{
	m0_sm_state_set(&fom->fo_sm_state, state);
}

static bool fom_is_blocked(const struct m0_fom *fom)
{
	return
		fom_state(fom) == M0_FOS_RUNNING &&
		M0_IN(fom->fo_thread->lt_state, (BLOCKED, UNBLOCKING));
}

/* Returns fom from state machine m0_fom::fo_sm_state */
static inline struct m0_fom *sm2fom(struct m0_sm *sm)
{
	return container_of(sm, struct m0_fom, fo_sm_state);
}

M0_INTERNAL bool m0_fom_invariant(const struct m0_fom *fom)
{
	return
		fom != NULL && fom->fo_loc != NULL &&
		fom->fo_type != NULL && fom->fo_ops != NULL &&

		m0_fom_group_is_locked(fom) &&

		/* fom magic is the same in runq and wail tlists,
		 * so we can use either one here.
		 * @todo replace this with bob_check() */
		M0_CHECK_EX(m0_tlink_invariant(&runq_tl, fom)) &&

		M0_IN(fom_state(fom), (M0_FOS_READY, M0_FOS_WAITING,
				       M0_FOS_RUNNING, M0_FOS_INIT)) &&
		(fom_state(fom) == M0_FOS_READY) == is_in_runq(fom) &&
		(fom_state(fom) == M0_FOS_WAITING) == is_in_wail(fom) &&
		ergo(fom->fo_thread != NULL,
		     fom_state(fom) == M0_FOS_RUNNING) &&
		ergo(fom->fo_pending != NULL,
		     (fom_state(fom) == M0_FOS_READY || fom_is_blocked(fom))) &&
		ergo(fom->fo_cb.fc_state != M0_FCS_DONE,
		     fom_state(fom) == M0_FOS_WAITING);
}

static bool fom_wait_time_is_out(const struct m0_fom_domain *dom,
                                 const struct m0_fom *fom)
{
	return false;
}

/**
 * Enqueues fom into locality runq list and increments
 * number of items in runq, m0_fom_locality::fl_runq_nr.
 * This function is invoked when a new fom is submitted for
 * execution or a waiting fom is re-scheduled for processing.
 *
 * @post m0_fom_invariant(fom)
 */
static void fom_ready(struct m0_fom *fom)
{
	struct m0_fom_locality *loc;
	bool                    empty;

	fom_state_set(fom, M0_FOS_READY);
	fom->fo_sched_epoch = fom->fo_sm_state.sm_state_epoch ?: m0_time_now();
	loc = fom->fo_loc;
	empty = runq_tlist_is_empty(&loc->fl_runq);
	runq_tlist_add_tail(&loc->fl_runq, fom);
	M0_CNT_INC(loc->fl_runq_nr);
	if (empty)
		m0_chan_signal(&loc->fl_runrun);
	M0_POST(m0_fom_invariant(fom));
}

M0_INTERNAL void m0_fom_ready(struct m0_fom *fom)
{
	M0_PRE(m0_fom_invariant(fom));

	wail_tlist_del(fom);
	M0_CNT_DEC(fom->fo_loc->fl_wail_nr);
	fom_ready(fom);
}

static void readyit(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_fom *fom = container_of(ast, struct m0_fom, fo_cb.fc_ast);

	m0_fom_ready(fom);
}

static void queueit(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_fom *fom = container_of(ast, struct m0_fom, fo_cb.fc_ast);

	M0_PRE(m0_fom_invariant(fom));
	M0_PRE(m0_fom_phase(fom) == M0_FOM_PHASE_INIT);

	M0_CNT_INC(fom->fo_loc->fl_foms);
	fom_ready(fom);
}

M0_INTERNAL void m0_fom_wakeup(struct m0_fom *fom)
{
	fom->fo_cb.fc_ast.sa_cb = readyit;
	m0_sm_ast_post(&fom->fo_loc->fl_group, &fom->fo_cb.fc_ast);
}

M0_INTERNAL void m0_fom_block_enter(struct m0_fom *fom)
{
	struct m0_fom_locality *loc;
	struct m0_loc_thread   *thr;

	M0_PRE(m0_fom_invariant(fom));
	M0_PRE(fom_state(fom) == M0_FOS_RUNNING);
	M0_PRE(!fom_is_blocked(fom));

	loc = fom->fo_loc;
	thr = fom->fo_thread;

	M0_PRE(thr->lt_state == HANDLER);
	M0_PRE(thr == loc->fl_handler);

	/*
	 * If there are unblocking threads, trying to complete
	 * m0_fom_block_leave() call, do nothing and release the group lock. One
	 * of these threads would grab it and become the handler.
	 *
	 * Otherwise, wake up one idle thread, creating it if necessary.
	 *
	 * Note that loc->fl_unblocking can change under us, but:
	 *
	 *     - it cannot become 0 if it wasn't, because it is decremented
	 *       under the group lock and,
	 *
	 *     - if it were increased after we check it, nothing bad would
	 *       happen: an extra idle thread wakeup is harmless.
	 */
	if (m0_atomic64_get(&loc->fl_unblocking) == 0) {
		if (!m0_chan_has_waiters(&loc->fl_idle))
			loc_thr_create(loc);
		m0_chan_signal(&loc->fl_idle);
	}

	thr->lt_state = BLOCKED;
	loc->fl_handler = NULL;
	M0_ASSERT(m0_locality_invariant(loc));
	group_unlock(loc);
}

M0_INTERNAL void m0_fom_block_leave(struct m0_fom *fom)
{
	struct m0_fom_locality *loc;
	struct m0_loc_thread   *thr;

	loc = fom->fo_loc;
	thr = fom->fo_thread;

	M0_PRE(thr->lt_state == BLOCKED);
	thr->lt_state = UNBLOCKING;
	/*
	 * Signal the handler that there is a thread that wants to unblock, just
	 * in case the handler is sleeping on empty runqueue.
	 *
	 * It is enough to do this only when loc->fl_unblocking increments from
	 * 0 to 1, because the handler won't go to sleep until
	 * loc->fl_unblocking drops to 0.
	 */
	if (m0_atomic64_add_return(&loc->fl_unblocking, 1) == 1)
		m0_clink_signal(&loc->fl_group.s_clink);
	group_lock(loc);
	M0_ASSERT(m0_locality_invariant(loc));
	M0_ASSERT(fom_is_blocked(fom));
	M0_ASSERT(loc->fl_handler == NULL);
	loc->fl_handler = thr;
	thr->lt_state = HANDLER;
	m0_atomic64_dec(&loc->fl_unblocking);
	M0_ASSERT(m0_locality_invariant(loc));
}

M0_INTERNAL void m0_fom_queue(struct m0_fom *fom, struct m0_reqh *reqh)
{
	struct m0_fom_domain *dom;
	size_t                loc_idx;

	M0_PRE(reqh != NULL);
	M0_PRE(fom != NULL);

	M0_ASSERT(reqh->rh_svc != NULL || fom->fo_service != NULL);

	dom = &reqh->rh_fom_dom;
	loc_idx = fom->fo_ops->fo_home_locality(fom) % dom->fd_localities_nr;
	M0_ASSERT(loc_idx >= 0 && loc_idx < dom->fd_localities_nr);
	fom->fo_loc = &reqh->rh_fom_dom.fd_localities[loc_idx];
	m0_fom_sm_init(fom);
	fom->fo_cb.fc_ast.sa_cb = queueit;
	m0_sm_ast_post(&fom->fo_loc->fl_group, &fom->fo_cb.fc_ast);
}

/**
 * Puts fom on locality wait list if fom performs a blocking operation, this
 * releases the handler thread to start executing another fom from the runq,
 * thus making the reqh non blocking.
 *
 * Fom state is changed to M0_FOS_WAITING.  m0_fom_locality::fl_group.s_lock
 * should be held before putting fom on the locality wait list.
 *
 * This function is invoked from fom_exec(), if the fom is performing a blocking
 * operation and m0_fom_ops::fo_tick() returns M0_FSO_WAIT.
 *
 * @post m0_fom_invariant(fom)
 */
static void fom_wait(struct m0_fom *fom)
{
	struct m0_fom_locality *loc;

	fom_state_set(fom, M0_FOS_WAITING);
	loc = fom->fo_loc;
	wail_tlist_add_tail(&loc->fl_wail, fom);
	M0_CNT_INC(loc->fl_wail_nr);
	M0_POST(m0_fom_invariant(fom));
}

/**
 * Helper function advancing a fom call-back from ARMED to DONE state.
 */
static void cb_done(struct m0_fom_callback *cb)
{
	struct m0_clink *clink = &cb->fc_clink;

	M0_PRE(m0_fom_invariant(cb->fc_fom));
	M0_PRE(cb->fc_state == M0_FCS_ARMED);

	cb->fc_state = M0_FCS_DONE;
	if (m0_clink_is_armed(clink))
		m0_clink_del_lock(clink);
}

/**
 * Helper to execute the bottom half of a fom call-back.
 */
static void cb_run(struct m0_fom_callback *cb)
{
	M0_PRE(m0_fom_invariant(cb->fc_fom));

	cb_done(cb);
	cb->fc_bottom(cb);
}

static void *cb_next(struct m0_fom_callback *cb)
{
	return cb->fc_ast.sa_next;
}

/**
 * Invokes fom phase transition method, which transitions fom
 * through various phases of its execution without blocking.
 * @post m0_fom_invariant(fom)
 */
static void fom_exec(struct m0_fom *fom)
{
	int			rc;
	struct m0_fom_locality *loc;
	m0_time_t               exec_time;

	loc = fom->fo_loc;
	fom->fo_thread = loc->fl_handler;
	fom_state_set(fom, M0_FOS_RUNNING);
	exec_time = fom->fo_sm_state.sm_state_epoch ?: m0_time_now();
	do {
		M0_ASSERT(m0_fom_invariant(fom));
		M0_ASSERT(m0_fom_phase(fom) != M0_FOM_PHASE_FINISH);
		rc = fom->fo_ops->fo_tick(fom);
		/*
		 * (rc == M0_FSO_AGAIN) means that next phase transition is
		 * possible. Current policy is to execute the transition
		 * immediately. Alternative is to put the fom on the runqueue
		 * and select "the best" fom from the runqueue.
		 */
		fom->fo_transitions++;
	} while (rc == M0_FSO_AGAIN);

	fom->fo_thread = NULL;

	M0_ASSERT(rc == M0_FSO_WAIT);
	M0_ASSERT(m0_fom_group_is_locked(fom));

	exec_time = m0_time_sub(m0_time_now(), exec_time);
	m0_addb_counter_update(&loc->fl_stat_run_times,
				exec_time >> 10); /* ~usec */

	if (m0_fom_phase(fom) == M0_FOM_PHASE_FINISH) {
                /*
                 * Finish fom itself.
                 */
		fom->fo_ops->fo_fini(fom);
		/*
		 * Don't touch the fom after this point.
		 */
	} else {
		struct m0_fom_callback *cb;

		fom_wait(fom);
		/*
		 * If there are pending call-backs, execute them, until one of
		 * them wakes the fom up. Don't bother to optimize moving
		 * between queues: this is a rare case.
		 *
		 * Note: call-backs are executed in LIFO order.
		 */
		while ((cb = fom->fo_pending) != NULL) {
			fom->fo_pending = cb_next(cb);
			cb_run(cb);
			/*
			 * call-back is not allowed to destroy a fom.
			 */
			M0_ASSERT(m0_fom_phase(fom) != M0_FOM_PHASE_FINISH);
			if (fom_state(fom) != M0_FOS_WAITING)
				break;
		}
		M0_ASSERT(m0_fom_invariant(fom));
	}
}

/**
 * Dequeues a fom from runq list of the locality.
 *
 * @retval m0_fom if queue is not empty, NULL otherwise
 */
static struct m0_fom *fom_dequeue(struct m0_fom_locality *loc)
{
	struct m0_fom  *fom;

	fom = runq_tlist_head(&loc->fl_runq);
	if (fom == NULL)
		return NULL;

	runq_tlist_del(fom);
	M0_CNT_DEC(loc->fl_runq_nr);

	m0_addb_counter_update(&loc->fl_stat_sched_wait_times, /* ~usec */
		m0_time_sub(m0_time_now(), fom->fo_sched_epoch) >> 10);

	return fom;
}

/**
 * Locality handler thread. See the "Locality internals" section.
 */
static void loc_handler_thread(struct m0_loc_thread *th)
{
	struct m0_clink	       *clink = &th->lt_clink;
	struct m0_fom_locality *loc   = th->lt_loc;

	while (1) {
		/*
		 * start idle, wait for work to do. The clink was registered
		 * with &loc->fl_idle by loc_thr_create().
		 */
		M0_ASSERT(th->lt_state == IDLE);
		m0_chan_wait(clink);

		/* become the handler thread */
		group_lock(loc);
		M0_ASSERT(loc->fl_handler == NULL);
		loc->fl_handler = th;
		th->lt_state = HANDLER;

		/*
		 * re-initialise the clink and arrange for it to receive group
		 * AST notifications and runrun wakeups.
		 */
		m0_clink_del(clink);
		m0_clink_fini(clink);
		m0_clink_init(clink, NULL);
		m0_clink_attach(clink, &loc->fl_group.s_clink, NULL);
		m0_clink_add(&loc->fl_runrun, clink);

		/*
		 * main handler loop.
		 *
		 * This loop terminates when the locality is finalised
		 * (loc->fl_shutdown) or this thread should go back to the idle
		 * state.
		 */
		while (1) {
			struct m0_fom *fom;

			M0_ASSERT(m0_locality_invariant(loc));
			/*
			 * Check for a blocked thread that tries to unblock and
			 * complete a phase transition.
			 */
			if (m0_atomic64_get(&loc->fl_unblocking) > 0)
				/*
				 * Idle ourselves. The unblocking thread (first
				 * to grab the group lock in case there are
				 * many), becomes the new handler.
				 */
				break;
			m0_sm_asts_run(&loc->fl_group);
			fom = fom_dequeue(loc);
			if (fom != NULL)
				fom_exec(fom);
			else if (loc->fl_shutdown)
				break;
			else
				/*
				 * Yes, sleep with the lock held. Knock on
				 * &loc->fl_runrun or &loc->fl_group.s_clink to
				 * wake.
				 */
				m0_chan_timedwait(clink, m0_time_from_now(
							  LOC_HT_WAIT, 0));
		}
		loc->fl_handler = NULL;
		th->lt_state = IDLE;
		m0_clink_del(clink);
		m0_clink_fini(clink);
		m0_clink_init(&th->lt_clink, NULL);
		m0_clink_add(&loc->fl_idle, &th->lt_clink);
		group_unlock(loc);
		if (loc->fl_shutdown)
			break;
	}
}

/**
 * Init function for a locality thread. Confines the thread to the locality
 * core.
 */
static int loc_thr_init(struct m0_loc_thread *th)
{
	return m0_thread_confine(&th->lt_thread, &th->lt_loc->fl_processors);
}

static void loc_thr_fini(struct m0_loc_thread *th)
{
	M0_PRE(m0_mutex_is_locked(&th->lt_loc->fl_group.s_lock));
	M0_PRE(th->lt_state == IDLE);
	m0_clink_del(&th->lt_clink);
	m0_clink_fini(&th->lt_clink);

	m0_thread_fini(&th->lt_thread);
	thr_tlink_del_fini(th);
	m0_free(th);
}

static int loc_thr_create(struct m0_fom_locality *loc)
{
	struct m0_loc_thread *thr;
	int                   result;

	M0_PRE(m0_mutex_is_locked(&loc->fl_group.s_lock));

	FOP_ALLOC_PTR(thr, LOC_THR_CREATE, &m0_fop_addb_ctx);
	if (thr == NULL)
		return -ENOMEM;
	thr->lt_state = IDLE;
	thr->lt_magix = M0_FOM_THREAD_MAGIC;
	thr->lt_loc   = loc;
	thr_tlink_init_at_tail(thr, &loc->fl_threads);

	m0_clink_init(&thr->lt_clink, NULL);
	m0_clink_add(&loc->fl_idle, &thr->lt_clink);

	result = M0_THREAD_INIT(&thr->lt_thread, struct m0_loc_thread *,
				loc_thr_init, &loc_handler_thread, thr,
				"loc thread");
	if (result != 0)
		loc_thr_fini(thr);
	return result;
}

/**
 * Finalises a given locality.
 */
static void loc_fini(struct m0_fom_locality *loc)
{
	struct m0_loc_thread *th;

	loc->fl_shutdown = true;
	m0_clink_signal(&loc->fl_group.s_clink);

	group_lock(loc);
	m0_chan_broadcast(&loc->fl_runrun);
	m0_chan_broadcast(&loc->fl_idle);
	while ((th = thr_tlist_head(&loc->fl_threads)) != NULL) {
		group_unlock(loc);
		m0_thread_join(&th->lt_thread);
		group_lock(loc);
		loc_thr_fini(th);
	}
	group_unlock(loc);

	runq_tlist_fini(&loc->fl_runq);
	M0_ASSERT(loc->fl_runq_nr == 0);

	wail_tlist_fini(&loc->fl_wail);
	M0_ASSERT(loc->fl_wail_nr == 0);

	thr_tlist_fini(&loc->fl_threads);
	M0_ASSERT(m0_atomic64_get(&loc->fl_unblocking) == 0);

	m0_chan_fini_lock(&loc->fl_idle);
	m0_chan_fini_lock(&loc->fl_runrun);
	m0_sm_group_fini(&loc->fl_group);

	m0_bitmap_fini(&loc->fl_processors);

	m0_addb_counter_fini(&loc->fl_stat_sched_wait_times);
	m0_addb_counter_fini(&loc->fl_stat_run_times);
	m0_addb_ctx_fini(&loc->fl_addb_ctx);
}

/**
 * Initialises a locality in fom domain.  Creates and adds threads to locality,
 * every thread is confined to the cpus represented by the
 * m0_fom_locality::fl_processors, this is done in the locality thread init
 * function (loc_thr_init()).
 *
 * A pool of LOC_IDLE_NR idle threads is created together with a handler thread.
 *
 * @see loc_thr_create()
 * @see loc_thr_init()
 *
 * @param loc     m0_fom_locality to be initialised
 * @param cpu     cpu assigned to the locality
 * @param cpu_max maximum number of cpus that can be present in a locality
 */
static int loc_init(struct m0_fom_locality *loc, size_t cpu, size_t cpu_max)
{
	int                result;
	struct m0_addb_mc *addb_mc;

	M0_PRE(loc != NULL);

	/**
	 * @todo Need a locality specific ADDB machine
	 * with a caching event manager.
	 */
	addb_mc = &loc->fl_dom->fd_reqh->rh_addb_mc;
	if (!m0_addb_mc_is_configured(addb_mc)) /* happens in UTs */
		addb_mc = &m0_addb_gmc;
	M0_ADDB_CTX_INIT(addb_mc, &loc->fl_addb_ctx, &m0_addb_ct_fom_locality,
			 &loc->fl_dom->fd_reqh->rh_addb_ctx, cpu);

	result = m0_addb_counter_init(&loc->fl_stat_run_times,
				      &m0_addb_rt_fl_run_times);
	if (result != 0) {
		m0_addb_ctx_fini(&loc->fl_addb_ctx);
		return result;
	}
	result = m0_addb_counter_init(&loc->fl_stat_sched_wait_times,
				      &m0_addb_rt_fl_sched_wait_times);
	if (result != 0) {
		m0_addb_counter_fini(&loc->fl_stat_run_times);
		m0_addb_ctx_fini(&loc->fl_addb_ctx);
		return result;
	}

	runq_tlist_init(&loc->fl_runq);
	loc->fl_runq_nr = 0;

	wail_tlist_init(&loc->fl_wail);
	loc->fl_wail_nr = 0;

	m0_sm_group_init(&loc->fl_group);
	m0_chan_init(&loc->fl_runrun, &loc->fl_group.s_lock);
	thr_tlist_init(&loc->fl_threads);
	m0_atomic64_set(&loc->fl_unblocking, 0);
	m0_chan_init(&loc->fl_idle, &loc->fl_group.s_lock);

	result = m0_bitmap_init(&loc->fl_processors, cpu_max);

	if (result == 0) {
		int i;

		m0_bitmap_set(&loc->fl_processors, cpu, true);
		/* create a pool of idle threads plus the handler thread. */
		group_lock(loc);
		for (i = 0; i < LOC_IDLE_NR + 1; ++i) {
			result = loc_thr_create(loc);
			if (result != 0)
				break;
		}
		/* wake up one idle thread. It becomes the handler thread. */
		m0_chan_signal(&loc->fl_idle);
		group_unlock(loc);
	}
	if (result != 0)
		loc_fini(loc);

	return result;
}

static void loc_ast_post_stats(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_fom_locality *loc = container_of(ast, struct m0_fom_locality,
						   fl_post_stats_ast);
	struct m0_addb_ctx *cv[] = {&loc->fl_addb_ctx, NULL};

	if (m0_addb_counter_nr(&loc->fl_stat_run_times) > 0)
		M0_ADDB_POST_CNTR(&loc->fl_dom->fd_reqh->rh_addb_mc, cv,
				  &loc->fl_stat_run_times);
	if (m0_addb_counter_nr(&loc->fl_stat_sched_wait_times) > 0)
		M0_ADDB_POST_CNTR(&loc->fl_dom->fd_reqh->rh_addb_mc, cv,
				  &loc->fl_stat_sched_wait_times);
	M0_ADDB_POST(&loc->fl_dom->fd_reqh->rh_addb_mc, &m0_addb_rt_fl_runq_nr,
		     cv, loc->fl_runq_nr);
	M0_ADDB_POST(&loc->fl_dom->fd_reqh->rh_addb_mc, &m0_addb_rt_fl_wail_nr,
		     cv, loc->fl_wail_nr);
}

M0_INTERNAL void m0_fom_locality_post_stats(struct m0_fom_locality *loc)
{
	if (loc->fl_post_stats_ast.sa_next == NULL) {
		loc->fl_post_stats_ast.sa_cb = loc_ast_post_stats;
		m0_sm_ast_post(&loc->fl_group, &loc->fl_post_stats_ast);
	}
}

M0_INTERNAL int m0_fom_domain_init(struct m0_fom_domain *dom)
{
	int                     result;
	size_t                  cpu;
	size_t                  cpu_max;
	struct m0_fom_locality *localities;
	struct m0_bitmap        onln_cpu_map;


	M0_PRE(dom != NULL);

	cpu_max = m0_processor_nr_max();
	dom->fd_ops = &m0_fom_dom_ops;
	result = m0_bitmap_init(&onln_cpu_map, cpu_max);
	if (result != 0)
		return result;

	m0_processors_online(&onln_cpu_map);
	FOP_ALLOC_ARR(dom->fd_localities, cpu_max, FOM_DOMAIN_INIT,
			&m0_fop_addb_ctx);
	if (dom->fd_localities == NULL) {
		m0_bitmap_fini(&onln_cpu_map);
		return -ENOMEM;
	}

	localities = dom->fd_localities;
	for (cpu = 0; cpu < cpu_max; ++cpu) {
		struct m0_fom_locality *loc;

		if (!m0_bitmap_get(&onln_cpu_map, cpu))
			continue;
		loc = &localities[dom->fd_localities_nr];
		loc->fl_dom = dom;
		result = loc_init(loc, cpu, cpu_max);
		if (result != 0) {
			m0_fom_domain_fini(dom);
			break;
		}
		m0_locality_set(cpu, &(struct m0_locality){
				.lo_grp  = &loc->fl_group,
				.lo_reqh = dom->fd_reqh,
				.lo_idx  = dom->fd_localities_nr });
		M0_CNT_INC(dom->fd_localities_nr);
	}
	m0_bitmap_fini(&onln_cpu_map);

	return result;
}

M0_INTERNAL void m0_fom_domain_fini(struct m0_fom_domain *dom)
{
	int fd_loc_nr;

	M0_ASSERT(m0_fom_domain_invariant(dom));

	fd_loc_nr = dom->fd_localities_nr;
	while (fd_loc_nr > 0) {
		loc_fini(&dom->fd_localities[fd_loc_nr - 1]);
		--fd_loc_nr;
	}

	m0_free(dom->fd_localities);
}

M0_INTERNAL bool m0_fom_domain_is_idle(const struct m0_fom_domain *dom)
{
	return m0_forall(i, dom->fd_localities_nr,
			 dom->fd_localities[i].fl_foms == 0);
}

static void fop_fini(struct m0_fop *fop, bool local)
{
	struct m0_rpc_machine  *rmachine;

	if (fop != NULL) {
		if (!local) {
			rmachine = fop->f_item.ri_rmachine;
			M0_ASSERT(rmachine != NULL);
			m0_sm_group_lock(&rmachine->rm_sm_grp);
			m0_fop_put(fop);
			m0_sm_group_unlock(&rmachine->rm_sm_grp);
		} else
			m0_fop_put(fop);
	}
}

void m0_fom_fini(struct m0_fom *fom)
{
	struct m0_fom_locality *loc;
	struct m0_reqh         *reqh;

	M0_PRE(m0_fom_phase(fom) == M0_FOM_PHASE_FINISH);
        M0_PRE(!m0_db_tx_is_active(&fom->fo_tx.tx_dbtx));

	loc  = fom->fo_loc;
	reqh = loc->fl_dom->fd_reqh;
	fom_state_set(fom, M0_FOS_FINISH);
	if (m0_addb_ctx_is_initialized(&fom->fo_addb_ctx)) {
		m0_sm_stats_post(&fom->fo_sm_phase, &reqh->rh_addb_mc,
				M0_FOM_ADDB_CTX_VEC(fom));
		m0_sm_stats_post(&fom->fo_sm_state, &reqh->rh_addb_mc,
				M0_FOM_ADDB_CTX_VEC(fom));
		m0_addb_sm_counter_fini(&fom->fo_sm_state_stats);
	}
	m0_sm_fini(&fom->fo_sm_phase);
	m0_sm_fini(&fom->fo_sm_state);
	m0_addb_ctx_fini(&fom->fo_addb_ctx);
	if (fom->fo_op_addb_ctx != NULL) {
		m0_addb_ctx_fini(fom->fo_op_addb_ctx);
		fom->fo_op_addb_ctx = NULL;
	}
	runq_tlink_fini(fom);
	m0_fom_callback_init(&fom->fo_cb);

	fop_fini(fom->fo_fop, fom->fo_local);
	fop_fini(fom->fo_rep_fop, fom->fo_local);

	M0_CNT_DEC(loc->fl_foms);
	if (loc->fl_foms == 0)
		m0_chan_signal_lock(&reqh->rh_sd_signal);
}
M0_EXPORTED(m0_fom_fini);

void m0_fom_init(struct m0_fom *fom, struct m0_fom_type *fom_type,
		 const struct m0_fom_ops *ops, struct m0_fop *fop,
		 struct m0_fop *reply, struct m0_reqh *reqh,
		 const struct m0_reqh_service_type *stype)
{
	M0_PRE(stype != NULL);
	M0_PRE(fom != NULL);
	M0_PRE(reqh != NULL);
	M0_PRE(ops->fo_addb_init != NULL);

	fom->fo_type	    = fom_type;
	fom->fo_ops	    = ops;
	fom->fo_transitions = 0;
	fom->fo_local	    = false;
	m0_fom_callback_init(&fom->fo_cb);
	runq_tlink_init(fom);

	if (fop != NULL)
		m0_fop_get(fop);
	fom->fo_fop = fop;

	if (reply != NULL)
		m0_fop_get(reply);
	fom->fo_rep_fop = reply;

	/**
	 * NOTE: The service may be in M0_RST_STARTING state
	 * if the fom was launched on startup
	 */
	fom->fo_service = m0_reqh_service_find(stype, reqh);
	M0_ASSERT(reqh->rh_svc != NULL || fom->fo_service != NULL);
	/**
	 * @todo This is conditional locking is required, since
	 * mdservice does not have reqh service, but has local
	 * service. Need to discuss about this
	 */
	if (reqh->rh_svc == NULL) {
	/** @todo locking may not be necessary with sync nb events */
		m0_mutex_lock(&fom->fo_service->rs_mutex);
		M0_ASSERT(fom->fo_addb_ctx.ac_magic == 0);
		(*fom->fo_ops->fo_addb_init)(fom, &reqh->rh_addb_mc);
		M0_ASSERT(fom->fo_addb_ctx.ac_magic != 0);
		m0_mutex_unlock(&fom->fo_service->rs_mutex);
	} else {
		M0_ASSERT(fom->fo_addb_ctx.ac_magic == 0);
		(*fom->fo_ops->fo_addb_init)(fom, &reqh->rh_addb_mc);
		M0_ASSERT(fom->fo_addb_ctx.ac_magic != 0);
	}
	if (m0_addb_ctx_is_initialized(&fom->fo_addb_ctx))
		M0_FOM_ADDB_POST(fom, &reqh->rh_addb_mc, &m0_addb_rt_fom_init);
	fom->fo_sm_phase.sm_state_epoch = 0;
}
M0_EXPORTED(m0_fom_init);

M0_INTERNAL void m0_fom_phase_stats_enable(struct m0_fom *fom,
					   struct m0_addb_sm_counter *c)
{
	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_sm_phase.sm_state_epoch == 0);

	fom->fo_sm_phase.sm_state_epoch = 1;
	fom->fo_sm_phase.sm_addb_stats = c;
}

static bool fom_clink_cb(struct m0_clink *link)
{
	struct m0_fom_callback *cb = container_of(link, struct m0_fom_callback,
	                                          fc_clink);
	M0_PRE(cb->fc_state >= M0_FCS_ARMED);

	if (cb->fc_state == M0_FCS_ARMED &&
	    (cb->fc_top == NULL || !cb->fc_top(cb)))
		m0_sm_ast_post(&cb->fc_fom->fo_loc->fl_group, &cb->fc_ast);

	return true;
}

static void fom_ast_cb(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct m0_fom_callback *cb  = container_of(ast, struct m0_fom_callback,
						   fc_ast);
	struct m0_fom          *fom = cb->fc_fom;

	M0_PRE(m0_fom_invariant(fom));
	M0_PRE(cb->fc_state == M0_FCS_ARMED);

	if (fom_state(fom) == M0_FOS_WAITING)
		cb_run(cb);
	else {
		M0_ASSERT(fom_state(fom) == M0_FOS_READY
			  || fom_is_blocked(fom));
		/*
		 * Call-back arrived while our fom is in READY state (hanging on
		 * the runqueue, waiting for its turn) or RUNNING state (blocked
		 * between m0_fom_block_enter() and
		 * m0_fom_block_leave()). Instead of executing the call-back
		 * immediately, add it to the stack of pending call-backs for
		 * this fom. The call-back will be executed by fom_exec() when
		 * the fom is about to return to the WAITING state.
		 */
		cb->fc_ast.sa_next = (void *)fom->fo_pending;
		fom->fo_pending = cb;
	}
}

M0_INTERNAL void m0_fom_callback_init(struct m0_fom_callback *cb)
{
	cb->fc_state = M0_FCS_DONE;
	m0_clink_init(&cb->fc_clink, &fom_clink_cb);
}

M0_INTERNAL void m0_fom_callback_arm(struct m0_fom *fom, struct m0_chan *chan,
				     struct m0_fom_callback *cb)
{
	M0_PRE(cb->fc_bottom != NULL);
	M0_PRE(cb->fc_state == M0_FCS_DONE);

	cb->fc_fom = fom;

	cb->fc_ast.sa_cb = &fom_ast_cb;
	cb->fc_state = M0_FCS_ARMED;
	m0_mb();
	m0_clink_add(chan, &cb->fc_clink);
}

static void fom_ready_cb(struct m0_fom_callback *cb)
{
	m0_fom_ready(cb->fc_fom);
}

M0_INTERNAL void m0_fom_wait_on(struct m0_fom *fom, struct m0_chan *chan,
				struct m0_fom_callback *cb)
{
	cb->fc_bottom = fom_ready_cb;
	m0_fom_callback_arm(fom, chan, cb);
}

M0_INTERNAL void m0_fom_callback_fini(struct m0_fom_callback *cb)
{
	M0_PRE(cb->fc_state == M0_FCS_DONE);
	m0_clink_fini(&cb->fc_clink);
}

static void cb_cancel(struct m0_fom_callback *cb)
{
	struct m0_fom_callback *prev;

	prev = cb->fc_fom->fo_pending;
	while (prev != NULL && cb_next(prev) != cb)
		prev = cb_next(prev);
	if (prev != NULL)
		prev->fc_ast.sa_next = cb_next(cb);
}

M0_INTERNAL void m0_fom_callback_cancel(struct m0_fom_callback *cb)
{
	M0_PRE(cb->fc_state >= M0_FCS_ARMED);

	if (cb->fc_state == M0_FCS_ARMED) {
		cb_done(cb);
		/* Once the clink is finalised, the AST cannot be posted, cancel
		   the AST. */
		m0_sm_ast_cancel(&cb->fc_fom->fo_loc->fl_group, &cb->fc_ast);
		/* Once the AST is cancelled, cb cannot be added to the pending
		   list, cancel cb. */
		cb_cancel(cb);
	}
}

M0_INTERNAL void m0_fom_timeout_init(struct m0_fom_timeout *to)
{
	M0_SET0(to);
	m0_sm_timer_init(&to->to_timer);
	m0_fom_callback_init(&to->to_cb);
}

M0_INTERNAL void m0_fom_timeout_fini(struct m0_fom_timeout *to)
{
	m0_fom_callback_fini(&to->to_cb);
	m0_sm_timer_fini(&to->to_timer);
}

static void fom_timeout_cb(struct m0_sm_timer *timer)
{
	struct m0_fom_timeout  *to = container_of(timer, struct m0_fom_timeout,
						  to_timer);
	struct m0_fom_callback *cb = &to->to_cb;

	cb->fc_state = M0_FCS_ARMED;
	fom_ast_cb(to->to_timer.tr_grp, &cb->fc_ast);
}

static int fom_timeout_start(struct m0_fom_timeout *to,
			     struct m0_fom *fom,
			     void (*cb)(struct m0_fom_callback *),
			     m0_time_t deadline)
{
	to->to_cb.fc_fom    = fom;
	to->to_cb.fc_bottom = cb;
	return m0_sm_timer_start(&to->to_timer, fom->fo_sm_state.sm_grp,
				 fom_timeout_cb, deadline);
}

M0_INTERNAL int m0_fom_timeout_wait_on(struct m0_fom_timeout *to,
				       struct m0_fom *fom,
				       m0_time_t deadline)
{
	return fom_timeout_start(to, fom, fom_ready_cb, deadline);
}

M0_INTERNAL int m0_fom_timeout_arm(struct m0_fom_timeout *to,
				   struct m0_fom *fom,
				   void (*cb)(struct m0_fom_callback *),
				   m0_time_t deadline)
{
	return fom_timeout_start(to, fom, cb, deadline);
}

M0_INTERNAL void m0_fom_timeout_cancel(struct m0_fom_timeout *to)
{
	struct m0_fom_callback *cb = &to->to_cb;

	M0_PRE(m0_fom_invariant(cb->fc_fom));

	m0_sm_timer_cancel(&to->to_timer);
	m0_fom_callback_cancel(cb);
}

M0_INTERNAL void m0_fom_type_init(struct m0_fom_type *type,
				  const struct m0_fom_type_ops *ops,
				  const struct m0_reqh_service_type *svc_type,
				  struct m0_sm_conf *sm)
{
	type->ft_ops    = ops;
	type->ft_conf   = sm;
	type->ft_rstype = svc_type;
}

static struct m0_sm_state_descr fom_states[] = {
	[M0_FOS_INIT] = {
		.sd_flags     = M0_SDF_INITIAL,
		.sd_name      = "Init",
		.sd_allowed   = M0_BITS(M0_FOS_FINISH, M0_FOS_READY)
	},
	[M0_FOS_READY] = {
		.sd_name      = "Ready",
		.sd_allowed   = M0_BITS(M0_FOS_RUNNING)
	},
	[M0_FOS_RUNNING] = {
		.sd_name      = "Running",
		.sd_allowed   = M0_BITS(M0_FOS_READY, M0_FOS_WAITING,
					M0_FOS_FINISH)
	},
	[M0_FOS_WAITING] = {
		.sd_name      = "Waiting",
		.sd_allowed   = M0_BITS(M0_FOS_READY, M0_FOS_FINISH)
	},
	[M0_FOS_FINISH] = {
		.sd_flags     = M0_SDF_TERMINAL,
		.sd_name      = "Finished",
	}
};

static struct m0_sm_trans_descr fom_trans[M0_FOS_TRANS_NR] = {
	{ "Schedule",  M0_FOS_INIT,     M0_FOS_READY },
	{ "Failed",    M0_FOS_INIT,     M0_FOS_FINISH },
	{ "Run",       M0_FOS_READY,    M0_FOS_RUNNING },
	{ "Yield",     M0_FOS_RUNNING,  M0_FOS_READY },
	{ "Sleep",     M0_FOS_RUNNING,  M0_FOS_WAITING },
	{ "Done",      M0_FOS_RUNNING,  M0_FOS_FINISH },
	{ "Wakeup",    M0_FOS_WAITING,  M0_FOS_READY },
	{ "Terminate", M0_FOS_WAITING,  M0_FOS_FINISH }
};

struct m0_sm_conf fom_states_conf = {
	.scf_name      = "FOM states",
	.scf_nr_states = ARRAY_SIZE(fom_states),
	.scf_state     = fom_states,
	.scf_trans_nr  = ARRAY_SIZE(fom_trans),
	.scf_trans     = fom_trans
};

M0_INTERNAL void m0_fom_sm_init(struct m0_fom *fom)
{
	struct m0_sm_group             *fom_group;
	struct m0_addb_ctx             *fom_addb_ctx = NULL;
	const struct m0_sm_conf        *conf;
	struct m0_addb_sm_counter      *phase_cntr;
	bool                            phase_stats_enabled;

	M0_PRE(fom != NULL);
	M0_PRE(fom->fo_loc != NULL);

	conf = fom->fo_type->ft_conf;
	M0_ASSERT(conf->scf_nr_states != 0);

	fom_group    = &fom->fo_loc->fl_group;

	/**
	 * @todo: replace this with assertion, after reqh service
	 * is associated with mdservice's ut
	 */
	if (fom->fo_service != NULL && fom->fo_service->rs_reqh != NULL)
		fom_addb_ctx = &fom->fo_service->rs_reqh->rh_addb_ctx;

	/* Preserve these across the call to m0_sm_init(). */
	phase_stats_enabled = fom->fo_sm_phase.sm_state_epoch != 0;
	phase_cntr = fom->fo_sm_phase.sm_addb_stats;

	m0_sm_init(&fom->fo_sm_phase, conf, M0_FOM_PHASE_INIT, fom_group);
	m0_sm_init(&fom->fo_sm_state, &fom_states_conf, M0_FOS_INIT, fom_group);

	if (m0_addb_ctx_is_initialized(&fom->fo_addb_ctx)) {
		m0_addb_sm_counter_init(&fom->fo_sm_state_stats,
					&m0_addb_rt_fom_state_stats,
					fom->fo_fos_stats_data,
					sizeof(fom->fo_fos_stats_data));
		m0_sm_stats_enable(&fom->fo_sm_state,
				   &fom->fo_sm_state_stats);
		if (phase_stats_enabled)
			m0_sm_stats_enable(&fom->fo_sm_phase, phase_cntr);
	}
}

void m0_fom_phase_set(struct m0_fom *fom, int phase)
{
	m0_sm_state_set(&fom->fo_sm_phase, phase);
}
M0_EXPORTED(m0_fom_phase_set);

void m0_fom_phase_move(struct m0_fom *fom, int32_t rc, int phase)
{
	m0_sm_move(&fom->fo_sm_phase, rc, phase);
}
M0_EXPORTED(m0_fom_phase_move);

void m0_fom_phase_moveif(struct m0_fom *fom, int32_t rc, int phase0, int phase1)
{
	m0_fom_phase_move(fom, rc, rc == 0 ? phase0 : phase1);
}
M0_EXPORTED(m0_fom_phase_moveif);

int m0_fom_phase(const struct m0_fom *fom)
{
	return fom->fo_sm_phase.sm_state;
}
M0_EXPORTED(m0_fom_phase);

M0_INTERNAL int m0_fom_rc(const struct m0_fom *fom)
{
	return fom->fo_sm_phase.sm_rc;
}

M0_INTERNAL bool m0_fom_is_waiting(const struct m0_fom *fom)
{
	return fom_state(fom) == M0_FOS_WAITING && is_in_wail(fom);
}

M0_INTERNAL int m0_fom_op_addb_ctx_import(struct m0_fom *fom,
					  const struct m0_addb_uint64_seq *id)
{
	M0_PRE(fom != NULL);
	M0_PRE(id  != NULL);

	if (id->au64s_nr <= 0)
		return -ENOENT;
	fom->fo_op_addb_ctx = &fom->fo_imp_op_addb_ctx;
	return m0_addb_ctx_import(fom->fo_op_addb_ctx, id);
}

/** @} endgroup fom */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
