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

	/* only care for registered buffers */
	if (!(buf->nb_flags & C2_NET_BUF_REGISTERED))
		return true;

	/* domain must be set and initialized */
	if (buf->nb_dom == NULL || buf->nb_dom->nd_xprt == NULL)
		return false;

	/* these checks are invalid if the domain is not locked */
	C2_ASSERT(c2_mutex_is_locked(&buf->nb_dom->nd_mutex));

	/* bufvec must be set */
	if (buf->nb_buffer.ov_buf == NULL ||
	    buf->nb_buffer.ov_vec.v_nr == 0 ||
	    buf->nb_buffer.ov_vec.v_count == NULL)
		return false;

	/* EXPENSIVE: on the domain registered list */
	if (!c2_list_contains(&buf->nb_dom->nd_registered_bufs,
			      &buf->nb_dom_linkage)) 
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
	C2_POST(c2_net__buffer_invariant(buf));

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
	bool check_length = false;
	bool check_ep = false;
	bool check_desc = false;
	bool post_check_desc = false;
	struct c2_list *ql = NULL;

	C2_PRE(tm != NULL && tm->ntm_dom != NULL);
	C2_PRE(buf != NULL && buf->nb_dom == tm->ntm_dom);
	C2_PRE(c2_net__qtype_is_valid(buf->nb_qtype) &&
	       buf->nb_flags & C2_NET_BUF_REGISTERED &&
	       !(buf->nb_flags & C2_NET_BUF_QUEUED) );
	C2_PRE(buf->nb_callbacks == NULL ||
	       buf->nb_callbacks->ntc_event_cb != NULL);

	dom = tm->ntm_dom;
	C2_PRE(dom->nd_xprt != NULL);
	c2_mutex_lock(&dom->nd_mutex);

	/* Receive queue accepts any starting state but otherwise
	   the TM has to be started
	*/
	if (tm->ntm_state != C2_NET_TM_STARTED) {
		if (buf->nb_qtype != C2_NET_QT_MSG_RECV ||
		    !(tm->ntm_state == C2_NET_TM_INITIALIZED ||
		      tm->ntm_state == C2_NET_TM_STARTING) ) {
			rc = -EPERM;
			goto m_err_exit;
		}
	}

	/* determine what to do by queue type */
	switch (buf->nb_qtype) {
	case C2_NET_QT_MSG_RECV:
		break;
	case C2_NET_QT_MSG_SEND:
		check_length = true;
		check_ep = true;
		break;
	case C2_NET_QT_PASSIVE_BULK_RECV:
		check_ep = true;
		post_check_desc = true;
		break;
	case C2_NET_QT_PASSIVE_BULK_SEND:
		check_length = true;
		check_ep = true;
		post_check_desc = true;
		break;
	case C2_NET_QT_ACTIVE_BULK_RECV:
		check_desc = true;
		break;
	case C2_NET_QT_ACTIVE_BULK_SEND:
		check_length = true;
		check_desc = true;
		break;
	default:
		C2_IMPOSSIBLE("invalid queue type");
		break;
	}
	ql = &tm->ntm_q[buf->nb_qtype];

	/* Check that length is set and is within buffer bounds.
	   The transport will make other checks on the buffer, such
	   as the max size and number of segments.
	 */
	if (check_length) {
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
	if (check_ep) {
		if (buf->nb_ep == NULL){
			rc = -EINVAL;
			goto m_err_exit;
		}
		/* increment count later */
	}

	/* validate that the descriptor is present */
	if (post_check_desc) {
		buf->nb_desc.nbd_len = 0;
		buf->nb_desc.nbd_data = NULL;
	}
	if (check_desc) {
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

	if (post_check_desc) {
		C2_POST(buf->nb_desc.nbd_len != 0 &&
			buf->nb_desc.nbd_data != NULL);
	}

	tm->ntm_qstats[buf->nb_qtype].nqs_num_adds += 1;

	if (check_ep) {
		/* Bump the reference count.
		   Should be decremented in c2_net_tm_event_post().
		*/
		c2_net_end_point_get(buf->nb_ep); /* mutex not used */
	}

	C2_POST(c2_net__buffer_invariant(buf));

 m_err_exit:
	c2_mutex_unlock(&dom->nd_mutex);
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

	dom = tm->ntm_dom;
	C2_PRE(dom->nd_xprt != NULL);
	c2_mutex_lock(&dom->nd_mutex);

	if (!(buf->nb_flags & C2_NET_BUF_QUEUED)) {
		rc = 0; /* completion race condition? no error */
		goto m_err_exit;
	}
	C2_PRE(buf->nb_tm == tm );
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
	c2_mutex_unlock(&dom->nd_mutex);
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
