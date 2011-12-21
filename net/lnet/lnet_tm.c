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
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>
 *                  Dave Cohrs <Dave_Cohrs@xyratex.com>
 * Original creation date: 12/15/2011
 */

/**
   @addtogroup LNetXODFS
   @{
*/

static inline bool all_tm_queues_are_empty(struct c2_net_transfer_mc *tm)
{
	return true; /* XXX temp */
}

/**
   The entry point of the LNet transport event processing thread.
   It is spawned when the transfer machine starts.  It completes
   the start-up process and then loops, handling asynchronous buffer event
   delivery, until the transfer machine enters the C2_NET_TM_STOPPING state.
   Once that state transition is detected, the thread completes its
   processing and terminates.
 */
static void nlx_tm_ev_worker(struct c2_net_transfer_mc *tm)
{
	struct nlx_xo_transfer_mc *tp;
	struct nlx_core_transfer_mc *ctp;
	struct c2_net_tm_event tmev = {
		.nte_type   = C2_NET_TEV_STATE_CHANGE,
		.nte_tm     = tm,
		.nte_status = 0
	};
	c2_time_t timeout;
	int rc = 0;

	C2_PRE(nlx_tm_invariant(tm));
	tp = tm->ntm_xprt_private;
	ctp = &tp->xtm_core;

	if (tp->xtm_processors.b_nr != 0) {
		struct c2_thread_handle me;
		c2_thread_self(&me);
		C2_ASSERT(c2_thread_handle_eq(&me, &tp->xtm_ev_thread.t_h));
		rc = c2_thread_confine(&tp->xtm_ev_thread, &tp->xtm_processors);
	}

	if (rc == 0) {
		c2_mutex_lock(&tm->ntm_mutex);
		rc = nlx_core_tm_start(tm, ctp, &ctp->ctm_addr);
		c2_mutex_unlock(&tm->ntm_mutex);
	}

	/*
	  Deliver a C2_NET_TEV_STATE_CHANGE event to transition the TM to
	  the C2_NET_TM_STARTED or C2_NET_TM_FAILED states.
	  Set the transfer machine's end point in the event on success.
	 */
	if (rc != 0) {
		tmev.nte_next_state = C2_NET_TM_FAILED;
		tmev.nte_status = rc;
	} else {
		tmev.nte_next_state = C2_NET_TM_STARTED;
		C2_ASSERT(tm->ntm_ep != NULL);
		tmev.nte_ep = tm->ntm_ep;
	}
	tmev.nte_time = c2_time_now();
	tm->ntm_ep = NULL;
	c2_net_tm_event_post(&tmev);
	if (rc != 0)
		return;

	while (1) {
		/* compute next timeout (XXX short if automatic or stopping) */
		timeout = c2_time_from_now(1, 0);
		if (tm->ntm_bev_auto_deliver) {
			rc = nlx_core_buf_event_wait(ctp, timeout);
			/* buffer event processing */
			if (rc == 0) { /* did not time out - events pending */
				c2_mutex_lock(&tm->ntm_mutex);
				nlx_xo_bev_deliver_all(tm);
				c2_mutex_unlock(&tm->ntm_mutex);
			}
		} else {		/* application initiated delivery */
			c2_mutex_lock(&tm->ntm_mutex);
			if (tp->xtm_ev_chan == NULL)
				c2_cond_timedwait(&tp->xtm_ev_cond,
						  &tm->ntm_mutex, timeout);
			if (tp->xtm_ev_chan != NULL) {
				rc = nlx_core_buf_event_wait(ctp, timeout);
				if (rc == 0) {
					c2_chan_signal(tp->xtm_ev_chan);
					tp->xtm_ev_chan = NULL;
				}
			}
			c2_mutex_unlock(&tm->ntm_mutex);
		}

		/* XXX do buffer operation timeout processing periodically */

		/* termination processing */
		if (tm->ntm_state == C2_NET_TM_STOPPING) {
			bool must_stop = false;
			c2_mutex_lock(&tm->ntm_mutex);
			if (all_tm_queues_are_empty(tm) && tp->xtm_busy == 0) {
				nlx_core_tm_stop(ctp);
				must_stop = true;
			}
			c2_mutex_unlock(&tm->ntm_mutex);
			if (must_stop) {
				tmev.nte_next_state = C2_NET_TM_STOPPED;
				tmev.nte_time = c2_time_now();
				c2_net_tm_event_post(&tmev);
				break;
			}
		}

		/* XXX Log statistical data periodically using ADDB */
	}
}

/**
   @} LNetXODFS
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
