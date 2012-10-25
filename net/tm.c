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
 * Original author: Dave Cohrs <Dave_Cohrs@xyratex.com>,
 *                  Carl Braganza <Carl_Braganza@xyratex.com>
 * Original creation date: 04/07/2011
 */

#include "lib/arith.h" /* C2_CNT_INC */
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/misc.h"
#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_NET
#include "lib/trace.h"
#include "lib/finject.h"
#include "colibri/magic.h"
#include "net/net_internal.h"
#include "net/buffer_pool.h"

/**
   @addtogroup net
   @{
 */
C2_TL_DESCR_DEFINE(c2_net_tm, "tm list", ,
		   struct c2_net_buffer, nb_tm_linkage, nb_magic,
		   C2_NET_BUFFER_LINK_MAGIC, C2_NET_BUFFER_HEAD_MAGIC);
C2_TL_DEFINE(c2_net_tm, , struct c2_net_buffer);
C2_EXPORTED(c2_net_tm_tlist_is_empty);

const struct c2_addb_ctx_type c2_net_tm_addb_ctx = {
	.act_name = "net-tm"
};

C2_INTERNAL bool c2_net__tm_state_is_valid(enum c2_net_tm_state ts)
{
	return ts >= C2_NET_TM_UNDEFINED && ts <= C2_NET_TM_FAILED;
}

C2_INTERNAL bool c2_net__tm_ev_type_is_valid(enum c2_net_tm_ev_type et)
{
	return et >= C2_NET_TEV_ERROR && et < C2_NET_TEV_NR;
}

C2_INTERNAL bool c2_net__tm_event_invariant(const struct c2_net_tm_event *ev)
{
	if (!c2_net__tm_ev_type_is_valid(ev->nte_type))
		return false;
	if (ev->nte_tm == NULL ||
	    !c2_net__tm_invariant(ev->nte_tm))
		return false;
	if (ev->nte_type == C2_NET_TEV_STATE_CHANGE &&
	    !c2_net__tm_state_is_valid(ev->nte_next_state))
		return false;
	if (ev->nte_type == C2_NET_TEV_STATE_CHANGE &&
	    ev->nte_next_state == C2_NET_TM_STARTED &&
	    !c2_net__ep_invariant(ev->nte_ep, ev->nte_tm, true))
		return false;
	return true;
}

C2_INTERNAL bool c2_net__tm_invariant(const struct c2_net_transfer_mc *tm)
{
	if (tm == NULL || tm->ntm_callbacks == NULL || tm->ntm_dom == NULL)
		return false;
	if (tm->ntm_callbacks->ntc_event_cb == NULL)
		return false;
	if (tm->ntm_state < C2_NET_TM_INITIALIZED ||
	    tm->ntm_state > C2_NET_TM_FAILED)
		return false;
	if (tm->ntm_state == C2_NET_TM_STARTED &&
	    tm->ntm_ep == NULL)
		return false;
	if (tm->ntm_state != C2_NET_TM_STARTED &&
	    tm->ntm_state != C2_NET_TM_STOPPING) {
		int i;
		for (i = 0; i < ARRAY_SIZE(tm->ntm_q); ++i)
			if (!c2_net_tm_tlist_is_empty(&tm->ntm_q[i]))
				return false;
	}
	return true;
}

C2_INTERNAL void c2_net_tm_event_post(const struct c2_net_tm_event *ev)
{
	struct c2_net_transfer_mc *tm;
	struct c2_net_buffer_pool *pool = NULL;

	C2_PRE(ev != NULL);
	tm = ev->nte_tm;
	C2_PRE(tm != NULL);
	C2_PRE(c2_mutex_is_not_locked(&tm->ntm_mutex));

	/* pre-callback, in mutex */
	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_event_invariant(ev));

	if (ev->nte_type == C2_NET_TEV_STATE_CHANGE) {
		tm->ntm_state = ev->nte_next_state;
		if (tm->ntm_state == C2_NET_TM_STARTED) {
			tm->ntm_ep = ev->nte_ep; /* ep now visible */
			pool = tm->ntm_recv_pool;
		}
	}

	C2_CNT_INC(tm->ntm_callback_counter);
	c2_mutex_unlock(&tm->ntm_mutex);

	(*tm->ntm_callbacks->ntc_event_cb)(ev);

	/* post-callback, out of mutex:
	   perform initial provisioning if required
	 */
	if (pool != NULL)
		c2_net__tm_provision_recv_q(tm);

	/* post-callback, in mutex:
	   decrement ref counts,
	   signal waiters
	 */
	c2_mutex_lock(&tm->ntm_mutex);
	C2_CNT_DEC(tm->ntm_callback_counter);
	if (tm->ntm_callback_counter == 0)
		c2_chan_broadcast(&tm->ntm_chan);
	c2_mutex_unlock(&tm->ntm_mutex);

	return;
}

static void c2_net__tm_cleanup(struct c2_net_transfer_mc *tm)
{
	int i;
	c2_mutex_fini(&tm->ntm_mutex);
	tm->ntm_dom = NULL;
	c2_chan_fini(&tm->ntm_chan);
	c2_list_fini(&tm->ntm_end_points);
	for (i = 0; i < ARRAY_SIZE(tm->ntm_q); ++i) {
		c2_net_tm_tlist_fini(&tm->ntm_q[i]);
	}
	tm->ntm_xprt_private = NULL;
	c2_addb_ctx_fini(&tm->ntm_addb);
	return;
}

C2_INTERNAL int c2_net_tm_init(struct c2_net_transfer_mc *tm,
			       struct c2_net_domain *dom)
{
	int rc;
	int i;

	c2_mutex_lock(&dom->nd_mutex);
	C2_PRE(tm != NULL);
	C2_PRE(tm->ntm_state == C2_NET_TM_UNDEFINED);
	C2_PRE(tm->ntm_callbacks != NULL &&
	       tm->ntm_callbacks->ntc_event_cb != NULL);

	if (C2_FI_ENABLED("fake_error")) {
		c2_mutex_unlock(&dom->nd_mutex);
		C2_RETURN(-EINVAL);
	}
	c2_mutex_init(&tm->ntm_mutex);
	tm->ntm_callback_counter = 0;
	tm->ntm_dom = dom;
	tm->ntm_ep = NULL;
	c2_list_init(&tm->ntm_end_points);
	c2_chan_init(&tm->ntm_chan);
	for (i = 0; i < ARRAY_SIZE(tm->ntm_q); ++i) {
		c2_net_tm_tlist_init(&tm->ntm_q[i]);
	}
	C2_SET_ARR0(tm->ntm_qstats);
	tm->ntm_xprt_private = NULL;
	tm->ntm_bev_auto_deliver = true;
	tm->ntm_recv_pool = NULL;
	tm->ntm_recv_queue_min_length = C2_NET_TM_RECV_QUEUE_DEF_LEN;
	c2_atomic64_set(&tm->ntm_recv_queue_deficit, 0);
	tm->ntm_pool_colour = C2_BUFFER_ANY_COLOUR;
	c2_addb_ctx_init(&tm->ntm_addb, &c2_net_tm_addb_ctx, &dom->nd_addb);

	rc = dom->nd_xprt->nx_ops->xo_tm_init(tm);
	if (rc >= 0) {
		c2_list_add_tail(&dom->nd_tms, &tm->ntm_dom_linkage);
		tm->ntm_state = C2_NET_TM_INITIALIZED;
	} else {
		c2_net__tm_cleanup(tm);
		NET_ADDB_FUNCFAIL_ADD(dom->nd_addb, rc);
	}
	c2_mutex_unlock(&dom->nd_mutex);

	return rc;
}
C2_EXPORTED(c2_net_tm_init);

C2_INTERNAL void c2_net_tm_fini(struct c2_net_transfer_mc *tm)
{
	struct c2_net_domain *dom = tm->ntm_dom;
	int i;

	/* wait for ongoing event processing to drain without holding lock:
	   events modify state and end point refcounts
	   Also applies to ongoing provisioning, which requires a check for
	   state in addition to counter.
	*/
	if (tm->ntm_callback_counter > 0) {
		struct c2_clink tmwait;
		c2_clink_init(&tmwait, NULL);
		c2_clink_add(&tm->ntm_chan, &tmwait);
		while (tm->ntm_callback_counter > 0 &&
		       tm->ntm_state == C2_NET_TM_STARTED)
			c2_chan_wait(&tmwait);
		c2_clink_del(&tmwait);
	}

	c2_mutex_lock(&dom->nd_mutex);
	C2_PRE(tm->ntm_state == C2_NET_TM_STOPPED ||
	       tm->ntm_state == C2_NET_TM_FAILED ||
	       tm->ntm_state == C2_NET_TM_INITIALIZED);

	for (i = 0; i < ARRAY_SIZE(tm->ntm_q); ++i) {
		C2_PRE(c2_net_tm_tlist_is_empty(&tm->ntm_q[i]));
	}
	C2_PRE((c2_list_is_empty(&tm->ntm_end_points) && tm->ntm_ep == NULL) ||
	       (c2_list_length(&tm->ntm_end_points) == 1 &&
		tm->ntm_ep != NULL &&
		c2_list_contains(&tm->ntm_end_points,
				 &tm->ntm_ep->nep_tm_linkage) &&
		c2_atomic64_get(&tm->ntm_ep->nep_ref.ref_cnt) == 1));

	/* release method requires TM mutex to be locked */
	c2_mutex_lock(&tm->ntm_mutex);
	if (tm->ntm_ep != NULL) {
		c2_ref_put(&tm->ntm_ep->nep_ref);
		tm->ntm_ep = NULL;
	}
	c2_mutex_unlock(&tm->ntm_mutex);

	dom->nd_xprt->nx_ops->xo_tm_fini(tm);

	C2_ASSERT(c2_list_is_empty(&tm->ntm_end_points));

	c2_list_del(&tm->ntm_dom_linkage);
	tm->ntm_state = C2_NET_TM_UNDEFINED;
	c2_net__tm_cleanup(tm);
	c2_list_link_fini(&tm->ntm_dom_linkage);

	c2_mutex_unlock(&dom->nd_mutex);
	return;
}
C2_EXPORTED(c2_net_tm_fini);

C2_INTERNAL int c2_net_tm_start(struct c2_net_transfer_mc *tm, const char *addr)
{
	int rc;

	C2_ASSERT(addr != NULL);
	C2_PRE(tm != NULL);
	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_invariant(tm));
	C2_PRE(tm->ntm_state == C2_NET_TM_INITIALIZED);

	if (C2_FI_ENABLED("fake_error")) {
		tm->ntm_state = C2_NET_TM_FAILED;
		c2_mutex_unlock(&tm->ntm_mutex);
		C2_RETURN(0);
	}

	tm->ntm_state = C2_NET_TM_STARTING;
	rc = tm->ntm_dom->nd_xprt->nx_ops->xo_tm_start(tm, addr);
	if (rc < 0) {
		/* xprt did not start, no retry supported */
		tm->ntm_state = C2_NET_TM_FAILED;
		C2_ASSERT(tm->ntm_ep == NULL);
		NET_ADDB_FUNCFAIL_ADD(tm->ntm_addb, rc);
	}
	C2_POST(c2_net__tm_invariant(tm));
	c2_mutex_unlock(&tm->ntm_mutex);
	C2_ASSERT(rc <= 0);
	return rc;
}
C2_EXPORTED(c2_net_tm_start);

C2_INTERNAL int c2_net_tm_stop(struct c2_net_transfer_mc *tm, bool abort)
{
	int rc;
	enum c2_net_tm_state oldstate;

	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_invariant(tm));
	C2_PRE(tm->ntm_state == C2_NET_TM_INITIALIZED ||
	       tm->ntm_state == C2_NET_TM_STARTING ||
	       tm->ntm_state == C2_NET_TM_STARTED);

	oldstate = tm->ntm_state;
	tm->ntm_state = C2_NET_TM_STOPPING;
	rc = tm->ntm_dom->nd_xprt->nx_ops->xo_tm_stop(tm, abort);
	if (rc < 0) {
		tm->ntm_state = oldstate;
		NET_ADDB_FUNCFAIL_ADD(tm->ntm_addb, rc);
	} else
		c2_atomic64_set(&tm->ntm_recv_queue_deficit, 0);

	C2_POST(c2_net__tm_invariant(tm));
	c2_mutex_unlock(&tm->ntm_mutex);
	C2_ASSERT(rc <= 0);
	return rc;
}
C2_EXPORTED(c2_net_tm_stop);

C2_INTERNAL int c2_net_tm_stats_get(struct c2_net_transfer_mc *tm,
				    enum c2_net_queue_type qtype,
				    struct c2_net_qstats *qs, bool reset)
{
	C2_PRE(qtype == C2_NET_QT_NR || c2_net__qtype_is_valid(qtype));
	C2_ASSERT(reset || qs != NULL);

	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_invariant(tm));
	if (qtype == C2_NET_QT_NR) {
		if (qs != NULL)
			memcpy(qs, tm->ntm_qstats, sizeof(tm->ntm_qstats));
		if (reset)
			C2_SET_ARR0(tm->ntm_qstats);
	} else {
		if (qs != NULL)
			*qs = tm->ntm_qstats[qtype];
		if (reset)
			C2_SET0(&tm->ntm_qstats[qtype]);
	}
	c2_mutex_unlock(&tm->ntm_mutex);

	return 0;
}
C2_EXPORTED(c2_net_tm_stats_get);

C2_INTERNAL int c2_net_tm_confine(struct c2_net_transfer_mc *tm,
				  const struct c2_bitmap *processors)
{
	int rc;
	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_invariant(tm));
	C2_PRE(tm->ntm_state == C2_NET_TM_INITIALIZED);
	C2_PRE(processors != NULL);
	if (tm->ntm_dom->nd_xprt->nx_ops->xo_tm_confine != NULL)
		rc =
		    tm->ntm_dom->nd_xprt->nx_ops->xo_tm_confine(tm, processors);
	else
		rc = -ENOSYS;
	C2_POST(c2_net__tm_invariant(tm));
	C2_POST(tm->ntm_state == C2_NET_TM_INITIALIZED);
	if (rc != 0)
		NET_ADDB_FUNCFAIL_ADD(tm->ntm_addb, rc);
	c2_mutex_unlock(&tm->ntm_mutex);
	return rc;
}
C2_EXPORTED(c2_net_tm_confine);

C2_INTERNAL int c2_net_buffer_event_deliver_synchronously(struct
							  c2_net_transfer_mc
							  *tm)
{
	int rc;
	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_invariant(tm));
	C2_PRE(tm->ntm_state == C2_NET_TM_INITIALIZED);
	C2_PRE(tm->ntm_bev_auto_deliver);
	if (tm->ntm_dom->nd_xprt->nx_ops->xo_bev_deliver_sync != NULL) {
		rc = tm->ntm_dom->nd_xprt->nx_ops->xo_bev_deliver_sync(tm);
		if (rc == 0)
			tm->ntm_bev_auto_deliver = false;
	} else
		rc = -ENOSYS;
	C2_POST(ergo(rc == 0, !tm->ntm_bev_auto_deliver));
	C2_POST(c2_net__tm_invariant(tm));
	if (rc != 0)
		NET_ADDB_FUNCFAIL_ADD(tm->ntm_addb, rc);
	c2_mutex_unlock(&tm->ntm_mutex);
	return rc;
}
C2_EXPORTED(c2_net_buffer_event_deliver_synchronously);

C2_INTERNAL void c2_net_buffer_event_deliver_all(struct c2_net_transfer_mc *tm)
{
	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_invariant(tm));
	C2_PRE(tm->ntm_state == C2_NET_TM_STARTED);
	C2_PRE(!tm->ntm_bev_auto_deliver);
	tm->ntm_dom->nd_xprt->nx_ops->xo_bev_deliver_all(tm);
	C2_POST(c2_net__tm_invariant(tm));
	c2_mutex_unlock(&tm->ntm_mutex);
	return;
}
C2_EXPORTED(c2_net_buffer_event_deliver_all);

C2_INTERNAL bool c2_net_buffer_event_pending(struct c2_net_transfer_mc *tm)
{
	bool result;
	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_invariant(tm));
	C2_PRE(tm->ntm_state == C2_NET_TM_STARTED);
	C2_PRE(!tm->ntm_bev_auto_deliver);
	result = tm->ntm_dom->nd_xprt->nx_ops->xo_bev_pending(tm);
	C2_POST(c2_net__tm_invariant(tm));
	c2_mutex_unlock(&tm->ntm_mutex);
	return result;
}
C2_EXPORTED(c2_net_buffer_event_pending);

C2_INTERNAL void c2_net_buffer_event_notify(struct c2_net_transfer_mc *tm,
					    struct c2_chan *chan)
{
	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_invariant(tm));
	C2_PRE(tm->ntm_state == C2_NET_TM_STARTED);
	C2_PRE(!tm->ntm_bev_auto_deliver);
	tm->ntm_dom->nd_xprt->nx_ops->xo_bev_notify(tm, chan);
	C2_POST(c2_net__tm_invariant(tm));
	c2_mutex_unlock(&tm->ntm_mutex);
	return;
}
C2_EXPORTED(c2_net_buffer_event_notify);

C2_INTERNAL void c2_net_tm_colour_set(struct c2_net_transfer_mc *tm,
				      uint32_t colour)
{
	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_invariant(tm));
	C2_PRE(tm->ntm_state == C2_NET_TM_INITIALIZED ||
	       tm->ntm_state == C2_NET_TM_STARTING ||
	       tm->ntm_state == C2_NET_TM_STARTED);
	tm->ntm_pool_colour = colour;
	c2_mutex_unlock(&tm->ntm_mutex);
	return;
}
C2_EXPORTED(c2_net_tm_colour_set);

C2_INTERNAL uint32_t c2_net_tm_colour_get(struct c2_net_transfer_mc *tm)
{
	uint32_t colour;
	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_invariant(tm));
	colour = tm->ntm_pool_colour;
	c2_mutex_unlock(&tm->ntm_mutex);
	return colour;
}
C2_EXPORTED(c2_net_tm_colour_get);

C2_INTERNAL int c2_net_tm_pool_attach(struct c2_net_transfer_mc *tm,
				      struct c2_net_buffer_pool *bufpool,
				      const struct c2_net_buffer_callbacks
				      *callbacks, c2_bcount_t min_recv_size,
				      uint32_t max_recv_msgs,
				      uint32_t min_recv_queue_len)
{
	int rc;
	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_invariant(tm));
	C2_PRE(tm->ntm_state == C2_NET_TM_INITIALIZED);
	C2_PRE(bufpool != NULL);
	C2_PRE(callbacks != NULL &&
	       callbacks->nbc_cb[C2_NET_QT_MSG_RECV] != NULL);
	C2_PRE(min_recv_size > 0);
	C2_PRE(max_recv_msgs > 0);
	if (bufpool->nbp_ndom == tm->ntm_dom) {
		tm->ntm_recv_pool		 = bufpool;
		tm->ntm_recv_pool_callbacks	 = callbacks;
		tm->ntm_recv_queue_min_recv_size = min_recv_size;
		tm->ntm_recv_queue_max_recv_msgs = max_recv_msgs;
		if(min_recv_queue_len > C2_NET_TM_RECV_QUEUE_DEF_LEN)
			tm->ntm_recv_queue_min_length = min_recv_queue_len;
		rc = 0;
	} else
		rc = -EINVAL;
	c2_mutex_unlock(&tm->ntm_mutex);
	return rc;
}
C2_EXPORTED(c2_net_tm_pool_attach);

C2_INTERNAL void c2_net_tm_pool_length_set(struct c2_net_transfer_mc *tm,
					   uint32_t len)
{
	struct c2_net_buffer_pool *pool = NULL;

	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_invariant(tm));
	C2_PRE(tm->ntm_state == C2_NET_TM_INITIALIZED ||
	       tm->ntm_state == C2_NET_TM_STARTING ||
	       tm->ntm_state == C2_NET_TM_STARTED);
	if (len > C2_NET_TM_RECV_QUEUE_DEF_LEN)
		tm->ntm_recv_queue_min_length = len;
	if (tm->ntm_recv_pool != NULL && tm->ntm_state == C2_NET_TM_STARTED) {
		pool = tm->ntm_recv_pool;
		C2_CNT_INC(tm->ntm_callback_counter);
	}
	c2_mutex_unlock(&tm->ntm_mutex);
	if (pool != NULL) {
		c2_net__tm_provision_recv_q(tm);
		c2_mutex_lock(&tm->ntm_mutex);
		C2_CNT_DEC(tm->ntm_callback_counter);
		c2_chan_broadcast(&tm->ntm_chan);
		c2_mutex_unlock(&tm->ntm_mutex);
	}
	return;
}
C2_EXPORTED(c2_net_tm_pool_length_set);

#undef C2_TRACE_SUBSYSTEM

/** @} end of net group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
