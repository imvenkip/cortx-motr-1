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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

#define MIN_IDLE_THREADS (1)
#define MAX_IDLE_THREADS (2)
#define CLEAN_ONE	(1)
#define CLEAN_ALL	(2)
#define WAIT_BEFORE_CLEANUP (2)

/**
   @addtogroup fom
   @{
 */

/**
 * Global c2_addb_loc object for logging addb context.
 */
extern const struct c2_addb_loc c2_reqh_addb_loc;

/**
 * Global addb context type for logging addb context.
 */
extern const struct c2_addb_ctx_type c2_reqh_addb_ctx_type;

/**
 * Global addb context for addb logging.
 */
extern struct c2_addb_ctx c2_reqh_addb_ctx;
#define FOM_ADDB_ADD(addb_ctx, name, rc)  \
C2_ADDB_ADD(&addb_ctx, &c2_reqh_addb_loc, c2_addb_func_fail, (name), (rc))

/**
 * fom domain operations structure,
 * for fom execution.
 */
static struct c2_fom_domain_ops c2_fom_dom_ops = {
	.fdo_time_is_out = NULL
};

/**
 * Function to create and initialize locality threads.
 */
int c2_loc_thr_create(struct c2_fom_locality *loc, bool confine);
extern void c2_reqh_send_err_rep(struct c2_service *service, void *cookie, int rc);

/**
 * fom domain invariant, checks is fom domain's members
 * are valid.
 * @param dom -> c2_fom_domain structure pointer.
 * @retval bool -> true, on success.
 *		false, on failure.
 */
bool c2_fom_domain_invariant(const struct c2_fom_domain *dom)
{
	if (dom == NULL || dom->fd_localities == NULL
			|| dom->fd_ops == NULL)
		return false;

	return true;
}

/**
 * locality invariant checks if members of locality are
 * valid.
 * @param loc -> c2_fom_locality structure pointer.
 * @retval bool -> true, on success.
 *		   false, on failure.
 */
bool c2_locality_invariant(const struct c2_fom_locality *loc)
{
	if (loc == NULL || loc->fl_dom == NULL)
		return false;
	if (!c2_queue_invariant(&loc->fl_runq))
		return false;
	if (!c2_list_invariant(&loc->fl_wail))
		return false;
	if (!c2_list_invariant(&loc->fl_threads))
		return false;

	return true;
}

/**
 * fom invariant
 * @param fom -> c2_fom structure pointer
 * @retval bool -> true, on success.
 *		   false, on failure.
 */
bool c2_fom_invariant(const struct c2_fom *fom)
{
	if (fom == NULL || fom->fo_type == NULL || fom->fo_ops == NULL ||
		fom->fo_fop_ctx == NULL || fom->fo_fop == NULL ||
		fom->fo_fol == NULL || fom->fo_domain == NULL)
		return false;

	if(!c2_list_link_invariant(&fom->fo_wlink))
		return false;

	if (!c2_locality_invariant(fom->fo_loc))
		return false;

	return true;
}

/**
 * Call back function to be invoked when the channel
 * is signalled.
 * This function removes the fom from the locality wait list
 * and puts it back on the locality run queue for further
 * execution.
 * @param clink -> c2_clink structure pointer received from call back.
 * @pre assumes clink is not null.
 */
static void fom_cb(struct c2_clink *clink)
{
	struct c2_fom *fom = NULL;

	C2_PRE(clink != NULL);

	fom = container_of(clink, struct c2_fom, fo_clink);
	C2_ASSERT(c2_fom_invariant(fom));

	c2_mutex_lock(&fom->fo_loc->fl_wail_lock);
	c2_list_del(&fom->fo_wlink);
	--fom->fo_loc->fl_wail_nr;
	fom->fo_state = FOS_READY;
	c2_mutex_unlock(&fom->fo_loc->fl_wail_lock);
	c2_fom_queue(fom);
}

/**
 * Function invoked before potential blocking point.
 * Checks whether the locality has "enough" idle threads. If not, additional
 * threads is started to cope with possible blocking point.
 * @param loc -> c2_fom_locality structure pointer.
 * @retval int -> returns 0, if thread is created,
 *		returns -ve value, if thread creation fails.
 * @pre assumes loc is valid, c2_locality_invariant
 *	should return true.
 */
int c2_fom_block_enter(struct c2_fom_locality *loc)
{
	int rc = 0;

	C2_PRE(c2_locality_invariant(loc));

	c2_mutex_lock(&loc->fl_lock);
	if (loc->fl_idle_threads_nr < loc->fl_lo_idle_nr) {
		while (loc->fl_idle_threads_nr != loc->fl_hi_idle_nr) {
			rc = c2_loc_thr_create(loc, false);
			if (rc)
				break;
		}
	}
	c2_mutex_unlock(&loc->fl_lock);
	return rc;
}

/**
 * Funtion to destroy extra worker threads,
 * in a locality.
 * @param loc -> c2_fom_locality structure pointer.
 * @pre assumes loc is valid, c2_locality_invariant
 *	should return true.
 */
int c2_fom_block_leave(struct c2_fom_locality *loc)
{
	struct c2_list_link *link = NULL;
	struct c2_fom_hthread *th = NULL;
	int rc = 0;

	C2_PRE(c2_locality_invariant(loc));

	c2_mutex_lock(&loc->fl_lock);
	while (loc->fl_idle_threads_nr > loc->fl_lo_idle_nr) {

		/*
		 * kill extra thread
		 * set fd_clean flag to CLEAN_ONE, to kill
		 * a single thread.
		 */
		loc->fl_dom->fd_clean = CLEAN_ONE;
		link = c2_list_first(&loc->fl_threads);
		if (link == NULL) {
			loc->fl_dom->fd_clean = 0;
			c2_mutex_unlock(&loc->fl_lock);
			return -EINVAL;
		}
		c2_list_del(link);
		c2_mutex_unlock(&loc->fl_lock);
		c2_chan_signal(&loc->fl_runrun);
		th = c2_list_entry(link, struct c2_fom_hthread,
				fht_linkage);
		if (th != NULL) {
			rc = c2_thread_join(&th->fht_thread);
			if (!rc) {
				c2_thread_fini(&th->fht_thread);
				c2_free(th);
			}
			else
				break;
		}
		c2_mutex_lock(&loc->fl_lock);
		loc->fl_dom->fd_clean = 0;
	}
	c2_mutex_unlock(&loc->fl_lock);

	return rc;
}

/**
 * Function to enqueue ready to be executed fom into the locality
 * run queue.
 * we first indentify an appropriate locality in which fom
 * could be executed and then enqueue fom into its run queue.
 * @param fom -> c2_fom structure pointer.
 */
void c2_fom_queue(struct c2_fom *fom)
{
	struct c2_fom_locality *loc = NULL;

	loc = fom->fo_loc;
	c2_mutex_lock(&loc->fl_runq_lock);
	c2_queue_put(&loc->fl_runq, &fom->fo_qlink);
	++loc->fl_runq_nr;
	c2_mutex_unlock(&loc->fl_runq_lock);
	c2_chan_signal(&loc->fl_runrun);
}

/**
 * Function to put fom into locality's wait list
 * during a fom's blocking operation.
 * fom state is changed to FOS_WAIT.
 * @param fom -> c2_fom structure pointer.
 */
void c2_fom_wait(struct c2_fom *fom)
{
	struct c2_fom_locality *loc = NULL;

	loc = fom->fo_loc;
	fom->fo_state = FOS_WAITING;
	c2_mutex_lock(&loc->fl_wail_lock);
	c2_list_add_tail(&loc->fl_wail, &fom->fo_wlink);
	++loc->fl_wail_nr;
	c2_mutex_unlock(&loc->fl_wail_lock);
}

/**
 * Function to register the fom with the given wait channel.
 * @param fom -> c2_fom structure pointer.
 * @param chan -> c2_chan structure pointer.
 * @retval int -> 0, on success.
 *		  -1, on failure.
 * @pre assumes fom is valid, c2_fom_invariant should return true.
 * @pre assumes fom->fo_clink -> is not armed.
 */
void c2_fom_block_at(struct c2_fom *fom, struct c2_chan *chan)
{
	C2_PRE(c2_fom_invariant(fom));

	C2_PRE(!c2_clink_is_armed(&fom->fo_clink));

	c2_clink_add(chan, &fom->fo_clink);
	c2_fom_wait(fom);
}

/**
 * Function to execute fop specific operation.
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is valid, c2_fom_variant should return true.
 */
void c2_fom_fop_exec(struct c2_fom *fom)
{
	int stresult = 0;

	C2_PRE(c2_fom_invariant(fom));

	stresult = fom->fo_ops->fo_state(fom);

	switch(stresult) {
	case FSO_WAIT:
		/*
		 * fop execution is blocked
		 * specific fom state execution would invoke
		 * c2_fom_block_at routine, to put fom on the
		 * wait list of this locality.
		 */
		C2_ASSERT(c2_list_contains(&fom->fo_loc->fl_wail, &fom->fo_wlink));
		break;
	case FSO_AGAIN:
		/*
		 *fop execution is done, and the reply is sent.
		 * check if we added fol record successfully,
		 * and record the state in addb accordingly.
		 */
		fom->fo_phase = FOPH_DONE;
		FOM_ADDB_ADD(fom->fo_fop->f_addb,
				"FOM execution success",
				0);
		fom->fo_ops->fo_fini(fom);
		break;
	default:
		 /*
		  * fop execution failed, record the failure in addb and
		  * clean up the fom.
		  * Assuming error reply fop is sent by the fom state method
		  * before returning.
		  */
		FOM_ADDB_ADD(fom->fo_fop->f_addb,
				"fop execution failed",
				stresult);
		fom->fo_ops->fo_fini(fom);
		break;
	}
}

/**
 * function to dequeue fom from a locality runq.
 * @param loc -> c2_fom_locality structure pointer.
 * @retval int -> returns 0, if fom is successfully dequeued
 *		returns -1, if fom dequeue fails.
 */
int c2_fom_dequeue(struct c2_fom_locality *loc, struct c2_fom **fom)
{
	struct c2_queue_link *fom_link = NULL;
	int rc = 0;

	c2_mutex_lock(&loc->fl_runq_lock);
	fom_link = c2_queue_get(&loc->fl_runq);

	if(fom_link == NULL) {
		FOM_ADDB_ADD(c2_reqh_addb_ctx,
				"c2_fom_dequeue: Invalid fom link",
				-EINVAL);
		c2_mutex_unlock(&loc->fl_runq_lock);
		rc = -EINVAL;
	}

	--loc->fl_runq_nr;
	c2_mutex_unlock(&loc->fl_runq_lock);

	/*
	 * Extract the fom and start processing
	 * the generic phases of the fom
	 * before executing the actual fop.
	 */
	*fom = container_of(fom_link, struct c2_fom, fo_qlink);
	C2_ASSERT(c2_fom_invariant(*fom));

	/* Change fom state to FOS_RUNNING */
	if ((*fom)->fo_state != FOS_READY)
		rc = -EINVAL;

	(*fom)->fo_state = FOS_RUNNING;

	return rc;
}

/**
 * Thread function to execute fom.
 * We start with executing generic phases of the fom
 * and then proceed to execute fop specific operation.
 * @param loc -> c2_fom_locality structure pointer.
 * @pre assumes loc is valid, c2_locality_invariant,
 *	should return true.
 */
void c2_loc_thr_start(struct c2_fom_locality *loc)
{
	int rc = 0;
	struct c2_clink th_clink;

	C2_PRE(c2_locality_invariant(loc));

	++loc->fl_threads_nr;
	++loc->fl_idle_threads_nr;

	c2_clink_init(&th_clink, NULL);
	c2_mutex_lock(&loc->fl_lock);
	c2_clink_add(&loc->fl_runrun, &th_clink);
	c2_mutex_unlock(&loc->fl_lock);

	while (1) {

		/*
		 * 1) Initialize a clink and add it to the locality's wait channel
		 * 2) wait until there's fom to execute in the locality's run queue
		 * 3) Fom is submitted for execution in the run queue
		 * 4) one of the waiting locality thread is woken up by
		 *    by signaling the wait channel.
		 * 5) A single thread dequeues the fom and starts executing it.
		 * 6) Decrements the idle number of threads in the locality.
		 */
		c2_chan_wait(&th_clink);

		c2_mutex_lock(&loc->fl_lock);
		if (loc->fl_idle_threads_nr > 0)
			--loc->fl_idle_threads_nr;
		c2_mutex_unlock(&loc->fl_lock);

		/*
		 * Check if we have anything in runq, else
		 * this could be a terminate request.
		 */
		c2_mutex_lock(&loc->fl_lock);
		if (loc->fl_dom->fd_clean > 0) {
			/* check if this is a terminate request */
			if (loc->fl_dom->fd_clean == CLEAN_ONE) {
				c2_mutex_unlock(&loc->fl_lock);
				/*
				 * we came here through c2_fom_block_enter
				 * wait for 2 seconds before exiting.
				 * again check if idle threads are higher than
				 * fl_lo_idle_nr, if yes, break, else continue.
				 */
				c2_time_t	now;
				c2_time_t	delta;
				c2_time_t	expire;
				c2_time_set(&delta, WAIT_BEFORE_CLEANUP, 0);
				expire = c2_time_add(c2_time_now(&now), delta);
				c2_chan_timedwait(&th_clink, expire);
				c2_mutex_lock(&loc->fl_lock);
				if (loc->fl_idle_threads_nr > loc->fl_lo_idle_nr) {
					c2_mutex_unlock(&loc->fl_lock);
					break;
				}
			} else
			if (loc->fl_dom->fd_clean == CLEAN_ALL) {
				c2_mutex_unlock(&loc->fl_lock);
				break;
			}
		}
		c2_mutex_unlock(&loc->fl_lock);

		c2_mutex_lock(&loc->fl_runq_lock);
		if (loc->fl_runq_nr <= 0) {
			c2_mutex_unlock(&loc->fl_runq_lock);
			continue;
		}
		c2_mutex_unlock(&loc->fl_runq_lock);

		struct c2_fom *fom = NULL;
		rc = c2_fom_dequeue(loc, &fom);
		if (rc) {
			c2_mutex_lock(&loc->fl_lock);
			++loc->fl_idle_threads_nr;
			c2_mutex_unlock(&loc->fl_lock);
			continue;
		}

		/* check if we need to execute generic phases */
		if (fom->fo_phase >= FOPH_INIT && fom->fo_phase < FOPH_EXEC)
			rc = c2_fom_state_generic(fom);

		if (rc) {

			/*
			 * fom execution failed in generic phase,
			 * send fop reply, and record failure in
			 * addb and clean up fom.
			 */
			if (rc == -ENOMEM) {

				FOM_ADDB_ADD(c2_reqh_addb_ctx,
						"FOM execution failed, out of memory",
						rc);
				continue;
			} else {
				if (fom != NULL && fom->fo_fop_ctx != NULL)
					c2_reqh_send_err_rep(fom->fo_fop_ctx->ft_service,
								fom->fo_fop_ctx->fc_cookie, rc);
				FOM_ADDB_ADD(c2_reqh_addb_ctx,
						"FOM execution failed in generic phase",
						rc);
				/* clean up fom */
				fom->fo_ops->fo_fini(fom);
			}
		}

		/*
		 * we reach here after the generic phases or if we come out of
		 * fop specific wait phase, which would be greater than FOPH_NR.
		 */
		if (!rc && (fom->fo_phase == FOPH_EXEC || fom->fo_phase > FOPH_NR)) {
			c2_fom_fop_exec(fom);
		}

		/* increment the idle threads count */
		c2_mutex_lock(&loc->fl_lock);
		++loc->fl_idle_threads_nr;
		c2_mutex_unlock(&loc->fl_lock);
	}
	c2_clink_del(&th_clink);
	c2_clink_fini(&th_clink);
}

/**
 * Function to create and add a new thread to the specified
 * locality, and confine it to run only on cores comprising
 * locality.
 * @param loc -> c2_fom_locality structure pointer.
 * @param confine -> bool 1, to clean single thread in a locality
 *			2, to clean up all the threads in the locality.
 * @retval int -> returns 0, on success.
 *		returns -1, on failure.
 */
int c2_loc_thr_create(struct c2_fom_locality *loc, bool confine)
{
	int result = 0;
	int i = 0;
	struct c2_fom_hthread *locthr = NULL;

	if (loc == NULL)
		return -EINVAL;

	for(i = 0; i < loc->fl_proc_nr && result == 0; ++i) {

		C2_ALLOC_PTR_ADDB(locthr, &c2_reqh_addb_ctx,
					&c2_reqh_addb_loc);
		if (locthr == NULL)
			return -ENOMEM;
		c2_list_link_init(&locthr->fht_linkage);
		c2_list_add_tail(&loc->fl_threads, &locthr->fht_linkage);

		result = C2_THREAD_INIT(&locthr->fht_thread, struct c2_fom_locality *,
				NULL, &c2_loc_thr_start, loc, "fom locality thread");

		if (result == 0 && confine) {
			result = c2_thread_confine(&locthr->fht_thread,
					&loc->fl_processors);
		}

		if (result) {
			c2_list_del(&locthr->fht_linkage);
			c2_list_link_fini(&locthr->fht_linkage);
			c2_free(locthr);
		}
	}

	return result;
}

/**
 * Function to initialize localities for a particular
 * fom domain.
 * @param cpu_id -> c2_processor_nr_t, represents a cpu id.
 * @param dom -> c2_fom_domain structure pointer.
 * @param max_proc -> c2_processor_nr_t, max number of processors.
 * @param fdnr -> int, locality number in the fd_localities array
 *			in fom domain.
 * @retval int -> 0, on success.
 *		-1, on failure.
 */
int c2_fom_loc_init(c2_processor_nr_t cpu_id, struct c2_fom_domain *dom,
			c2_processor_nr_t max_proc, int fdnr)
{
	int result = 0;
	struct c2_fom_locality *loc = NULL;

	loc = &dom->fd_localities[fdnr];
	if (loc == NULL)
		return -EINVAL;
	/* Initialize a locality */
	if (loc->fl_proc_nr == 0) {
		++dom->fd_nr;
		loc->fl_dom = dom;
		c2_queue_init(&loc->fl_runq);
		loc->fl_runq_nr = 0;
		c2_mutex_init(&loc->fl_runq_lock);

		c2_list_init(&loc->fl_wail);
		loc->fl_wail_nr = 0;
		c2_mutex_init(&loc->fl_wail_lock);

		c2_mutex_init(&loc->fl_lock);
		c2_chan_init(&loc->fl_runrun);
		c2_list_init(&loc->fl_threads);

		loc->fl_lo_idle_nr = MIN_IDLE_THREADS;
		loc->fl_hi_idle_nr = MAX_IDLE_THREADS;
		c2_bitmap_init(&loc->fl_processors, max_proc);
	}
	++loc->fl_proc_nr;
	c2_bitmap_set(&loc->fl_processors, cpu_id, true);
	return result;
}

/**
 * Function to clean up individual fom domain
 * localities.
 * @param loc -> c2_fom_locality structure pointer.
 * @pre assumes locality is valid, c2_locality_invariant should
 *	return true.
 */
void c2_fom_loc_fini(struct c2_fom_locality *loc)
{
	struct c2_list_link *link = NULL;
	struct c2_fom_hthread *th = NULL;

	C2_PRE(c2_locality_invariant(loc));

	/*
	 * Remove each thread from the thread list
	 * delete the linkage, and invoke thread clean
	 * up function. Later free thread object.
	 */
	while (!c2_list_is_empty(&loc->fl_threads)) {
		c2_mutex_lock(&loc->fl_lock);
		/*
		 * First broadcast all the threads in locality, that
		 * we are terminating.
		 * set fd_clean flag to CLEAN_ALL in fom domain to clean up
		 * all the threads.
		 */
		loc->fl_dom->fd_clean = CLEAN_ALL;
		c2_chan_broadcast(&loc->fl_runrun);
		link = c2_list_first(&loc->fl_threads);
		if (link != NULL)
			c2_list_del(link);
		c2_mutex_unlock(&loc->fl_lock);
		if (link == NULL)
			return;
		th = c2_list_entry(link, struct c2_fom_hthread,
					fht_linkage);
		if (th != NULL) {
			c2_thread_join(&th->fht_thread);
			c2_thread_fini(&th->fht_thread);
		}
		c2_free(th);
	}

	/*
	 * Invoke clean up functions for individual members
	 * of locality.
	 */
	loc->fl_dom = NULL;
	c2_queue_fini(&loc->fl_runq);
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
 * @param cpu1 -> c2_processor_descr structure pointer to a cpu.
 * @param cpu2 -> c2_processor_descr structure pointer to a cpu.
 * @retval bool -> returns true, if cpu resource is shared.
 *		   returns false, if no cpu resource is shared.
 */
static bool is_resource_shared(struct c2_processor_descr *cpu1,
			struct c2_processor_descr *cpu2)
{
	return (cpu1->pd_l2 == cpu2->pd_l2 ||
		cpu1->pd_numa_node == cpu2->pd_numa_node);
}

/**
 * function to check if the cpu descriptor is initialized.
 * @param cpu -> c2_processor_descr structure pointer.
 * @retval bool -> returns true, on success.
 *		   returns false, on failure.
 */
static bool check_cpu(struct c2_processor_descr *cpu)
{
	return (cpu->pd_l1_sz > 0 ||
		cpu->pd_l2_sz > 0 );
}

/**
 * Funtion to initialize fom domain.
 * @param fomdom -> c2_fom_domain structure pointer.
 * @param nr -> size_t number of localities to create.
 * @retval int -> returns 0, on success.
 *		  returns -1, on failure.
 * @pre assumes fomdom is not null.
 */
int  c2_fom_domain_init(struct c2_fom_domain *fomdom)
{
	int	i;
	int	j;
	int	i_cpu;
	int	iloc = 0;
	int	result = 0;
	int ncpus;
	struct  c2_processor_descr	*cpu_info;
	c2_processor_nr_t	max_proc;
	c2_processor_nr_t	rc;
	bool	val;
	struct c2_bitmap	onln_map;

	C2_PRE(fomdom != NULL);

	/*
	 * check number of processors online and create localities
	 * between one's sharing common resources.
	 * Currently considering shared L2 cache, numa node,
	 * and pipeline between cores, as shared resources.
	 */
	max_proc = c2_processor_nr_max();
	/* Temporary array of c2_processor descriptor */
	cpu_info = c2_alloc(max_proc * sizeof *cpu_info);
	if (cpu_info == NULL)
		return -ENOMEM;

	fomdom->fd_nr = 0;
	fomdom->fd_ops = &c2_fom_dom_ops;
	fomdom->fd_clean = 0;
	c2_bitmap_init(&onln_map, max_proc);
	c2_processors_online(&onln_map);

	/* Get the info of online processors. */
	for (i = 0, i_cpu = 0; i < max_proc; ++i) {
		val = c2_bitmap_get(&onln_map, i);
			if (val) {
				rc = c2_processor_describe(i, &cpu_info[i_cpu]);
				if (rc) {
					C2_SET0(&cpu_info[i_cpu]);
					continue;
				} else
					++i_cpu;
			}
	}

	C2_ALLOC_ARR_ADDB(fomdom->fd_localities, i_cpu, &c2_reqh_addb_ctx, &c2_reqh_addb_loc);
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
			 * initialized to proceed further.
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
		c2_mutex_lock(&fomdom->fd_localities[i].fl_lock);
		result = c2_loc_thr_create(&fomdom->fd_localities[i], true);
		c2_mutex_unlock(&fomdom->fd_localities[i].fl_lock);
	}

	if (result)
		c2_fom_domain_fini(fomdom);

	c2_free(cpu_info);
	c2_bitmap_fini(&onln_map);
	return result;
}

/**
 * Clean up function for fom domain.
 * @param dom -> c2_fom_domain structure pointer.
 * @pre assumes dom is valid, c2_fom_domain_invariant,
 * 	should return true.
 */
void c2_fom_domain_fini(struct c2_fom_domain *dom)
{
	int i = 0;
	size_t lfd_nr = 0;

	C2_PRE(c2_fom_domain_invariant(dom));

	lfd_nr  = dom->fd_nr;
	for(i = 0; i < lfd_nr; ++i) {
		c2_fom_loc_fini(&dom->fd_localities[i]);
		--lfd_nr;
	}

	c2_free(dom->fd_localities);

	dom->fd_localities = NULL;
	dom->fd_nr = 0;
	dom->fd_ops = NULL;
	dom->fd_clean = 0;

	c2_free(dom);
}

/**
 * Fom initializing function.
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is not null.
 */
int c2_fom_init(struct c2_fom *fom)
{
	/*
	 * Set fom state to FOS_READY and fom phase to FOPH_INIT.
	 * Initialize temporary wait channel, would be removed later.
	 */

	C2_PRE(fom != NULL);

	fom->fo_state = FOS_READY;
	fom->fo_phase = FOPH_INIT;
	fom->fo_fop_ctx = c2_alloc(sizeof *fom->fo_fop_ctx);
	if (fom->fo_fop_ctx == NULL)
		return -ENOMEM;

        c2_addb_ctx_init(&fom->fo_fop->f_addb, &c2_reqh_addb_ctx_type,
                                &c2_addb_global_ctx);

	c2_clink_init(&fom->fo_clink, &fom_cb);
	c2_queue_link_init(&fom->fo_qlink);
	c2_list_link_init(&fom->fo_wlink);

	return 0;
}

/**
 * Function to check if transaction context is valid.
 * In case of failure, we abort the transaction, thus,
 * if we fail before even the transaction is initialized.
 * the abort will fail.
 * @param tx -> struct c2_db_tx pointer.
 * @retval bool -> returns true, if transaction is initialized
 *		return false, if transaction is unintialized.
 */
static bool c2_chk_tx(struct c2_db_tx *tx)
{
	return (tx->dt_env != 0);
}

/**
 * Fom clean up function.
 * @param fom -> c2_fom structure pointer.
 * @pre assumes fom is valid, c2_fom_invariant shold return true.
 */
void c2_fom_fini(struct c2_fom *fom)
{
	int rc = 0;

	C2_PRE(c2_fom_invariant(fom));

	if (fom->fo_phase == FOPH_DONE) {
		/* Commit db transaction.*/
		rc = c2_db_tx_commit(&fom->fo_tx.tx_dbtx);
		if (rc)
			FOM_ADDB_ADD(fom->fo_fop->f_addb,
					"DB commit failed",
					rc);
	} else {
		if (c2_chk_tx(&fom->fo_tx.tx_dbtx)) {
			/* Abort db transaction in case of failure.*/
			rc = c2_db_tx_abort(&fom->fo_tx.tx_dbtx);
		}
		FOM_ADDB_ADD(fom->fo_fop->f_addb,
				"DB abort failed",
				rc);
	}

	c2_addb_ctx_fini(&fom->fo_fop->f_addb);
	if (c2_clink_is_armed(&fom->fo_clink))
		c2_clink_del(&fom->fo_clink);
	c2_clink_fini(&fom->fo_clink);
	c2_queue_link_fini(&fom->fo_qlink);
	c2_list_link_fini(&fom->fo_wlink);

	c2_free(fom->fo_fop_ctx);
	fom->fo_loc = NULL;
	fom->fo_type = NULL;
	fom->fo_ops = NULL;
	fom->fo_fop = NULL;
	fom->fo_fol = NULL;
	fom->fo_domain = NULL;
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
