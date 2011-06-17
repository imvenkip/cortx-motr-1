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
#include "lib/thread.h"
#include "lib/queue.h"
#include "lib/chan.h"
#include "lib/processor.h"
#include "lib/time.h"
#include "lib/timer.h"

#include "addb/addb.h"
#include "fop/fom.h"
#include "fop/fop.h"
#include "reqh/reqh.h"

#define MIN_IDLE_THREADS 1
#define MAX_IDLE_THREADS 2

/**
   @addtogroup fom
   @{
 */

const struct c2_addb_loc c2_reqh_addb_loc = {
        .al_name = "reqh"
};

const struct c2_addb_ctx_type c2_reqh_addb_ctx_type = {
        .act_name = "t1-reqh"
};

struct c2_addb_ctx c2_reqh_addb_ctx;
#define REQH_ADDB_ADD(addb_ctx, name, rc)  \
C2_ADDB_ADD(&addb_ctx, &c2_reqh_addb_loc, c2_addb_func_fail, (name), (rc))

/**
 * fom domain operations structure, containing
 * function pointer to identify home locality
 * for fom execution.
 */
static struct c2_fom_domain_ops c2_fom_dom_ops = {
        .fdo_time_is_out = NULL
};

int c2_loc_thr_create(struct c2_fom_locality *loc, bool confine);

/** 
 * fom domain invariant 
 */
bool c2_fom_domain_invariant(const struct c2_fom_domain *dom)
{
	if (dom == NULL)
		return false;
	if (dom->fd_localities == NULL)
		return false;
	if (dom->fd_ops == NULL)
		return false;
	return true;
}

/**
 * Call back function to be invoked when the channel
 * is signalled.
 * This function removes the fom from the locality wait list
 * and puts it back on the locality run queue for further
 * execution.
 */
static void c2_fom_cb(struct c2_clink *clink)
{
	C2_ASSERT(clink != NULL);

	struct c2_fom *fom = container_of(clink, struct c2_fom, fo_clink);
	C2_ASSERT(fom != NULL);
	c2_list_del(&fom->fo_wlink);
	c2_mutex_lock(&fom->fo_loc->fl_wail_lock);
	--fom->fo_loc->fl_wail_nr;
	c2_mutex_unlock(&fom->fo_loc->fl_wail_lock);
	fom->fo_state = FOS_READY;
	c2_fom_queue(fom);
}

/**
 * Function invoked before potential blocking point.
 * Checks whether the locality has "enough" idle threads. If not, additional
 * threads is started to cope with possible blocking point.
 */
void c2_fom_block_enter(struct c2_fom_locality *loc)
{
	int result;
	
	result = 0;
	C2_PRE(loc != NULL);
	if (loc->fl_idle_threads_nr < loc->fl_lo_idle_nr) {
		result = c2_loc_thr_create(loc, false);
		C2_ASSERT(result == 0);
	}
}

/**
 * Funtion to destroy extra worker threads,
 * in a locality.
 */
void c2_fom_block_leave(struct c2_fom_locality *loc)
{
	struct c2_list_link *link = NULL;
	struct c2_fom_hthread *th = NULL;
	C2_ASSERT(loc != NULL);

	if (loc->fl_idle_threads_nr >= loc->fl_hi_idle_nr) {

		/* kill extra thread
		 * set fd_clean flag to 1, to kill
		 * a single thread.
	 	 */
		loc->fl_dom->fd_clean = 1;
		c2_chan_signal(&loc->fl_runrun);
		if (!c2_list_is_empty(&loc->fl_threads)) {
			c2_mutex_lock(&loc->fl_lock);
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
		loc->fl_dom->fd_clean = 0;
	}
}

/**
 * Funtion to identify and assign an appropriate home locality
 * to the fom, for execution.
 */
/*size_t fdo_find_home_locality(const struct c2_fom_domain *dom, struct c2_fom *fom)
{	
	int result;
	result = 0;
	size_t lfd_nr = dom->fd_nr;
	C2_ASSERT(dom != NULL);
	C2_ASSERT(fom != NULL);

	while(lfd_nr) {
		if (dom->fd_localities[lfd_nr].fl_idle_threads_nr > 0) {
			fom->fo_loc = &dom->fd_localities[i];
			break;
		}
		--lfd_nr;
	}
	if (fom->fo_loc == NULL)
		result = -EAGAIN;

	return result;
}*/

/**
 * Function to enqueue ready to be executed fom into the locality
 * run queue.
 * we first indentify an appropriate locality in which fom
 * could be executed and then enqueue fom into its run queue.
 */
void c2_fom_queue(struct c2_fom *fom)
{
	C2_PRE(fom->fo_loc != NULL);
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
 */
void c2_fom_wait(struct c2_fom *fom)
{
	C2_PRE(fom->fo_loc != NULL);
	struct c2_fom_locality *loc = NULL;
	loc = fom->fo_loc;
	/* change fom state to FOS_WAIT */
	fom->fo_state = FOS_WAITING;
	c2_mutex_lock(&loc->fl_wail_lock);
	c2_list_add_tail(&loc->fl_wail, &fom->fo_wlink);
	++loc->fl_wail_nr;
	c2_mutex_unlock(&loc->fl_wail_lock);
}

/**
 * Function to register the fom with the given wait channel.
 */
int c2_fom_block_at(struct c2_fom *fom, struct c2_chan *chan)
{
	if (fom == NULL || chan == NULL)
		return -EINVAL;
	C2_PRE(!c2_clink_is_armed(&fom->fo_clink));
	c2_clink_add(chan, &fom->fo_clink);
	c2_fom_wait(fom);
	return 0;
}

/**
 * Function to execute fop specific operation.
 * success : returns 0
 * failure : returns negative value
 */
int c2_fom_fop_exec(struct c2_fom *fom)
{
	int stresult;
	int rc;

	stresult = 0;
	rc = 0;
	if (fom == NULL)
		return -EINVAL;

	stresult = fom->fo_ops->fo_state(fom);
	if (stresult ==  FSO_WAIT) {
		/* fop execution is blocked 
		 * specific fom state execution would invoke
		 * c2_fom_block_at routine, to put fom on the 
		 * wait list of this locality.
		 */
		C2_ASSERT(c2_list_contains(&fom->fo_loc->fl_wail, &fom->fo_wlink));
	} else
	if (stresult == FSO_AGAIN) {
		if (fom->fo_phase != FOPH_FAILED){
			fom->fo_phase = FOPH_TXN_CONTEXT;
			rc = c2_fom_state_generic(fom);

			/* fop execution is done, and the reply is sent.
			 * check if we added fol record successfully,
			 * and record the state in addb accordingly.
			 */
			if(!rc && fom->fo_phase == FOPH_DONE) {
				REQH_ADDB_ADD(fom->fo_fop->f_addb,
						"FOM execution success",
						rc);
			} else {
				REQH_ADDB_ADD(fom->fo_fop->f_addb,
						"Adding fol record failed",
						rc);
			}
			fom->fo_ops->fo_fini(fom);
		} else {

			 /* Reply sent, but fop execution failed,
			  * record the failure in addb and clean up the fom.
			  */
			rc = -ECANCELED;
		 	REQH_ADDB_ADD(fom->fo_fop->f_addb,
					"reply sent but fop execution failed",
					rc);
			fom->fo_ops->fo_fini(fom);
		}
	}
	return rc;
}

/**
 * Thread function to execute fom.
 * We start with executing generic phases of the fom
 * and then proceed to execute fop specific operation.
 */
void c2_loc_thr_start(struct c2_fom_locality *loc)
{
	int rc;
	struct c2_clink th_clink;

	rc = 0;
	if (loc == NULL)
		return;

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
		c2_mutex_lock(&loc->fl_lock);
		++loc->fl_idle_threads_nr;
		c2_mutex_unlock(&loc->fl_lock);
		c2_chan_wait(&th_clink);

		c2_mutex_lock(&loc->fl_lock);
		if (loc->fl_idle_threads_nr > 0)
			--loc->fl_idle_threads_nr;
		c2_mutex_unlock(&loc->fl_lock);

		/* Check if we have anything in runq, else
		 * this could be a terminate request.
		 */
		if (loc->fl_runq_nr <= 0) {
			/* check if this is a terminate request */
			if (loc->fl_dom->fd_clean == 1) {
				/* we came here through c2_fom_block_enter 
				 * wait will expire after 2 seconds
				 */
				c2_time_t       now;
			        c2_time_t       delta;
				c2_time_t       expire;
				c2_time_set(&delta, 2, 0);
				expire = c2_time_add(c2_time_now(&now), delta);
				/* wait for 2 seconds */
				c2_chan_timedwait(&th_clink, expire);
				if (loc->fl_idle_threads_nr > loc->fl_lo_idle_nr)
					break;
			} else 
			  if (loc->fl_dom->fd_clean == 2)
				break;
			continue;
		}

		c2_mutex_lock(&loc->fl_runq_lock);
		struct c2_queue_link *fom_link = NULL;
		fom_link = c2_queue_get(&loc->fl_runq);
		if(fom_link == NULL) {
			c2_mutex_unlock(&loc->fl_runq_lock);
			continue;
		}
			--loc->fl_runq_nr;
		c2_mutex_unlock(&loc->fl_runq_lock);

		/* Extract the fom and start processing
		 * the generic phases of the fom
		 * before executing the actual fop.
		 */
		struct c2_fom *fom = NULL;
		fom = container_of(fom_link, struct c2_fom, fo_qlink);

		C2_ASSERT(fom->fo_loc == loc);

		/* Change fom state to FOS_RUNNING */
		if (fom->fo_state == FOS_READY)
			fom->fo_state = FOS_RUNNING;
		
		if (fom->fo_phase < FOPH_EXEC)
			rc = c2_fom_state_generic(fom);

		if (rc) {

			/* fom execution failed in generic phase,
			 * send fop reply, and record failure in
			 * addb and clean up fom.
			 */
			 rc = fom->fo_ops->fo_fail(fom);
			 REQH_ADDB_ADD(fom->fo_fop->f_addb,
					"FOM execution failed in generic phase",
					rc);
			/* clean up fom */
			fom->fo_ops->fo_fini(fom);
		}

		if (fom->fo_phase == FOPH_EXEC || fom->fo_phase > FOPH_NR) {
			rc = c2_fom_fop_exec(fom);
		}
	}
	c2_clink_del(&th_clink);
	c2_clink_fini(&th_clink);
}

/**
 * Function to create and add a new thread to the specified
 * locality, and confine it to run only on cores comprising
 * locality.
 * success : returns 0
 * failure : returns negative value
 */
int c2_loc_thr_create(struct c2_fom_locality *loc, bool confine)
{
	int result;
	int i;
	if (loc == NULL)
		return -EINVAL;
	for(i = 0; i < loc->fl_proc_nr; ++i) {
		struct c2_fom_hthread *locthr = NULL;
	 
		result = 0;

		C2_ALLOC_PTR_ADDB(locthr, &c2_reqh_addb_ctx, 
					&c2_reqh_addb_loc);
		if (locthr == NULL)
			return -ENOMEM;
		c2_list_link_init(&locthr->fht_linkage);
		c2_mutex_lock(&loc->fl_lock);
		c2_list_add_tail(&loc->fl_threads, &locthr->fht_linkage);
		++loc->fl_threads_nr;
		c2_mutex_unlock(&loc->fl_lock);

		result = C2_THREAD_INIT(&locthr->fht_thread, struct c2_fom_locality *,
				NULL, &c2_loc_thr_start, loc, "fom locality thread");
		if (confine)
			result = c2_thread_confine(&locthr->fht_thread,
					&loc->fl_processors);
		C2_ASSERT(result == 0);
	}
	return result;
}

/**
 * Function to initialize localities for a particular
 * fom domain. 
 */
int c2_fom_loc_init(c2_processor_nr_t cpu_id, struct c2_fom_domain *dom, 
			c2_processor_nr_t max_proc, int fdnr) 
{
	int result;
	struct c2_fom_locality *loc = NULL;
	
	loc = &dom->fd_localities[fdnr];
	result = 0;	
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
 */
void c2_fom_loc_fini(struct c2_fom_locality *loc)
{
	struct c2_list_link *link = NULL;
	struct c2_fom_hthread *th = NULL;

	/* First broadcast all the threads in locality, that
	 * we are terminating.
	 * set fd_clean flag to 2 in fom domain to clean up
	 * all the threads.
	 */
	loc->fl_dom->fd_clean = 2;
	c2_chan_broadcast(&loc->fl_runrun);

	/* Remove each thread from the thread list
	 * delete the linkage, and invoke thread clean
	 * up function. Later free thread object.
	 */
	while (!c2_list_is_empty(&loc->fl_threads)) {
		c2_mutex_lock(&loc->fl_lock);
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

	/* Invoke clean up functions for individual members
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

bool is_resource_shared(struct c2_processor_descr *cpu1, struct c2_processor_descr *cpu2)
{
	return (cpu1->pd_l2 == cpu2->pd_l2 || 
		cpu1->pd_numa_node == cpu2->pd_numa_node ||
		cpu1->pd_pipeline == cpu2->pd_pipeline);
}

bool check_cpu(struct c2_processor_descr *cpu)
{
	return (cpu->pd_l1_sz > 0 ||
		cpu->pd_l2_sz > 0 );

}
/**
 * Funtion to initialize fom domain.
 * success: returns 0
 * failure: returns negative value
 */
int  c2_fom_domain_init(struct c2_fom_domain *fomdom, size_t nr)
{
	int	i;
	int	j;
	int 	i_cpu;
	int	iloc = 0;
	int	result = 0;
	struct  c2_processor_descr	*cpu_info;
	c2_processor_nr_t	max_proc, rc;
	bool	val;
	struct c2_bitmap	onln_map;

	/* check number of processors online and create localities
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
	fomdom->fd_clean = false;
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

	int ncpus;
	ncpus = i_cpu;
	/* Find the processors sharing resources and 
	 * create localities between them.
	 */
	for (i = 0; i < i_cpu; ++i) {
		for (j = i; j < i_cpu; ++j) {
			if (check_cpu(&cpu_info[j])) {
				if (is_resource_shared(&cpu_info[i],
					&cpu_info[j]))
					ncpus--;
					result = c2_fom_loc_init(cpu_info[j].pd_id,
					fomdom, max_proc, iloc);
					if (result)
						break;
				}
		}
		C2_SET0(&cpu_info[i]);
		++iloc;
		if (ncpus <= 0)
			break;
	}

	for (i = 0; i < iloc; ++i) {
		c2_loc_thr_create(&fomdom->fd_localities[i], true);	
	}
	return result;
}

/**
 * Clean up function for fom domain.
 */
void c2_fom_domain_fini(struct c2_fom_domain *dom)
{
	int i;
	size_t lfd_nr;
	lfd_nr  = dom->fd_nr;
	C2_ASSERT(c2_fom_domain_invariant(dom) == true);

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
 */
void c2_fom_init(struct c2_fom *fom)
{
	/* Set fom state to FOS_READY and fom phase to FOPH_INIT.
	 * Initialize temporary wait channel, would be removed later.
	 */
	int result = 0;
	C2_ASSERT(fom != NULL);
	fom->fo_state = FOS_READY;
	fom->fo_phase = FOPH_INIT;

	/* Initialize addb context present in fop and use it to record
	 * fop execution path.
	 */
	result = fom->fo_domain->sd_ops->sdo_tx_make(fom->fo_domain, 
							&fom->fo_tx);
	c2_addb_ctx_init(&fom->fo_fop->f_addb, &c2_reqh_addb_ctx_type,
				&c2_addb_global_ctx);
	C2_ASSERT(result == 0);
	c2_clink_init(&fom->fo_clink, &c2_fom_cb);
	c2_queue_link_init(&fom->fo_qlink);
	c2_list_link_init(&fom->fo_wlink);
}

/**
 * Fom clean up function.
 */
void c2_fom_fini(struct c2_fom *fom)
{
	int rc = 0;
	C2_ASSERT(fom != NULL);
	if (fom->fo_phase == FOPH_DONE) {
		/* Commit db transaction.*/
		rc = c2_db_tx_commit(&fom->fo_tx.tx_dbtx);
		if (rc)
			REQH_ADDB_ADD(fom->fo_fop->f_addb, 
					"DB commit failed", 
					rc);
	} else {
		/* Abort db transaction in case of failure.*/
		rc = c2_db_tx_abort(&fom->fo_tx.tx_dbtx);
		if (rc)
			REQH_ADDB_ADD(fom->fo_fop->f_addb, 
					"DB abort failed", 
					rc);
	}

	c2_addb_ctx_fini(&fom->fo_fop->f_addb);
	if(c2_clink_is_armed(&fom->fo_clink))
		c2_clink_del(&fom->fo_clink);
	c2_clink_fini(&fom->fo_clink);
	c2_queue_link_fini(&fom->fo_qlink);
	c2_list_link_fini(&fom->fo_wlink);
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
