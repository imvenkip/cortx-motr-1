/* -*- C -*- */

#include "lib/assert.h"
#include "lib/errno.h"
#include "lib/time.h"
#include "net/net_internal.h"

/** @}
 @addtogroup net
 @{
*/

int c2_net_buffer_register(struct c2_net_buffer *buf, 
			   struct c2_net_domain *dom)
{
	int rc;

	C2_PRE(dom != NULL );
	C2_PRE(dom->nd_xprt != NULL);
	c2_mutex_lock(&dom->nd_mutex);

	C2_PRE(buf &&
	       (buf->nb_flags == 0) && 
	       (buf->nb_buffer.ov_buf != NULL) && 
	       (buf->nb_buffer.ov_vec.v_nr > 0) && 
	       (buf->nb_buffer.ov_vec.v_count != NULL));

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
	else {
		C2_POST((buf->nb_flags & C2_NET_BUF_REGISTERED) == 0);
	}

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

	C2_PRE(buf &&
	       (buf->nb_flags == C2_NET_BUF_REGISTERED) &&
	       (buf->nb_dom == dom) );
	C2_PRE(c2_list_contains(&dom->nd_registered_bufs,&buf->nb_dom_linkage));

	rc = dom->nd_xprt->nx_ops->xo_buf_deregister(buf);
	if ( rc == 0 ) {
		buf->nb_flags &= ~C2_NET_BUF_REGISTERED;
		c2_list_del(&buf->nb_dom_linkage);
		buf->nb_xprt_private = NULL;
	}
	else {
		C2_POST(buf->nb_flags & C2_NET_BUF_REGISTERED);
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
	bool check_length=false;
	bool check_ep=false;
	bool check_desc=false;
	bool post_check_desc=false;
	struct c2_list *ql=NULL;

	C2_PRE((tm != NULL) && (tm->ntm_dom != NULL));
	C2_PRE(buf && (buf->nb_dom == tm->ntm_dom));
	C2_PRE((buf->nb_qtype >= C2_NET_QT_MSG_RECV) &&
	       (buf->nb_qtype < C2_NET_QT_NR) &&
	       (buf->nb_flags & C2_NET_BUF_REGISTERED) &&
	       ((buf->nb_flags & C2_NET_BUF_QUEUED) == 0) );
	C2_ASSERT(buf->nb_callbacks == NULL ||
		  buf->nb_callbacks->ntc_event_cb != NULL);

	dom = tm->ntm_dom;
	C2_PRE(dom->nd_xprt != NULL);
	c2_mutex_lock(&dom->nd_mutex);

	/* Receive queue accepts any starting state but otherwise
	   the TM has to be started
	*/
	if ( tm->ntm_state != C2_NET_TM_STARTED ) {
		if ( (buf->nb_qtype != C2_NET_QT_MSG_RECV) ||
		     !((tm->ntm_state == C2_NET_TM_INITIALIZED) ||
		       (tm->ntm_state == C2_NET_TM_STARTING)) ) {
			rc = -EPERM;
			goto m_err_exit;
		}
	}

	/* determine what to do by queue type */
	switch ( buf->nb_qtype ){
	case C2_NET_QT_MSG_RECV:
		ql = &tm->ntm_msg_bufs;
		break;
	case C2_NET_QT_MSG_SEND:
		check_length = true;
		check_ep = true;
		ql = &tm->ntm_msg_bufs;
		break;
	case C2_NET_QT_PASSIVE_BULK_RECV:
		check_ep = true;
		post_check_desc = true;
		ql = &tm->ntm_passive_bufs;
		break;
	case C2_NET_QT_PASSIVE_BULK_SEND:
		check_length = true;
		check_ep = true;
		post_check_desc = true;
		ql = &tm->ntm_passive_bufs;
		break;
	case C2_NET_QT_ACTIVE_BULK_RECV:
		check_desc = true;
		ql = &tm->ntm_active_bufs;
		break;
	case C2_NET_QT_ACTIVE_BULK_SEND:
		check_length = true;
		check_desc = true;
		ql = &tm->ntm_active_bufs;
		break;
	default:
		break;
	}
	C2_ASSERT( ql != NULL );

	/* Check that length is set and is within buffer bounds.
	   The transport will make other checks on the buffer, such
	   as the max size and number of segments.
	 */
	if ( check_length ) {
		if ( buf->nb_length == 0 ) {
			rc = -EINVAL;
			goto m_err_exit;
		}
		blen = c2_vec_count(&buf->nb_buffer.ov_vec);
		if ( buf->nb_length > blen ) {
			rc = -EFBIG;
			goto m_err_exit;
		}
	}

	/* validate end point usage */
	if ( check_ep ) {
		if ( buf->nb_ep == NULL ){
			rc = -EINVAL;
			goto m_err_exit;
		}
		/* increment count later */
	}

	/* validate that the descriptor is present */
	if ( post_check_desc ) {
		buf->nb_desc.nbd_len = 0;
		buf->nb_desc.nbd_data = NULL;
	}
	if ( check_desc ) {
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
	if ( rc ) {
		c2_list_del(&buf->nb_tm_linkage);
		buf->nb_flags &= ~C2_NET_BUF_QUEUED;
		goto m_err_exit;
	}

	if ( post_check_desc ) {
		C2_POST( (buf->nb_desc.nbd_len != 0) &&
			 (buf->nb_desc.nbd_data != NULL) );
	}

	tm->ntm_qstats[buf->nb_qtype].nqs_num_adds += 1;

	if ( check_ep ) {
		/* Bump the reference count.
		   Should be decremented in c2_net_tm_event_post().
		*/
		c2_net_end_point_get(buf->nb_ep); /* mutex not used */
	}

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

	C2_PRE((tm != NULL) && (tm->ntm_dom != NULL));
	C2_PRE(buf && (buf->nb_dom == tm->ntm_dom));
	C2_PRE(buf->nb_flags & C2_NET_BUF_REGISTERED);

	dom = tm->ntm_dom;
	C2_PRE(dom->nd_xprt != NULL);
	c2_mutex_lock(&dom->nd_mutex);

	if ( (buf->nb_flags & C2_NET_BUF_QUEUED) == 0 ) {
		rc = 0; /* completion race condition? no error */
		goto m_err_exit;
	}
	C2_PRE( buf->nb_tm == tm );
	C2_PRE( buf->nb_qtype >= C2_NET_QT_MSG_RECV );
	C2_PRE( buf->nb_qtype < C2_NET_QT_NR );

	/* the transport may not support operation cancellation */
	rc = dom->nd_xprt->nx_ops->xo_buf_del(buf);
	if ( rc ) {
		goto m_err_exit;
	}

	c2_list_del(&buf->nb_tm_linkage);
	buf->nb_flags &= ~C2_NET_BUF_QUEUED;

	tm->ntm_qstats[buf->nb_qtype].nqs_num_dels += 1;

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
