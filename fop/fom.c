/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
#include "lib/processor.h"
#include "lib/time.h"
#include "lib/timer.h"
#include "lib/arith.h"
#include "lib/cdefs.h" /* ergo */

#include "addb/addb.h"
#include "colibri/magic.h"
#include "fop/fop.h"
#include "fop/fom_long_lock.h"
#include "reqh/reqh.h"
#include "sm/sm.h"

/**
 * @addtogroup fom
 *
 * <b>Locality internals</b>
 *
 * A locality has 4 groups of threads associated with it:
 *
 *     - a handler thread: c2_fom_locality::fl_handler. This thread executes
 *       main handler loop (loc_handler_thread()), where it waits until there
 *       are foms in runqueue and executes their phase transitions. This thread
 *       keeps group lock (c2_fom_locality::fl_group::s_lock) all the
 *       time. As a result, if phase transitions do not block, group block is
 *       rarely touched;
 *
 *     - blocked threads: threads that executed a c2_fom_block_enter(), but not
 *       yet a matching c2_fom_block_leave() call as part of phase transition;
 *
 *     - unblocking threads: threads that are executing a c2_fom_block_leave()
 *       call and trying to re-acquire the group lock to complete phase
 *       transition.
 *
 *     - idle threads: these are waiting on c2_fom_locality::fl_idle to
 *       become the new handler thread when the previous handler is blocked.
 *
 * Transitions between thread groups are as following:
 *
 *     - the handler thread and a pool of idle threads are created when the
 *       locality is initialised;
 *
 *     - as part of c2_fom_block_leave() call, the blocked thread increments
 *       c2_fom_locality::fl_unblocking counter and acquires the group
 *       lock. When the group lock is acquired, the thread makes itself the
 *       handler thread;
 *
 *     - on each iteration of the main handler thread loop, the handler checks
 *       c2_fom_locality::fl_unblocking. If the counter is greater than 0, the
 *       handler releases the group lock and makes itself idle;
 *
 *     - as part of c2_fom_block_enter() call, current handler thread releases
 *       the group lock and checks c2_fom_locality::fl_unblocking. If the
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
 *           c2_fom_block_enter()   V |       .
 *        +-----------------------HANDLER     .
 *        |                          ^        .
 *        V                          |        .
 *     BLOCKED                  UNBLOCKING    .
 *        |                          ^        .
 *        |                          |        .
 *        +--------------------------+        .
 *           c2_fom_block_leave()             .
 *                    .                       .
 *                    .........................
 *
 * @endverbatim
 *
 * In the diagram above, dotted arrow means that the state transition causes a
 * change making state transition for *other* thread possible.
 *
 * All threads are linked into c2_fom_locality::fl_threads. This list is used
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

const struct c2_addb_loc c2_fom_addb_loc = {
	.al_name = "fom"
};

const struct c2_addb_ctx_type c2_fom_addb_ctx_type = {
	.act_name = "fom"
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
 * loc_thr_fini(). At any time c2_loc_thread::lt_linkage is in
 * c2_fom_locality::fl_threads list and c2_loc_thread::lt_link is registered
 * with some channel.
 *
 * ->lt_linkage is protected by the locality's group lock. ->lt_state is updated
 * under the group lock, so it can be used by invariants. Other fields are only
 * accessed by the current thread and require no locking.
 */
struct c2_loc_thread {
	enum loc_thread_state   lt_state;
	struct c2_thread        lt_thread;
	struct c2_tlink         lt_linkage;
	struct c2_fom_locality *lt_loc;
	struct c2_clink         lt_clink;
	uint64_t                lt_magix;
};

C2_TL_DESCR_DEFINE(thr, "fom thread", static, struct c2_loc_thread, lt_linkage,
		   lt_magix, C2_FOM_THREAD_MAGIC, C2_FOM_THREAD_HEAD_MAGIC);
C2_TL_DEFINE(thr, static, struct c2_loc_thread);

static bool fom_wait_time_is_out(const struct c2_fom_domain *dom,
                                 const struct c2_fom *fom);
static int loc_thr_create(struct c2_fom_locality *loc);
static void fom_ast_cb(struct c2_sm_group *grp, struct c2_sm_ast *ast);

/**
 * Fom domain operations.
 * @todo Support fom timeout functionality.
 */
static struct c2_fom_domain_ops c2_fom_dom_ops = {
	.fdo_time_is_out = fom_wait_time_is_out
};

static void group_lock(struct c2_fom_locality *loc)
{
	c2_sm_group_lock(&loc->fl_group);
}

static void group_unlock(struct c2_fom_locality *loc)
{
	c2_sm_group_unlock(&loc->fl_group);
}

bool c2_fom_group_is_locked(const struct c2_fom *fom)
{
	return c2_mutex_is_locked(&fom->fo_loc->fl_group.s_lock);
}

static bool is_in_runq(const struct c2_fom *fom)
{
	return c2_list_contains(&fom->fo_loc->fl_runq, &fom->fo_linkage);
}

static bool is_in_wail(const struct c2_fom *fom)
{
	return c2_list_contains(&fom->fo_loc->fl_wail, &fom->fo_linkage);
}

static bool thread_invariant(const struct c2_loc_thread *t)
{
	struct c2_fom_locality *loc = t->lt_loc;

	return
		C2_IN(t->lt_state, (HANDLER, BLOCKED, UNBLOCKING, IDLE)) &&
		(loc->fl_handler == t) == (t->lt_state == HANDLER) &&
		ergo(t->lt_state == UNBLOCKING,
		     c2_atomic64_get(&loc->fl_unblocking) > 0);
}

bool c2_fom_domain_invariant(const struct c2_fom_domain *dom)
{
	return dom != NULL && dom->fd_localities != NULL &&
		dom->fd_ops != NULL;
}

bool c2_locality_invariant(const struct c2_fom_locality *loc)
{
	return	loc != NULL && loc->fl_dom != NULL &&
		c2_mutex_is_locked(&loc->fl_group.s_lock) &&
		c2_list_invariant(&loc->fl_runq) &&
		c2_list_invariant(&loc->fl_wail) &&
		c2_tl_forall(thr, t, &loc->fl_threads,
			     t->lt_loc == loc && thread_invariant(t)) &&
		ergo(loc->fl_handler != NULL,
		     thr_tlist_contains(&loc->fl_threads, loc->fl_handler));

}

struct c2_reqh *c2_fom_reqh(const struct c2_fom *fom)
{
	return fom->fo_loc->fl_dom->fd_reqh;
}

static inline enum c2_fom_state fom_state(const struct c2_fom *fom)
{
	return fom->fo_sm_state.sm_state;
}

static inline void fom_state_set(struct c2_fom *fom, enum c2_fom_state state)
{
	c2_sm_state_set(&fom->fo_sm_state, state);
}

static bool fom_is_blocked(const struct c2_fom *fom)
{
	return
		fom_state(fom) == C2_FOS_RUNNING &&
		C2_IN(fom->fo_thread->lt_state, (BLOCKED, UNBLOCKING));
}

/* Returns fom from state machine c2_fom::fo_sm_state */
static inline struct c2_fom *sm2fom(struct c2_sm *sm)
{
	return container_of(sm, struct c2_fom, fo_sm_state);
}

bool c2_fom_invariant(const struct c2_fom *fom)
{
	return
		fom != NULL && fom->fo_loc != NULL &&
		fom->fo_type != NULL && fom->fo_ops != NULL &&

		c2_fom_group_is_locked(fom) &&
		c2_list_link_invariant(&fom->fo_linkage) &&

		C2_IN(fom_state(fom), (C2_FOS_READY, C2_FOS_WAITING,
				       C2_FOS_RUNNING, C2_FOS_INIT)) &&
		(fom_state(fom) == C2_FOS_READY) == is_in_runq(fom) &&
		(fom_state(fom) == C2_FOS_WAITING) == is_in_wail(fom) &&
		ergo(fom->fo_thread != NULL, fom_state(fom) == C2_FOS_RUNNING) &&
		ergo(fom->fo_pending != NULL,
		     (fom_state(fom) == C2_FOS_READY || fom_is_blocked(fom))) &&
		ergo(fom->fo_cb.fc_state != C2_FCS_DONE,
		     fom_state(fom) == C2_FOS_WAITING);
}

static bool fom_wait_time_is_out(const struct c2_fom_domain *dom,
                                 const struct c2_fom *fom)
{
	return false;
}

/**
 * Enqueues fom into locality runq list and increments
 * number of items in runq, c2_fom_locality::fl_runq_nr.
 * This function is invoked when a new fom is submitted for
 * execution or a waiting fom is re-scheduled for processing.
 *
 * @post c2_fom_invariant(fom)
 */
static void fom_ready(struct c2_fom *fom)
{
	struct c2_fom_locality *loc;
	bool                    empty;

	fom_state_set(fom, C2_FOS_READY);
	loc = fom->fo_loc;
	empty = c2_list_is_empty(&loc->fl_runq);
	c2_list_add_tail(&loc->fl_runq, &fom->fo_linkage);
	C2_CNT_INC(loc->fl_runq_nr);
	if (empty)
		c2_chan_signal(&loc->fl_runrun);
	C2_POST(c2_fom_invariant(fom));
}

void c2_fom_ready(struct c2_fom *fom)
{
	C2_PRE(c2_fom_invariant(fom));

	c2_list_del(&fom->fo_linkage);
	C2_CNT_DEC(fom->fo_loc->fl_wail_nr);
	fom_ready(fom);
}

static void readyit(struct c2_sm_group *grp, struct c2_sm_ast *ast)
{
	struct c2_fom *fom = container_of(ast, struct c2_fom, fo_cb.fc_ast);

	c2_fom_ready(fom);
}

static void queueit(struct c2_sm_group *grp, struct c2_sm_ast *ast)
{
	struct c2_fom *fom = container_of(ast, struct c2_fom, fo_cb.fc_ast);

	C2_PRE(c2_fom_invariant(fom));
	C2_PRE(c2_fom_phase(fom) == C2_FOM_PHASE_INIT);

	fom_ready(fom);
}

void c2_fom_wakeup(struct c2_fom *fom)
{
	fom->fo_cb.fc_ast.sa_cb = readyit;
	c2_sm_ast_post(&fom->fo_loc->fl_group, &fom->fo_cb.fc_ast);
}

void c2_fom_block_enter(struct c2_fom *fom)
{
	struct c2_fom_locality *loc;
	struct c2_loc_thread   *thr;

	C2_PRE(c2_fom_invariant(fom));
	C2_PRE(fom_state(fom) == C2_FOS_RUNNING);
	C2_PRE(!fom_is_blocked(fom));

	loc = fom->fo_loc;
	thr = fom->fo_thread;

	C2_PRE(thr->lt_state == HANDLER);
	C2_PRE(thr == loc->fl_handler);

	/*
	 * If there are unblocking threads, trying to complete
	 * c2_fom_block_leave() call, do nothing and release the group lock. One
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
	if (c2_atomic64_get(&loc->fl_unblocking) == 0) {
		if (!c2_chan_has_waiters(&loc->fl_idle))
			loc_thr_create(loc);
		c2_chan_signal(&loc->fl_idle);
	}

	thr->lt_state = BLOCKED;
	loc->fl_handler = NULL;
	C2_ASSERT(c2_locality_invariant(loc));
	group_unlock(loc);
}

void c2_fom_block_leave(struct c2_fom *fom)
{
	struct c2_fom_locality *loc;
	struct c2_loc_thread   *thr;

	loc = fom->fo_loc;
	thr = fom->fo_thread;

	C2_PRE(thr->lt_state == BLOCKED);
	thr->lt_state = UNBLOCKING;
	/*
	 * Signal the handler that there is a thread that wants to unblock, just
	 * in case the handler is sleeping on empty runqueue.
	 *
	 * It is enough to do this only when loc->fl_unblocking increments from
	 * 0 to 1, because the handler won't go to sleep until
	 * loc->fl_unblocking drops to 0.
	 */
	if (c2_atomic64_add_return(&loc->fl_unblocking, 1) == 1)
		c2_chan_signal(&loc->fl_runrun);
	group_lock(loc);
	C2_ASSERT(c2_locality_invariant(loc));
	C2_ASSERT(fom_is_blocked(fom));
	C2_ASSERT(loc->fl_handler == NULL);
	loc->fl_handler = thr;
	thr->lt_state = HANDLER;
	c2_atomic64_dec(&loc->fl_unblocking);
	C2_ASSERT(c2_locality_invariant(loc));
}

void c2_fom_queue(struct c2_fom *fom, struct c2_reqh *reqh)
{
	struct c2_fom_domain		  *dom;
	const struct c2_reqh_service_type *stype;
	size_t				   loc_idx;

	C2_PRE(reqh != NULL);
	C2_PRE(fom != NULL);

	stype = fom->fo_type->ft_rstype;
	if (stype != NULL) {
		fom->fo_service = c2_reqh_service_find(stype, reqh);
		C2_ASSERT(fom->fo_service != NULL);
	}

	dom = &reqh->rh_fom_dom;
	loc_idx = fom->fo_ops->fo_home_locality(fom) %
		dom->fd_localities_nr;
	C2_ASSERT(loc_idx >= 0 && loc_idx < dom->fd_localities_nr);
	fom->fo_loc = &reqh->rh_fom_dom.fd_localities[loc_idx];
	C2_CNT_INC(fom->fo_loc->fl_foms);
	c2_fom_sm_init(fom);
	fom->fo_cb.fc_ast.sa_cb = queueit;
	c2_sm_ast_post(&fom->fo_loc->fl_group, &fom->fo_cb.fc_ast);
}

/**
 * Puts fom on locality wait list if fom performs a blocking operation, this
 * releases the handler thread to start executing another fom from the runq,
 * thus making the reqh non blocking.
 *
 * Fom state is changed to C2_FOS_WAITING.  c2_fom_locality::fl_group.s_lock
 * should be held before putting fom on the locality wait list.
 *
 * This function is invoked from fom_exec(), if the fom is performing a blocking
 * operation and c2_fom_ops::fo_tick() returns C2_FSO_WAIT.
 *
 * @post c2_fom_invariant(fom)
 */
static void fom_wait(struct c2_fom *fom)
{
	struct c2_fom_locality *loc;

	fom_state_set(fom, C2_FOS_WAITING);
	loc = fom->fo_loc;
	c2_list_add_tail(&loc->fl_wail, &fom->fo_linkage);
	C2_CNT_INC(loc->fl_wail_nr);
	C2_POST(c2_fom_invariant(fom));
}

/**
 * Helper to execute the bottom half of a fom call-back.
 */
static void cb_run(struct c2_fom_callback *cb)
{
	C2_PRE(cb->fc_state == C2_FCS_TOP_DONE);
	C2_PRE(c2_fom_invariant(cb->fc_fom));

	cb->fc_state = C2_FCS_DONE;
	cb->fc_bottom(cb);
	c2_clink_del(&cb->fc_clink);
	c2_clink_fini(&cb->fc_clink);
}

/**
 * Invokes fom phase transition method, which transitions fom
 * through various phases of its execution without blocking.
 * @post c2_fom_invariant(fom)
 */
static void fom_exec(struct c2_fom *fom)
{
	int			rc;
	struct c2_fom_locality *loc;


	loc = fom->fo_loc;
	fom->fo_thread = loc->fl_handler;
	fom_state_set(fom, C2_FOS_RUNNING);
	do {
		C2_ASSERT(c2_fom_invariant(fom));
		rc = fom->fo_ops->fo_tick(fom);
		/*
		 * (rc == C2_FSO_AGAIN) means that next phase transition is
		 * possible. Current policy is to execute the transition
		 * immediately. Alternative is to put the fom on the runqueue
		 * and select "the best" fom from the runqueue.
		 */
		fom->fo_transitions++;
	} while (rc == C2_FSO_AGAIN);

	fom->fo_thread = NULL;

	C2_ASSERT(rc == C2_FSO_WAIT);
	C2_ASSERT(c2_fom_group_is_locked(fom));

	if (c2_fom_phase(fom) == C2_FOM_PHASE_FINISH) {
		fom->fo_ops->fo_fini(fom);
		/*
		 * Don't touch the fom after this point.
		 */
	} else {
		struct c2_fom_callback *cb;

		fom_wait(fom);
		/*
		 * If there are pending call-backs, execute them, until one of
		 * them wakes the fom up. Don't bother to optimize moving
		 * between queues: this is a rare case.
		 *
		 * Note: call-backs are executed in LIFO order.
		 */
		while ((cb = fom->fo_pending) != NULL) {
			fom->fo_pending = (void *)cb->fc_ast.sa_next;
			cb_run(cb);
			/*
			 * call-back is not allowed to destroy a fom.
			 */
			C2_ASSERT(c2_fom_phase(fom) != C2_FOM_PHASE_FINISH);
			if (fom_state(fom) != C2_FOS_WAITING)
				break;
		}
		C2_ASSERT(c2_fom_invariant(fom));
	}
}

/**
 * Dequeues a fom from runq list of the locality.
 *
 * @retval c2_fom if queue is not empty, NULL otherwise
 */
static struct c2_fom *fom_dequeue(struct c2_fom_locality *loc)
{
	struct c2_list_link	*fom_link;
	struct c2_fom		*fom = NULL;

	fom_link = c2_list_first(&loc->fl_runq);
	if (fom_link != NULL) {
		c2_list_del(fom_link);
		fom = container_of(fom_link, struct c2_fom, fo_linkage);
		C2_CNT_DEC(loc->fl_runq_nr);
	}

	return fom;
}

/**
 * Locality handler thread. See the "Locality internals" section.
 */
static void loc_handler_thread(struct c2_loc_thread *th)
{
	c2_time_t		delta;
	struct c2_clink	       *clink = &th->lt_clink;
	struct c2_fom_locality *loc   = th->lt_loc;

	c2_time_set(&delta, LOC_HT_WAIT, 0);

	while (1) {
		/*
		 * start idle, wait for work to do. The clink was registered
		 * with &loc->fl_idle by loc_thr_create().
		 */
		C2_ASSERT(th->lt_state == IDLE);
		c2_chan_wait(clink);

		/* become the handler thread */
		group_lock(loc);
		C2_ASSERT(loc->fl_handler == NULL);
		loc->fl_handler = th;
		th->lt_state = HANDLER;

		/*
		 * re-initialise the clink and arrange for it to receive group
		 * AST notifications and runrun wakeups.
		 */
		c2_clink_del(clink);
		c2_clink_fini(clink);
		c2_clink_init(clink, NULL);
		c2_clink_attach(clink, &loc->fl_group.s_clink, NULL);
		c2_clink_add(&loc->fl_runrun, clink);

		/*
		 * main handler loop.
		 *
		 * This loop terminates when the locality is finalised
		 * (loc->fl_shutdown) or this thread should go back to the idle
		 * state.
		 */
		while (1) {
			struct c2_fom *fom;

			C2_ASSERT(c2_locality_invariant(loc));
			/*
			 * Check for a blocked thread that tries to unblock and
			 * complete a phase transition.
			 */
			if (c2_atomic64_get(&loc->fl_unblocking) > 0)
				/*
				 * Idle ourselves. The unblocking thread (first
				 * to grab the group lock in case there are
				 * many), becomes the new handler.
				 */
				break;
			c2_sm_asts_run(&loc->fl_group);
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
				c2_chan_timedwait(clink,
						  c2_time_add(c2_time_now(),
							      delta));
		}
		loc->fl_handler = NULL;
		th->lt_state = IDLE;
		c2_clink_del(clink);
		c2_clink_fini(clink);
		group_unlock(loc);
		c2_clink_init(&th->lt_clink, NULL);
		c2_clink_add(&loc->fl_idle, &th->lt_clink);
		if (loc->fl_shutdown)
			break;
	}
}

/**
 * Init function for a locality thread. Confines the thread to the locality
 * core.
 */
static int loc_thr_init(struct c2_loc_thread *th)
{
	return c2_thread_confine(&th->lt_thread, &th->lt_loc->fl_processors);
}

static void loc_thr_fini(struct c2_loc_thread *th)
{
	C2_PRE(c2_mutex_is_locked(&th->lt_loc->fl_group.s_lock));
	C2_PRE(th->lt_state == IDLE);
	c2_clink_del(&th->lt_clink);
	c2_clink_fini(&th->lt_clink);

	c2_thread_fini(&th->lt_thread);
	thr_tlink_del_fini(th);
	c2_free(th);
}

static int loc_thr_create(struct c2_fom_locality *loc)
{
	struct c2_loc_thread *thr;
	int                   result;

	C2_PRE(c2_mutex_is_locked(&loc->fl_group.s_lock));

	C2_ALLOC_PTR_ADDB(thr, &loc->fl_dom->fd_addb_ctx, &c2_fom_addb_loc);
	if (thr == NULL)
		return -ENOMEM;
	thr->lt_state = IDLE;
	thr->lt_magix = C2_FOM_THREAD_MAGIC;
	thr->lt_loc   = loc;
	thr_tlink_init_at_tail(thr, &loc->fl_threads);

	c2_clink_init(&thr->lt_clink, NULL);
	c2_clink_add(&loc->fl_idle, &thr->lt_clink);

	result = C2_THREAD_INIT(&thr->lt_thread, struct c2_loc_thread *,
				loc_thr_init, &loc_handler_thread, thr,
				"loc thread");
	if (result != 0)
		loc_thr_fini(thr);
	return result;
}

/**
 * Finalises a given locality.
 */
static void loc_fini(struct c2_fom_locality *loc)
{
	struct c2_loc_thread *th;

	loc->fl_shutdown = true;
	c2_chan_broadcast(&loc->fl_runrun);
	c2_chan_broadcast(&loc->fl_idle);
	group_lock(loc);
	while ((th = thr_tlist_head(&loc->fl_threads)) != NULL) {
		group_unlock(loc);
		c2_thread_join(&th->lt_thread);
		group_lock(loc);
		loc_thr_fini(th);
	}
	group_unlock(loc);

	c2_list_fini(&loc->fl_runq);
	C2_ASSERT(loc->fl_runq_nr == 0);

	c2_list_fini(&loc->fl_wail);
	C2_ASSERT(loc->fl_wail_nr == 0);

	c2_sm_group_fini(&loc->fl_group);
	c2_chan_fini(&loc->fl_runrun);
	thr_tlist_fini(&loc->fl_threads);
	C2_ASSERT(c2_atomic64_get(&loc->fl_unblocking) == 0);
	c2_chan_fini(&loc->fl_idle);

	c2_bitmap_fini(&loc->fl_processors);
}

/**
 * Initialises a locality in fom domain.  Creates and adds threads to locality,
 * every thread is confined to the cpus represented by the
 * c2_fom_locality::fl_processors, this is done in the locality thread init
 * function (loc_thr_init()).
 *
 * A pool of LOC_IDLE_NR idle threads is created together with a handler thread.
 *
 * @see loc_thr_create()
 * @see loc_thr_init()
 *
 * @param loc     c2_fom_locality to be initialised
 * @param cpu     cpu assigned to the locality
 * @param cpu_max maximum number of cpus that can be present in a locality
 */
static int loc_init(struct c2_fom_locality *loc, size_t cpu, size_t cpu_max)
{
	int result;

	C2_PRE(loc != NULL);

	c2_list_init(&loc->fl_runq);
	loc->fl_runq_nr = 0;

	c2_list_init(&loc->fl_wail);
	loc->fl_wail_nr = 0;

	c2_sm_group_init(&loc->fl_group);
	c2_chan_init(&loc->fl_runrun);
	thr_tlist_init(&loc->fl_threads);
	c2_atomic64_set(&loc->fl_unblocking, 0);
	c2_chan_init(&loc->fl_idle);

	result = c2_bitmap_init(&loc->fl_processors, cpu_max);

	if (result == 0) {
		int i;

		c2_bitmap_set(&loc->fl_processors, cpu, true);
		/* create a pool of idle threads plus the handler thread. */
		group_lock(loc);
		for (i = 0; i < LOC_IDLE_NR + 1; ++i) {
			result = loc_thr_create(loc);
			if (result != 0)
				break;
		}
		/* wake up one idle thread. It becomes the handler thread. */
		c2_chan_signal(&loc->fl_idle);
		group_unlock(loc);
	}
	if (result != 0)
		loc_fini(loc);

	return result;
}

int c2_fom_domain_init(struct c2_fom_domain *dom)
{
	int                     result;
	size_t                  cpu;
	size_t                  cpu_max;
	struct c2_fom_locality *localities;
	struct c2_bitmap        onln_cpu_map;


	C2_PRE(dom != NULL);

	cpu_max = c2_processor_nr_max();
	dom->fd_ops = &c2_fom_dom_ops;
	result = c2_bitmap_init(&onln_cpu_map, cpu_max);
	if (result != 0)
		return result;

	c2_processors_online(&onln_cpu_map);
        c2_addb_ctx_init(&dom->fd_addb_ctx, &c2_fom_addb_ctx_type,
			 &c2_addb_global_ctx);
	C2_ALLOC_ARR_ADDB(dom->fd_localities, cpu_max, &dom->fd_addb_ctx,
			  &c2_fom_addb_loc);
	if (dom->fd_localities == NULL) {
		c2_addb_ctx_fini(&dom->fd_addb_ctx);
		c2_bitmap_fini(&onln_cpu_map);
		return -ENOMEM;
	}

	localities = dom->fd_localities;
	for (cpu = 0; cpu < cpu_max; ++cpu) {
		if (!c2_bitmap_get(&onln_cpu_map, cpu))
			continue;
		localities[dom->fd_localities_nr].fl_dom = dom;
		result = loc_init(&localities[dom->fd_localities_nr], cpu,
				  cpu_max);
		if (result != 0) {
			c2_fom_domain_fini(dom);
			break;
		}
		C2_CNT_INC(dom->fd_localities_nr);
	}
	c2_bitmap_fini(&onln_cpu_map);

	return result;
}

void c2_fom_domain_fini(struct c2_fom_domain *dom)
{
	int fd_loc_nr;

	C2_ASSERT(c2_fom_domain_invariant(dom));

	fd_loc_nr = dom->fd_localities_nr;
	while (fd_loc_nr > 0) {
		loc_fini(&dom->fd_localities[fd_loc_nr - 1]);
		--fd_loc_nr;
	}

	c2_addb_ctx_fini(&dom->fd_addb_ctx);
	c2_free(dom->fd_localities);
}

bool c2_fom_domain_is_idle(const struct c2_fom_domain *dom)
{
	return c2_forall(i, dom->fd_localities_nr,
			 dom->fd_localities[i].fl_foms == 0);
}

void c2_fom_fini(struct c2_fom *fom)
{
	struct c2_fom_domain   *fdom;
	struct c2_fom_locality *loc;
	struct c2_reqh         *reqh;

	C2_PRE(c2_fom_phase(fom) == C2_FOM_PHASE_FINISH);

	loc  = fom->fo_loc;
	fdom = loc->fl_dom;
	reqh = fdom->fd_reqh;
	fom_state_set(fom, C2_FOS_FINISH);
	c2_sm_fini(&fom->fo_sm_phase);
	c2_sm_fini(&fom->fo_sm_state);
	c2_list_link_fini(&fom->fo_linkage);
	c2_fom_callback_init(&fom->fo_cb);
	C2_CNT_DEC(loc->fl_foms);
	if (loc->fl_foms == 0)
		c2_chan_signal(&reqh->rh_sd_signal);
}
C2_EXPORTED(c2_fom_fini);

void c2_fom_init(struct c2_fom *fom, struct c2_fom_type *fom_type,
		 const struct c2_fom_ops *ops, struct c2_fop *fop,
		 struct c2_fop *reply)
{
	C2_PRE(fom != NULL);

	fom->fo_type	= fom_type;
	fom->fo_ops	= ops;
	fom->fo_fop	= fop;
	fom->fo_rep_fop = reply;
	c2_fom_callback_init(&fom->fo_cb);
	c2_list_link_init(&fom->fo_linkage);

	fom->fo_transitions = 0;
}
C2_EXPORTED(c2_fom_init);

static bool fom_clink_cb(struct c2_clink *link)
{
	struct c2_fom_callback *cb = container_of(link, struct c2_fom_callback,
	                                          fc_clink);
	C2_PRE(cb->fc_state >= C2_FCS_ARMED);

	if (c2_atomic64_cas(&cb->fc_state, C2_FCS_ARMED, C2_FCS_TOP_DONE) &&
	    (cb->fc_top == NULL || !cb->fc_top(cb)))
		c2_sm_ast_post(&cb->fc_fom->fo_loc->fl_group, &cb->fc_ast);

	return true;
}

static void fom_ast_cb(struct c2_sm_group *grp, struct c2_sm_ast *ast)
{
	struct c2_fom_callback *cb  = container_of(ast, struct c2_fom_callback,
						   fc_ast);
	struct c2_fom          *fom = cb->fc_fom;

	C2_PRE(c2_fom_invariant(fom));
	/*
	 * there is no need to use CAS here, because this place is only reached
	 * if CAS in fom_clink_cb() was successful and, hence, races with
	 * c2_fom_callback_cancel() are no longer possible.
	 */
	C2_PRE(cb->fc_state == C2_FCS_TOP_DONE);

	if (fom_state(fom) == C2_FOS_WAITING)
		cb_run(cb);
	else {
		C2_ASSERT(fom_state(fom) == C2_FOS_READY || fom_is_blocked(fom));
		/*
		 * Call-back arrived while our fom is in READY state (hanging on
		 * the runqueue, waiting for its turn) or RUNNING state (blocked
		 * between c2_fom_block_enter() and
		 * c2_fom_block_leave()). Instead of executing the call-back
		 * immediately, add it to the stack of pending call-backs for
		 * this fom. The call-back will be executed by fom_exec() when
		 * the fom is about to return to the WAITING state.
		 */
		cb->fc_ast.sa_next = (void *)fom->fo_pending;
		fom->fo_pending = cb;
	}
}

void c2_fom_callback_init(struct c2_fom_callback *cb)
{
	cb->fc_state = C2_FCS_DONE;
}

void c2_fom_callback_arm(struct c2_fom *fom, struct c2_chan *chan,
                         struct c2_fom_callback *cb)
{
	C2_PRE(cb->fc_bottom != NULL);
	C2_PRE(cb->fc_state == C2_FCS_DONE);

	cb->fc_fom = fom;

	c2_clink_init(&cb->fc_clink, &fom_clink_cb);
	cb->fc_ast.sa_cb = &fom_ast_cb;
	cb->fc_state = C2_FCS_ARMED;
	/* XXX a memory barrier is required
	 * if c2_clink_add() doesn't do its own locking. */
	c2_clink_add(chan, &cb->fc_clink);
}

static void fom_ready_cb(struct c2_fom_callback *cb)
{
	c2_fom_ready(cb->fc_fom);
}

void c2_fom_wait_on(struct c2_fom *fom, struct c2_chan *chan,
                    struct c2_fom_callback *cb)
{
	cb->fc_bottom = fom_ready_cb;
	c2_fom_callback_arm(fom, chan, cb);
}

void c2_fom_callback_fini(struct c2_fom_callback *cb)
{
	C2_PRE(cb->fc_state == C2_FCS_DONE);
	/* c2_clink_fini() is called in cb_run() */
}

bool c2_fom_callback_cancel(struct c2_fom_callback *cb)
{
	bool result;
	C2_PRE(cb->fc_state >= C2_FCS_ARMED);

	result = c2_atomic64_cas(&cb->fc_state, C2_FCS_ARMED, C2_FCS_DONE);

	if (result) {
		c2_clink_del(&cb->fc_clink);
		c2_clink_fini(&cb->fc_clink);
	}

	return result;
}

void c2_fom_type_init(struct c2_fom_type *type,
		      const struct c2_fom_type_ops *ops,
		      const struct c2_reqh_service_type  *svc_type,
		      const struct c2_sm_conf *sm)
{
	type->ft_ops    = ops;
	type->ft_conf   = sm;
	type->ft_rstype = svc_type;
}

static const struct c2_sm_state_descr fom_states[] = {
	[C2_FOS_INIT] = {
		.sd_flags     = C2_SDF_INITIAL,
		.sd_name      = "Init",
		.sd_allowed   = C2_BITS(C2_FOS_FINISH, C2_FOS_READY)
	},
	[C2_FOS_READY] = {
		.sd_name      = "Ready",
		.sd_allowed   = C2_BITS(C2_FOS_RUNNING)
	},
	[C2_FOS_RUNNING] = {
		.sd_name      = "Running",
		.sd_allowed   = C2_BITS(C2_FOS_WAITING, C2_FOS_READY,
					C2_FOS_FINISH)
	},
	[C2_FOS_WAITING] = {
		.sd_name      = "Wait",
		.sd_allowed   = C2_BITS(C2_FOS_FINISH, C2_FOS_READY)
	},
	[C2_FOS_FINISH] = {
		.sd_flags     = C2_SDF_TERMINAL,
		.sd_name      = "Finished",
	}
};

static const struct c2_sm_conf	fom_conf = {
	.scf_name      = "FOM states",
	.scf_nr_states = ARRAY_SIZE(fom_states),
	.scf_state     = fom_states
};

void c2_fom_sm_init(struct c2_fom *fom)
{
	struct c2_sm_group	*fom_group;
	struct c2_addb_ctx	*fom_addb_ctx;
	const struct c2_sm_conf	*conf;

	C2_PRE(fom != NULL);
	C2_PRE(fom->fo_loc != NULL);

	conf = fom->fo_type->ft_conf;
	C2_ASSERT(conf->scf_nr_states != 0);

	fom_group    = &fom->fo_loc->fl_group;
	fom_addb_ctx = &fom->fo_loc->fl_dom->fd_addb_ctx;

	c2_sm_init(&fom->fo_sm_phase, conf, C2_FOM_PHASE_INIT, fom_group,
		    fom_addb_ctx);
	c2_sm_init(&fom->fo_sm_state, &fom_conf, C2_FOS_INIT, fom_group,
		    fom_addb_ctx);
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
