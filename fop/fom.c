
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>  /* mkdir */
#include <sys/types.h> /* mkdir */
#include <unistd.h>    /* sleep */

#include "lib/misc.h"   /* C2_SET0 */
#include "lib/getopts.h"
#include "lib/arith.h"  /* min64u */
#include "lib/trace.h"
#include "lib/errno.h"
#include "lib/assert.h"
#include "lib/memory.h"
#include "lib/thread.h"
#include "lib/queue.h"
#include "lib/chan.h"
#include "lib/processor.h"

#include "fop/fom.h"
#include "fop/fop.h"
#include "reqh/reqh.h"

/**
 * fom domain operations structure, containing
 * function pointer to identify home locality
 * for fom execution.
 */
static struct c2_fom_domain_ops c2_fom_dom_ops = {
        .fdo_home_locality = fdo_find_home_loc,
        .fdo_time_is_out = NULL,
};

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
	if(clink != NULL) {
	        struct c2_fom *fom = container_of(clink, struct c2_fom, fo_clink);
		if(fom != NULL) {
			c2_list_del(&fom->fom_link);
			fom->fo_state = FOS_READY;
			/* queue it back to the run queue of fom's locality */
			c2_mutex_lock(&fom->fo_loc->fl_runq_lock);
			c2_queue_put(&fom->fo_loc->fl_runq, &fom->fom_linkage);
			c2_mutex_unlock(&fom->fo_loc->fl_runq_lock);
			c2_chan_signal(&fom->fo_loc->fl_runrun);
        	}	

	}	
	
}

/**
 * Function invoked before potential blocking point.
 * Checks whether the locality has "enough" idle threads. If not, additional
 * threads is started to cope with possible blocking point.
 */
void c2_fom_block_enter(struct c2_fom_locality *loc)
{
	C2_ASSERT(loc != NULL);
	if(loc != NULL) {
		if(loc->fl_idle_threads_nr <= 0) {
			c2_loc_thr_create(loc);
		}		
	}
}

/**
 * 
 */
void c2_fom_block_leave(struct c2_fom_locality *loc)
{
	C2_ASSERT(loc != NULL);
	if(loc != NULL) {

	}
}

/**
 * Funtion to identify and assign an appropriate home locality
 * to the fom, for execution.
 */
size_t fdo_find_home_loc(const struct c2_fom_domain *dom, struct c2_fom *fom)
{
	int i = 0;
        c2_processor_nr_t       max_proc;
	
	C2_ASSERT(dom != NULL);
	C2_ASSERT(fom != NULL);

	if((dom != NULL) && (fom != NULL)) {
		 c2_processors_init();
		 max_proc = c2_processor_nr_max();
		 for(i = 0; i < max_proc; ++i) {
			if(dom->fd_localities[i].fl_idle_threads_nr > 0) {
				fom->fo_loc = &dom->fd_localities[i];
				break;
			}
		 }
	}
return 0;
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

	if((dom != NULL) && (fom != NULL)) {
		if(fom->fo_loc == NULL)
			dom->fd_ops->fdo_home_locality(dom, fom);

		c2_mutex_lock(&fom->fo_loc->fl_runq_lock);
                c2_queue_put(&fom->fo_loc->fl_runq, &fom->fom_linkage);
                c2_mutex_unlock(&fom->fo_loc->fl_runq_lock);
	 	c2_chan_signal(&fom->fo_loc->fl_runrun);	
	}
	
}

/**
 * Function to register the fom with the given wait channel.
 */
void c2_fom_block_at(struct c2_fom *fom, struct c2_chan *chan)
{
	C2_ASSERT(fom != NULL);
	C2_ASSERT(chan != NULL);

	if((fom != NULL) && (chan != NULL)) {
		/* fom->fo_phase = FOPH  */
		if(!c2_clink_is_armed(&fom->fo_clink)) {
        		c2_clink_add(chan, &fom->fo_clink);
		}		
	}
}

/**
 * Thread function to execute fom.
 * We start with executing generic phases of the fom
 * and then proceed to execute fop specific operation.
 */
void c2_loc_thr_start(void *loc)
{
	C2_ASSERT(loc != NULL);

	struct c2_queue_link *c2_fom_link = NULL;
	struct c2_fom *fom = NULL;
	struct c2_fom_locality *tloc = (struct c2_fom_locality *)loc;

	if(tloc != NULL)
	{
	  	while(1) {
			
			/** 
			 * 1) Initialize a clink and add it to the locality's wait channel
			 * 2) wait until there's fom to execute in the locality's run queue
			 * 3) Fom is submitted for execution in the run queue
			 * 4) one of the waiting locality thread is woken up by
			 *    by signaling the wait channel.
			 * 5) A single thread dequeues the fom and starts executing it. 
			 * 6) Decrements the idle number of threads in the locality.
			 */
        		struct c2_clink th_clink;
			c2_clink_init(&th_clink, NULL);
			c2_clink_add(&tloc->fl_runrun, &th_clink);
			c2_mutex_lock(&tloc->fl_lock);
		     	++tloc->fl_idle_threads_nr;
			c2_mutex_unlock(&tloc->fl_lock);	
			c2_chan_wait(&th_clink);

			c2_mutex_lock(&tloc->fl_lock);
			if(tloc->fl_idle_threads_nr > 0)
				--tloc->fl_idle_threads_nr;
			c2_mutex_unlock(&tloc->fl_lock);

			c2_mutex_lock(&tloc->fl_runq_lock);
			c2_fom_link = c2_queue_get(&tloc->fl_runq);
			c2_mutex_unlock(&tloc->fl_runq_lock);

			c2_clink_del(&th_clink);
	       	 	c2_clink_fini(&th_clink);
				
			if(c2_fom_link != NULL) {
				/* Extract the fom and start processing the generic phases of the fom
				 * before executing the actual fop.
				 */
				fom = container_of(c2_fom_link, struct c2_fom, fom_linkage);
				if(fom != NULL) {
					switch(fom->fo_phase) {
						case FOPH_INIT:
						{	
							/* Initial fom phase, start with fop authetication */
							int rc = 0;
							rc = c2_fom_state_generic(fom);
							if(fom->fo_phase == FOPH_AUTHENTICATE_WAIT) {
								/* operation seems to be blocking,
								 * put this fom on the wait list of this locality
								 * execution will be resumed later 
								 */
								printf("\n Aunthenticate wait....\n");
					 			c2_mutex_lock(&tloc->fl_wail_lock);
        		                 			c2_list_link_init(&fom->fom_link);
	                                 			c2_list_add_tail(&tloc->fl_wail, &fom->fom_link);
                    			 			c2_mutex_unlock(&tloc->fl_wail_lock);
								c2_chan_signal(&fom->chan_gen_wait);
							}
							break;	
						}
						case FOPH_AUTHENTICATE_WAIT:
						{
							/* Authentication is done, change phase back to 
							 * FOPH_AUTHENTICATE and proceed to next phase.
							 */
							int rc = 0;
							fom->fo_phase = FOPH_AUTHENTICATE;
							rc = c2_fom_state_generic(fom);
							if(fom->fo_phase ==  FOPH_RESOURCE_LOCAL_WAIT) {
								/* operation seems to be blocking,
								 * put this fom on the wait list of this locality
								 * execution will be resumed later. 
								 */
					 			c2_mutex_lock(&tloc->fl_wail_lock);
        		                 			c2_list_link_init(&fom->fom_link);
	                                 			c2_list_add_tail(&tloc->fl_wail, &fom->fom_link);
                    			 			c2_mutex_unlock(&tloc->fl_wail_lock);
								c2_chan_signal(&fom->chan_gen_wait);
							}
							break;
						}
						case FOPH_RESOURCE_LOCAL_WAIT:
						{
							/* we have acquired local resource information
							 * for this fom, proceed to next phase.
							 * acquiring distributed resource information.
							 */
							int rc = 0;
							fom->fo_phase = FOPH_RESOURCE_LOCAL;
							rc = c2_fom_state_generic(fom);
							if(fom->fo_phase ==  FOPH_RESOURCE_DISTRIBUTED_WAIT) {
								/* operation seems to be blocking,
							 	 * put this fom on the wait list of this locality
								 * execution will be resumed later.
								 */
					 			c2_mutex_lock(&tloc->fl_wail_lock);
        		                 			c2_list_link_init(&fom->fom_link);
	                                 			c2_list_add_tail(&tloc->fl_wail, &fom->fom_link);
                    			 			c2_mutex_unlock(&tloc->fl_wail_lock);
								c2_chan_signal(&fom->chan_gen_wait);
							}
							break;
						}
						case FOPH_RESOURCE_DISTRIBUTED_WAIT:
						{
							/* we have acquired distributed resource information
							 * proceed to next fom phase.
							 * locating and loading file system objects.
							 */
							int rc = 0;
							fom->fo_phase = FOPH_RESOURCE_DISTRIBUTED;
							rc = c2_fom_state_generic(fom);
							if(fom->fo_phase == FOPH_OBJECT_CHECK_WAIT) {
								/* operation seems to be blocking,
								 * put this fom on the wait list of this locality.
								 * execution will be resumed later.
								 */
					 			c2_mutex_lock(&tloc->fl_wail_lock);
        		                 			c2_list_link_init(&fom->fom_link);
	                                 			c2_list_add_tail(&tloc->fl_wail, &fom->fom_link);
                    			 			c2_mutex_unlock(&tloc->fl_wail_lock);
								c2_chan_signal(&fom->chan_gen_wait);
							}
							break;
						}
						case FOPH_OBJECT_CHECK_WAIT:
						{
							/* we have identified and loaded required file
							 * system objects, proceed with next fom phase.
							 * fop authorisation.
							 */
							int rc = 0;
                                                        fom->fo_phase = FOPH_OBJECT_CHECK;
                                                        rc = c2_fom_state_generic(fom);
                                                        if(fom->fo_phase == FOPH_AUTHORISATION_WAIT) {
								/* operation seems to be blocking,
								 * put this fom on the wait list of this locality.
								 * execution will be resumed later.
								 */
                                                                c2_mutex_lock(&tloc->fl_wail_lock);
                                                                c2_list_link_init(&fom->fom_link);
                                                                c2_list_add_tail(&tloc->fl_wail, &fom->fom_link);
                                                                c2_mutex_unlock(&tloc->fl_wail_lock);
                                                                c2_chan_signal(&fom->chan_gen_wait);
                                                        }
                                                        break;
						}
						case FOPH_AUTHORISATION_WAIT:
						{
							/* Done with fop authorisation, proceed to 
							 * next fom phase.
							 * create local transaction context.
							 */
							int rc = 0;
                                                        fom->fo_phase = FOPH_AUTHORISATION;
                                                        rc = c2_fom_state_generic(fom);
                                                        if(fom->fo_phase == FOPH_TXN_CONTEXT_WAIT) {
								/* operation seems to be blocking,
								 * put this fom on the wait list of this locality.
								 * execution will be resumed later.
								 */
                                                                c2_mutex_lock(&tloc->fl_wail_lock);
                                                                c2_list_link_init(&fom->fom_link);
                                                                c2_list_add_tail(&tloc->fl_wail, &fom->fom_link);
                                                                c2_mutex_unlock(&tloc->fl_wail_lock);
                                                                c2_chan_signal(&fom->chan_gen_wait);
                                                        }
                                                        break;
						}

					}//switch
					
					if((fom->fo_phase ==  FOPH_EXEC) || (fom->fo_phase == FOPH_EXEC_WAIT))
					{
						/* we have completed all the generic phases of the fom.
						 * starting executing the fop specific operation of the fom.
						 */
						if(fom->fo_ops->fo_state(fom) ==  FSO_WAIT) {
							/* fop execution is blocked, put this
							 * fom on the wait list of the locality.
							 * execution will be resumed later.
							 */
							fom->fo_phase = FOPH_EXEC_WAIT;
					 		c2_mutex_lock(&tloc->fl_wail_lock);
        		                 		c2_list_link_init(&fom->fom_link);
	                                 		c2_list_add_tail(&tloc->fl_wail, &fom->fom_link);
                    			 		c2_mutex_unlock(&tloc->fl_wail_lock);
					  	}
						else 
						if(fom->fo_phase == FOPH_DONE) {
							/* fop execution is done, and the reply is sent.
							* proceed to fom clean up.
							*/
							fom->fo_ops->fo_fini(fom);
							printf("\n Mandar: Fop Execution Done..\n");
						}
					}
					else
					if(fom->fo_phase == FOPH_FAILED) {
						/* fop failed in generic phase, send fop reply.
						 * Below is the temporary interface included in
						 * fom to send reply fop in case of failure.
						 */
						 int rc = 0;
						 rc = fom->fo_ops->fo_fail(fom);
						 C2_ASSERT(rc == 0)
						 /* clean up fom */
						 fom->fo_ops->fo_fini(fom);
					
					}//else
				}//if	
			}//if					
		
		}//while
	}//if
}

/**
 * Function to create and add a new thread to the specified 
 * locality, and confine it to run only on cores comprising 
 * locality.
 */  
void c2_loc_thr_create(struct c2_fom_locality *loc)
{
	C2_ASSERT(loc != NULL);
	int result = 0;

	if(loc != NULL)
	{
		     struct c2_fom_hthread *locthr = c2_alloc(sizeof(struct c2_fom_hthread));	
		     c2_mutex_lock(&loc->fl_lock);
		     c2_list_link_init(&locthr->fht_linkage);
		     c2_list_add_tail(&loc->fl_threads, &locthr->fht_linkage);
		     ++loc->fl_threads_nr;
 		     c2_mutex_unlock(&loc->fl_lock);
	
		     result = c2_thread_init(&locthr->fht_thread,NULL, (void (*)(void *))&c2_loc_thr_start,
                              (void*)(unsigned long)loc);
		     result = c2_thread_confine(&locthr->fht_thread, &loc->fl_processors);
                     C2_ASSERT(result == 0);

	}
}

/**
 * Function to initialize localities for a particular
 * fom domain. This function also creates certain threads per locality.
 * currently the number of threads created are same as the number of
 * cores sharing the resource.
 */
int c2_fom_loc_init(struct c2_processor_descr *c2_cpu_info, struct c2_fom_locality *c2_fom_loc, 
		   struct c2_fom_domain *c2_fom_dom, c2_processor_nr_t max_proc)
{
	if((c2_cpu_info != NULL) && (c2_fom_loc != NULL) && (c2_fom_dom != NULL)) {
			
			/* Check if we need to create a new locality or 
	 		 * add threads to the existing one.
			 */
			if(!c2_fom_loc->fl_dom) {
				/* create a new locality */
				c2_fom_loc->fl_dom = c2_fom_dom;
				c2_queue_init(&c2_fom_loc->fl_runq);
				c2_mutex_init(&c2_fom_loc->fl_runq_lock);
				c2_list_init(&c2_fom_loc->fl_wail);
				c2_fom_loc->fl_wail_nr = 0;
				c2_mutex_init(&c2_fom_loc->fl_wail_lock);
				c2_mutex_init(&c2_fom_loc->fl_lock);
				c2_chan_init(&c2_fom_loc->fl_runrun);
				c2_list_init(&c2_fom_loc->fl_threads);
				c2_fom_loc->fl_threads_nr = 0;
				c2_bitmap_init(&c2_fom_loc->fl_processors, max_proc);
				c2_bitmap_set(&c2_fom_loc->fl_processors, c2_cpu_info->pd_id, 1);
				c2_loc_thr_create(c2_fom_loc);
				c2_fom_dom->fd_nr++;
			}
			else
			{	
				/* locality already exists, just add another thread to the same locality */
				c2_loc_thr_create(c2_fom_loc);
				c2_bitmap_set(&c2_fom_loc->fl_processors, c2_cpu_info->pd_id, 1);
				memset(c2_cpu_info, 0 , sizeof(struct c2_processor_descr));
			}

		return 0;
	}//if
	
return 1;
}

/**
 * Funtion to initialize fom domain.
 */
int  c2_fom_domain_init(struct c2_fom_domain **fomdom, size_t nr)
{
	int 	i = 0, j = 0, onln_cpus = 0;
	struct  c2_processor_descr *c2_cpu_info;	
        c2_processor_nr_t       max_proc, rc;
        bool    val;
	struct c2_bitmap	onln_map;
	
 		/* Allocate memory for fom domain.
		 * check number of processors online and create loacalities 
   	         * between one's sharing common resources.
 		 * Currently considering shared L2 cache between cores, as a shared resource.
		 */

		*fomdom = c2_alloc(sizeof(struct c2_fom_domain));
        	c2_processors_init();
        	max_proc = c2_processor_nr_max();
                (*fomdom)->fd_localities = c2_alloc(max_proc * sizeof(struct c2_fom_locality));
		c2_cpu_info = c2_alloc(max_proc * sizeof(struct c2_processor_descr));
		(*fomdom)->fd_nr = 0;
		(*fomdom)->fd_ops = &c2_fom_dom_ops;
        	c2_bitmap_init(&onln_map, max_proc);
		c2_processors_online(&onln_map);                
		
		for (i = 0; i < max_proc; ++i) {
        		val = c2_bitmap_get(&onln_map, i);
                		if (val == true) {
                                	rc = c2_processor_describe(i, &c2_cpu_info[i]);
					onln_cpus++;
				}//if
		}//for				
		
		for(i = 0; i < max_proc; ++i) {

			if((c2_cpu_info[i].pd_l1_sz > 0 ) || (c2_cpu_info[i].pd_l2_sz > 0 )) {

				for(j = i; j < max_proc; ++j) {

					if((c2_cpu_info[j].pd_l1_sz > 0 ) || (c2_cpu_info[j].pd_l2_sz > 0 )) {

						if(c2_cpu_info[i].pd_l2 == c2_cpu_info[j].pd_l2) {
							onln_cpus--;	
							c2_fom_loc_init(&c2_cpu_info[j], &(*fomdom)->fd_localities[i], 
                                                                        *fomdom, max_proc);
						}//if
					}//if
				}//for
				memset(&c2_cpu_info[i], 0 , sizeof(struct c2_processor_descr));
			}//if
			if(!onln_cpus)
				break;
		}//for
	
		c2_processors_fini();
return 0;	
}

/** 
 * Fom initializing function.
 */
void c2_fom_init(struct c2_fom *fom)
{
	/* Set fom state to FOS_READY and fom phase to FOPH_INIT.
         * Initialize temporary wait channel, would be removed later.
         */
	if(fom != NULL) {
		fom->fo_state = FOS_READY;
		fom->fo_phase = FOPH_INIT;
		c2_clink_init(&fom->fo_clink, &c2_fom_cb); 
		c2_chan_init(&fom->chan_gen_wait);
	}
}

/**
 * clean up function for fom.
 */
void c2_fom_fini(struct c2_fom *fom)
{
	if(fom != NULL) {
		if(fom->fo_phase == FOPH_DONE) {
		       /* commit database transaction */
		       c2_db_tx_commit(&fom->fo_fop_ctx->fc_tx->tx_dbtx);
 	               /*c2_dbenv_sync(reqh->rh_site->s_mdstore->md_dom.cd_dbenv);*/

		}
		c2_clink_del(&fom->fo_clink);
	        c2_clink_fini(&fom->fo_clink);
		c2_chan_fini(&fom->chan_gen_wait);
	}
}
