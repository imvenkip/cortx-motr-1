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
	enum c2_net_bulk_mem_tm_state next_state = wi->xwi_next_state;
	struct c2_net_bulk_mem_tm_pvt *tp = mem_tm_to_pvt(tm);
	struct c2_net_tm_event ev = {
		.nte_type   = C2_NET_TEV_STATE_CHANGE,
		.nte_tm     = tm,
		.nte_status = 0
	};

	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));
	C2_ASSERT(next_state == C2_NET_XTM_STARTED ||
		  next_state == C2_NET_XTM_STOPPED);

	if (next_state == C2_NET_XTM_STARTED) {
		/*
		  Application can call c2_net_tm_stop -> mem_xo_tm_stop
		  before the C2_NET_XTM_STARTED work item gets processed.
		  If that happens, ignore the C2_NET_XTM_STARTED item.
		 */
		if (tp->xtm_state < C2_NET_XTM_STOPPING) {
			C2_ASSERT(tp->xtm_state == C2_NET_XTM_STARTING);
			if (wi->xwi_status != 0) {
				tp->xtm_state = C2_NET_XTM_FAILED;
				ev.nte_next_state = C2_NET_TM_FAILED;
				ev.nte_status = wi->xwi_status;
			} else {
				tp->xtm_state = next_state;
				ev.nte_next_state = C2_NET_TM_STARTED;
			}
			c2_mutex_unlock(&tm->ntm_mutex);
			c2_time_now(&ev.nte_time);
			c2_net_tm_event_post(&ev);
			c2_mutex_lock(&tm->ntm_mutex);
		}
	} else { /* C2_NET_XTM_STOPPED, as per assert */
		C2_ASSERT(tp->xtm_state == C2_NET_XTM_STOPPING);
		tp->xtm_state = next_state;
		ev.nte_next_state = C2_NET_TM_STOPPED;

		/* broadcast on cond and wait for work item queue to empty */
		c2_cond_broadcast(&tp->xtm_work_list_cv, &tm->ntm_mutex);
		while (!c2_list_is_empty(&tp->xtm_work_list) ||
		       tp->xtm_callback_counter > 1)
			c2_cond_wait(&tp->xtm_work_list_cv, &tm->ntm_mutex);

		c2_mutex_unlock(&tm->ntm_mutex);
		c2_time_now(&ev.nte_time);
		c2_net_tm_event_post(&ev);
		c2_mutex_lock(&tm->ntm_mutex);
	}

	c2_list_link_fini(&wi->xwi_link);
	c2_free(wi);
}

/**
   Deliver a callback during operation cancel.
 */
static void mem_wf_cancel_cb(struct c2_net_transfer_mc *tm,
			     struct c2_net_bulk_mem_work_item *wi)
{
	struct c2_net_buffer *nb = mem_wi_to_buffer(wi);

	C2_PRE(c2_mutex_is_not_locked(&tm->ntm_mutex));
	C2_PRE(nb->nb_flags & C2_NET_BUF_IN_USE);

	/* post the completion callback (will clear C2_NET_BUF_IN_USE) */
	wi->xwi_status = -ECANCELED;
	mem_wi_post_buffer_event(wi);
	return;
}

/**
   Work function for the C2_NET_XOP_ERROR_CB work item.
   @param tm The corresponding transfer machine (unlocked).
   @param wi The work item. This will be freed.
 */
static void mem_wf_error_cb(struct c2_net_transfer_mc *tm,
			    struct c2_net_bulk_mem_work_item *wi)
{
	struct c2_net_tm_event ev = {
		.nte_type   = C2_NET_TEV_ERROR,
		.nte_tm     = tm,
		.nte_status = wi->xwi_status,
	};

	C2_PRE(wi->xwi_op == C2_NET_XOP_ERROR_CB);
	C2_PRE(wi->xwi_status < 0);
	c2_time_now(&ev.nte_time);
	c2_net_tm_event_post(&ev);
	c2_free(wi);
}

/**
   Support subroutine to send an error event.
   @param tm     The transfer machine.
   @param status The error code.
   @pre c2_mutex_is_locked(&tm->ntm_mutex)
 */
static void mem_post_error(struct c2_net_transfer_mc *tm, int32_t status)
{
	struct c2_net_bulk_mem_tm_pvt *tp = mem_tm_to_pvt(tm);
	struct c2_net_bulk_mem_work_item *wi;
	C2_PRE(status < 0);
	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));
	C2_ALLOC_PTR(wi);
	if (wi == NULL)
		return;
	wi->xwi_op = C2_NET_XOP_ERROR_CB;
	wi->xwi_status = status;
	mem_wi_add(wi, tp);
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
	struct c2_net_bulk_mem_tm_pvt *tp;
	struct c2_net_bulk_mem_domain_pvt *dp;
	struct c2_list_link *link;
	struct c2_net_bulk_mem_work_item *wi;
	c2_net_bulk_mem_work_fn_t fn;

	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_invariant(tm));
	tp = mem_tm_to_pvt(tm);
	dp = mem_dom_to_pvt(tm->ntm_dom);

	while (1) {
		while (!c2_list_is_empty(&tp->xtm_work_list)) {
			link = c2_list_first(&tp->xtm_work_list);
			wi = c2_list_entry(link,
					   struct c2_net_bulk_mem_work_item,
					   xwi_link);
			c2_list_del(&wi->xwi_link);
			fn = dp->xd_ops->bmo_work_fn[wi->xwi_op];
			C2_ASSERT(fn != NULL);

			tp->xtm_callback_counter++;
			if (wi->xwi_op == C2_NET_XOP_STATE_CHANGE) {
				fn(tm, wi);
			} else {
				/* others expect mutex to be released
				   and the C2_NET_BUF_IN_USE flag set
				   if a buffer is involved.
				*/
				if (wi->xwi_op != C2_NET_XOP_ERROR_CB) {
					struct c2_net_buffer *nb =
						mem_wi_to_buffer(wi);
					nb->nb_flags |= C2_NET_BUF_IN_USE;
				}
				c2_mutex_unlock(&tm->ntm_mutex);
				fn(tm, wi);
				c2_mutex_lock(&tm->ntm_mutex);
				C2_ASSERT(c2_net__tm_invariant(tm));
			}
			tp->xtm_callback_counter--;
			/* signal that wi was removed from queue */
			c2_cond_signal(&tp->xtm_work_list_cv, &tm->ntm_mutex);
		}
		if (tp->xtm_state == C2_NET_XTM_STOPPED)
			break;
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
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
