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

#include "addb/addb.h"
#include "fop/fom.h"
#include "fop/fop.h"
#include "reqh/reqh.h"

/**
   @addtogroup fom
   @{
 */

const struct c2_addb_loc reqh_addb_loc = {
        .al_name = "reqh"
};

const struct c2_addb_ctx_type reqh_addb_ctx_type = {
        .act_name = "t1-reqh"
};

#define REQH_ADDB_ADD(addb_ctx, name, rc)  \
C2_ADDB_ADD(&addb_ctx, &reqh_addb_loc, c2_addb_func_fail, (name), (rc))

/**
 * fom domain operations structure, containing
 * function pointer to identify home locality
 * for fom execution.
 */
static struct c2_fom_domain_ops c2_fom_dom_ops = {
        .fdo_home_locality = fdo_find_home_loc,
        .fdo_time_is_out = NULL,
};

int c2_loc_thr_create(struct c2_fom_locality *loc, int confine);

/**
 * Call back function to be invoked when the channel
 * is signalled.
 * This function removes the fom from the locality wait queue
 * and puts it back on the locality run queue for further
 * execution.
 */
static void c2_fom_cb(struct c2_clink *clink)
{
	C2_ASSERT(clink != NULL);

	/* Remove fom from wait list and put fom back on run queue of fom locality */
	struct c2_fom *fom = container_of(clink, struct c2_fom, fo_clink);
	C2_ASSERT(fom != NULL);
	c2_list_del(&fom->fom_link);
	c2_mutex_lock(&fom->fo_loc->fl_wail_lock);
	--fom->fo_loc->fl_wail_nr;
	c2_mutex_unlock(&fom->fo_loc->fl_wail_lock);
	fom->fo_state = FOS_READY;
	/* queue it back to the run queue of fom's locality */
	c2_fom_queue(fom->fo_loc->fl_dom, fom);
}

/**
 * Function invoked before potential blocking point.
 * Checks whether the locality has "enough" idle threads. If not, additional
 * threads is started to cope with possible blocking point.
 */
void c2_fom_block_enter(struct c2_fom_locality *loc)
{
	int result = 0;
	C2_ASSERT(loc != NULL);
	if (loc->fl_idle_threads_nr <= 0 || loc->fl_threads_nr <= 1) {
		result = c2_loc_thr_create(loc, 0);
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

	if (loc->fl_idle_threads_nr > 0) {

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
size_t fdo_find_home_loc(const struct c2_fom_domain *dom, struct c2_fom *fom)
{	
	int i = 0;
	int result = 0;
	size_t lfd_nr = dom->fd_nr;
	C2_ASSERT(dom != NULL);
	C2_ASSERT(fom != NULL);

	while(lfd_nr) {
		if (dom->fd_localities[i].fl_dom != NULL) {
			if (dom->fd_localities[i].fl_idle_threads_nr > 0) {
				fom->fo_loc = &dom->fd_localities[i];
				break;
			}
			--lfd_nr;
		}
		++i;
	}
	if (fom->fo_loc == NULL)
		result = -EAGAIN;

	return result;
}

/**
 * Function to enqueue ready to be executed fom into the locality
 * run queue.
 * we first indentify an appropriate locality in which fom
 * could be executed and then enqueue fom into its run queue.
 */
void c2_fom_queue(struct c2_fom_domain *dom, struct c2_fom *fom)
{
	C2_ASSERT(dom != NULL);
	C2_ASSERT(fom != NULL);

	while((dom->fd_ops->fdo_home_locality(dom, fom)) == -EAGAIN);

	c2_mutex_lock(&fom->fo_loc->fl_runq_lock);
	c2_queue_put(&fom->fo_loc->fl_runq, &fom->fom_linkage);
	++fom->fo_loc->fl_runq_nr;
	c2_mutex_unlock(&fom->fo_loc->fl_runq_lock);
	c2_chan_signal(&fom->fo_loc->fl_runrun);
}

/**
 * Function to register the fom with the given wait channel.
 */
void c2_fom_block_at(struct c2_fom *fom, struct c2_chan *chan)
{
	C2_ASSERT(fom != NULL);
	C2_ASSERT(chan != NULL);

	if (!c2_clink_is_armed(&fom->fo_clink)) {
		c2_clink_add(chan, &fom->fo_clink);
	}
}

/**
 * Function to put fom into locality's wait list
 * during a fom's blocking operation.
 */
void c2_fom_wait(struct c2_fom *fom, struct c2_chan *chan)
{
	C2_ASSERT(fom != NULL);
	C2_ASSERT(chan != NULL);

	c2_fom_block_at(fom, chan);
	c2_list_link_init(&fom->fom_link);
	/* change fom state to FOS_WAIT */
	fom->fo_state = FOS_WAITING;
	c2_mutex_lock(&fom->fo_loc->fl_wail_lock);
	c2_list_add_tail(&fom->fo_loc->fl_wail, &fom->fom_link);
	++fom->fo_loc->fl_wail_nr;
	c2_mutex_unlock(&fom->fo_loc->fl_wail_lock);
	
	/* signaling temporary channel to simulate wake up
	 * from generic phase wait
	 */
	c2_chan_signal(&fom->chan_gen_wait);
}

/**
 * Function to execute fop specific operation.
 * success : returns 0
 * failure : returns negative value
 */
int c2_fom_fop_exec(struct c2_fom *fom)
{
	int stresult = 0;
	int rc = 0;
	if (fom == NULL)
		return -EINVAL;

	stresult = fom->fo_ops->fo_state(fom);
	if (stresult ==  FSO_WAIT) {

		/* fop execution is blocked, put this
		 * fom on the wait list of the locality.
		 * execution will be resumed later.
		 */
		fom->fo_phase = FOPH_EXEC_WAIT;
		c2_list_link_init(&fom->fom_link);
		c2_mutex_lock(&fom->fo_loc->fl_wail_lock);
		c2_list_add_tail(&fom->fo_loc->fl_wail, &fom->fom_link);
		++fom->fo_loc->fl_wail_nr;
		c2_mutex_unlock(&fom->fo_loc->fl_wail_lock);
	} else
	if (stresult == FSO_AGAIN) {
		if (fom->fo_phase != FOPH_FAILED){
			fom->fo_phase = FOPH_TXN_CONTEXT;
			rc = c2_fom_state_generic(fom);

			/* fop execution is done, and the reply is sent.
			 * check if we added fol record successfully,
			 * and record the state in addb accordingly.
			 */
			printf("FOM: fom execution done.\n");
			if(fom->fo_phase == FOPH_DONE) {
				REQH_ADDB_ADD(fom->fo_fop->f_addb,
						"FOM execution success",
						0);
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
			printf("FOM: fop execution failed\n");
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
int c2_loc_thr_start(void *loc)
{
	int rc = 0;
	struct c2_clink th_clink;
	struct c2_queue_link *fom_link = NULL;
	struct c2_fom *fom = NULL;

	if (loc == NULL)
		return -EINVAL;

	struct c2_fom_locality *tloc = (struct c2_fom_locality *)loc;

	c2_clink_init(&th_clink, NULL);
	c2_mutex_lock(&tloc->fl_lock);
	c2_clink_add(&tloc->fl_runrun, &th_clink);
	c2_mutex_unlock(&tloc->fl_lock);

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
wait:		c2_mutex_lock(&tloc->fl_lock);
		++tloc->fl_idle_threads_nr;
		c2_mutex_unlock(&tloc->fl_lock);
		c2_chan_wait(&th_clink);

		c2_mutex_lock(&tloc->fl_lock);
		if (tloc->fl_idle_threads_nr > 0)
			--tloc->fl_idle_threads_nr;
		c2_mutex_unlock(&tloc->fl_lock);

		/* Check if we have anything in runq, else
		 * this could be a terminate request.
		 */
		if (tloc->fl_runq_nr <= 0) {
			/* check if this is a terminate request */
			if (tloc->fl_dom->fd_clean) {
				printf("\nFOM:Thread terminating.. \n");
				break;
			}
			goto wait;
		}

		c2_mutex_lock(&tloc->fl_runq_lock);
		fom_link = c2_queue_get(&tloc->fl_runq);
		if(fom_link == NULL) {
			c2_mutex_unlock(&tloc->fl_runq_lock);
			goto wait;
		}
			--tloc->fl_runq_nr;
		c2_mutex_unlock(&tloc->fl_runq_lock);

		C2_ASSERT(fom_link != NULL);

		/* Extract the fom and start processing
		 * the generic phases of the fom
		 * before executing the actual fop.
		 */
		fom = container_of(fom_link, struct c2_fom, fom_linkage);

		C2_ASSERT(fom != NULL);
		C2_ASSERT(fom->fo_loc == tloc);

		/* Change fom state to FOS_RUNNING */
		if ((fom->fo_state == FOS_READY) ||
			(fom->fo_state == FOS_WAITING))
			fom->fo_state = FOS_RUNNING;

		rc = c2_fom_state_generic(fom);

		if (rc || fom->fo_phase == FOPH_FAILED) {

			/* fom execution failed in generic phase,
			 * send fop reply, and record failure in
			 * addb and clean up fom.
			 */
			 printf("FOM: fom execution failed.\n");
			 rc = fom->fo_ops->fo_fail(fom);
			 REQH_ADDB_ADD(fom->fo_fop->f_addb, 
					"FOM execution failed in generic phase", 
					rc);
			/* clean up fom */
			fom->fo_ops->fo_fini(fom);
		} else
		if (fom->fo_phase == FOPH_EXEC) {
			rc = c2_fom_fop_exec(fom);
		}
	}
	c2_clink_del(&th_clink);
	c2_clink_fini(&th_clink);
	return rc;
}

/**
 * Function to create and add a new thread to the specified
 * locality, and confine it to run only on cores comprising
 * locality.
 * success : returns 0
 * failure : returns negative value
 */
int c2_loc_thr_create(struct c2_fom_locality *loc, int confine)
{
	int result = 0;
	C2_ASSERT(loc != NULL);

	struct c2_fom_hthread *locthr = c2_alloc(sizeof *locthr);
	if (locthr == NULL)
		return -ENOMEM;
	c2_list_link_init(&locthr->fht_linkage);
	c2_mutex_lock(&loc->fl_lock);
	c2_list_add_tail(&loc->fl_threads, &locthr->fht_linkage);
	++loc->fl_threads_nr;
	c2_mutex_unlock(&loc->fl_lock);

	result = c2_thread_init(&locthr->fht_thread,NULL,
			(void (*)(void *))&c2_loc_thr_start, 
			(void*)(unsigned long)loc);
	if (confine)
		result = c2_thread_confine(&locthr->fht_thread, 
				&loc->fl_processors);
	C2_ASSERT(result == 0);

	return result;
}

/**
 * Function to initialize localities for a particular
 * fom domain. This function also creates certain threads per locality.
 * currently the number of threads created are same as the number of
 * cores sharing the resource.
 */
int c2_fom_loc_init(struct c2_processor_descr *cpu_info, 
			struct c2_fom_locality *loc, 
			struct c2_fom_domain *dom, 
			c2_processor_nr_t max_proc)
{
	int result = 0;
	
	/* Check if we need to create a new locality or
	 * add threads to the existing one.
	 */
	if (!loc->fl_dom) {
		/* create a new locality */
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
		loc->fl_threads_nr = 0;
		c2_bitmap_init(&loc->fl_processors, max_proc);
		c2_bitmap_set(&loc->fl_processors, cpu_info->pd_id, 1);
		result = c2_loc_thr_create(loc, 1);
	} else {

		/* locality already exists, just add another
		 * thread to the same locality
		 */
		c2_bitmap_set(&loc->fl_processors, cpu_info->pd_id, 1);
		result = c2_loc_thr_create(loc, 1);
		memset(cpu_info, 0 , sizeof(struct c2_processor_descr));
	}
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

/**
 * Funtion to initialize fom domain.
 * success: returns 0
 * failure: returns negative value
 */
int  c2_fom_domain_init(struct c2_fom_domain **fomdom, size_t nr)
{
	int	i = 0;
	int	j = 0;
	int	onln_cpus = 0;
	int	iloc = 0;
	int	result = 0;
	struct  c2_processor_descr	*cpu_info;
	c2_processor_nr_t	max_proc, rc;
	bool	val;
	struct c2_bitmap	onln_map;

	/* Allocate memory for fom domain.
	 * check number of processors online and create localities
	 * between one's sharing common resources.
	 * Currently considering shared L2 cache, numa node,
	 * and pipeline between cores, as shared resources.
	 */
	*fomdom = c2_alloc(sizeof **fomdom);
	if (*fomdom == NULL)
		return -ENOMEM;
	c2_processors_init();
	max_proc = c2_processor_nr_max();
	(*fomdom)->fd_localities = c2_alloc(max_proc * sizeof *(*fomdom)->fd_localities);
	if ((*fomdom)->fd_localities == NULL)
		return -ENOMEM;
	/* Temporary array of c2_processor descriptor */
	cpu_info = c2_alloc(max_proc * sizeof *cpu_info);
	if (cpu_info == NULL)
		return -ENOMEM;

	(*fomdom)->fd_nr = 0;
	(*fomdom)->fd_ops = &c2_fom_dom_ops;
	(*fomdom)->fd_clean = false;
	c2_bitmap_init(&onln_map, max_proc);
	c2_processors_online(&onln_map);

	/* Get the info of online processors. */
	for (i = 0; i < max_proc; ++i) {
		val = c2_bitmap_get(&onln_map, i);
			if (val) {
				rc = c2_processor_describe(i, &cpu_info[i]);
				onln_cpus++;
			}
	}

	/* Initialize localities */
	for(i = 0; i < max_proc; ++i) {
		if ((cpu_info[i].pd_l1_sz > 0 ) ||
			(cpu_info[i].pd_l2_sz > 0 )) {
			for(j = i; j < max_proc; ++j) {
				if ((cpu_info[j].pd_l1_sz > 0 ) || 
					(cpu_info[j].pd_l2_sz > 0 )) {
					if ((cpu_info[i].pd_pipeline == 
						cpu_info[j].pd_pipeline) || 
						(cpu_info[i].pd_numa_node == 
						cpu_info[j].pd_numa_node) || 
						(cpu_info[i].pd_pipeline == 
						cpu_info[j].pd_pipeline)) {
						onln_cpus--;
						result = c2_fom_loc_init(&cpu_info[j], 
						&(*fomdom)->fd_localities[iloc], 
						*fomdom, max_proc);
						if (result)
							break;
					}
				}
				if (onln_cpus <= 0)
					break;
			}
			memset(&cpu_info[i], 0 , sizeof(struct c2_processor_descr));
			++iloc;
		}
		if (onln_cpus <= 0)
			break;
	}
	c2_processors_fini();
	return result;
}

/**
 * Clean up function for fom domain.
 */
void c2_fom_domain_fini(struct c2_fom_domain *dom)
{

	int i = 0;
	size_t lfd_nr = dom->fd_nr;
	C2_ASSERT(dom != NULL);
	C2_ASSERT(dom->fd_localities != NULL);

	while (lfd_nr > 0) {
		if (dom->fd_localities[i].fl_dom != NULL) {
			c2_fom_loc_fini(&dom->fd_localities[i]);
			--lfd_nr;
		}
		++i;
	}
	c2_free(dom->fd_localities);
	dom->fd_nr = 0;
	dom->fd_ops = NULL;
	dom->fd_clean = 0;

	c2_free(dom);
}

/**
 * Create addb context
 */
void create_addb_context(struct c2_addb_ctx  *addb)
{
	/* create or open a stob in which to store the record.
	 * currently using file.
	 */
	c2_addb_store_stob = (struct c2_stob*)fopen("/tmp/reqh_addb_log", "a");
	C2_ASSERT(c2_addb_store_stob != NULL);
	/* write addb record into stob */
	c2_addb_stob_add_p = c2_addb_stob_add;
	c2_addb_store_type = C2_ADDB_REC_STORE_STOB;

	c2_addb_ctx_init(addb, &reqh_addb_ctx_type,
				&c2_addb_global_ctx);
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
	create_addb_context(&fom->fo_fop->f_addb);
	result = fom->fo_stdom->sd_ops->sdo_tx_make(fom->fo_stdom, &fom->fo_tx);
	C2_ASSERT(result == 0);
	c2_clink_init(&fom->fo_clink, &c2_fom_cb);
	c2_queue_link_init(&fom->fom_linkage);
	c2_list_link_init(&fom->fom_link);
	c2_chan_init(&fom->chan_gen_wait);
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
		if(rc)
			REQH_ADDB_ADD(fom->fo_fop->f_addb, 
					"DB abort failed", 
					rc);
	}
	
	c2_addb_ctx_fini(&fom->fo_fop->f_addb);
	if(c2_clink_is_armed(&fom->fo_clink))
		c2_clink_del(&fom->fo_clink);
	c2_clink_fini(&fom->fo_clink);
	c2_queue_link_fini(&fom->fom_linkage);
	c2_list_link_fini(&fom->fom_link);
	c2_chan_fini(&fom->chan_gen_wait);
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
