/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

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
#include "fop/fom.h"
#include "fop/fop.h"
#include "fop/fom_long_lock.h"
#include "reqh/reqh.h"
#include "sm/sm.h"
/**
   @addtogroup fom
   @{
 */

enum locality_ht_wait {
	LOC_HT_WAIT = 1
};

enum {
        DEFAULT_THREAD_NR = 1
};

/**
 * Fom addb event location object
 */
const struct c2_addb_loc c2_fom_addb_loc = {
	.al_name = "fom"
};

/**
 * Fom addb context state.
 */
const struct c2_addb_ctx_type c2_fom_addb_ctx_type = {
	.act_name = "fom"
};

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
		c2_list_invariant(&loc->fl_threads);
}

static inline struct c2_fom * sm2fom(const struct c2_sm *sm)
{
	C2_PRE(sm != NULL);
	return container_of(sm, struct c2_fom, fo_sm_state);
}

bool c2_fom_invariant(const struct c2_fom *fom)
{
	return
		fom != NULL && fom->fo_loc != NULL &&
		fom->fo_type != NULL && fom->fo_ops != NULL &&
		fom->fo_fop != NULL &&

		c2_fom_group_is_locked(fom) &&
		c2_list_link_invariant(&fom->fo_linkage) &&

		ergo(C2_IN(fom->fo_sm_state.sm_state, (C2_FOS_RUNNING,
						       C2_FOS_QUEUE)),
		     !is_in_runq(fom) && !is_in_wail(fom));
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
 * execution.
 *
 * @pre c2_fom_invariant(fom)
 * @param fom Ready to be executed fom, is put on locality runq
 */
static int fom_queue(struct c2_sm *sm)
{
	struct c2_fom	       *fom  = sm2fom(sm);
	struct c2_fom_locality *loc;

	C2_PRE(c2_fom_invariant(fom));
	loc = fom->fo_loc;
	c2_list_add_tail(&loc->fl_runq, &fom->fo_linkage);
	C2_CNT_INC(loc->fl_runq_nr);
	c2_chan_signal(&loc->fl_runrun);
	return -1;
}

/**
 * Enqueues fom into locality runq list and increments
 * number of items in runq, c2_fom_locality::fl_runq_nr.
 * This function is invoked when a waiting fom is re scheduled
 * for processing.
 *
 * It dequeues fom from loclaity wait queue then it enqueues
 * it into locality runq list.
 *
 * @pre c2_fom_invariant(fom)
 * @param fom Ready to be executed fom, is put on locality runq
 */

static int fom_ready(struct c2_sm *sm)
{
	struct c2_fom	       *fom = sm2fom(sm);
	struct c2_fom_locality *loc;

	C2_PRE(c2_fom_invariant(fom));

	loc = fom->fo_loc;
	c2_list_del(&fom->fo_linkage);
	C2_CNT_DEC(loc->fl_wail_nr);
	return fom_queue(sm);
}

static void queueit(struct c2_sm_group *grp, struct c2_sm_ast *ast)
{
	struct c2_fom *fom = container_of(ast, struct c2_fom, fo_cb.fc_ast);

	C2_PRE(c2_fom_invariant(fom));

	c2_sm_state_set(&fom->fo_sm_state, C2_FOS_QUEUE);
}

static void fom_queueit(struct c2_fom *fom)
{
	fom->fo_cb.fc_ast.sa_cb = queueit;
	c2_sm_ast_post(&fom->fo_loc->fl_group, &fom->fo_cb.fc_ast);
}

static void readyit(struct c2_sm_group *grp, struct c2_sm_ast *ast)
{
	struct c2_fom *fom = container_of(ast, struct c2_fom, fo_cb.fc_ast);

	C2_PRE(c2_fom_invariant(fom));

	c2_sm_state_set(&fom->fo_sm_state, C2_FOS_READY);
}

void c2_fom_wakeup(struct c2_fom *fom)
{
	fom->fo_cb.fc_ast.sa_cb = readyit;
	c2_sm_ast_post(&fom->fo_loc->fl_group, &fom->fo_cb.fc_ast);
}

void c2_fom_block_enter(struct c2_fom *fom)
{
	int			rc;
	size_t			max_idle_threads;
	struct c2_fom_locality *loc;

	loc = fom->fo_loc;

	C2_ASSERT(c2_locality_invariant(loc));
	group_unlock(loc);

	c2_mutex_lock(&loc->fl_lock);
	C2_CNT_INC(loc->fl_lo_idle_threads_nr);
	max_idle_threads = max_check(loc->fl_lo_idle_threads_nr,
				     loc->fl_hi_idle_threads_nr);
	while (loc->fl_idle_threads_nr < max_idle_threads) {
		rc = loc_thr_create(loc);
		if (rc != 0) {
			FOM_ADDB_ADD(fom, "c2_fom_block_enter", rc);
			break;
		}
	}
	c2_mutex_unlock(&loc->fl_lock);
}

void c2_fom_block_leave(struct c2_fom *fom)
{
	struct c2_fom_locality *loc;

	loc = fom->fo_loc;

	c2_mutex_lock(&loc->fl_lock);
	/* See the description for c2_fom_locality to understand why
	 * it is decremented before taking the group lock. */
	C2_CNT_DEC(loc->fl_lo_idle_threads_nr);
	c2_mutex_unlock(&loc->fl_lock);

	group_lock(loc);
	C2_ASSERT(c2_locality_invariant(loc));
}

void c2_fom_queue(struct c2_fom *fom, struct c2_reqh *reqh)
{
	struct c2_fom_domain   *dom;
	size_t			loc_idx;

	C2_PRE(reqh != NULL);
	C2_PRE(fom != NULL);

	/*
	 * To access service specific data,
	 * FOM needs pointer to service instance.
	 */
	if (fom->fo_ops->fo_service_name != NULL) {
		const char *service_name = NULL;
		service_name = fom->fo_ops->fo_service_name(fom);
		fom->fo_service = c2_reqh_service_get(service_name,
						      reqh);
	}
	fom->fo_fol = reqh->rh_fol;
	dom = &reqh->rh_fom_dom;
	c2_atomic64_inc(&dom->fd_foms_nr);
	loc_idx = fom->fo_ops->fo_home_locality(fom) %
		dom->fd_localities_nr;
	C2_ASSERT(loc_idx >= 0 && loc_idx < dom->fd_localities_nr);
	fom->fo_loc = &reqh->rh_fom_dom.fd_localities[loc_idx];
	c2_fom_sm_init(fom);
	fom_queueit(fom);
}

/**
 * Puts fom on locality wait list if fom performs a blocking
 * operation, this releases the current thread to start
 * executing another fom from the runq, thus making the reqh
 * non blocking.
 * Fom state is changed to C2_FOS_WAITING.
 * c2_fom_locality::fl_group.s_lock should be held before putting
 * fom on the locality wait list.
 * This function is invoked from fom_exec(), if the fom
 * is performing a blocking operation and c2_fom::fo_state()
 * returns C2_FSO_WAIT.
 *
 * @pre c2_fom_invariant(fom)
 * @param fom A fom blocking on an operation that is to be
 *		put on the locality wait list
 */
static int fom_wait(struct c2_sm *sm)
{
	struct c2_fom	       *fom = sm2fom(sm);
	struct c2_fom_locality *loc;

	C2_PRE(c2_fom_invariant(fom));
	loc = fom->fo_loc;
	c2_list_add_tail(&loc->fl_wail, &fom->fo_linkage);
	C2_CNT_INC(loc->fl_wail_nr);
	return -1;
}

/**
 * Invokes fom state transition method, which transitions fom
 * through various phases of its execution without blocking.
 * Fom state method is executed until it returns C2_FSO_WAIT,
 * indicating fom has either completed its execution or is
 * going to block on an operation.
 * If a fom needs to block on an operation, the state transition
 * function should register the AST callback (by calling
 * c2_fom_callback_arm() or c2_fom_wake_on()) with the channel
 * where the completion event will be signalled. The callback
 * should wake up the fom with fom_ready().
 * If the state method returns C2_FSO_WAIT, and fom has not yet
 * finished its execution, then it is put on the locality wait list.
 *
 * @see c2_fom_state_outcome
 * @see c2_fom_wake_on()
 * @see c2_fom_callback_arm()
 *
 * @param fom A fom under execution
 */
static int fom_exec(struct c2_sm *sm)
{
	int			rc;
	struct c2_fom_locality *loc;
	struct c2_fom	       *fom = sm2fom(sm);

	loc = fom->fo_loc;

	do {
		C2_ASSERT(c2_fom_invariant(fom));
		rc = fom->fo_ops->fo_state(fom);
		fom->fo_transitions++;
	} while (rc == C2_FSO_AGAIN);

	C2_ASSERT(rc == C2_FSO_WAIT);
	C2_ASSERT(c2_fom_group_is_locked(fom));

	if (fom->fo_phase == C2_FOPH_FINISH)
		return -1;
	C2_POST(c2_fom_invariant(fom));
	return C2_FOS_WAITING;
}

/**
 * Dequeues a fom from runq list of the locality.
 *
 * @param loc Locality assigned for fom execution
 *
 * @retval c2_fom if succeeds
 *	else returns NULL
 */
static struct c2_fom *fom_dequeue(struct c2_fom_locality *loc)
{
	struct c2_list_link	*fom_link;
	struct c2_fom		*fom = NULL;

	fom_link = c2_list_first(&loc->fl_runq);
	if (fom_link != NULL) {
		c2_list_del(fom_link);
		fom = container_of(fom_link, struct c2_fom, fo_linkage);
		C2_ASSERT(fom != NULL);
		C2_CNT_DEC(loc->fl_runq_nr);
	}

	return fom;
}

/**
 * Locality handler thread.
 * Handler thread waits on re-scheduling channel for specific time.
 * Thread is then woken up either by fom enqueue operation in a locality runq
 * list, or if thread times out waiting on the channel.
 * When woken up, thread dequeue's a fom from the locality runq list and starts
 * executing it. If number of idle threads are higher than threshold value, they
 * are terminated.
 *
 * @see c2_fom_locality::fl_runrun
 * @see c2_fom_locality::fl_lo_idle_threads_nr
 *
 * @param th c2_fom_hthread, contains thread reference, locality reference
 *		and thread linkage in locality
 *
 * @pre th != NULL
 */
static void loc_handler_thread(struct c2_fom_hthread *th)
{
	c2_time_t		delta;
	bool			idle;
	struct c2_clink		th_clink;
	struct c2_fom_locality *loc;

	C2_PRE(th != NULL);
	loc = th->fht_locality;
	idle = true;
	c2_time_set(&delta, LOC_HT_WAIT, 0);
	c2_clink_init(&th_clink, NULL);
	c2_clink_attach(&th_clink, &loc->fl_group.s_clink, NULL);

	group_lock(loc);
	c2_clink_add(&loc->fl_runrun, &th_clink);

	while (1) {
		struct c2_fom *fom;

		C2_ASSERT(c2_locality_invariant(loc));

		fom = fom_dequeue(loc);
		if (fom != NULL) {
			if (idle) {
				C2_CNT_DEC(loc->fl_idle_threads_nr);
				idle = false;
			}
			c2_sm_state_set(&fom->fo_sm_state, C2_FOS_RUNNING);
			if (fom->fo_phase == C2_FOPH_FINISH) {
				c2_sm_state_set(&fom->fo_sm_phase,
						C2_FOPH_SM_FINISH);
				c2_sm_state_set(&fom->fo_sm_state,
						C2_FOS_SM_FINISH);
				c2_sm_fini(&fom->fo_sm_phase);
				c2_sm_fini(&fom->fo_sm_state);
				fom->fo_ops->fo_fini(fom);
			}
		} else {
			if (!idle) {
				C2_CNT_INC(loc->fl_idle_threads_nr);
				idle = true;
			}
		}
		if (idle) {
			group_unlock(loc);
			c2_chan_timedwait(&th_clink,
			                  c2_time_add(c2_time_now(), delta));
			group_lock(loc);
		}
		if (loc->fl_idle_threads_nr > loc->fl_hi_idle_threads_nr)
			break;
		if (loc->fl_idle_threads_nr > loc->fl_lo_idle_threads_nr) {
			/*
			 * Get the other thread waiting in c2_fom_block_leave()
			 * a chance to complete state transition.
			 */
			group_unlock(loc);
			group_lock(loc);
		}
		c2_sm_asts_run(&loc->fl_group);
	}

	if (idle)
		C2_CNT_DEC(loc->fl_idle_threads_nr);
	C2_CNT_DEC(loc->fl_threads_nr);
	c2_clink_del(&th_clink);
	group_unlock(loc);
	c2_clink_fini(&th_clink);
}

/**
 * Init function for a reqh worker thread.
 * Adds thread to the locality thread list and
 * increments thread count in locality atomically.
 *
 * @param th c2_fom_hthread, contains thread reference, locality reference
 *		and thread linkage in locality
 */
static int loc_thr_init(struct c2_fom_hthread *th)
{
	struct c2_fom_locality *loc;
	int                     rc;

	loc = th->fht_locality;
	C2_ASSERT(loc != NULL);

	rc = c2_thread_confine(&th->fht_thread, &loc->fl_processors);
	if (rc == 0) {
		group_lock(loc);
		c2_list_add_tail(&loc->fl_threads, &th->fht_linkage);
		group_unlock(loc);
		c2_mutex_lock(&loc->fl_lock);
		C2_CNT_INC(loc->fl_threads_nr);
		C2_CNT_INC(loc->fl_idle_threads_nr);
		c2_mutex_unlock(&loc->fl_lock);
	}

	return rc;
}

/**
 * Creates and adds a request handler worker thread to the given
 * locality.
 *
 * @see loc_thr_init()
 *
 * @param loc Locality in which the threads are added
 *
 * @retval 0 If a thread is successfully created and
 *		added to the given locality
 *	-errno on failure
 *
 * @pre loc != NULL
 */
static int loc_thr_create(struct c2_fom_locality *loc)
{
	int			result;
	struct c2_fom_hthread  *locthr;
	struct c2_addb_ctx     *fom_addb_ctx;

	C2_PRE(loc != NULL);

	c2_mutex_unlock(&loc->fl_lock);

	fom_addb_ctx = &loc->fl_dom->fd_addb_ctx;
	C2_ALLOC_PTR_ADDB(locthr, fom_addb_ctx, &c2_fom_addb_loc);
	if (locthr == NULL) {
		c2_mutex_lock(&loc->fl_lock);
		return -ENOMEM;
	}
	/*
	 * Initialize the linkage here so that c2_list_del() below can be safely
	 * called even if thread creation failed before adding thread to the
	 * list.
	 */
	c2_list_link_init(&locthr->fht_linkage);
	locthr->fht_locality = loc;
	result = C2_THREAD_INIT(&locthr->fht_thread, struct c2_fom_hthread *,
			loc_thr_init, &loc_handler_thread, locthr,
			"locality_thread");

	c2_mutex_lock(&loc->fl_lock);
	if (result != 0) {
		c2_list_del(&locthr->fht_linkage);
		c2_free(locthr);
	}

	return result;
}

/**
 * Finalises a given locality.
 * Sets c2_fom_locality::fl_lo_idle_threads_nr to 0, and
 * iterates over the c2_fom_locality::fl_threads list.
 * Extracts a thread, and waits until the thread exits,
 * finalises each thread. After removing all the threads
 * from the locality, other locality members are finalised.
 *
 * @param loc Locality to be finalised
 *
 * @pre loc != NULL
 */
static void loc_fini(struct c2_fom_locality *loc)
{
	struct c2_list_link	*link;
	struct c2_fom_hthread	*th;

	C2_PRE(loc != NULL);

	group_lock(loc);
	C2_ASSERT(c2_locality_invariant(loc));
	loc->fl_hi_idle_threads_nr = loc->fl_lo_idle_threads_nr = 0;
	c2_chan_broadcast(&loc->fl_runrun);
	while (!c2_list_is_empty(&loc->fl_threads)) {

		link = c2_list_first(&loc->fl_threads);
		C2_ASSERT(link != NULL);
		c2_list_del(link);
		group_unlock(loc);
		th = container_of(link, struct c2_fom_hthread, fht_linkage);
		C2_ASSERT(th != NULL);
		c2_thread_join(&th->fht_thread);
		c2_thread_fini(&th->fht_thread);
		c2_free(th);
		group_lock(loc);
	}
	group_unlock(loc);

	c2_list_fini(&loc->fl_runq);
	C2_ASSERT(loc->fl_runq_nr == 0);

	c2_list_fini(&loc->fl_wail);
	C2_ASSERT(loc->fl_wail_nr == 0);

	c2_sm_group_fini(&loc->fl_group);
	c2_mutex_fini(&loc->fl_lock);
	c2_chan_fini(&loc->fl_runrun);
	c2_list_fini(&loc->fl_threads);
	C2_ASSERT(loc->fl_threads_nr == 0);

	c2_bitmap_fini(&loc->fl_processors);
}

/**
 * Initialises a locality in fom domain.
 * Creates and adds threads to locality, every thread is confined to the cpus
 * represented by the c2_fom_locality::fl_processors, this is done in the
 * locality thread init function. Number of threads in the locality corresponds
 * to the number of cpus represented by c2_fom_locality::fl_processors.
 *
 * @see loc_thr_create()
 * @see loc_thr_init()
 *
 * @param loc     c2_fom_locality to be initialised
 * @param cpu     cpu assigned to the locality
 * @param cpu_max maximum number of cpus that can be present in a locality
 *
 * @retval 0 If locality is initialised
 *	-errno on failure
 *
 * @pre loc != NULL
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
	c2_mutex_init(&loc->fl_lock);
	c2_chan_init(&loc->fl_runrun);
	c2_list_init(&loc->fl_threads);

	result = c2_bitmap_init(&loc->fl_processors, cpu_max);

	if (result == 0) {
		c2_bitmap_set(&loc->fl_processors, cpu, true);
		loc->fl_lo_idle_threads_nr = loc->fl_hi_idle_threads_nr =
					     DEFAULT_THREAD_NR;
		c2_mutex_lock(&loc->fl_lock);
		result = loc_thr_create(loc);
		c2_mutex_unlock(&loc->fl_lock);
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

void c2_fom_fini(struct c2_fom *fom)
{
	struct c2_fom_domain *fdom;
	struct c2_reqh       *reqh;

	fdom = fom->fo_loc->fl_dom;
	reqh = fdom->fd_reqh;
	c2_list_link_fini(&fom->fo_linkage);

	if (c2_atomic64_dec_and_test(&fdom->fd_foms_nr))
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
	fom->fo_phase   = C2_FOPH_INIT;

	c2_list_link_init(&fom->fo_linkage);

	fom->fo_transitions = 0;
}
C2_EXPORTED(c2_fom_init);

static bool fom_clink_cb(struct c2_clink *link)
{
	struct c2_fom_callback *cb = container_of(link, struct c2_fom_callback,
	                                          fc_clink);
	C2_PRE(cb->fc_state >= C2_FCS_INIT);

	if (c2_atomic64_cas(&cb->fc_state, C2_FCS_INIT, C2_FCS_TOP_DONE) &&
	    (cb->fc_top == NULL || !cb->fc_top(cb)))
		c2_sm_ast_post(&cb->fc_fom->fo_loc->fl_group, &cb->fc_ast);

	return true;
}

static void fom_ast_cb(struct c2_sm_group *grp, struct c2_sm_ast *ast)
{
	struct c2_fom_callback *cb = container_of(ast, struct c2_fom_callback,
	                                          fc_ast);
	C2_PRE(c2_fom_invariant(cb->fc_fom));
	C2_PRE(cb->fc_state >= C2_FCS_TOP_DONE);

	if (c2_atomic64_cas(&cb->fc_state, C2_FCS_TOP_DONE, C2_FCS_DONE)) {
		cb->fc_bottom(cb);
		c2_clink_del(&cb->fc_clink);
		c2_clink_fini(&cb->fc_clink);
	}
}

void c2_fom_callback_arm(struct c2_fom *fom, struct c2_chan *chan,
                         struct c2_fom_callback *cb)
{
	C2_PRE(cb->fc_bottom != NULL);

	cb->fc_fom = fom;

	c2_clink_init(&cb->fc_clink, &fom_clink_cb);
	cb->fc_ast.sa_cb = &fom_ast_cb;
	cb->fc_state = C2_FCS_INIT;
	/* XXX a memory barrier is required
	 * if c2_clink_add() doesn't do its own locking. */
	c2_clink_add(chan, &cb->fc_clink);
}

static void fom_ready_cb(struct c2_fom_callback *cb)
{
	c2_fom_wakeup(cb->fc_fom);
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
	/* c2_clink_fini() is called in fom_ast_cb() */
}

bool c2_fom_callback_cancel(struct c2_fom_callback *cb)
{
	bool result;
	C2_PRE(cb->fc_state >= C2_FCS_INIT);

	result = c2_atomic64_cas(&cb->fc_state, C2_FCS_INIT, C2_FCS_DONE);

	if (result) {
		c2_clink_del(&cb->fc_clink);
		c2_clink_fini(&cb->fc_clink);
	}

	return result;
}

const struct c2_sm_state_descr fom_states[C2_FOS_SM_FINISH + 1] = {
	[C2_FOS_INIT] = {
		.sd_flags     = C2_SDF_INITIAL,
		.sd_name      = "SM init",
		.sd_in        = NULL,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOS_SM_FINISH) |
				(1 << C2_FOS_QUEUE)
	},
	[C2_FOS_QUEUE] = {
		.sd_flags     = 0,
		.sd_name      = "fom queue",
		.sd_in        = &fom_queue,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOS_RUNNING)
	},
	[C2_FOS_READY] = {
		.sd_flags     = 0,
		.sd_name      = "fom ready",
		.sd_in        = &fom_ready,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOS_RUNNING)
	},
	[C2_FOS_RUNNING] = {
		.sd_flags     = 0,
		.sd_name      = "fom running",
		.sd_in        = &fom_exec,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOS_WAITING) |
				(1 << C2_FOS_SM_FINISH)
	},
	[C2_FOS_WAITING] = {
		.sd_flags     = 0,
		.sd_name      = "fom wait",
		.sd_in        = &fom_wait,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = (1 << C2_FOS_SM_FINISH) |
				(1 << C2_FOS_READY)
	},
	[C2_FOS_SM_FINISH] = {
		.sd_flags     = C2_SDF_TERMINAL,
		.sd_name      = "finished",
		.sd_in        = NULL,
		.sd_ex        = NULL,
		.sd_invariant = NULL,
		.sd_allowed   = 0
	}
};

const struct c2_sm_conf	fom_conf = {
	.scf_name      = "FOM states",
	.scf_nr_states = C2_FOS_SM_FINISH + 1,
	.scf_state     = fom_states
};

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
