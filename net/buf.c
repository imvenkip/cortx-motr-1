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
 * Original creation date: 04/05/2011
 */

#include "lib/arith.h" /* max_check */
#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/time.h"
#include "lib/finject.h"
#include "colibri/magic.h"
#include "net/net_internal.h"

/**
   @addtogroup net
   @{
 */

const struct c2_addb_ctx_type c2_net_buffer_addb_ctx = {
	.act_name = "net-buffer"
};

bool c2_net__qtype_is_valid(enum c2_net_queue_type qt)
{
	return qt >= C2_NET_QT_MSG_RECV && qt < C2_NET_QT_NR;
}

bool c2_net__buffer_invariant(const struct c2_net_buffer *buf)
{
	if (buf == NULL)
		return false;

	/* must be a registered buffer */
	if (!(buf->nb_flags & C2_NET_BUF_REGISTERED))
		return false;

	/* domain must be set and initialized */
	if (buf->nb_dom == NULL || buf->nb_dom->nd_xprt == NULL)
		return false;

	/* bufvec must be set */
	if (buf->nb_buffer.ov_buf == NULL ||
	    c2_vec_count(&buf->nb_buffer.ov_vec) == 0)
		return false;

	/* is it queued? */
	if (!(buf->nb_flags & C2_NET_BUF_QUEUED))
		return true;

	/* must have a valid queue type */
	if (!c2_net__qtype_is_valid(buf->nb_qtype))
		return false;

	/* if queued, must have the appropriate callback */
	if (buf->nb_callbacks == NULL)
		return false;
	if (buf->nb_callbacks->nbc_cb[buf->nb_qtype] == NULL)
		return false;

	/* Must be associated with a TM.
	   Note: Buffer state does not imply TM state so don't test latter.
	 */
	if (buf->nb_tm == NULL)
		return false;

	/* TM's domain must be the buffer's domain */
	if (buf->nb_dom != buf->nb_tm->ntm_dom)
		return false;

	/* EXPENSIVE: on the right TM list */
	if (!c2_net_tm_tlist_contains(&buf->nb_tm->ntm_q[buf->nb_qtype], buf))
		return false;
	return true;
}

int c2_net_buffer_register(struct c2_net_buffer *buf,
			   struct c2_net_domain *dom)
{
	int rc;

	C2_PRE(dom != NULL);
	C2_PRE(dom->nd_xprt != NULL);

	if (C2_FI_ENABLED("fake_error"))
		return -EINVAL;

	c2_mutex_lock(&dom->nd_mutex);

	C2_PRE(buf != NULL &&
	       buf->nb_dom == NULL &&
	       buf->nb_flags == 0 &&
	       buf->nb_buffer.ov_buf != NULL &&
	       c2_vec_count(&buf->nb_buffer.ov_vec) > 0);

	buf->nb_dom = dom;
	buf->nb_xprt_private = NULL;
	buf->nb_timeout = C2_TIME_NEVER;
	buf->nb_magic = C2_NET_BUFFER_LINK_MAGIC;
	c2_addb_ctx_init(&buf->nb_addb, &c2_net_buffer_addb_ctx, &dom->nd_addb);

	/* The transport will validate buffer size and number of
	   segments, and optimize it for future use.
	 */
	rc = dom->nd_xprt->nx_ops->xo_buf_register(buf);
	if (rc == 0) {
		buf->nb_flags |= C2_NET_BUF_REGISTERED;
		c2_list_add_tail(&dom->nd_registered_bufs,&buf->nb_dom_linkage);
	} else {
		NET_ADDB_FUNCFAIL_ADD(dom->nd_addb, rc);
		c2_addb_ctx_fini(&buf->nb_addb);
	}

	C2_POST(ergo(rc == 0, c2_net__buffer_invariant(buf)));
	C2_POST(ergo(rc == 0, buf->nb_timeout == C2_TIME_NEVER));

	c2_mutex_unlock(&dom->nd_mutex);
	return rc;
}
C2_EXPORTED(c2_net_buffer_register);

void c2_net_buffer_deregister(struct c2_net_buffer *buf,
			      struct c2_net_domain *dom)
{
	C2_PRE(dom != NULL);
	C2_PRE(dom->nd_xprt != NULL);
	c2_mutex_lock(&dom->nd_mutex);

	C2_PRE(c2_net__buffer_invariant(buf) && buf->nb_dom == dom);
	C2_PRE(buf->nb_flags == C2_NET_BUF_REGISTERED);
	C2_PRE(c2_list_contains(&dom->nd_registered_bufs,&buf->nb_dom_linkage));

	dom->nd_xprt->nx_ops->xo_buf_deregister(buf);
	buf->nb_flags &= ~C2_NET_BUF_REGISTERED;
	c2_list_del(&buf->nb_dom_linkage);
	buf->nb_xprt_private = NULL;
	buf->nb_magic = 0;
	c2_addb_ctx_fini(&buf->nb_addb);
        buf->nb_dom = NULL;

	c2_mutex_unlock(&dom->nd_mutex);
	return;
}
C2_EXPORTED(c2_net_buffer_deregister);

int c2_net__buffer_add(struct c2_net_buffer *buf, struct c2_net_transfer_mc *tm)
{
	int rc;
	struct c2_net_domain *dom;
	struct c2_tl	     *ql;
	struct buf_add_checks {
		bool check_length;
		bool check_ep;
		bool check_desc;
		bool post_check_desc;
	};
	static const struct buf_add_checks checks[C2_NET_QT_NR] = {
		[C2_NET_QT_MSG_RECV]          = { false, false, false, false },
		[C2_NET_QT_MSG_SEND]          = { true,  true,  false, false },
		[C2_NET_QT_PASSIVE_BULK_RECV] = { false, false, false, true  },
		[C2_NET_QT_PASSIVE_BULK_SEND] = { true,  false, false, true  },
		[C2_NET_QT_ACTIVE_BULK_RECV]  = { false, false, true,  false },
		[C2_NET_QT_ACTIVE_BULK_SEND]  = { true,  false, true,  false }
	};
	const struct buf_add_checks *todo;

	C2_PRE(tm != NULL);
	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));
	C2_PRE(c2_net__tm_invariant(tm));
	C2_PRE(c2_net__buffer_invariant(buf));
	C2_PRE(buf->nb_dom == tm->ntm_dom);

	dom = tm->ntm_dom;
	C2_PRE(dom->nd_xprt != NULL);

	C2_PRE(!(buf->nb_flags &
	       (C2_NET_BUF_QUEUED | C2_NET_BUF_IN_USE | C2_NET_BUF_CANCELLED |
		C2_NET_BUF_TIMED_OUT | C2_NET_BUF_RETAIN)));
	C2_PRE(ergo(buf->nb_qtype == C2_NET_QT_MSG_RECV,
		    buf->nb_ep == NULL &&
		    buf->nb_min_receive_size != 0 &&
		    buf->nb_min_receive_size <=
		                        c2_vec_count(&buf->nb_buffer.ov_vec) &&
		    buf->nb_max_receive_msgs != 0));
	C2_PRE(tm->ntm_state == C2_NET_TM_STARTED);

	/* determine what to do by queue type */
	todo = &checks[buf->nb_qtype];
	ql = &tm->ntm_q[buf->nb_qtype];

	/* Validate that that length is set and is within buffer bounds.
	   The transport will make other checks on the buffer, such
	   as the max size and number of segments.
	 */
	C2_PRE(ergo(todo->check_length,
		    buf->nb_length > 0 &&
		    (buf->nb_length + buf->nb_offset) <=
		    c2_vec_count(&buf->nb_buffer.ov_vec)));

	/* validate end point usage; increment ref count later */
	C2_PRE(ergo(todo->check_ep,
		    buf->nb_ep != NULL &&
		    c2_net__ep_invariant(buf->nb_ep, tm, true)));

	/* validate that the descriptor is present */
	if (todo->post_check_desc) {
		buf->nb_desc.nbd_len = 0;
		buf->nb_desc.nbd_data = NULL;
	}
	C2_PRE(ergo(todo->check_desc,
		    buf->nb_desc.nbd_len > 0 &&
		    buf->nb_desc.nbd_data != NULL));

	/* validate that a timeout, if set, is in the future */
	if (buf->nb_timeout != C2_TIME_NEVER) {
		/* Don't want to assert here as scheduling is unpredictable. */
		if (c2_time_now() >= buf->nb_timeout) {
			rc = -ETIME; /* not -ETIMEDOUT */
			goto m_err_exit;
		}
	}

	/* Optimistically add it to the queue's list before calling the xprt.
	   Post will unlink on completion, or del on cancel.
	 */
	c2_net_tm_tlink_init_at_tail(buf, ql);
	buf->nb_flags |= C2_NET_BUF_QUEUED;
	buf->nb_add_time = c2_time_now(); /* record time added */

	/* call the transport */
	buf->nb_tm = tm;
	rc = dom->nd_xprt->nx_ops->xo_buf_add(buf);
	if (rc != 0) {
		c2_net_tm_tlink_del_fini(buf);
		buf->nb_flags &= ~C2_NET_BUF_QUEUED;
		goto m_err_exit;
	}

	tm->ntm_qstats[buf->nb_qtype].nqs_num_adds += 1;

	if (todo->check_ep) {
		/* Bump the reference count.
		   Should be decremented in c2_net_buffer_event_post().
		   The caller holds a reference to the end point.
		 */
		c2_net_end_point_get(buf->nb_ep);
	}

	C2_POST(ergo(todo->post_check_desc,
		     buf->nb_desc.nbd_len != 0 &&
		     buf->nb_desc.nbd_data != NULL));
	C2_POST(c2_net__buffer_invariant(buf));
	C2_POST(c2_net__tm_invariant(tm));

 m_err_exit:
	return rc;
}

int c2_net_buffer_add(struct c2_net_buffer *buf, struct c2_net_transfer_mc *tm)
{
	int rc;
	C2_PRE(tm != NULL);
	if (C2_FI_ENABLED("fake_error"))
		return -EMSGSIZE;
	c2_mutex_lock(&tm->ntm_mutex);
	rc = c2_net__buffer_add(buf, tm);
	c2_mutex_unlock(&tm->ntm_mutex);
	if (rc != 0)
		NET_ADDB_FUNCFAIL_ADD(buf->nb_addb, rc);
	return rc;
}
C2_EXPORTED(c2_net_buffer_add);

void c2_net_buffer_del(struct c2_net_buffer *buf,
		       struct c2_net_transfer_mc *tm)
{
	struct c2_net_domain *dom;

	C2_PRE(tm != NULL && tm->ntm_dom != NULL);
	C2_PRE(buf != NULL);

	dom = tm->ntm_dom;
	C2_PRE(dom->nd_xprt != NULL);
	c2_mutex_lock(&tm->ntm_mutex);

	C2_PRE(c2_net__buffer_invariant(buf));
	C2_PRE(buf->nb_tm == NULL || buf->nb_tm == tm);

	if (!(buf->nb_flags & C2_NET_BUF_QUEUED)) {
		/* completion race condition? no error */
		goto m_err_exit;
	}

	/* tell the transport to cancel */
	dom->nd_xprt->nx_ops->xo_buf_del(buf);

	tm->ntm_qstats[buf->nb_qtype].nqs_num_dels += 1;

	C2_POST(c2_net__buffer_invariant(buf));
	C2_POST(c2_net__tm_invariant(tm));

 m_err_exit:
	c2_mutex_unlock(&tm->ntm_mutex);
	return;
}
C2_EXPORTED(c2_net_buffer_del);

bool c2_net__buffer_event_invariant(const struct c2_net_buffer_event *ev)
{
	if (ev == NULL)
		return false;
	if (ev->nbe_status > 0)
		return false;
	if (ev->nbe_buffer == NULL)
		return false; /* can't check buf invariant here */
	if (!ergo(ev->nbe_status == 0 &&
		  ev->nbe_buffer->nb_qtype == C2_NET_QT_MSG_RECV,
		  ev->nbe_ep != NULL))
		return false; /* don't check ep invariant here */
	if (!ergo(ev->nbe_buffer->nb_flags & C2_NET_BUF_CANCELLED,
		  ev->nbe_status == -ECANCELED))
		return false;
	if (!ergo(ev->nbe_buffer->nb_flags & C2_NET_BUF_TIMED_OUT,
		  ev->nbe_status == -ETIMEDOUT))
		return false;
	if (!ergo(ev->nbe_buffer->nb_flags & C2_NET_BUF_RETAIN,
		  ev->nbe_status == 0))
		return false;
	return true;
}

void c2_net_buffer_event_post(const struct c2_net_buffer_event *ev)
{
	struct c2_net_buffer	  *buf = NULL;
	struct c2_net_end_point	  *ep;
	bool			   check_ep;
	bool			   retain;
	enum c2_net_queue_type	   qtype = C2_NET_QT_NR;
	struct c2_net_transfer_mc *tm;
	struct c2_net_qstats	  *q;
	c2_time_t		   tdiff;
	c2_net_buffer_cb_proc_t	   cb;
	c2_bcount_t		   len = 0;
	struct c2_net_buffer_pool *pool = NULL;

	C2_PRE(c2_net__buffer_event_invariant(ev));
	buf = ev->nbe_buffer;
	tm  = buf->nb_tm;
	C2_PRE(c2_mutex_is_not_locked(&tm->ntm_mutex));

	/* pre-callback, in mutex:
	   update buffer (if present), state and statistics
	 */
	c2_mutex_lock(&tm->ntm_mutex);

	C2_PRE(c2_net__tm_invariant(tm));
	C2_PRE(c2_net__buffer_invariant(buf));
	C2_PRE(buf->nb_flags & C2_NET_BUF_QUEUED);

	if (!(buf->nb_flags & C2_NET_BUF_RETAIN)) {
		c2_net_tm_tlist_del(buf);
		buf->nb_flags &= ~(C2_NET_BUF_QUEUED | C2_NET_BUF_CANCELLED |
				   C2_NET_BUF_IN_USE | C2_NET_BUF_TIMED_OUT);
		buf->nb_timeout = C2_TIME_NEVER;
		retain = false;
	} else {
		buf->nb_flags &= ~C2_NET_BUF_RETAIN;
		retain = true;
	}

	qtype = buf->nb_qtype;
	q = &tm->ntm_qstats[qtype];
	if (ev->nbe_status < 0) {
		q->nqs_num_f_events++;
		len = 0; /* length not counted on failure */
	} else {
		q->nqs_num_s_events++;
		if (qtype == C2_NET_QT_MSG_RECV ||
		    qtype == C2_NET_QT_PASSIVE_BULK_RECV ||
		    qtype == C2_NET_QT_ACTIVE_BULK_RECV)
			len = ev->nbe_length;
		else
			len = buf->nb_length;
	}
	if (!(buf->nb_flags & C2_NET_BUF_QUEUED)) {
		tdiff = c2_time_sub(ev->nbe_time, buf->nb_add_time);
		q->nqs_time_in_queue = c2_time_add(q->nqs_time_in_queue, tdiff);
	}
	q->nqs_total_bytes += len;
	q->nqs_max_bytes = max_check(q->nqs_max_bytes, len);

	ep = NULL;
	check_ep = false;
	switch (qtype) {
	case C2_NET_QT_MSG_RECV:
		if (ev->nbe_status == 0) {
			check_ep = true;
			ep = ev->nbe_ep; /* from event */
		}
		if (!(buf->nb_flags & C2_NET_BUF_QUEUED) &&
		    tm->ntm_state == C2_NET_TM_STARTED &&
		    tm->ntm_recv_pool != NULL)
			pool = tm->ntm_recv_pool;
		break;
	case C2_NET_QT_MSG_SEND:
		/* must put() ep to match get in buffer_add() */
		ep = buf->nb_ep;   /* from buffer */
		break;
	default:
		break;
	}

	if (check_ep) {
		C2_ASSERT(c2_net__ep_invariant(ep, tm, true));
	}

	cb = buf->nb_callbacks->nbc_cb[qtype];
	C2_CNT_INC(tm->ntm_callback_counter);
	c2_mutex_unlock(&tm->ntm_mutex);

	if (pool != NULL && !retain)
		c2_net__tm_provision_recv_q(tm);

	cb(ev);

	/* Decrement the reference to the ep */
	if (ep != NULL)
		c2_net_end_point_put(ep);

	/* post callback, in mutex:
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
