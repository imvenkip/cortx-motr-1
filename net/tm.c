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
 * Original author: Dave Cohrs <Dave_Cohrs@us.xyratex.com>,
 *                  Carl Braganza <Carl_Braganza@us.xyratex.com>
 * Original creation date: 04/07/2011
 */

#include "lib/arith.h" /* max64u */
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/misc.h"
#include "net/net_internal.h"

/**
   @addtogroup net Networking.
   @{
 */
bool c2_net__tm_state_is_valid(enum c2_net_tm_state ts)
{
	return ts >= C2_NET_TM_UNDEFINED && ts <= C2_NET_TM_FAILED;
}

bool c2_net__tm_ev_type_is_valid(enum c2_net_tm_ev_type et)
{
	return et >= C2_NET_TEV_ERROR && et < C2_NET_TEV_NR;
}

bool c2_net__tm_event_invariant(const struct c2_net_tm_event *ev)
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

bool c2_net__tm_invariant(const struct c2_net_transfer_mc *tm)
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
			if (!c2_list_is_empty(&tm->ntm_q[i]))
				return false;
	}
	return true;
}

void c2_net_tm_event_post(const struct c2_net_tm_event *ev)
{
	struct c2_net_transfer_mc *tm;

	C2_PRE(ev != NULL);
	tm = ev->nte_tm;
	C2_PRE(tm != NULL);
	C2_PRE(c2_mutex_is_not_locked(&tm->ntm_mutex));

	/* pre-callback, in mutex */
	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_event_invariant(ev));

	if (ev->nte_type == C2_NET_TEV_STATE_CHANGE) {
		tm->ntm_state = ev->nte_next_state;
		if (tm->ntm_state == C2_NET_TM_STARTED)
			tm->ntm_ep = ev->nte_ep; /* ep now visible */
	}

	tm->ntm_callback_counter++;
	c2_mutex_unlock(&tm->ntm_mutex);

	(*tm->ntm_callbacks->ntc_event_cb)(ev);

	/* post callback, in mutex:
	   decrement ref counts,
	   signal waiters
	 */
	c2_mutex_lock(&tm->ntm_mutex);
	tm->ntm_callback_counter--;
	c2_chan_broadcast(&tm->ntm_chan);
	c2_mutex_unlock(&tm->ntm_mutex);

	return;
}
C2_EXPORTED(c2_net_tm_event_post);

static void c2_net__tm_cleanup(struct c2_net_transfer_mc *tm)
{
	int i;
	c2_mutex_fini(&tm->ntm_mutex);
	tm->ntm_dom = NULL;
	c2_chan_fini(&tm->ntm_chan);
	for (i = 0; i < ARRAY_SIZE(tm->ntm_q); ++i) {
		c2_list_fini(&tm->ntm_q[i]);
	}
	tm->ntm_xprt_private = NULL;
	return;
}

int c2_net_tm_init(struct c2_net_transfer_mc *tm, struct c2_net_domain *dom)
{
	int result;
	int i;

	c2_mutex_lock(&dom->nd_mutex);
	C2_PRE(tm != NULL);
	C2_PRE(tm->ntm_state == C2_NET_TM_UNDEFINED);
	C2_PRE(tm->ntm_callbacks != NULL &&
	       tm->ntm_callbacks->ntc_event_cb != NULL);

	c2_mutex_init(&tm->ntm_mutex);
	tm->ntm_callback_counter = 0;
	tm->ntm_dom = dom;
	tm->ntm_ep = NULL;
	c2_list_init(&tm->ntm_end_points);
	c2_chan_init(&tm->ntm_chan);
	for (i = 0; i < ARRAY_SIZE(tm->ntm_q); ++i) {
		c2_list_init(&tm->ntm_q[i]);
	}
	C2_SET_ARR0(tm->ntm_qstats);
	tm->ntm_xprt_private = NULL;

	result = dom->nd_xprt->nx_ops->xo_tm_init(tm);
	if (result >= 0) {
		c2_list_add_tail(&dom->nd_tms, &tm->ntm_dom_linkage);
		tm->ntm_state = C2_NET_TM_INITIALIZED;
	} else
		c2_net__tm_cleanup(tm);
	c2_mutex_unlock(&dom->nd_mutex);

	return result;
}
C2_EXPORTED(c2_net_tm_init);

void c2_net_tm_fini(struct c2_net_transfer_mc *tm)
{
	struct c2_net_domain *dom = tm->ntm_dom;
	int i;

	c2_mutex_lock(&dom->nd_mutex);
	C2_PRE(tm->ntm_state == C2_NET_TM_STOPPED ||
	       tm->ntm_state == C2_NET_TM_FAILED ||
	       tm->ntm_state == C2_NET_TM_INITIALIZED);

	for (i = 0; i < ARRAY_SIZE(tm->ntm_q); ++i) {
		C2_PRE(c2_list_is_empty(&tm->ntm_q[i]));
	}
	C2_PRE(tm->ntm_callback_counter == 0);

	/* release method requires TM mutex to be locked */
	c2_mutex_lock(&tm->ntm_mutex);
	if (tm->ntm_ep != NULL) {
		c2_ref_put(&tm->ntm_ep->nep_ref);
		tm->ntm_ep = NULL;
	}
	c2_mutex_unlock(&tm->ntm_mutex);

	dom->nd_xprt->nx_ops->xo_tm_fini(tm);

	C2_ASSERT(c2_list_is_empty(&tm->ntm_end_points));
	c2_list_fini(&tm->ntm_end_points);

	c2_list_del(&tm->ntm_dom_linkage);
	tm->ntm_state = C2_NET_TM_UNDEFINED;
	c2_net__tm_cleanup(tm);
	c2_list_link_fini(&tm->ntm_dom_linkage);

	c2_mutex_unlock(&dom->nd_mutex);
	return;
}
C2_EXPORTED(c2_net_tm_fini);

int c2_net_tm_start(struct c2_net_transfer_mc *tm, const char *addr)
{
	int result;

	C2_ASSERT(addr != NULL);
	C2_PRE(tm != NULL);
	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_invariant(tm));
	C2_PRE(tm->ntm_state == C2_NET_TM_INITIALIZED);

	tm->ntm_state = C2_NET_TM_STARTING;
	result = tm->ntm_dom->nd_xprt->nx_ops->xo_tm_start(tm, addr);
	if (result < 0) {
		/* xprt did not start, no retry supported */
		tm->ntm_state = C2_NET_TM_FAILED;
		C2_ASSERT(tm->ntm_ep == NULL);
	}
	C2_POST(c2_net__tm_invariant(tm));
	c2_mutex_unlock(&tm->ntm_mutex);

	return result;
}
C2_EXPORTED(c2_net_tm_start);

int c2_net_tm_stop(struct c2_net_transfer_mc *tm, bool abort)
{
	int result;
	enum c2_net_tm_state oldstate;

	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_invariant(tm));
	C2_PRE(tm->ntm_state == C2_NET_TM_INITIALIZED ||
	       tm->ntm_state == C2_NET_TM_STARTING ||
	       tm->ntm_state == C2_NET_TM_STARTED);

	C2_PRE((c2_list_is_empty(&tm->ntm_end_points) && tm->ntm_ep == NULL) ||
	       (c2_list_length(&tm->ntm_end_points) == 1 &&
		tm->ntm_ep != NULL &&
		c2_list_contains(&tm->ntm_end_points,
				 &tm->ntm_ep->nep_tm_linkage) &&
		c2_atomic64_get(&tm->ntm_ep->nep_ref.ref_cnt) == 1));

	oldstate = tm->ntm_state;
	tm->ntm_state = C2_NET_TM_STOPPING;
	result = tm->ntm_dom->nd_xprt->nx_ops->xo_tm_stop(tm, abort);
	if (result < 0)
		tm->ntm_state = oldstate;
	C2_POST(c2_net__tm_invariant(tm));
	c2_mutex_unlock(&tm->ntm_mutex);

	return result;
}
C2_EXPORTED(c2_net_tm_stop);

int c2_net_tm_stats_get(struct c2_net_transfer_mc *tm,
			enum c2_net_queue_type qtype,
			struct c2_net_qstats *qs,
			bool reset)
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
