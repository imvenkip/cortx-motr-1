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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>,
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 04/12/2011
 */

/**
   @addtogroup bulksunrpc
   @{
 */

/**
   Inherit the message receive callback method.
 */
static void sunrpc_wf_msg_recv_cb(struct c2_net_transfer_mc *tm,
				  struct c2_net_bulk_mem_work_item *wi)
{
	struct c2_net_bulk_sunrpc_domain_pvt *dp;
	C2_PRE(sunrpc_dom_invariant(tm->ntm_dom));
	dp = sunrpc_dom_to_pvt(tm->ntm_dom);
	(*dp->xd_base_ops->bmo_work_fn[C2_NET_XOP_MSG_RECV_CB])(tm, wi);
}

/**
   Work function to send a message using an RPC call.
   Messages must fit within the first buffer segment.
 */
static void sunrpc_wf_msg_send(struct c2_net_transfer_mc *tm,
			       struct c2_net_bulk_mem_work_item *wi)
{
	struct c2_net_buffer   *nb   = mem_wi_to_buffer(wi);
	struct c2_fop          *f    = NULL;
	struct c2_fop          *r    = NULL;
	struct sunrpc_msg      *fop  = NULL;
	struct c2_net_conn     *conn = NULL;
	struct c2_net_bulk_sunrpc_conn *sconn;
	int rc;
	struct c2_net_bulk_sunrpc_domain_pvt *dp;

	C2_PRE(nb != NULL &&
	       nb->nb_qtype == C2_NET_QT_MSG_SEND &&
	       nb->nb_tm == tm &&
	       nb->nb_ep != NULL);
	C2_PRE(nb->nb_flags & C2_NET_BUF_IN_USE);
	dp = sunrpc_dom_to_pvt(nb->nb_dom);

	do {
		struct c2_bufvec_cursor  cur;
		struct sunrpc_msg_resp  *rep;
		struct c2_net_end_point *tm_ep;

		/* get a connection for this end point */
		rc = sunrpc_ep_get_conn(nb->nb_ep, &conn, &sconn);
		if (rc != 0)
			break;

		/* allocate the fops */
		f = c2_fop_alloc(&sunrpc_msg_fopt, NULL);
		r = c2_fop_alloc(&sunrpc_msg_resp_fopt, NULL);
		if (f == NULL || r == NULL) {
			rc = -ENOMEM;
			break;
		}
		fop = c2_fop_data(f);

		/* Set up the outgoing fop. */
		tm_ep = nb->nb_tm->ntm_ep;
		fop->sm_sender.sep_addr = mem_ep_addr(tm_ep); /* network byte */
		fop->sm_sender.sep_port = mem_ep_port(tm_ep); /* order */
		fop->sm_sender.sep_id   = mem_ep_sid(tm_ep);
		fop->sm_receiver.sep_addr = mem_ep_addr(nb->nb_ep); /* NBO */
		fop->sm_receiver.sep_port = mem_ep_port(nb->nb_ep); /* NBO */
		fop->sm_receiver.sep_id   = mem_ep_sid(nb->nb_ep);
		c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
		rc = sunrpc_buffer_init(&fop->sm_buf, &cur, nb->nb_length);
		if (rc != 0)
			break;

		/* make the RPC call */
		{
			struct c2_net_call call = {
				.ac_arg = f,
				.ac_ret = r
			};
			rc = c2_net_cli_call(conn, &call);
		}
		if (rc == 0) {
			rep = c2_fop_data(r);
			rc = rep->smr_rc;
		}
	} while (0);

	if (fop != NULL)
		sunrpc_buffer_fini(&fop->sm_buf);
	if (f != NULL)
		c2_fop_free(f);
	if (r != NULL)
		c2_fop_free(r);
	if (conn != NULL)
		sunrpc_ep_put_conn(sconn, conn, rc);

	/* post the send completion callback (will clear C2_NET_BUF_IN_USE) */
	wi->xwi_status = rc;
	(*dp->xd_base_ops->bmo_wi_post_buffer_event)(wi);
	return;
}

static int sunrpc_msg_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	struct sunrpc_msg         *in = c2_fop_data(fop);
	struct sunrpc_msg_resp    *ex;
	struct c2_fop             *reply;
	struct c2_net_buffer      *nb = NULL;
	struct c2_bufvec_cursor    cur;
	struct c2_net_transfer_mc *tm;
	int rc;

	reply = c2_fop_alloc(&sunrpc_msg_resp_fopt, NULL);
	C2_ASSERT(reply != NULL);
	ex = c2_fop_data(reply);

	/* locate the tm, identified by its sid in the buffer desc */
	tm = sunrpc_find_tm(in->sm_receiver.sep_id);
	if (tm == NULL) {
		rc = -ENXIO;
		goto err_exit;
	}
	/* TM mutex is locked */

	do {
		/* get the first available receive buffer */
		bool found_nb = false;
		c2_tl_for(c2_net_tm, &tm->ntm_q[C2_NET_QT_MSG_RECV], nb) {
			if ((nb->nb_flags &
			     (C2_NET_BUF_IN_USE | C2_NET_BUF_CANCELLED |
			      C2_NET_BUF_TIMED_OUT)) == 0) {
				found_nb = true;
				break;
			}
		} c2_tl_endfor;
		if (!found_nb) {
			tm->ntm_qstats[C2_NET_QT_MSG_RECV].nqs_num_f_events++;
			rc = -ENOBUFS;
			nb = NULL;
			break;
		}
		C2_ASSERT(sunrpc_buffer_invariant(nb));
		nb->nb_flags |= C2_NET_BUF_IN_USE;
		c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
		if (in->sm_buf.sb_len > c2_vec_count(&nb->nb_buffer.ov_vec)) {
			struct c2_net_bulk_mem_work_item *wi =
				mem_buffer_to_wi(nb);
			struct c2_net_bulk_sunrpc_tm_pvt *tp =
				sunrpc_tm_to_pvt(nb->nb_tm);
			rc = -EMSGSIZE;
			/* set desired length */
			wi->xwi_nbe_length = in->sm_buf.sb_len;
			/* schedule the receive msg callback */
			wi->xwi_op = C2_NET_XOP_MSG_RECV_CB;
			wi->xwi_status = rc;
			sunrpc_wi_add(wi, &tp->xtm_base);
			break;
		}
		rc = 0;
	} while (0);
	c2_mutex_unlock(&tm->ntm_mutex);

	if (rc == 0) {
		/* got a buffer */
		struct c2_net_bulk_mem_work_item *wi = mem_buffer_to_wi(nb);
		struct c2_net_bulk_sunrpc_tm_pvt *tp =
			sunrpc_tm_to_pvt(nb->nb_tm);
		struct sockaddr_in sa = {
			.sin_addr.s_addr = in->sm_sender.sep_addr, /* network */
			.sin_port        = in->sm_sender.sep_port, /* order */
		};
		uint32_t sid = in->sm_sender.sep_id;

		/* create an end point for the message sender */
		c2_mutex_lock(&tm->ntm_mutex);
		rc = sunrpc_ep_create(&wi->xwi_nbe_ep, tm, &sa, sid);
		c2_mutex_unlock(&tm->ntm_mutex);

		if (rc == 0) {
			/* copy the message to the buffer */
			rc = sunrpc_buffer_copy_out(&cur, &in->sm_buf);
			wi->xwi_nbe_length = in->sm_buf.sb_len;
		}
		wi->xwi_status = rc;

		/* schedule the receive msg callback */
		wi->xwi_op = C2_NET_XOP_MSG_RECV_CB;
		c2_mutex_lock(&tm->ntm_mutex);
		sunrpc_wi_add(wi, &tp->xtm_base);
		c2_mutex_unlock(&tm->ntm_mutex);
	}

	if (rc < 0 && nb == NULL) {
		/* post the error to the TM */
		struct c2_net_bulk_sunrpc_domain_pvt *dp;
		dp = sunrpc_dom_to_pvt(tm->ntm_dom);
		c2_mutex_lock(&tm->ntm_mutex);
		(*dp->xd_base_ops->bmo_post_error)(tm, rc);
		c2_mutex_unlock(&tm->ntm_mutex);
	}

 err_exit:
	/* send the RPC response (note: not delivered yet, but enqueued) */
	ex->smr_rc = rc;
	c2_net_reply_post(ctx->ft_service, reply, ctx->fc_cookie);

	return 0;
}

/** @} */ /* bulksunrpc */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
