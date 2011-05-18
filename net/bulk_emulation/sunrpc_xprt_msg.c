/* -*- C -*- */

/**
   @addtogroup bulksunrpc
   @{
 */

/**
   Work function to send a message using an RPC call.
   Messages must fit within the first buffer segment.
 */
static void sunrpc_wf_msg_send(struct c2_net_transfer_mc *tm,
			       struct c2_net_bulk_mem_work_item *wi)
{
	struct c2_net_buffer   *nb   = MEM_WI_TO_BUFFER(wi);
	struct c2_fop          *f    = NULL;
	struct c2_fop          *r    = NULL;
	struct c2_net_conn     *conn = NULL;
	int rc;

	C2_PRE(nb != NULL &&
	       nb->nb_qtype == C2_NET_QT_MSG_SEND &&
	       nb->nb_tm == tm &&
	       nb->nb_ep != NULL);
	C2_PRE(nb->nb_flags & C2_NET_BUF_IN_USE);

	do {
		struct c2_bufvec_cursor  cur;
		struct sunrpc_msg       *fop;
		struct sunrpc_msg_resp  *rep;
		struct c2_net_end_point *tm_ep;

		/* get a connection for this end point */
		rc = sunrpc_ep_get_conn(nb->nb_ep, &conn);
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
		fop->sm_sender.sep_addr = MEM_EP_ADDR(tm_ep); /* network byte */
		fop->sm_sender.sep_port = MEM_EP_PORT(tm_ep); /* order */
		fop->sm_sender.sep_id   = MEM_EP_SID(tm_ep);
		fop->sm_receiver.sep_addr = MEM_EP_ADDR(nb->nb_ep); /* NBO */
		fop->sm_receiver.sep_port = MEM_EP_PORT(nb->nb_ep); /* NBO */
		fop->sm_receiver.sep_id   = MEM_EP_SID(nb->nb_ep);
		c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
		C2_ASSERT(nb->nb_length <= c2_bufvec_cursor_step(&cur));
		fop->sm_buf.sb_len = nb->nb_length;
		fop->sm_buf.sb_buf = c2_bufvec_cursor_addr(&cur);

		/* make the RPC call */
		struct c2_net_call call = {
			.ac_arg = f,
			.ac_ret = r
		};
		rc = c2_net_cli_call(conn, &call);
		if (rc == 0) {
			rep = c2_fop_data(r);
			rc = rep->smr_rc;
		}
	} while(0);

	if (f != NULL)
		c2_fop_free(f);
	if (r != NULL)
		c2_fop_free(r);
	if (conn != NULL)
		c2_net_conn_release(conn);

	/* post the send completion callback (will clear C2_NET_BUF_IN_USE) */
	C2_POST(rc <= 0);
	struct c2_net_event ev = {
		.nev_type    = C2_NET_EV_BUFFER_RELEASE,
		.nev_tm      = tm,
		.nev_buffer  = nb,
		.nev_status  = rc,
		.nev_payload = wi
	};
	c2_time_now(&ev.nev_time);
	c2_net_tm_event_post(&ev);
}

static int sunrpc_msg_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	struct sunrpc_msg      *in = c2_fop_data(fop);
	struct sunrpc_msg_resp *ex;
	struct c2_fop          *reply;
	struct c2_net_buffer   *nb = NULL;
	struct c2_bufvec_cursor cur;
	int rc;

	reply = c2_fop_alloc(&sunrpc_msg_resp_fopt, NULL);
	C2_ASSERT(reply != NULL);
	ex = c2_fop_data(reply);

	/* locate the tm, identified by its sid in the buffer desc */
	struct c2_net_transfer_mc *tm = sunrpc_find_tm(in->sm_receiver.sep_id);
	if (tm == NULL) {
		rc = -ENXIO;
		goto err_exit;
	}
	/* TM mutex is locked */

	do {
		/* get the first available receive buffer */
		bool found_nb = false;
		c2_list_for_each_entry(&tm->ntm_q[C2_NET_QT_MSG_RECV], nb,
				       struct c2_net_buffer,
				       nb_tm_linkage) {
			if ((nb->nb_flags & C2_NET_BUF_IN_USE) == 0) {
				found_nb = true;
				break;
			}
		}
		if (!found_nb) {
			tm->ntm_qstats[C2_NET_QT_MSG_RECV].nqs_num_f_events++;
			rc = -ENOBUFS;
			nb = NULL;
			break;
		}
		C2_ASSERT(sunrpc_buffer_invariant(nb));
		nb->nb_flags |= C2_NET_BUF_IN_USE;
		c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
		if (in->sm_buf.sb_len > c2_bufvec_cursor_step(&cur)) {
			struct c2_net_bulk_mem_work_item *wi =
				MEM_BUFFER_TO_WI(nb);
			struct c2_net_bulk_sunrpc_tm_pvt *tp =
				nb->nb_tm->ntm_xprt_private;
			rc = -EMSGSIZE;
			nb->nb_status = rc;
			nb->nb_length = in->sm_buf.sb_len; /* desired length */
			/* schedule the receive msg callback */
			wi->xwi_op = C2_NET_XOP_MSG_RECV_CB;
			sunrpc_wi_add(wi, tp);
			break;
		}
		rc = 0;
	} while(0);
	c2_mutex_unlock(&tm->ntm_mutex);

	if (rc == 0) {
		/* got a buffer */
		struct c2_net_bulk_mem_work_item *wi = MEM_BUFFER_TO_WI(nb);
		struct c2_net_domain *dom = tm->ntm_dom;
		struct c2_net_bulk_sunrpc_tm_pvt *tp =
			nb->nb_tm->ntm_xprt_private;
		struct sockaddr_in sa = {
			.sin_addr.s_addr = in->sm_sender.sep_addr, /* network */
			.sin_port        = in->sm_sender.sep_port, /* order */
		};
		uint32_t sid = in->sm_sender.sep_id;

		/* create an end point for the message sender */
		c2_mutex_lock(&dom->nd_mutex);
		rc = sunrpc_ep_create(&nb->nb_ep, dom, &sa, sid);
		c2_mutex_unlock(&dom->nd_mutex);

		if (rc == 0) {
			/* copy the message to the buffer */
			memcpy(c2_bufvec_cursor_addr(&cur), in->sm_buf.sb_buf,
			       in->sm_buf.sb_len);
			nb->nb_length = in->sm_buf.sb_len;
		}
		nb->nb_status = rc;

		/* schedule the receive msg callback */
		wi->xwi_op = C2_NET_XOP_MSG_RECV_CB;
		c2_mutex_lock(&tm->ntm_mutex);
		sunrpc_wi_add(wi, tp);
		c2_mutex_unlock(&tm->ntm_mutex);
	}

	if (rc < 0 && nb == NULL) {
		/* post the error to the TM */
		struct c2_net_bulk_sunrpc_domain_pvt *dp;
		dp = tm->ntm_dom->nd_xprt_private;
		c2_mutex_lock(&tm->ntm_mutex);
		(*dp->xd_base_ops.bmo_post_error)(tm, rc);
		c2_mutex_unlock(&tm->ntm_mutex);
	}

 err_exit:
	/* send the RPC response (note: not delivered yet, but enqueued) */
	ex->smr_rc = rc;
	c2_net_reply_post(ctx->ft_service, reply, ctx->fc_cookie);

	return 0;
}

/**
   @} bulksunrpc
*/

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
