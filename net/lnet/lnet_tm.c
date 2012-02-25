/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
	int i;
	for (i = 0; i < ARRAY_SIZE(tm->ntm_q); ++i)
		if (!tm_tlist_is_empty(&tm->ntm_q[i]))
			return false;
	return true;
}

/**
   Logs periodic statistical data using ADDB.
   @param tm the transfer machine to report about
 */
static void nlx_tm_stats_report(struct c2_net_transfer_mc *tm)
{
	struct c2_net_qstats *qs;
	char name[128];
	int i;

	C2_PRE(tm != NULL && nlx_tm_invariant(tm));
	snprintf(name, sizeof name, "nlx_tm_stats:%s", tm->ntm_ep->nep_addr);
	for (i = 0; i < ARRAY_SIZE(tm->ntm_q); ++i) {
		qs = &tm->ntm_qstats[i];
		if (qs->nqs_num_adds == 0)
			continue;
		LNET_ADDB_STAT_ADD(tm->ntm_addb, name, i, qs);
	}
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
	c2_time_t stat_time;
	c2_time_t now;
	int rc = 0;

	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(nlx_tm_invariant(tm));
	tp = tm->ntm_xprt_private;
	ctp = &tp->xtm_core;

	nlx_core_tm_set_debug(ctp, tp->_debug_);

	if (tp->xtm_processors.b_nr != 0) {
		struct c2_thread_handle me;
		c2_thread_self(&me);
		C2_ASSERT(c2_thread_handle_eq(&me, &tp->xtm_ev_thread.t_h));
		rc = c2_thread_confine(&tp->xtm_ev_thread, &tp->xtm_processors);
	}

	if (rc == 0)
		rc = nlx_core_tm_start(tm, ctp, &ctp->ctm_addr, &tmev.nte_ep);

	/*
	  Deliver a C2_NET_TEV_STATE_CHANGE event to transition the TM to
	  the C2_NET_TM_STARTED or C2_NET_TM_FAILED states.
	  Set the transfer machine's end point in the event on success.
	 */
	if (rc == 0) {
		tmev.nte_next_state = C2_NET_TM_STARTED;
	} else {
		tmev.nte_next_state = C2_NET_TM_FAILED;
		tmev.nte_status = rc;
		LNET_ADDB_FUNCFAIL_ADD(tm->ntm_addb, rc);
	}
	tmev.nte_time = c2_time_now();
	tm->ntm_ep = NULL;
	c2_mutex_unlock(&tm->ntm_mutex);
	c2_net_tm_event_post(&tmev);
	if (rc != 0)
		return;
	stat_time = c2_time_now();

	while (1) {
		/* compute next timeout (short if automatic or stopping) */
		if (tm->ntm_bev_auto_deliver ||
		    tm->ntm_state == C2_NET_TM_STOPPING)
			timeout = c2_time_from_now(
					   C2_NET_LNET_EVT_SHORT_WAIT_SECS, 0);
		else
			timeout = c2_time_from_now(
					    C2_NET_LNET_EVT_LONG_WAIT_SECS, 0);
		if (tm->ntm_bev_auto_deliver) {
			rc = NLX_core_buf_event_wait(ctp, timeout);
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
				c2_mutex_unlock(&tm->ntm_mutex);
				rc = nlx_core_buf_event_wait(ctp, timeout);
				c2_mutex_lock(&tm->ntm_mutex);
				if (rc == 0 && tp->xtm_ev_chan != NULL) {
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
			if (all_tm_queues_are_empty(tm) &&
			    tm->ntm_callback_counter == 0) {
				nlx_core_tm_stop(ctp);
				nlx_tm_stats_report(tm);
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

		now = c2_time_now();
		c2_mutex_lock(&tm->ntm_mutex);
		if (c2_time_after_eq(now,
				     c2_time_add(stat_time,
						 tp->xtm_stat_interval))) {
			nlx_tm_stats_report(tm);
			stat_time = now;
		}
		c2_mutex_unlock(&tm->ntm_mutex);
	}
}

/**
   Helper subroutine to create the network buffer event from the internal
   core buffer event.

   @param tm Pointer to TM.
   @param lcbev Pointer to LNet transport core buffer event.
   @param nbev Pointer to network buffer event to fill in.
   @pre c2_mutex_is_locked(&tm->ntm_mutex)
   @post ergo(nbev->nbe_buffer->nb_flags & C2_NET_BUF_RETAIN,
              rc == 0 && !lcbev->cbe_unlinked);
   @post rc == 0 || rc == -ENOMEM
 */
int nlx_xo_core_bev_to_net_bev(struct c2_net_transfer_mc *tm,
			       struct nlx_core_buffer_event *lcbev,
			       struct c2_net_buffer_event *nbev)
{
	struct nlx_xo_transfer_mc *tp;
	struct c2_net_buffer *nb;
	int rc = 0;

	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));
	C2_PRE(nlx_tm_invariant(tm));
	tp = tm->ntm_xprt_private;

	/* Recover the transport space network buffer address from the
	   opaque data set during registration.
	 */
	nb = (struct c2_net_buffer *) lcbev->cbe_buffer_id;
	C2_ASSERT(c2_net__buffer_invariant(nb));

	C2_SET0(nbev);
	nbev->nbe_buffer = nb;
	nbev->nbe_status = lcbev->cbe_status;
	nbev->nbe_time   = lcbev->cbe_time;
	if (nbev->nbe_status != 0)
		goto done; /* this is not an error from this sub */
	if (nb->nb_qtype == C2_NET_QT_MSG_RECV) {
		rc = NLX_ep_create(&nbev->nbe_ep, tm, &lcbev->cbe_sender);
		if (rc != 0) {
			nbev->nbe_status = rc;
			goto done;
		}
		nbev->nbe_offset = lcbev->cbe_offset;
	}
	if (nb->nb_qtype == C2_NET_QT_MSG_RECV ||
	    nb->nb_qtype == C2_NET_QT_PASSIVE_BULK_RECV ||
	    nb->nb_qtype == C2_NET_QT_ACTIVE_BULK_RECV) {
		nbev->nbe_length = lcbev->cbe_length;
	}
	if (!lcbev->cbe_unlinked)
		nb->nb_flags |= C2_NET_BUF_RETAIN;
 done:
	NLXDBG(tp,2,nlx_print_core_buffer_event("bev_to_net_bev: cbev", lcbev));
	NLXDBG(tp,2,nlx_print_net_buffer_event("bev_to_net_bev: nbev:", nbev));
	NLXDBG(tp,2,NLXP("bev_to_net_bev: rc=%d\n", rc));

	C2_POST(ergo(nb->nb_flags & C2_NET_BUF_RETAIN,
		     rc == 0 && !lcbev->cbe_unlinked));
	/* currently we only support RETAIN for received messages */
	C2_POST(ergo(nb->nb_flags & C2_NET_BUF_RETAIN,
		     nb->nb_qtype == C2_NET_QT_MSG_RECV));
	C2_POST(rc == 0 || rc == -ENOMEM);
	return rc;
}

/**
   @}
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
