/* -*- C -*- */

/* This file is included into mem_xprt_xo.c */

/**
   @addtogroup bulkmem
   @{
 */


/**
   Work function for the C2_NET_XOP_STATE_CHANGE work item.
   @param tm the corresponding transfer machine
   @param wi the work item, this will be freed
 */
static void mem_wf_state_change(struct c2_net_transfer_mc *tm,
				struct c2_net_bulk_mem_work_item *wi)
{
	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));
	C2_ASSERT(wi->xwi_next_state == C2_NET_XTM_STARTED ||
		  wi->xwi_next_state == C2_NET_XTM_STOPPED);

	struct c2_net_bulk_mem_tm_pvt *tp = tm->ntm_xprt_private;
	int rc;
	struct c2_net_event ev = {
		.nev_qtype = C2_NET_QT_NR,
		.nev_tm = tm,
		.nev_buffer = NULL,
		.nev_status = 0
	};
	c2_time_now(&ev.nev_time);

	if (wi->xwi_next_state == C2_NET_XTM_STARTED) {
		/*
		  Application can call c2_net_tm_stop -> mem_xo_tm_stop
		  before the C2_NET_XTM_STARTED work item gets processed.
		  If that happens, ignore the C2_NET_XTM_STARTED item.
		 */
		if (tp->xtm_state < C2_NET_XTM_STOPPING) {
			C2_ASSERT(tp->xtm_state == C2_NET_XTM_STARTING);
			tp->xtm_state = wi->xwi_next_state;
			ev.nev_payload = (void *) C2_NET_TM_STARTED;

			c2_mutex_unlock(&tm->ntm_mutex);
			rc = c2_net_tm_event_post(ev.nev_tm, &ev);
			c2_mutex_lock(&tm->ntm_mutex);
		}
	} else { /* C2_NET_XTM_STOPPED, as per assert */
		C2_ASSERT(tp->xtm_state == C2_NET_XTM_STOPPING);
		tp->xtm_state = wi->xwi_next_state;
		ev.nev_payload = (void *) C2_NET_TM_STOPPED;

		/* broadcast on cond and wait for work item queue to empty */
		c2_cond_broadcast(&tp->xtm_work_list_cv, &tm->ntm_mutex);
		while (!c2_list_is_empty(&tp->xtm_work_list))
			c2_cond_wait(&tp->xtm_work_list_cv, &tm->ntm_mutex);

		c2_mutex_unlock(&tm->ntm_mutex);
		rc = c2_net_tm_event_post(ev.nev_tm, &ev);
		c2_mutex_lock(&tm->ntm_mutex);
	}

	c2_free(wi);
}

/**
 */
static void mem_wf_cancel_cb(struct c2_net_transfer_mc *tm,
			     struct c2_net_bulk_mem_work_item *wi)
{
}

/**
   The entry point of the worker thread started when a transfer machine
   transitions from STARTING to STARTED.  The thread runs its main loop
   until it the transfer machine state changes to X2_NET_XTM_STOPPED, at which
   time the function returns, causing the thread to exit.
   @param tm Transfer machine pointer
 */
static void mem_xo_tm_worker(struct c2_net_transfer_mc *tm)
{
	c2_mutex_lock(&tm->ntm_mutex);

	struct c2_net_bulk_mem_tm_pvt *tp = tm->ntm_xprt_private;
	struct c2_net_bulk_mem_domain_pvt *dp = tm->ntm_dom->nd_xprt_private;
	struct c2_list_link *link;
	struct c2_net_bulk_mem_work_item *wi;
	c2_net_bulk_mem_work_fn_t fn;

	while (tp->xtm_state != C2_NET_XTM_STOPPED) {
		while (tp->xtm_state != C2_NET_XTM_STOPPED &&
		       !c2_list_is_empty(&tp->xtm_work_list)) {
			link = c2_list_first(&tp->xtm_work_list);
			wi = c2_list_entry(link,
					   struct c2_net_bulk_mem_work_item,
					   xwi_link);
			c2_list_del(&wi->xwi_link);
			fn = dp->xd_work_fn[wi->xwi_op];
			if (fn != NULL) {
				if (wi->xwi_op == C2_NET_XOP_STATE_CHANGE)
					fn(tm, wi);
				else {
					/* others expect mutex to be released 
					   and the C2_NET_BUF_IN_USE flag set.
					 */
					struct c2_net_buffer *nb =
						MEM_WI_TO_BUFFER(wi);
					nb->nb_flags |= C2_NET_BUF_IN_USE;
					c2_mutex_unlock(&tm->ntm_mutex);
					fn(tm, wi);
					c2_mutex_lock(&tm->ntm_mutex);
				}
			}
			/* signal that wi was removed from queue */
			c2_cond_signal(&tp->xtm_work_list_cv, &tm->ntm_mutex);
		}
		if (tp->xtm_state != C2_NET_XTM_STOPPED)
			c2_cond_wait(&tp->xtm_work_list_cv, &tm->ntm_mutex);
	}

	c2_mutex_unlock(&tm->ntm_mutex);
}

/**
   @} bulkmem
*/

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
