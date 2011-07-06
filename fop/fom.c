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
 * Original author: Original author: Mandar Sawant <Mandar_Sawant@xyratex.com>
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

/**
 * validates a fom domain.
 *
 * @param dom -> c2_fom_domain.
 *
 * @retval bool -> true, if succeeds.
 *		false, on failure.
 */
bool c2_fom_domain_invariant(const struct c2_fom_domain *dom)
{
	return dom != NULL && dom->fd_localities != NULL
			&& dom->fd_ops != NULL;
}

/**
 * validates a locality.
 *
 * @param loc -> c2_fom_locality.
 *
 * @retval bool -> true, if succeeds.
 *		false, on failure.
 */
bool c2_locality_invariant(const struct c2_fom_locality *loc)
{
	return	loc != NULL && loc->fl_dom != NULL && c2_list_invariant(&loc->fl_runq) &&
		c2_list_invariant(&loc->fl_wail) && c2_list_invariant(&loc->fl_threads);
}

/**
 * validates a fom.
 *
 * @param fom -> c2_fom.
 *
 * @retval bool -> true, if succeeds.
 *		false, on failure.
 */
bool c2_fom_invariant(const struct c2_fom *fom)
{
	return  fom != NULL && fom->fo_type != NULL && fom->fo_ops != NULL &&
		fom->fo_fop_ctx != NULL && fom->fo_fop != NULL &&
		fom->fo_fol != NULL && fom->fo_domain != NULL &&
		fom->fo_stdomain != NULL && fom->fo_loc != NULL &&
		c2_list_link_invariant(&fom->fo_rwlink);
}

/**
 * Enqueues fom into locality's runq list.
 *
 * Invoked by call back fuction fom_cb and c2_fom_queue.
 *
 * @param fom -> c2_fom
 */
static void fom_ready(struct c2_fom *fom)
{
	struct c2_fom_locality *loc;

	loc = fom->fo_loc;
	c2_mutex_lock(&loc->fl_runq_lock);
	c2_list_add_tail(&loc->fl_runq, &fom->fo_rwlink);
	++loc->fl_runq_nr;
	c2_mutex_unlock(&loc->fl_runq_lock);
	c2_mutex_lock(&loc->fl_lock);
	c2_chan_signal(&loc->fl_runrun);
	c2_mutex_unlock(&loc->fl_lock);
}

/**
 * Call back function.
 * Removes fom from the locality wait list
 * and puts it back on the locality runq list for further
 * execution.
 *
 * @param clink -> c2_clink.
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
	C2_ASSERT(loc->fl_wail_nr > 0);
	--loc->fl_wail_nr;
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
	if (loc->fl_idle_threads_nr > loc->fl_lo_idle_threads_nr)
		c2_chan_broadcast(&loc->fl_runrun);
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
 *
 * @param fom -> c2_fom.
 */
static void fom_wait(struct c2_fom *fom)
{
	struct c2_fom_locality *loc;

	loc = fom->fo_loc;
	C2_ASSERT(fom->fo_state == FOS_RUNNING);
	fom->fo_state = FOS_WAITING;
	c2_mutex_lock(&loc->fl_wail_lock);
	c2_list_add_tail(&loc->fl_wail, &fom->fo_rwlink);
	++loc->fl_wail_nr;
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
 * Executes fop specific operation.
 *
 * @param fom -> c2_fom.
 *
 * @pre fom->fo_state == FOS_RUNNING
 */
void c2_fom_fop_exec(struct c2_fom *fom)
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
 * @param loc -> c2_fom_locality.
 *
 * @retval -> returns valid c2_fom object if succeeds,
 *		else returns NULL.
 *
 * @pre c2_mutex_is_locked(&loc->fl_runq_lock)
 */
struct c2_fom * c2_fom_dequeue(struct c2_fom_locality *loc)
{
	struct c2_list_link	*fom_link;
	struct c2_fom		*fom;

	C2_PRE(c2_mutex_is_locked(&loc->fl_runq_lock));

	fom_link = c2_list_first(&loc->fl_runq);
	if (fom_link == NULL)
		return NULL;
	c2_list_del(fom_link);
	fom = container_of(fom_link, struct c2_fom, fo_rwlink);
	C2_ASSERT(fom != NULL);
	C2_ASSERT(loc->fl_runq_nr > 0);
	--loc->fl_runq_nr;
	return fom;
}

/**
 * Locality handler thread function.
 * Thread waits on locality channel for specific time, it is woken up either by
 * fom enqueue operation in runq list or if wait time expires.
 * when woken up, thread dequeue's a fom from the locality's runq list and starts
 * executing it. standard/generic fom phases are executed before fop specific fom phases.
 * if number of idle threads are higher than threshold value, they are terminated.
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
			fom->fo_state = FOS_RUNNING;
			c2_fom_fop_exec(fom);
		}

		c2_chan_timedwait(&th_clink, c2_time_add(c2_time_now(&now), delta));

		/* acquire runq lock */
		c2_mutex_lock(&loc->fl_runq_lock);
		fom = c2_fom_dequeue(loc);
		c2_mutex_unlock(&loc->fl_runq_lock);

		/* acquire locality lock */
		c2_mutex_lock(&loc->fl_lock);
		if (fom == NULL && !idle)
			++loc->fl_idle_threads_nr;
		else if (fom != NULL && idle)
			--loc->fl_idle_threads_nr;
		idle = fom == NULL;

	} while (!idle || loc->fl_idle_threads_nr <= loc->fl_lo_idle_threads_nr);

	--loc->fl_idle_threads_nr;
	--loc->fl_threads_nr;
	c2_mutex_unlock(&loc->fl_lock);
	c2_clink_del(&th_clink);
	c2_clink_fini(&th_clink);
}

/**
 * Thread initialisation function.
 * Adds thread to the locality thread list and
 * increments thread count in locality.
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
	++loc->fl_threads_nr;
	c2_thread_confine(&th->fht_thread, &loc->fl_processors);
	c2_mutex_unlock(&loc->fl_lock);
}

/**
 * Creates and add a new thread to the specified
 * locality.
 *
 * @param loc -> c2_fom_locality.
 *
 * @retval int -> returns 0, if succeeds.
 *		returns -errno, on failure.
 *
 * @pre loc != NULL.
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
			&loc_thr_init, &loc_handler_thread, locthr, "locality_thread");

	if (result != 0)
		c2_free(locthr);

	return result;
}

/**
 * Initialises a locality in fom domain.
 *
 * @param loc -> c2_fom_locality.
 * @param pmap -> c2_bitmap, bitmap representing number of cpus in locality.
 *
 * @retval int -> 0, if succeeds.
 *		-errno, on failure.
 *
 * @pre loc != NULL.
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
			++ncpus;
		}
	}

	for (i = 0; i < ncpus && result == 0; ++i) {
		result = loc_thr_create(loc);
	}

	return result;
}

/**
 * Finalises all the localities in fom domain.
 *
 * @param loc -> c2_fom_locality.
 *
 * @pre loc != NULL.
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

	/*
	 * Invoke clean up functions for individual members
	 * of locality.
	 */
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
 * Checks if cpu resource is shared.
 *
 * @param cpu1 -> cpu descriptor.
 * @param cpu2 -> cpu descriptor.
 *
 * @retval bool -> returns true, if cpu resource is shared.
 *		returns false, if no cpu resource is shared.
 */
static bool resource_is_shared(struct c2_processor_descr *cpu1,
			struct c2_processor_descr *cpu2)
{
	return cpu1->pd_l2 == cpu2->pd_l2 || cpu1->pd_numa_node == cpu2->pd_numa_node;
}

/**
 * Checks if the cpu is alive and usable.
 *
 * @param cpu -> cpu descriptor.
 *
 * @retval bool -> returns true, if succeeds.
 *		returns false, on failure.
 */
static bool cpu_is_usable(struct c2_processor_descr *cpu)
{
	return cpu->pd_l1_sz > 0 || cpu->pd_l2_sz > 0;
}

int  c2_fom_domain_init(struct c2_fom_domain *dom)
{
	int				i;
	int				j;
	int				onln_cpus;
	int				result = 0;
	struct  c2_processor_descr     *cpu_info;
	c2_processor_nr_t		max_proc;
	c2_processor_nr_t		rc;
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
	/* Temporary array of c2_processor descriptor */
	C2_ALLOC_ARR(cpu_info, max_proc);
	if (cpu_info == NULL)
		return -ENOMEM;

	dom->fd_ops = &c2_fom_dom_ops;
	c2_bitmap_init(&onln_cpu_map, max_proc);
	c2_bitmap_init(&loc_cpu_map, max_proc);
	c2_processors_online(&onln_cpu_map);

	/* Get the info of online processors. */
	for (i = 0, onln_cpus = 0; i < max_proc; ++i) {
		if (c2_bitmap_get(&onln_cpu_map, i)) {
			rc = c2_processor_describe(i, &cpu_info[onln_cpus]);
			if (rc != 0)
				continue;
			else
				++onln_cpus;
		}
	}

	C2_ALLOC_ARR_ADDB(dom->fd_localities, onln_cpus, &c2_reqh_addb_ctx, &c2_fom_addb_loc);
	if (dom->fd_localities == NULL) {
		c2_free(cpu_info);
		return -ENOMEM;
	}

	localities = dom->fd_localities;
	/*
	 * Find the processors sharing resources and
	 * create localities between them.
	 */
	for (i = 0; i < max_proc && result == 0; ++i) {
		for (j = i; j < max_proc && result == 0; ++j) {
			/*
			 * we first check if the cpu is alive and usable.
			 * If jth cpu is usable and shares resources with ith cpu then
			 * we set the bit corresponding to the jth cpu id in temporary
			 * bitmap (later used to confine locality threads) and reset
			 * the jth cpu descriptor in cpu_info array to 0.
			 * Later once we traverse all the cpus sharing a set of resources,
			 * we create a locality among them, reintialise the cpu bitmap,
			 * increment locality number of localities in a fom domain and
			 * reset ith cpu descriptor in cpu_info array to 0.
			 */
			if (cpu_is_usable(&cpu_info[j]) &&
				resource_is_shared(&cpu_info[i], &cpu_info[j])) {
				--onln_cpus;
				c2_bitmap_set(&loc_cpu_map, cpu_info[j].pd_id, true);
				if (j != i)
					C2_SET0(&cpu_info[j]);
			}
		}
		if (cpu_is_usable(&cpu_info[i])) {
			localities[dom->fd_localities_nr].fl_dom = dom;
			result = locality_init(&localities[dom->fd_localities_nr], &loc_cpu_map);
			if (result != 0)
				break;
			c2_bitmap_init(&loc_cpu_map, max_proc);
			++dom->fd_localities_nr;
			C2_SET0(&cpu_info[i]);
		}
		if (onln_cpus <= 0)
			break;
	}

	if (result)
		c2_fom_domain_fini(dom);

	c2_free(cpu_info);
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
	c2_free(dom);
}

int c2_fom_init(struct c2_fom *fom)
{
	/* Set fom state to FOS_READY and fom phase to FOPH_INIT. */

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
