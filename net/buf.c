/* -*- C -*- */

#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/time.h"
#include "net/net_internal.h"

/**
   @addtogroup net
   @{
*/

bool c2_net__qtype_is_valid(enum c2_net_queue_type qt)
{
	return (qt >= C2_NET_QT_MSG_RECV && qt < C2_NET_QT_NR);
}

bool c2_net__buffer_invariant(struct c2_net_buffer *buf)
{
	C2_ASSERT(buf != NULL);

	/* must be a registered buffer */
	if (!(buf->nb_flags & C2_NET_BUF_REGISTERED))
		return false;

	/* domain must be set and initialized */
	if (buf->nb_dom == NULL || buf->nb_dom->nd_xprt == NULL)
		return false;

	/* bufvec must be set */
	if (buf->nb_buffer.ov_buf == NULL ||
	    buf->nb_buffer.ov_vec.v_nr == 0 ||
	    buf->nb_buffer.ov_vec.v_count == NULL)
		return false;

	/* optional callbacks, but if provided then ntc_event_cb required */
	if (buf->nb_callbacks != NULL &&
	    buf->nb_callbacks->ntc_event_cb == NULL)
		return false;

	/* is it queued? */
	if (!(buf->nb_flags & C2_NET_BUF_QUEUED))
		return true;

	/* must have a valid queue type */
	if (!c2_net__qtype_is_valid(buf->nb_qtype))
		return false;

	/* Must be associated with a TM.
	   Note: Buffer state does not imply TM state so don't test latter.
	 */
	if (buf->nb_tm == NULL)
		return false;

	/* EXPENSIVE: on the right TM list */
	if (!c2_list_contains(&buf->nb_tm->ntm_q[buf->nb_qtype],
			      &buf->nb_tm_linkage))
		return false;
	return true;
}

int c2_net_buffer_register(struct c2_net_buffer *buf, 
			   struct c2_net_domain *dom)
{
	int rc;

	C2_PRE(dom != NULL );
	C2_PRE(dom->nd_xprt != NULL);
	c2_mutex_lock(&dom->nd_mutex);

	C2_PRE(buf != NULL &&
	       buf->nb_flags == 0 && 
	       buf->nb_buffer.ov_buf != NULL && 
	       buf->nb_buffer.ov_vec.v_nr > 0 && 
	       buf->nb_buffer.ov_vec.v_count != NULL);

	buf->nb_dom = dom;
	c2_list_link_init(&buf->nb_dom_linkage);
	buf->nb_xprt_private = NULL;

	/* The transport will validate buffer size and number of
	   segments, and optimize it for future use.
	*/
	rc = dom->nd_xprt->nx_ops->xo_buf_register(buf);
	if ( rc == 0 ) {
		buf->nb_flags |= C2_NET_BUF_REGISTERED;
		c2_list_add_tail(&dom->nd_registered_bufs,&buf->nb_dom_linkage);
	}
	C2_POST(c2_net__buffer_invariant(buf));

	c2_mutex_unlock(&dom->nd_mutex);
	return rc;
}
C2_EXPORTED(c2_net_buffer_register);

int c2_net_buffer_deregister(struct c2_net_buffer *buf,
			     struct c2_net_domain *dom)
{
	int rc;

	C2_PRE(dom != NULL );
	C2_PRE(dom->nd_xprt != NULL);
	c2_mutex_lock(&dom->nd_mutex);

	C2_PRE(buf != NULL &&
	       buf->nb_flags == C2_NET_BUF_REGISTERED &&
	       buf->nb_dom == dom);
	C2_PRE(c2_list_contains(&dom->nd_registered_bufs,&buf->nb_dom_linkage));

	rc = dom->nd_xprt->nx_ops->xo_buf_deregister(buf);
	if (!rc) {
		buf->nb_flags &= ~C2_NET_BUF_REGISTERED;
		c2_list_del(&buf->nb_dom_linkage);
		buf->nb_xprt_private = NULL;
	}

	c2_mutex_unlock(&dom->nd_mutex);
	return rc;
}
C2_EXPORTED(c2_net_buffer_deregister);

int c2_net_buffer_add(struct c2_net_buffer *buf, 
		      struct c2_net_transfer_mc *tm)
{
	int rc;
	struct c2_net_domain *dom;
	c2_bcount_t blen;
	struct c2_list *ql = NULL;
	struct buf_add_checks {
		bool check_length;
		bool check_ep;
		bool check_desc;
		bool post_check_desc;
	};
	static const struct buf_add_checks checks[C2_NET_QT_NR] = {
		[C2_NET_QT_MSG_RECV]          = { false, false, false, false },
		[C2_NET_QT_MSG_SEND]          = { true,  true,  false, false },
		[C2_NET_QT_PASSIVE_BULK_RECV] = { false, true,  false, true  },
		[C2_NET_QT_PASSIVE_BULK_SEND] = { true,  true,  false, true  },
		[C2_NET_QT_ACTIVE_BULK_RECV]  = { false, false, true,  false },
		[C2_NET_QT_ACTIVE_BULK_SEND]  = { true,  false, true,  false }
	};
	const struct buf_add_checks *todo;

	C2_PRE(tm != NULL && tm->ntm_dom != NULL);
	C2_PRE(buf->nb_dom == tm->ntm_dom);

	dom = tm->ntm_dom;
	C2_PRE(dom->nd_xprt != NULL);
	c2_mutex_lock(&tm->ntm_mutex);

	C2_PRE(c2_net__buffer_invariant(buf));
	C2_PRE(!(buf->nb_flags & C2_NET_BUF_QUEUED));

	C2_PRE(buf->nb_qtype != C2_NET_QT_MSG_RECV || buf->nb_ep == NULL);

	/* the TM has to be started */
	if (tm->ntm_state != C2_NET_TM_STARTED) {
		rc = -EPERM;
		goto m_err_exit;
	}

	/* determine what to do by queue type */
	todo = &checks[buf->nb_qtype];
	ql = &tm->ntm_q[buf->nb_qtype];

	/* Check that length is set and is within buffer bounds.
	   The transport will make other checks on the buffer, such
	   as the max size and number of segments.
	 */
	if (todo->check_length) {
		if (buf->nb_length == 0) {
			rc = -EINVAL;
			goto m_err_exit;
		}
		blen = c2_vec_count(&buf->nb_buffer.ov_vec);
		if (buf->nb_length > blen) {
			rc = -EFBIG;
			goto m_err_exit;
		}
	}

	/* validate end point usage */
	if (todo->check_ep) {
		if (buf->nb_ep == NULL) {
			rc = -EINVAL;
			goto m_err_exit;
		}
		/* increment count later */
	}

	/* validate that the descriptor is present */
	if (todo->post_check_desc) {
		buf->nb_desc.nbd_len = 0;
		buf->nb_desc.nbd_data = NULL;
	}
	if (todo->check_desc) {
		if ( buf->nb_desc.nbd_len == 0 ||
		     buf->nb_desc.nbd_data == NULL ) {
			rc = -EINVAL;
			goto m_err_exit;
		}
	}

	/* Optimistically add it to the queue's list before calling the xprt.
	   Post will unlink on completion, or del on cancel.
	 */
	c2_list_link_init(&buf->nb_tm_linkage);
	c2_list_add_tail(ql, &buf->nb_tm_linkage);
	buf->nb_flags &= ~C2_NET_BUF_IN_USE; /* for transport use */
	buf->nb_flags |= C2_NET_BUF_QUEUED;
	(void)c2_time_now(&buf->nb_add_time); /* record time added */

	/* call the transport */
	buf->nb_tm = tm;
	rc = dom->nd_xprt->nx_ops->xo_buf_add(buf);
	if (rc) {
		c2_list_del(&buf->nb_tm_linkage);
		buf->nb_flags &= ~C2_NET_BUF_QUEUED;
		goto m_err_exit;
	}

	if (todo->post_check_desc) {
		C2_POST(buf->nb_desc.nbd_len != 0 &&
			buf->nb_desc.nbd_data != NULL);
	}

	tm->ntm_qstats[buf->nb_qtype].nqs_num_adds += 1;

	if (todo->check_ep) {
		/* Bump the reference count.
		   Should be decremented in c2_net_tm_event_post().
		*/
		c2_net_end_point_get(buf->nb_ep); /* mutex not used */
	}

	C2_POST(c2_net__buffer_invariant(buf));

 m_err_exit:
	c2_mutex_unlock(&tm->ntm_mutex);
	return rc;
}
C2_EXPORTED(c2_net_buffer_add);

int c2_net_buffer_del(struct c2_net_buffer *buf,
		      struct c2_net_transfer_mc *tm)
{
	int rc;
	struct c2_net_domain *dom;

	C2_PRE(tm != NULL && tm->ntm_dom != NULL);
	C2_PRE(buf != NULL && buf->nb_dom == tm->ntm_dom);
	C2_PRE(buf->nb_flags & C2_NET_BUF_REGISTERED);

	C2_PRE(buf->nb_tm == NULL || buf->nb_tm == tm );
	dom = tm->ntm_dom;
	C2_PRE(dom->nd_xprt != NULL);
	c2_mutex_lock(&tm->ntm_mutex);

	/* wait for callbacks to clear */
	while ((buf->nb_flags & C2_NET_BUF_IN_CALLBACK) != 0)
		c2_cond_wait(&tm->ntm_cond, &tm->ntm_mutex);

	if (!(buf->nb_flags & C2_NET_BUF_QUEUED)) {
		rc = 0; /* completion race condition? no error */
		c2_chan_broadcast(&tm->ntm_chan);
		goto m_err_exit;
	}
	C2_PRE(c2_net__qtype_is_valid(buf->nb_qtype));

	/* the transport may not support operation cancellation */
	rc = dom->nd_xprt->nx_ops->xo_buf_del(buf);
	if (rc) {
		goto m_err_exit;
	}

	c2_list_del(&buf->nb_tm_linkage);
	buf->nb_flags &= ~C2_NET_BUF_QUEUED;

	tm->ntm_qstats[buf->nb_qtype].nqs_num_dels += 1;

	C2_POST(c2_net__buffer_invariant(buf));

 m_err_exit:
	c2_mutex_unlock(&tm->ntm_mutex);
	return rc;
}
C2_EXPORTED(c2_net_buffer_del);


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
