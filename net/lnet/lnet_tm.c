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
   Logs statistical data periodically using ADDB.
   @param tm the transfer machine to report about
   @todo addb lacks a mechanism for statistics reporting, use of LNET_ADDB_ADD
   is temporary
 */
static void nlx_tm_stats_report(struct c2_net_transfer_mc *tm)
{
	struct c2_net_qstats *qs;
	char stat_name[128];
	static const char *qn[] = {
		"msg_recv", "msg_send",
		"pas_recv", "pas_send",
		"act_recv", "act_send",
	};
	int i;

#define STAT_NAME(field, i)						\
	snprintf(stat_name, sizeof stat_name,				\
		 "nlx_tm_stats:%s:%s:" field, tm->ntm_ep->nep_addr, qn[i])

	C2_PRE(tm != NULL && nlx_tm_invariant(tm));
	C2_CASSERT(ARRAY_SIZE(tm->ntm_q) == ARRAY_SIZE(qn));
	C2_CASSERT(sizeof qs->nqs_time_in_queue == sizeof(uint64_t));
	for (i = 0; i < ARRAY_SIZE(tm->ntm_q); ++i) {
		qs = &tm->ntm_qstats[i];
		STAT_NAME("adds", i);
		LNET_ADDB_ADD(tm->ntm_addb, stat_name, qs->nqs_num_adds);
		STAT_NAME("dels", i);
		LNET_ADDB_ADD(tm->ntm_addb, stat_name, qs->nqs_num_dels);
		STAT_NAME("success", i);
		LNET_ADDB_ADD(tm->ntm_addb, stat_name, qs->nqs_num_s_events);
		STAT_NAME("fail", i);
		LNET_ADDB_ADD(tm->ntm_addb, stat_name, qs->nqs_num_f_events);
		STAT_NAME("inqueue", i);
		LNET_ADDB_ADD(tm->ntm_addb, stat_name, qs->nqs_time_in_queue);
		STAT_NAME("totbytes", i);
		LNET_ADDB_ADD(tm->ntm_addb, stat_name, qs->nqs_total_bytes);
		STAT_NAME("maxbytes", i);
		LNET_ADDB_ADD(tm->ntm_addb, stat_name, qs->nqs_max_bytes);
	}
#undef STAT_NAME
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
		LNET_ADDB_ADD(tm->ntm_addb, "nlx_tm_ev_worker", rc);
	}
	tmev.nte_time = c2_time_now();
	tm->ntm_ep = NULL;
	c2_mutex_unlock(&tm->ntm_mutex);
	c2_net_tm_event_post(&tmev);
	if (rc != 0)
		return;
	stat_time = c2_time_now();

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
		if (c2_time_add(stat_time, tp->xtm_stat_interval) <= now) {
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
		rc = nlx_ep_create(&nbev->nbe_ep, tm, &lcbev->cbe_sender);
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
