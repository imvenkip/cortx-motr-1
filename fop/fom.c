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
#include <config.h>
#endif

#include "lib/misc.h"
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/processor.h"
#include "lib/time.h"
#include "lib/timer.h"
#include "lib/arith.h"

#include "addb/addb.h"
#include "fop/fom.h"
#include "fop/fop.h"
#include "reqh/reqh.h"

/**
   @addtogroup fom
   @{
 */

enum locality_ht_wait {
	LOC_HT_WAIT = 1
};

enum {
        MIN_CPU_NR = 1
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

bool fom_wait_time_is_out(const struct c2_fom_domain *dom, const struct c2_fom *fom);

#define FOM_ADDB_ADD(fom, name, rc)  \
C2_ADDB_ADD(&(fom)->fo_fop->f_addb, &c2_fom_addb_loc, c2_addb_func_fail, (name), (rc))

/**
 * Fom domain operations.
 * @todo Support fom timeout functionality.
 */
static struct c2_fom_domain_ops c2_fom_dom_ops = {
	.fdo_time_is_out = fom_wait_time_is_out
};

static int loc_thr_create(struct c2_fom_locality *loc);

bool c2_fom_domain_invariant(const struct c2_fom_domain *dom)
{
	return dom != NULL && dom->fd_localities != NULL &&
		dom->fd_ops != NULL;
}

bool c2_locality_invariant(const struct c2_fom_locality *loc)
{
	return	loc != NULL && loc->fl_dom != NULL &&
		c2_mutex_is_locked(&loc->fl_lock) &&
		c2_list_invariant(&loc->fl_runq) &&
		c2_list_invariant(&loc->fl_wail) &&
		c2_list_invariant(&loc->fl_threads);
}

bool c2_fom_invariant(const struct c2_fom *fom)
{
	struct c2_fom_locality *loc;

	if ( fom == NULL || fom->fo_loc == NULL || fom->fo_type == NULL ||
		fom->fo_ops == NULL || fom->fo_fop == NULL ||
		!c2_list_link_invariant(&fom->fo_linkage))
		return false;

	loc = fom->fo_loc;
	if (!c2_mutex_is_locked(&loc->fl_lock))
		return false;

	switch (fom->fo_state) {
	case FOS_READY:
		return c2_list_contains(&loc->fl_runq, &fom->fo_linkage);

	case FOS_RUNNING:
		return !c2_list_contains(&loc->fl_runq, &fom->fo_linkage) &&
			!c2_list_contains(&loc->fl_wail, &fom->fo_linkage);

	case FOS_WAITING:
		return c2_list_contains(&loc->fl_wail, &fom->fo_linkage);

	default:
		return false;
	}
}

bool fom_wait_time_is_out(const struct c2_fom_domain *dom, const struct c2_fom *fom)
{
	return false;
}

/**
 * Enqueues fom into locality runq list and increments
 * number of items in runq, c2_fom_locality::fl_runq_nr.
 * This function is invoked when a new fom is submitted for
 * execution or a waiting fom is re scheduled for processing.
 *
 * @pre fom->fo_state == FOS_READY
 * @param fom Ready to be executed fom, is put on locality runq
 */
static void fom_ready(struct c2_fom *fom)
{
	struct c2_fom_locality *loc;

	loc = fom->fo_loc;
	C2_PRE(c2_mutex_is_locked(&loc->fl_lock));
	C2_PRE(fom->fo_state == FOS_READY);

	c2_list_add_tail(&loc->fl_runq, &fom->fo_linkage);
	C2_CNT_INC(loc->fl_runq_nr);
	c2_chan_signal(&loc->fl_runrun);
}

/**
 * Call back function to remove fom from the locality wait
 * list and to put it back on the locality runq list for further
 * execution.
 *
 * This function returns true, meaning that the event is "consumed", because
 * nobody is supposed to be waiting on fom->fo_clink.
 *
 * @param clink Fom linkage into waiting channel registered
 *		during a blocking operation
 *
 * @pre clink != NULL
 */
static bool fom_cb(struct c2_clink *clink)
{
	struct c2_fom_locality	*loc;
	struct c2_fom		*fom;

	C2_PRE(clink != NULL);

	fom = container_of(clink, struct c2_fom, fo_clink);
	loc = fom->fo_loc;
	c2_mutex_lock(&loc->fl_lock);
	C2_ASSERT(c2_fom_invariant(fom));
	C2_ASSERT(fom->fo_state == FOS_WAITING);
	C2_ASSERT(c2_list_contains(&loc->fl_wail, &fom->fo_linkage));
	c2_list_del(&fom->fo_linkage);
	C2_CNT_DEC(loc->fl_wail_nr);
	fom->fo_state = FOS_READY;
	fom_ready(fom);
	c2_mutex_unlock(&loc->fl_lock);
	return true;
}

void c2_fom_block_enter(struct c2_fom *fom)
{
	int			rc;
	int			i;
	size_t			idle_threads;
	size_t			max_idle_threads;
	struct c2_fom_locality *loc;

	loc = fom->fo_loc;
	c2_mutex_lock(&loc->fl_lock);
	C2_ASSERT(c2_locality_invariant(loc));
	idle_threads = loc->fl_idle_threads_nr;
	C2_CNT_INC(loc->fl_lo_idle_threads_nr);
	max_idle_threads = max_check(loc->fl_lo_idle_threads_nr,
				loc->fl_hi_idle_threads_nr);
	c2_mutex_unlock(&loc->fl_lock);

	for (i = idle_threads; i < max_idle_threads; ++i) {
		rc = loc_thr_create(loc);
		if (rc != 0) {
			FOM_ADDB_ADD(fom, "c2_fom_block_enter", rc);
			break;
		}
	}
}

void c2_fom_block_leave(struct c2_fom *fom)
{
	struct c2_fom_locality *loc;

	loc = fom->fo_loc;
	c2_mutex_lock(&loc->fl_lock);
	C2_ASSERT(c2_locality_invariant(loc));
	C2_CNT_DEC(loc->fl_lo_idle_threads_nr);
	c2_mutex_unlock(&loc->fl_lock);
}

void c2_fom_queue(struct c2_fom *fom)
{
	struct c2_fom_locality *loc;

	C2_PRE(fom->fo_phase == FOPH_INIT ||
		fom->fo_phase == FOPH_FAILURE);

	loc = fom->fo_loc;
	c2_atomic64_inc(&loc->fl_dom->fd_foms_nr);
	c2_mutex_lock(&loc->fl_lock);
	fom->fo_state = FOS_READY;
	fom_ready(fom);
	c2_mutex_unlock(&loc->fl_lock);
}

/**
 * Puts fom on locality wait list if fom performs a blocking
 * operation, this releases the current thread to start
 * executing another fom from the runq, thus making the reqh
 * non blocking.
 * Fom state is changed to FOS_WAITING.
 * c2_fom_locality::fl_lock should be held before putting
 * fom on the locality wait list.
 * This function is invoked from fom_exec(), if the fom
 * is performing a blocking operation and c2_fom::fo_state()
 * returns FSO_WAIT.
 *
 * @pre fom->fo_state == FOS_RUNNING
 * @param fom A fom blocking on an operation that is to be
 *		put on the locality wait list
 */
static void fom_wait(struct c2_fom *fom)
{
	struct c2_fom_locality *loc;

	C2_PRE(fom->fo_state == FOS_RUNNING);

	loc = fom->fo_loc;
	C2_ASSERT(c2_mutex_is_locked(&loc->fl_lock));
	fom->fo_state = FOS_WAITING;
	c2_list_add_tail(&loc->fl_wail, &fom->fo_linkage);
	C2_CNT_INC(loc->fl_wail_nr);
}

void c2_fom_block_at(struct c2_fom *fom, struct c2_chan *chan)
{
	C2_PRE(!c2_clink_is_armed(&fom->fo_clink));

	c2_mutex_lock(&fom->fo_loc->fl_lock);
	c2_clink_add(chan, &fom->fo_clink);
}

/**
 * Invokes fom state transition method, which transitions fom
 * through various phases of its execution without blocking.
 * Fom state method is executed until it returns FSO_WAIT,
 * indicating fom has either completed its execution or is
 * going to block on an operation.
 * If a fom needs to block on an operation, the state transition
 * function should register the fom's clink (by calling
 * c2_fom_block_at()) with the channel where the completion event
 * will be signalled.
 * If the state method returns FSO_WAIT, and fom has not yet
 * finished its execution, then it is put on the locality wait
 * list with c2_fom_locality::fl_lock mutex already held by
 * c2_fom_block_at().
 *
 * @see c2_fom_state_outcome
 * @see c2_fom_block_at()
 *
 * @param fom A fom under execution
 * @pre fom->fo_state == FOS_RUNNING
 */
static void fom_exec(struct c2_fom *fom)
{
	int			rc;
	struct c2_fom_locality *loc;

	C2_PRE(fom->fo_state == FOS_RUNNING);

	loc = fom->fo_loc;
	/*
	 * If fom just came out of a wait state, we need to delete the
	 * fom->fo_clink from the waiting channel list, as it was added
	 * into it by c2_fom_block_at().
	 */
	if (c2_clink_is_armed(&fom->fo_clink))
		c2_clink_del(&fom->fo_clink);

	do {
		c2_mutex_lock(&loc->fl_lock);
		C2_ASSERT(c2_fom_invariant(fom));
		c2_mutex_unlock(&loc->fl_lock);
		rc = fom->fo_ops->fo_state(fom);
	} while (rc == FSO_AGAIN);

	C2_ASSERT(rc == FSO_WAIT);
	if (fom->fo_phase == FOPH_FINISH) {
		fom->fo_ops->fo_fini(fom);
	} else {
		C2_ASSERT(c2_mutex_is_locked(&loc->fl_lock));
		fom_wait(fom);
		C2_ASSERT(fom->fo_state == FOS_WAITING);
		C2_ASSERT(c2_list_contains(&loc->fl_wail, &fom->fo_linkage));
		c2_mutex_unlock(&loc->fl_lock);
	}
}

/**
 * Dequeues a fom from runq list of the locality.
 *
 * @param loc Locality assigned for fom execution
 *
 * @pre c2_mutex_is_locked(&loc->fl_lock)
 *
 * @retval c2_fom if succeeds
 *	else returns NULL
 */
static struct c2_fom *fom_dequeue(struct c2_fom_locality *loc)
{
	struct c2_list_link	*fom_link;
	struct c2_fom		*fom = NULL;

	C2_PRE(c2_mutex_is_locked(&loc->fl_lock));

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
 * Thread is then woken up either by fom enqueue operation in a locality runq list,
 * or if thread times out waiting on the channel.
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
	struct c2_fom	       *fom;

	C2_PRE(th != NULL);

	loc = th->fht_locality;
	idle = false;
	fom = NULL;
        c2_time_set(&delta, LOC_HT_WAIT, 0);
	c2_clink_init(&th_clink, NULL);

	c2_mutex_lock(&loc->fl_lock);
	C2_ASSERT(c2_locality_invariant(loc));
	c2_clink_add(&loc->fl_runrun, &th_clink);

	do {
		c2_mutex_unlock(&loc->fl_lock);
		if (fom != NULL) {
			C2_ASSERT(fom->fo_state == FOS_READY);
			fom->fo_state = FOS_RUNNING;
			fom_exec(fom);
		}
		c2_chan_timedwait(&th_clink, c2_time_add(c2_time_now(), delta));
		c2_mutex_lock(&loc->fl_lock);
		fom = fom_dequeue(loc);
		if (fom == NULL && !idle)
			C2_CNT_INC(loc->fl_idle_threads_nr);
		else if (fom != NULL && idle)
			C2_CNT_DEC(loc->fl_idle_threads_nr);
		idle = fom == NULL;
	} while (!idle || loc->fl_idle_threads_nr <= loc->fl_lo_idle_threads_nr);

	C2_CNT_DEC(loc->fl_idle_threads_nr);
	C2_CNT_DEC(loc->fl_threads_nr);
	c2_mutex_unlock(&loc->fl_lock);
	c2_clink_del(&th_clink);
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

	c2_mutex_lock(&loc->fl_lock);
	rc = c2_thread_confine(&th->fht_thread, &loc->fl_processors);
	if (rc == 0) {
		c2_list_add_tail(&loc->fl_threads, &th->fht_linkage);
		C2_CNT_INC(loc->fl_threads_nr);
	}
	c2_mutex_unlock(&loc->fl_lock);

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

	fom_addb_ctx = &loc->fl_dom->fd_addb_ctx;
	C2_ALLOC_PTR_ADDB(locthr, fom_addb_ctx, &c2_fom_addb_loc);
	if (locthr == NULL)
		return -ENOMEM;

	locthr->fht_locality = loc;
	result = C2_THREAD_INIT(&locthr->fht_thread, struct c2_fom_hthread *,
			loc_thr_init, &loc_handler_thread, locthr,
			"locality_thread");

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
static void locality_fini(struct c2_fom_locality *loc)
{
	struct c2_list_link	*link;
	struct c2_fom_hthread	*th;

	C2_PRE(loc != NULL);

	c2_mutex_lock(&loc->fl_lock);
	C2_ASSERT(c2_locality_invariant(loc));
	loc->fl_lo_idle_threads_nr = 0;
	c2_chan_broadcast(&loc->fl_runrun);
	while (!c2_list_is_empty(&loc->fl_threads)) {

		link = c2_list_first(&loc->fl_threads);
		C2_ASSERT(link != NULL);
		c2_list_del(link);
		c2_mutex_unlock(&loc->fl_lock);
		th = container_of(link, struct c2_fom_hthread,
					fht_linkage);
		C2_ASSERT(th != NULL);
		c2_thread_join(&th->fht_thread);
		c2_thread_fini(&th->fht_thread);
		c2_free(th);
		c2_mutex_lock(&loc->fl_lock);
	}
	c2_mutex_unlock(&loc->fl_lock);

	c2_list_fini(&loc->fl_runq);
	C2_ASSERT(loc->fl_runq_nr == 0);

	c2_list_fini(&loc->fl_wail);
	C2_ASSERT(loc->fl_wail_nr == 0);

	c2_mutex_fini(&loc->fl_lock);
	c2_chan_fini(&loc->fl_runrun);
	c2_list_fini(&loc->fl_threads);
	C2_ASSERT(loc->fl_threads_nr == 0);

	c2_bitmap_fini(&loc->fl_processors);
}

/**
 * Initialises a locality in fom domain.
 * Creates and adds threads to locality, every thread is
 * confined to the cpus represented by the pmap, this is
 * done in the thread init function.
 * Number of threads in the locality corresponds to the
 * number of cpus represented by the bits set in the pmap.
 *
 * @see loc_thr_init()
 *
 * @param loc  c2_fom_locality to be initialised
 * @param pmap Bitmap representing number of cpus
 *		present in the locality
 *
 * @retval 0 If locality is initialised
 *	-errno on failure
 *
 * @pre loc != NULL
 */
static int locality_init(struct c2_fom_locality *loc, struct c2_bitmap *pmap)
{
	int			result;
	int			i;
	int			ncpus;
	c2_processor_nr_t	max_proc;

	C2_PRE(loc != NULL);

	max_proc = c2_processor_nr_max();

	c2_list_init(&loc->fl_runq);
	loc->fl_runq_nr = 0;

	c2_list_init(&loc->fl_wail);
	loc->fl_wail_nr = 0;

	c2_mutex_init(&loc->fl_lock);
	c2_chan_init(&loc->fl_runrun);
	c2_list_init(&loc->fl_threads);

	result = c2_bitmap_init(&loc->fl_processors, max_proc);

	if (result == 0) {
		for (i = 0, ncpus = 0; i < max_proc; ++i) {
			if (c2_bitmap_get(pmap, i)) {
				c2_bitmap_set(&loc->fl_processors, i, true);
				C2_CNT_INC(ncpus);
			}
		}

                if (ncpus > MIN_CPU_NR)
                        loc->fl_lo_idle_threads_nr = ncpus/2;
                else
                        loc->fl_lo_idle_threads_nr = ncpus;

                loc->fl_hi_idle_threads_nr = ncpus;

		for (i = 0; i < ncpus; ++i) {
			result = loc_thr_create(loc);
			if (result != 0)
				break;
		}
	}

	if (result != 0)
		locality_fini(loc);

	return result;
}

/**
 * Checks if cpu resource is shared.
 *
 * @retval bool true if cpu resource is shared
 *		false if no cpu resource is shared
 */
static bool resource_is_shared(const struct c2_processor_descr *cpu1,
			const struct c2_processor_descr *cpu2)
{
	return cpu1->pd_l2 == cpu2->pd_l2 ||
		cpu1->pd_numa_node == cpu2->pd_numa_node;
}

int  c2_fom_domain_init(struct c2_fom_domain *dom)
{
	int				i;
	int				j;
	struct c2_processor_descr	cpui;
	struct c2_processor_descr	cpuj;
	c2_processor_nr_t		max_proc;
	c2_processor_nr_t		result;
	struct c2_bitmap		onln_cpu_map;
	struct c2_bitmap		loc_cpu_map;
	struct c2_fom_locality	       *localities;

	C2_PRE(dom != NULL);

	/*
	 * Check number of processors online and create localities
	 * between one's sharing common resources.
	 * Currently considering shared L2 cache and numa node,
	 * between cores, as shared resources.
	 */
	max_proc = c2_processor_nr_max();
	dom->fd_ops = &c2_fom_dom_ops;
	result = c2_bitmap_init(&onln_cpu_map, max_proc);
	if (result != 0)
		return result;

	result = c2_bitmap_init(&loc_cpu_map, max_proc);
	if (result != 0) {
		c2_bitmap_fini(&onln_cpu_map);
		return result;
	}

	c2_processors_online(&onln_cpu_map);

        c2_addb_ctx_init(&dom->fd_addb_ctx, &c2_fom_addb_ctx_type,
						&c2_addb_global_ctx);
	C2_ALLOC_ARR_ADDB(dom->fd_localities, max_proc, &dom->fd_addb_ctx,
							&c2_fom_addb_loc);
	if (dom->fd_localities == NULL) {
		c2_addb_ctx_fini(&dom->fd_addb_ctx);
		c2_bitmap_fini(&onln_cpu_map);
		c2_bitmap_fini(&loc_cpu_map);
		return -ENOMEM;
	}

	localities = dom->fd_localities;
	for (i = 0; i < max_proc; ++i) {
		if (!c2_bitmap_get(&onln_cpu_map, i))
			continue;
		result = c2_processor_describe(i, &cpui);
		if (result != 0)
			break;
		for (j = i; j < max_proc; ++j) {
			if (!c2_bitmap_get(&onln_cpu_map, j))
				continue;
			result = c2_processor_describe(j, &cpuj);
			if (result == 0 && resource_is_shared(&cpui, &cpuj)) {
				c2_bitmap_set(&loc_cpu_map, cpuj.pd_id, true);
				c2_bitmap_set(&onln_cpu_map, j, false);
			}
		}
		if (result == 0) {
			localities[dom->fd_localities_nr].fl_dom = dom;
			result = locality_init(&localities[dom->fd_localities_nr],
						&loc_cpu_map);
			if (result == 0) {
				c2_bitmap_fini(&loc_cpu_map);
				C2_CNT_INC(dom->fd_localities_nr);
				result = c2_bitmap_init(&loc_cpu_map, max_proc);
			}
		}
		if (result != 0)
			break;

	}

	if (result != 0)
		c2_fom_domain_fini(dom);

	c2_bitmap_fini(&onln_cpu_map);
	c2_bitmap_fini(&loc_cpu_map);

	return result;
}

void c2_fom_domain_fini(struct c2_fom_domain *dom)
{
	int fd_loc_nr;

	C2_ASSERT(c2_fom_domain_invariant(dom));

	fd_loc_nr = dom->fd_localities_nr;
	while (fd_loc_nr > 0) {
		locality_fini(&dom->fd_localities[fd_loc_nr - 1]);
		--fd_loc_nr;
	}

	c2_addb_ctx_fini(&dom->fd_addb_ctx);
	c2_free(dom->fd_localities);
}

void c2_fom_init(struct c2_fom *fom)
{
	C2_PRE(fom != NULL);

	fom->fo_phase = FOPH_INIT;
	fom->fo_rep_fop = NULL;

	c2_clink_init(&fom->fo_clink, &fom_cb);
	c2_list_link_init(&fom->fo_linkage);
}
C2_EXPORTED(c2_fom_init);

void c2_fom_fini(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_FINISH);

	c2_atomic64_dec(&fom->fo_loc->fl_dom->fd_foms_nr);
	c2_clink_fini(&fom->fo_clink);
	c2_list_link_fini(&fom->fo_linkage);
}
C2_EXPORTED(c2_fom_fini);

void c2_fom_create(struct c2_fom *fom, struct c2_fom_type *fom_type,
		const struct c2_fom_ops *ops, struct c2_fop *fop,
		struct c2_fop *reply)
{
	C2_PRE(fom != NULL);

	c2_fom_init(fom);

	fom->fo_type	= fom_type;
	fom->fo_ops	= ops;
	fom->fo_fop	= fop;
	fom->fo_rep_fop = reply;
}
C2_EXPORTED(c2_fom_create);
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
