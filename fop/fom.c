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

enum loc_idle_threads {
	MIN_IDLE_THREADS = 1,
	MAX_IDLE_THREADS,
};

/**
   @addtogroup fom
   @{
 */

/**
 * fom addb event location object
 */
const struct c2_addb_loc c2_fom_addb_loc = {
	.al_name = "fom"
};

/**
 * fom addb context state.
 */
const struct c2_addb_ctx_type c2_fom_addb_ctx_type = {
	.act_name = "fom"
};

extern struct c2_addb_ctx c2_reqh_addb_ctx;

#define FOM_ADDB_ADD(fom, name, rc)  \
C2_ADDB_ADD(&fom->fo_fop->f_addb, &c2_fom_addb_loc, c2_addb_func_fail, (name), (rc))

/**
 * fom domain operations.
 * @todo -> support fom timeout fucntionality.
 */
static struct c2_fom_domain_ops c2_fom_dom_ops = {
	.fdo_time_is_out = NULL
};

static int loc_thr_create(struct c2_fom_locality *loc);

bool c2_fom_domain_invariant(const struct c2_fom_domain *dom)
{
	return dom != NULL && dom->fd_localities != NULL &&
		dom->fd_ops != NULL;
}

bool c2_locality_invariant(const struct c2_fom_locality *loc)
{
	return	loc != NULL && loc->fl_dom != NULL && c2_list_invariant(&loc->fl_runq) &&
		c2_list_invariant(&loc->fl_wail) && c2_list_invariant(&loc->fl_threads);
}

bool c2_fom_invariant(const struct c2_fom *fom)
{
	return  fom != NULL && fom->fo_type != NULL && fom->fo_ops != NULL &&
		fom->fo_fop != NULL && fom->fo_fol != NULL &&
		fom->fo_domain != NULL && fom->fo_stdomain != NULL &&
		fom->fo_loc != NULL && c2_list_link_invariant(&fom->fo_rwlink);
}

/**
 * Enqueues fom into locality's runq list.
 *
 * This function is invoked when a new fom is submitted,
 * or a waiting fom is re scheduled for processing.
 */
static void fom_ready(struct c2_fom *fom)
{
	struct c2_fom_locality *loc;

	loc = fom->fo_loc;
	c2_mutex_lock(&loc->fl_runq_lock);
	c2_list_add_tail(&loc->fl_runq, &fom->fo_rwlink);
	C2_CNT_INC(loc->fl_runq_nr);
	c2_mutex_unlock(&loc->fl_runq_lock);
	c2_mutex_lock(&loc->fl_lock);
	c2_chan_signal(&loc->fl_runrun);
	c2_mutex_unlock(&loc->fl_lock);
}

/**
 * Removes fom from the locality wait list
 * and puts it back on the locality runq list for further
 * execution.
 * This function is invoked as a call back, when the fom
 * waiting channel is signalled.
 *
 * @pre clink != NULL.
 */
static void fom_cb(struct c2_clink *clink)
{
	struct c2_fom_locality	*loc;
	struct c2_fom		*fom;

	C2_PRE(clink != NULL);

	fom = container_of(clink, struct c2_fom, fo_clink);
	C2_ASSERT(c2_fom_invariant(fom));
	C2_ASSERT(fom->fo_state == FOS_WAITING);

	loc = fom->fo_loc;
	c2_mutex_lock(&loc->fl_wail_lock);
	C2_ASSERT(c2_list_contains(&loc->fl_wail, &fom->fo_rwlink));
	c2_list_del(&fom->fo_rwlink);
	C2_CNT_DEC(loc->fl_wail_nr);
	fom->fo_state = FOS_READY;
	c2_mutex_unlock(&loc->fl_wail_lock);
	fom_ready(fom);
}

void c2_fom_block_enter(struct c2_fom *fom)
{
	int			rc = 0;
	int			i;
	size_t			idle_threads;
	size_t			hi_idle_threads;
	struct c2_fom_locality *loc;

	loc = fom->fo_loc;
	C2_ASSERT(c2_locality_invariant(loc));
	c2_mutex_lock(&loc->fl_lock);
	idle_threads = loc->fl_idle_threads_nr;
	hi_idle_threads = loc->fl_hi_idle_threads_nr;
	C2_CNT_INC(loc->fl_lo_idle_threads_nr);
	c2_mutex_unlock(&loc->fl_lock);

	for (i = idle_threads; i < hi_idle_threads && rc == 0; ++i) {
		rc = loc_thr_create(loc);
	}

	if (rc != 0)
		FOM_ADDB_ADD(fom, "c2_fom_block_enter", rc);
}

void c2_fom_block_leave(struct c2_fom *fom)
{
	struct c2_fom_locality *loc;

	loc = fom->fo_loc;
	C2_ASSERT(c2_locality_invariant(loc));
	c2_mutex_lock(&loc->fl_lock);
	C2_CNT_DEC(loc->fl_lo_idle_threads_nr);
	c2_mutex_unlock(&loc->fl_lock);
}

void c2_fom_queue(struct c2_fom *fom)
{
	C2_PRE(fom->fo_phase == FOPH_INIT);

	fom_ready(fom);
}

/**
 * puts fom on locality wait list if fom
 * execution blocks.
 * fom state is changed to FOS_WAITING.
 */
static void fom_wait(struct c2_fom *fom)
{
	struct c2_fom_locality *loc;

	loc = fom->fo_loc;
	C2_ASSERT(fom->fo_state == FOS_RUNNING);
	fom->fo_state = FOS_WAITING;
	c2_mutex_lock(&loc->fl_wail_lock);
	c2_list_add_tail(&loc->fl_wail, &fom->fo_rwlink);
	C2_CNT_INC(loc->fl_wail_nr);
	c2_mutex_unlock(&loc->fl_wail_lock);
}

void c2_fom_block_at(struct c2_fom *fom, struct c2_chan *chan)
{
	C2_ASSERT(c2_fom_invariant(fom));

	C2_PRE(!c2_clink_is_armed(&fom->fo_clink));

	c2_clink_add(chan, &fom->fo_clink);
	fom_wait(fom);
}

/**
 * Executes fom and performs non blocking fom
 * state transitioins.
 *
 * @see c2_fom_state_outcome
 *
 * @pre fom->fo_state == FOS_RUNNING
 */
static void fom_fop_exec(struct c2_fom *fom)
{
	int			rc;
	struct c2_fom_locality *loc;

	C2_PRE(fom->fo_state == FOS_RUNNING);

	do {
		rc = fom->fo_ops->fo_state(fom);
	} while (rc == FSO_AGAIN);

	C2_ASSERT(rc == FSO_WAIT);
	if (fom->fo_phase == FOPH_DONE)
		fom->fo_ops->fo_fini(fom);
	else {
		loc = fom->fo_loc;
		c2_mutex_lock(&loc->fl_wail_lock);
		C2_ASSERT(c2_list_contains(&loc->fl_wail, &fom->fo_rwlink));
		c2_mutex_unlock(&loc->fl_wail_lock);
	}
}

/**
 * Dequeue's a fom from locality's runq list.
 *
 * @retval -> returns valid c2_fom object if succeeds,
 *		else returns NULL.
 */
static struct c2_fom *fom_dequeue(struct c2_fom_locality *loc)
{
	struct c2_list_link	*fom_link;
	struct c2_fom		*fom;

	c2_mutex_lock(&loc->fl_runq_lock);
	fom_link = c2_list_first(&loc->fl_runq);
	if (fom_link == NULL) {
		c2_mutex_unlock(&loc->fl_runq_lock);
		return NULL;
	}
	c2_list_del(fom_link);
	fom = container_of(fom_link, struct c2_fom, fo_rwlink);
	C2_ASSERT(fom != NULL);
	C2_CNT_DEC(loc->fl_runq_nr);
	c2_mutex_unlock(&loc->fl_runq_lock);
	return fom;
}

/**
 * Locality handler thread.
 * Handler thread waits on re-scheduling channel for specific time.
 * Thread is then woken up either by fom enqueue operation in a locality runq list,
 * or if thread times out waiting on the channel.
 * when woken up, thread dequeue's a fom from the locality runq list and starts
 * executing it. If number of idle threads are higher than threshold value, they
 * are terminated.
 *
 * @see c2_fom_locality::fl_runrun
 * @see c2_fom_locality::fl_lo_idle_threads_nr
 *
 * @param th -> c2_fom_hthread, contains thread reference, locality reference
 *		and thread linkage in locality.
 *
 * @pre th != NULL
 */
static void loc_handler_thread(struct c2_fom_hthread *th)
{
	c2_time_t		now;
	c2_time_t		delta;
	bool			idle;
	struct c2_clink		th_clink;
	struct c2_fom_locality *loc;
	struct c2_fom	       *fom;
	struct c2_fop_ctx	fop_ctx;

	C2_PRE(th != NULL);

	loc = th->fht_locality;
	idle = false;
	fom = NULL;
        c2_time_set(&delta, 1, 0);
	c2_clink_init(&th_clink, NULL);

	c2_mutex_lock(&loc->fl_lock);
	C2_ASSERT(c2_locality_invariant(loc));
	c2_clink_add(&loc->fl_runrun, &th_clink);

	do {
		c2_mutex_unlock(&loc->fl_lock);

		if (fom != NULL) {
			C2_ASSERT(c2_fom_invariant(fom));
			C2_ASSERT(fom->fo_state == FOS_READY);
			/*
			   Initialise fop context.
			   This could change post integration with
			   new rpc layer implementation.
			 */
			fop_ctx.ft_service = fom->fo_domain->fd_reqh->rh_serv;
			fop_ctx.fc_cookie  = fom->fo_cookie;
			fom->fo_fop_ctx = &fop_ctx;
			fom->fo_state = FOS_RUNNING;
			fom_fop_exec(fom);
		}

		c2_chan_timedwait(&th_clink, c2_time_add(c2_time_now(&now), delta));

		fom = fom_dequeue(loc);
		c2_mutex_lock(&loc->fl_lock);
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
 * Thread initialisation function.
 * Adds thread to the locality thread list and
 * increments thread count in locality atomically.
 *
 * @param th -> c2_fom_hthread, contains thread reference, locality reference
 *		and thread linkage in locality.
 */
static void loc_thr_init(struct c2_fom_hthread *th)
{
	struct c2_fom_locality *loc;
	loc = th->fht_locality;
	C2_ASSERT(loc != NULL);

	c2_mutex_lock(&loc->fl_lock);
	c2_list_add_tail(&loc->fl_threads, &th->fht_linkage);
	C2_CNT_INC(loc->fl_threads_nr);
	c2_thread_confine(&th->fht_thread, &loc->fl_processors);
	c2_mutex_unlock(&loc->fl_lock);
}

/**
 * Creates and adds a thread to the given
 * locality.
 *
 * @param loc -> c2_fom_locality to which threads
 *		are added
 *
 * @retval 0, if a thread is successfully created and
 *		added to the given locality
 *	-errno, on failure
 *
 * @pre loc != NULL
 */
static int loc_thr_create(struct c2_fom_locality *loc)
{
	int			result;
	struct c2_fom_hthread  *locthr;

	C2_PRE(loc != NULL);

	C2_ALLOC_PTR_ADDB(locthr, &c2_reqh_addb_ctx,
				&c2_fom_addb_loc);
	if (locthr == NULL)
		return -ENOMEM;

	locthr->fht_locality = loc;
	result = C2_THREAD_INIT(&locthr->fht_thread, struct c2_fom_hthread *,
			&loc_thr_init, &loc_handler_thread, locthr,
			"locality_thread");

	if (result != 0)
		c2_free(locthr);

	return result;
}

/**
 * Finalises a locality.
 *
 * @param loc -> c2_fom_locality, to be finalised
 *
 * @pre loc != NULL
 */
static void locality_fini(struct c2_fom_locality *loc)
{
	struct c2_list_link	*link;
	struct c2_fom_hthread	*th;

	C2_PRE(loc != NULL);

	/*
	 * Remove each thread from the thread list
	 * delete the linkage, and invoke thread clean
	 * up function. Later free thread object.
	 */
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
	c2_mutex_fini(&loc->fl_runq_lock);

	c2_list_fini(&loc->fl_wail);
	C2_ASSERT(loc->fl_wail_nr == 0);
	c2_mutex_fini(&loc->fl_wail_lock);

	c2_mutex_fini(&loc->fl_lock);
	c2_chan_fini(&loc->fl_runrun);
	c2_list_fini(&loc->fl_threads);
	C2_ASSERT(loc->fl_threads_nr == 0);

	c2_bitmap_fini(&loc->fl_processors);
}

/**
 * Initialises a locality in fom domain.
 * Creates and adds threads to locality, every thread is
 * confined to the cpus represented by the pmap.
 *
 * @param loc -> c2_fom_locality to be initialised
 * @param pmap -> c2_bitmap, bitmap representing number of cpus
 *				present in the locality
 *
 * @retval int -> 0, if locality is initialised,
 *		-errno, on failure
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

	/* Initialise a locality */
	c2_list_init(&loc->fl_runq);
	loc->fl_runq_nr = 0;
	c2_mutex_init(&loc->fl_runq_lock);

	c2_list_init(&loc->fl_wail);
	loc->fl_wail_nr = 0;
	c2_mutex_init(&loc->fl_wail_lock);

	c2_mutex_init(&loc->fl_lock);
	c2_chan_init(&loc->fl_runrun);
	c2_list_init(&loc->fl_threads);

	loc->fl_lo_idle_threads_nr = MIN_IDLE_THREADS;
	loc->fl_hi_idle_threads_nr = MAX_IDLE_THREADS;

	result = c2_bitmap_init(&loc->fl_processors, max_proc);

	for (i = 0, ncpus = 0; i < max_proc && result == 0; ++i) {
		if (c2_bitmap_get(pmap, i)) {
			c2_bitmap_set(&loc->fl_processors, i, true);
			C2_CNT_INC(ncpus);
		}
	}

	for (i = 0; i < ncpus && result == 0; ++i)
		result = loc_thr_create(loc);

	if (result != 0)
		locality_fini(loc);

	return result;
}

/**
 * Checks if cpu resource is shared.
 *
 * @retval bool -> returns true, if cpu resource is shared
 *		returns false, if no cpu resource is shared
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
	c2_processor_nr_t		result = 0;
	struct c2_bitmap		onln_cpu_map;
	struct c2_bitmap		loc_cpu_map;
	struct c2_fom_locality	       *localities;

	C2_PRE(dom != NULL);

	/*
	 * check number of processors online and create localities
	 * between one's sharing common resources.
	 * Currently considering shared L2 cache and numa node,
	 * between cores, as shared resources.
	 */
	max_proc = c2_processor_nr_max();
	dom->fd_ops = &c2_fom_dom_ops;
	if ((result = c2_bitmap_init(&onln_cpu_map, max_proc)) != 0)
		return result;
	if ((result = c2_bitmap_init(&loc_cpu_map, max_proc)) != 0)
		return result;

	c2_processors_online(&onln_cpu_map);
	C2_ALLOC_ARR_ADDB(dom->fd_localities, max_proc, &c2_reqh_addb_ctx,
						&c2_fom_addb_loc);
	if (dom->fd_localities == NULL)
		return -ENOMEM;

	localities = dom->fd_localities;
	/*
	 * Find the processors sharing resources and
	 * create localities between them.
	 */
	for (i = 0; i < max_proc; ++i) {
		if (!c2_bitmap_get(&onln_cpu_map, i))
			continue;
		result = c2_processor_describe(i, &cpui);
		for (j = i; j < max_proc && result == 0; ++j) {
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
		}
		if (result != 0)
			break;
		c2_bitmap_fini(&loc_cpu_map);
		if ((result = c2_bitmap_init(&loc_cpu_map, max_proc)) != 0)
			break;
		C2_CNT_INC(dom->fd_localities_nr);
	}

	c2_bitmap_fini(&onln_cpu_map);
	c2_bitmap_fini(&loc_cpu_map);

	return result;
}

void c2_fom_domain_fini(struct c2_fom_domain *dom)
{
	int	i;
	size_t	fd_localities_nr;

	C2_ASSERT(c2_fom_domain_invariant(dom));

	fd_localities_nr  = dom->fd_localities_nr;
	for(i = 0; i < fd_localities_nr; ++i) {
		locality_fini(&dom->fd_localities[i]);
	}

	c2_free(dom->fd_localities);
}

int c2_fom_init(struct c2_fom *fom)
{
	C2_PRE(fom != NULL);

	fom->fo_state = FOS_READY;
	fom->fo_phase = FOPH_INIT;
	fom->fo_rep_fop = NULL;

	c2_addb_ctx_init(&fom->fo_fop->f_addb, &c2_fom_addb_ctx_type,
				&c2_addb_global_ctx);

	c2_clink_init(&fom->fo_clink, &fom_cb);
	c2_list_link_init(&fom->fo_rwlink);

	return 0;
}

void c2_fom_fini(struct c2_fom *fom)
{
	C2_ASSERT(c2_fom_invariant(fom));
	C2_PRE(fom->fo_phase == FOPH_DONE);

	c2_addb_ctx_fini(&fom->fo_fop->f_addb);
	if (c2_clink_is_armed(&fom->fo_clink))
		c2_clink_del(&fom->fo_clink);
	c2_clink_fini(&fom->fo_clink);
	c2_list_link_fini(&fom->fo_rwlink);
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
