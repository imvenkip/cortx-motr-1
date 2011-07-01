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

enum c2_loc_idle_threads {
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
	.act_name = "t1-fom"
};

extern struct c2_addb_ctx c2_reqh_addb_ctx;

#define FOM_ADDB_ADD(fom, name, rc)  \
C2_ADDB_ADD(&fom->fo_fop->f_addb, &c2_fom_addb_loc, c2_addb_func_fail, (name), (rc))

/**
 * fom domain operations structure.
 * @todo -> support fom timeout fucntionality.
 */
static struct c2_fom_domain_ops c2_fom_dom_ops = {
	.fdo_time_is_out = NULL
};

int c2_loc_thr_create(struct c2_fom_locality *loc, bool confine);

/**
 * function to validate a fom domain.
 *
 * @param dom -> c2_fom_domain structure pointer.
 *
 * @retval bool -> true, on success.
 *		false, on failure.
 */
bool c2_fom_domain_invariant(const struct c2_fom_domain *dom)
{
	return dom != NULL && dom->fd_localities != NULL
			&& dom->fd_ops != NULL;
}

/**
 * function to validate a locality.
 *
 * @param loc -> c2_fom_locality structure pointer.
 *
 * @retval bool -> true, on success.
 *		false, on failure.
 */
bool c2_locality_invariant(struct c2_fom_locality *loc)
{
	bool result = true;

	if (loc == NULL)
		return false;
	c2_mutex_lock(&loc->fl_lock);
	if (loc->fl_dom == NULL || !c2_list_invariant(&loc->fl_runq) ||
		!c2_list_invariant(&loc->fl_wail) || !c2_list_invariant(&loc->fl_threads))
		result = false;
	c2_mutex_unlock(&loc->fl_lock);

	return result;
}

/**
 * function to validate a fom.
 *
 * @param fom -> c2_fom structure pointer.
 *
 * @retval bool -> true, on success.
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
 * Call back function to be invoked when the channel
 * is signalled.
 * This function removes the fom from the locality wait list
 * and puts it back on the locality runq list for further
 * execution.
 *
 * @param clink -> c2_clink structure pointer received from call back.
 *
 * @pre assumes clink is not null.
 */
static void fom_cb(struct c2_clink *clink)
{
	struct c2_fom *fom = NULL;
	struct c2_fom_locality *loc;

	C2_PRE(clink != NULL);

	fom = container_of(clink, struct c2_fom, fo_clink);
	C2_ASSERT(c2_fom_invariant(fom));

	loc = fom->fo_loc;
	c2_mutex_lock(&loc->fl_wail_lock);
	C2_ASSERT(c2_list_contains(&loc->fl_wail, &fom->fo_rwlink));
	c2_list_del(&fom->fo_rwlink);
	if (loc->fl_wail_nr > 0)
		--loc->fl_wail_nr;
	fom->fo_state = FOS_READY;
	c2_mutex_unlock(&fom->fo_loc->fl_wail_lock);
	fom_ready(fom);
}

void c2_fom_block_enter(struct c2_fom *fom)
{
	int rc = 0;
	int i;
	size_t idle_threads;
	size_t hi_idle_threads;
	struct c2_fom_locality *loc;

	C2_PRE(c2_fom_invariant(fom));

	loc = fom->fo_loc;
	c2_mutex_lock(&loc->fl_lock);
	idle_threads = loc->fl_idle_threads_nr;
	hi_idle_threads = loc->fl_hi_idle_threads_nr;
	c2_mutex_unlock(&loc->fl_lock);

	for (i = idle_threads; i < hi_idle_threads && rc == 0; ++i) {
		rc = c2_loc_thr_create(loc, false);
	}

	if (rc != 0)
		FOM_ADDB_ADD(fom, "FOM: c2_fom_block_enter failed", rc);
}

void c2_fom_block_leave(struct c2_fom *fom)
{
	struct c2_fom_locality *loc;

	C2_PRE(c2_fom_invariant(fom));

	loc = fom->fo_loc;
	c2_mutex_lock(&loc->fl_lock);
	if (loc->fl_idle_threads_nr > loc->fl_lo_idle_threads_nr)
		c2_chan_broadcast(&loc->fl_runrun);
	c2_mutex_unlock(&loc->fl_lock);
}

void c2_fom_queue(struct c2_fom *fom)
{
	C2_PRE(c2_fom_invariant(fom));
	C2_PRE(fom->fo_phase == FOPH_INIT);

	fom_ready(fom);
}

/**
 * Function to put fom into locality's wait list
 * during a fom's blocking operation.
 * fom state is changed to FOS_WAITING.
 *
 * @param fom -> c2_fom structure pointer.
 */
void c2_fom_wait(struct c2_fom *fom)
{
	struct c2_fom_locality *loc;

	loc = fom->fo_loc;
	fom->fo_state = FOS_WAITING;
	c2_mutex_lock(&loc->fl_wail_lock);
	c2_list_add_tail(&loc->fl_wail, &fom->fo_rwlink);
	++loc->fl_wail_nr;
	c2_mutex_unlock(&loc->fl_wail_lock);
}

void c2_fom_block_at(struct c2_fom *fom, struct c2_chan *chan)
{
	C2_PRE(c2_fom_invariant(fom));

	C2_PRE(!c2_clink_is_armed(&fom->fo_clink));

	c2_clink_add(chan, &fom->fo_clink);
	c2_fom_wait(fom);
}

/**
 * Function to execute fop specific operation.
 *
 * @param fom -> c2_fom structure pointer.
 *
 * @pre assumes fom is valid, c2_fom_variant should return true.
 */
void c2_fom_fop_exec(struct c2_fom *fom)
{
	int rc;

	C2_PRE(c2_fom_invariant(fom));

	do {
		rc = fom->fo_ops->fo_state(fom);
	}while (rc == FSO_AGAIN);
	C2_ASSERT(rc == FSO_WAIT);
	if (fom->fo_phase == FOPH_DONE)
		fom->fo_ops->fo_fini(fom);
	else {
		c2_mutex_lock(&fom->fo_loc->fl_wail_lock);
		C2_ASSERT(c2_list_contains(&fom->fo_loc->fl_wail, &fom->fo_rwlink));
		c2_mutex_unlock(&fom->fo_loc->fl_wail_lock);
	}
}

/**
 * Dequeue's a fom from locality's runq list.
 *
 * @param loc -> c2_fom_locality.
 *
 * @retval -> returns valid c2_fom object on success,
 * 		else returns NULL.
 */
struct c2_fom * c2_fom_dequeue(struct c2_fom_locality *loc)
{
	struct c2_list_link *fom_link;
	struct c2_fom *fom;

	fom_link = c2_list_first(&loc->fl_runq);
	if (fom_link == NULL)
		return NULL;
	c2_list_del(fom_link);
	fom = c2_list_entry(fom_link, struct c2_fom, fo_rwlink);
	if (fom != NULL && loc->fl_runq_nr > 0)
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
 * @param loc -> c2_fom_locality.
 *
 * @pre c2_loc_invariant(loc) == true.
 */
void c2_loc_handler_thread(struct c2_fom_locality *loc)
{
	c2_time_t	now;
	c2_time_t	delta;
	c2_time_t	expire;
	bool		idle = false;
	struct c2_clink th_clink;

	C2_PRE(c2_locality_invariant(loc));

	c2_mutex_lock(&loc->fl_lock);
	++loc->fl_threads_nr;
        c2_time_set(&delta, 1, 0);
        expire = c2_time_add(c2_time_now(&now), delta);
	c2_clink_init(&th_clink, NULL);
	c2_clink_add(&loc->fl_runrun, &th_clink);
	c2_mutex_unlock(&loc->fl_lock);

	while (true) {

		c2_chan_timedwait(&th_clink, expire);

		/* acquire runq lock */
		c2_mutex_lock(&loc->fl_runq_lock);
		struct c2_fom *fom = NULL;
		fom = c2_fom_dequeue(loc);
		c2_mutex_unlock(&loc->fl_runq_lock);

		/* acquire locality lock */
		c2_mutex_lock(&loc->fl_lock);
		if (fom == NULL) {
			if (!idle) {
				idle = true;
				++loc->fl_idle_threads_nr;
			}

			if (loc->fl_idle_threads_nr > loc->fl_lo_idle_threads_nr) {
				--loc->fl_idle_threads_nr;
				--loc->fl_threads_nr;
				c2_mutex_unlock(&loc->fl_lock);
				break;
			}
			c2_mutex_unlock(&loc->fl_lock);
			continue;
		} else {
			if (idle) {
				idle = false;
				--loc->fl_idle_threads_nr;
			}
			c2_mutex_unlock(&loc->fl_lock);

			fom->fo_state = FOS_RUNNING;
			c2_fom_fop_exec(fom);
		}
	}
	c2_clink_del(&th_clink);
	c2_clink_fini(&th_clink);
}

/**
 * Function to create and add a new thread to the specified
 * locality, and confine it to run only on cores comprising
 * locality.
 *
 * @param loc -> c2_fom_locality.
 * @param confine -> bool -> if true, thread is confined to the processors in locality.
 *			if false, thread is not confined.
 * @retval int -> returns 0, on success.
 *		returns -errno, on failure.
 */
int c2_loc_thr_create(struct c2_fom_locality *loc, bool confine)
{
	int result;
	struct c2_fom_hthread *locthr;

	if (loc == NULL)
		return -EINVAL;

	C2_ALLOC_PTR_ADDB(locthr, &c2_reqh_addb_ctx,
				&c2_fom_addb_loc);
	if (locthr == NULL)
		return -ENOMEM;

	c2_list_link_init(&locthr->fht_linkage);
	c2_mutex_lock(&loc->fl_lock);
	c2_list_add_tail(&loc->fl_threads, &locthr->fht_linkage);

	result = C2_THREAD_INIT(&locthr->fht_thread, struct c2_fom_locality *,
			NULL, &c2_loc_handler_thread, loc, "fom locality thread");

	if (result == 0 && confine) {
		result = c2_thread_confine(&locthr->fht_thread,
				&loc->fl_processors);
	}
	c2_mutex_unlock(&loc->fl_lock);

	if (result) {
		c2_list_del(&locthr->fht_linkage);
		c2_list_link_fini(&locthr->fht_linkage);
		c2_free(locthr);
	}

	return result;
}

/**
 * Function to initialise localities for a fom domain.
 *
 * @param cpu_id -> c2_processor_nr_t, represents a cpu id.
 * @param dom -> c2_fom_domain.
 * @param max_proc -> c2_processor_nr_t, max number of processors.
 * @param fdnr -> int, locality number in the fd_localities array
 *			in fom domain.
 *
 * @retval int -> 0, on success.
 *		-errno, on failure.
 */
int c2_fom_loc_init(c2_processor_nr_t cpu_id, struct c2_fom_domain *dom,
			c2_processor_nr_t max_proc, int fdnr)
{
	int result = 0;
	struct c2_fom_locality *loc;

	loc = &dom->fd_localities[fdnr];
	if (loc == NULL)
		return -EINVAL;
	/* Initialise a locality */
	if (loc->fl_proc_nr == 0) {
		++dom->fd_nr;
		loc->fl_dom = dom;
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
		c2_bitmap_init(&loc->fl_processors, max_proc);
	}
	++loc->fl_proc_nr;
	c2_bitmap_set(&loc->fl_processors, cpu_id, true);
	return result;
}

/**
 * Function to clean up individual fom domain
 * localities.
 *
 * @param loc -> c2_fom_locality.
 *
 * @pre c2_locality_invariant(loc) == true.
 */
void c2_fom_loc_fini(struct c2_fom_locality *loc)
{
	struct c2_list_link *link;
	struct c2_fom_hthread *th;

	C2_PRE(c2_locality_invariant(loc));

	/*
	 * Remove each thread from the thread list
	 * delete the linkage, and invoke thread clean
	 * up function. Later free thread object.
	 */
	c2_mutex_lock(&loc->fl_lock);
	loc->fl_lo_idle_threads_nr = 0;
	c2_chan_broadcast(&loc->fl_runrun);
	while (!c2_list_is_empty(&loc->fl_threads)) {

		link = c2_list_first(&loc->fl_threads);
		if (link == NULL)
			break;
		c2_list_del(link);
		c2_mutex_unlock(&loc->fl_lock);
		th = c2_list_entry(link, struct c2_fom_hthread,
					fht_linkage);
		if (th != NULL) {
			c2_thread_join(&th->fht_thread);
			c2_thread_fini(&th->fht_thread);
		}
		c2_free(th);
		c2_mutex_lock(&loc->fl_lock);
	}
	c2_mutex_unlock(&loc->fl_lock);

	/*
	 * Invoke clean up functions for individual members
	 * of locality.
	 */
	loc->fl_dom = NULL;
	c2_list_fini(&loc->fl_runq);
	loc->fl_runq_nr = 0;
	c2_mutex_fini(&loc->fl_runq_lock);

	c2_list_fini(&loc->fl_wail);
	loc->fl_wail_nr = 0;
	c2_mutex_fini(&loc->fl_wail_lock);

	c2_mutex_fini(&loc->fl_lock);
	c2_chan_fini(&loc->fl_runrun);
	c2_list_fini(&loc->fl_threads);
	loc->fl_threads_nr = 0;

	c2_bitmap_fini(&loc->fl_processors);
}

/**
 * function to check if cpu resource is shared.
 *
 * @param cpu1 -> cpu descriptor object.
 * @param cpu2 -> cpu descriptor object.
 *
 * @retval bool -> returns true, if cpu resource is shared.
 *		returns false, if no cpu resource is shared.
 */
static bool is_resource_shared(struct c2_processor_descr *cpu1,
			struct c2_processor_descr *cpu2)
{
	return (cpu1->pd_l2 == cpu2->pd_l2 ||
		cpu1->pd_numa_node == cpu2->pd_numa_node);
}

/**
 * function to check if the cpu descriptor is initialised.
 *
 * @param cpu -> cpu descriptor object.
 *
 * @retval bool -> returns true, on success.
 * 		returns false, on failure.
 */
static bool check_cpu(struct c2_processor_descr *cpu)
{
	return (cpu->pd_l1_sz > 0 ||
		cpu->pd_l2_sz > 0 );
}

int  c2_fom_domain_init(struct c2_fom_domain *fomdom)
{
	int	i;
	int	j;
	int	i_cpu;
	int	iloc = 0;
	int	result = 0;
	int	nprocs;
	int 	ncpus;
	struct  c2_processor_descr	*cpu_info;
	c2_processor_nr_t		 max_proc;
	c2_processor_nr_t		 rc;
	bool				 val;
	struct c2_bitmap		 onln_map;

	C2_PRE(fomdom != NULL);

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

	fomdom->fd_nr = 0;
	fomdom->fd_ops = &c2_fom_dom_ops;
	c2_bitmap_init(&onln_map, max_proc);
	c2_processors_online(&onln_map);

	/* Get the info of online processors. */
	for (i = 0, i_cpu = 0; i < max_proc; ++i) {
		val = c2_bitmap_get(&onln_map, i);
			if (val) {
				rc = c2_processor_describe(i, &cpu_info[i_cpu]);
				if (rc != 0) {
					C2_SET0(&cpu_info[i_cpu]);
					continue;
				} else
					++i_cpu;
			}
	}

	C2_ALLOC_ARR_ADDB(fomdom->fd_localities, i_cpu, &c2_reqh_addb_ctx, &c2_fom_addb_loc);
	if (fomdom->fd_localities == NULL)
		return -ENOMEM;

	ncpus = i_cpu;
	/*
	 * Find the processors sharing resources and
	 * create localities between them.
	 */
	for (i = 0; i < i_cpu && result == 0; ++i) {
		for (j = i; j < i_cpu && result == 0; ++j) {
			/*
			 * As every cpu descriptor in the array is reset to 0, when
			 * visited first time (to improve performance), so during
			 * further iterations, we first check if the descriptor is
			 * initialised to proceed further.
			 */
			if (check_cpu(&cpu_info[j])) {
				if (is_resource_shared(&cpu_info[i],
					&cpu_info[j]))
					ncpus--;
					result = c2_fom_loc_init(cpu_info[j].pd_id,
					fomdom, max_proc, iloc);
				}
		}
		C2_SET0(&cpu_info[i]);
		++iloc;
		if (ncpus <= 0)
			break;
	}

	for (i = 0; i < iloc && result == 0; ++i) {
		nprocs = fomdom->fd_localities[i].fl_proc_nr;
		while (nprocs > 0 && result == 0) {
			result = c2_loc_thr_create(&fomdom->fd_localities[i], true);
			--nprocs;
		}
	}

	if (result)
		c2_fom_domain_fini(fomdom);

	c2_free(cpu_info);
	c2_bitmap_fini(&onln_map);
	return result;
}

void c2_fom_domain_fini(struct c2_fom_domain *dom)
{
	int	i;
	size_t	lfd_nr;

	C2_PRE(c2_fom_domain_invariant(dom));

	lfd_nr  = dom->fd_nr;
	for(i = 0; i < lfd_nr; ++i) {
		c2_fom_loc_fini(&dom->fd_localities[i]);
	}

	c2_free(dom->fd_localities);

	dom->fd_localities = NULL;
	dom->fd_nr = 0;
	dom->fd_ops = NULL;

	c2_free(dom);
}

int c2_fom_init(struct c2_fom *fom)
{
	/* Set fom state to FOS_READY and fom phase to FOPH_INIT. */

	C2_PRE(fom != NULL);

	fom->fo_state = FOS_READY;
	fom->fo_phase = FOPH_INIT;
	fom->fo_rep_fop = NULL;
	C2_ALLOC_PTR(fom->fo_fop_ctx);
	if (fom->fo_fop_ctx == NULL)
		return -ENOMEM;

	c2_addb_ctx_init(&fom->fo_fop->f_addb, &c2_fom_addb_ctx_type,
				&c2_addb_global_ctx);

	c2_clink_init(&fom->fo_clink, &fom_cb);
	c2_list_link_init(&fom->fo_rwlink);

	return 0;
}

void c2_fom_fini(struct c2_fom *fom)
{
	C2_PRE(c2_fom_invariant(fom));

	c2_addb_ctx_fini(&fom->fo_fop->f_addb);
	if (c2_clink_is_armed(&fom->fo_clink))
		c2_clink_del(&fom->fo_clink);
	c2_clink_fini(&fom->fo_clink);
	c2_list_link_fini(&fom->fo_rwlink);

	c2_free(fom->fo_fop_ctx);
	fom->fo_loc = NULL;
	fom->fo_type = NULL;
	fom->fo_ops = NULL;
	fom->fo_fop = NULL;
	fom->fo_fol = NULL;
	fom->fo_domain = NULL;
	fom->fo_stdomain = NULL;
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
