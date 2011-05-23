/* -*- C -*- */

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

bool c2_net__ev_type_is_valid(enum c2_net_ev_type et)
{
	return et >= C2_NET_EV_ERROR && et < C2_NET_EV_NR;
}

bool c2_net__event_invariant(const struct c2_net_event *ev)
{
	if (!c2_net__ev_type_is_valid(ev->nev_type))
		return false;
	if (ev->nev_tm == NULL ||
	    !c2_net__tm_invariant(ev->nev_tm))
		return false;
	if (ev->nev_type == C2_NET_EV_BUFFER_RELEASE) {
		if (ev->nev_buffer == NULL)
			return false;
		if (!c2_net__buffer_invariant(ev->nev_buffer))
			return false;
	}
	if (ev->nev_type == C2_NET_EV_STATE_CHANGE &&
	    !c2_net__tm_state_is_valid(ev->nev_next_state))
		return false;
	return true;
}

bool c2_net__tm_invariant(const struct c2_net_transfer_mc *tm)
{
	if (tm == NULL || tm->ntm_callbacks == NULL || tm->ntm_dom == NULL)
		return false;
	if (tm->ntm_state < C2_NET_TM_INITIALIZED ||
	    tm->ntm_state > C2_NET_TM_FAILED)
		return false;
	if ((tm->ntm_state == C2_NET_TM_STARTING ||
	     tm->ntm_state == C2_NET_TM_STARTED) &&
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

void c2_net_tm_event_post(const struct c2_net_event *ev)
{
	struct c2_net_buffer *buf = NULL;
	const struct c2_net_tm_callbacks *cbs;
	c2_net_tm_cb_proc_t cb;
	struct c2_net_end_point *ep;
	bool check_ep;
	enum c2_net_queue_type qtype = C2_NET_QT_NR;
	struct c2_net_transfer_mc *tm;

	C2_PRE(ev != NULL);
	tm = ev->nev_tm;
	C2_PRE(tm != NULL);
	C2_PRE(c2_mutex_is_not_locked(&tm->ntm_mutex));

	/* pre-callback, in mutex:
	   update buffer (if present), state and statistics
	 */
	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__event_invariant(ev));

	cbs = tm->ntm_callbacks; /* by default use the TM callbacks */

	if (ev->nev_type == C2_NET_EV_BUFFER_RELEASE) {
		struct c2_net_qstats *q;
		c2_time_t tdiff;

		buf = ev->nev_buffer;
		while ((buf->nb_flags & C2_NET_BUF_IN_CALLBACK) != 0)
			c2_cond_wait(&tm->ntm_cond, &tm->ntm_mutex);
		C2_PRE(c2_net__tm_invariant(tm));
		C2_PRE(c2_net__buffer_invariant(buf));
		C2_PRE(buf->nb_flags & C2_NET_BUF_QUEUED);
		c2_list_del(&buf->nb_tm_linkage);

		qtype = buf->nb_qtype;
		buf->nb_status = ev->nev_status;
		if (buf->nb_flags & C2_NET_BUF_CANCELLED)
			buf->nb_status = -ECANCELED;
		buf->nb_flags &= ~(C2_NET_BUF_QUEUED | C2_NET_BUF_CANCELLED |
				   C2_NET_BUF_IN_USE);
		buf->nb_flags |= C2_NET_BUF_IN_CALLBACK;

		q = &tm->ntm_qstats[qtype];
		if (ev->nev_status < 0) {
			q->nqs_num_f_events++;
			if (qtype == C2_NET_QT_MSG_RECV ||
			    qtype == C2_NET_QT_PASSIVE_BULK_RECV ||
			    qtype == C2_NET_QT_ACTIVE_BULK_RECV)
				buf->nb_length = 0; /* may not be valid */
		} else {
			q->nqs_num_s_events++;
		}
		tdiff = c2_time_sub(ev->nev_time, buf->nb_add_time);
		q->nqs_time_in_queue = c2_time_add(q->nqs_time_in_queue, tdiff);
		q->nqs_total_bytes += buf->nb_length;
		q->nqs_max_bytes = max_check(q->nqs_max_bytes, buf->nb_length);

		/* buffer callback takes precedence */
		if (buf->nb_callbacks != NULL)
			cbs = buf->nb_callbacks;

	} else if (ev->nev_type == C2_NET_EV_STATE_CHANGE) {
		tm->ntm_state = ev->nev_next_state;
	}

	tm->ntm_callback_counter++;
	c2_mutex_unlock(&tm->ntm_mutex);

	cb = cbs->ntc_event_cb; /* default to general callback */
	check_ep = false;
	switch (qtype) {
	case C2_NET_QT_MSG_RECV:
		check_ep = true;	/* special case */
		if (cbs->ntc_msg_recv_cb != NULL)
			cb = cbs->ntc_msg_recv_cb;
		break;
	case C2_NET_QT_MSG_SEND:
		check_ep = true;
		if (cbs->ntc_msg_send_cb != NULL)
			cb = cbs->ntc_msg_send_cb;
		break;
	case C2_NET_QT_PASSIVE_BULK_RECV:
		check_ep = true;
		if (cbs->ntc_passive_bulk_recv_cb != NULL)
			cb = cbs->ntc_passive_bulk_recv_cb;
		break;
	case C2_NET_QT_PASSIVE_BULK_SEND:
		check_ep = true;
		if (cbs->ntc_passive_bulk_send_cb != NULL)
			cb = cbs->ntc_passive_bulk_send_cb;
		break;
	case C2_NET_QT_ACTIVE_BULK_RECV:
		if (cbs->ntc_active_bulk_recv_cb != NULL)
			cb = cbs->ntc_active_bulk_recv_cb;
		break;
	case C2_NET_QT_ACTIVE_BULK_SEND:
		if (cbs->ntc_active_bulk_send_cb != NULL)
			cb = cbs->ntc_active_bulk_send_cb;
		break;
	default:
		break;
	}
	C2_ASSERT(cb != NULL);
	if (check_ep) {
		ep = buf->nb_ep; /* save pointer to decrement ref post cb */

		C2_ASSERT(ep == NULL || ({
			  bool eprc;
			  c2_mutex_lock(&tm->ntm_dom->nd_mutex);
			  eprc = c2_net__ep_invariant(ep, tm->ntm_dom, true);
			  c2_mutex_unlock(&tm->ntm_dom->nd_mutex);
			  eprc; }));
	}

	cb(ev);

	/* post callback, in mutex:
	   decrement ref counts,
	   signal waiters
	 */
	c2_mutex_lock(&tm->ntm_mutex);
	tm->ntm_callback_counter--;
	if (buf != NULL) {
		buf->nb_flags &= ~C2_NET_BUF_IN_CALLBACK;
		c2_cond_signal(&tm->ntm_cond, &tm->ntm_mutex);
	}
	c2_chan_broadcast(&tm->ntm_chan);
	c2_mutex_unlock(&tm->ntm_mutex);

	/* Decrement the transport reference to an EP; put re-gets mutex */
	if (check_ep && ep)
		c2_net_end_point_put(ep);

	return;
}
C2_EXPORTED(c2_net_tm_event_post);

static void c2_net__tm_cleanup(struct c2_net_transfer_mc *tm)
{
	int i;
	c2_cond_fini(&tm->ntm_cond);
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
	c2_cond_init(&tm->ntm_cond);
	tm->ntm_callback_counter = 0;
	tm->ntm_dom = dom;
	tm->ntm_ep = NULL;
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

	dom->nd_xprt->nx_ops->xo_tm_fini(tm);

	if (tm->ntm_ep != NULL) {
		c2_ref_put(&tm->ntm_ep->nep_ref);
		tm->ntm_ep = NULL;
	}
	c2_list_del(&tm->ntm_dom_linkage);
	tm->ntm_state = C2_NET_TM_UNDEFINED;
	c2_net__tm_cleanup(tm);
	c2_list_link_fini(&tm->ntm_dom_linkage);

	c2_mutex_unlock(&dom->nd_mutex);
	return;
}
C2_EXPORTED(c2_net_tm_fini);

int c2_net_tm_start(struct c2_net_transfer_mc *tm,
		    struct c2_net_end_point *ep)
{
	int result;

	C2_ASSERT(ep != NULL);
	c2_mutex_lock(&tm->ntm_mutex);
	C2_PRE(c2_net__tm_invariant(tm));
	C2_PRE(tm->ntm_state == C2_NET_TM_INITIALIZED);

	c2_ref_get(&ep->nep_ref);
	tm->ntm_ep = ep;
	tm->ntm_state = C2_NET_TM_STARTING;
	result = tm->ntm_dom->nd_xprt->nx_ops->xo_tm_start(tm);
	if (result < 0) {
		/* xprt did not start, no retry supported */
		tm->ntm_state = C2_NET_TM_FAILED;
		tm->ntm_ep = NULL;
		c2_ref_put(&ep->nep_ref);
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
