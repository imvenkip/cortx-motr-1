/* -*- C -*- */

/**
   @addtogroup bulksunrpc
   @{
 */

static void sunrpc_queue_passive_cb(struct c2_net_buffer *nb, int rc)
{
	struct c2_net_bulk_mem_work_item *passive_wi = MEM_BUFFER_TO_WI(nb);
	struct c2_net_bulk_sunrpc_tm_pvt *passive_tp =
	    nb->nb_tm->ntm_xprt_private;

	nb->nb_status = rc;
	passive_wi->xwi_op = C2_NET_XOP_PASSIVE_BULK_CB;
	sunrpc_wi_add(passive_wi, passive_tp);
}

static int sunrpc_get_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	struct sunrpc_get      *in = c2_fop_data(fop);
	struct sunrpc_get_resp *ex;
	struct c2_fop          *reply;
	int                     rc = 0;

	reply = c2_fop_alloc(&sunrpc_get_resp_fopt, NULL);
	C2_ASSERT(reply != NULL);
	ex = c2_fop_data(reply);

	struct c2_net_bulk_sunrpc_tm_pvt *tp =
	    container_of(ctx->ft_service,
			 struct c2_net_bulk_sunrpc_tm_pvt, xtm_service);
	struct c2_net_transfer_mc *tm = tp->xtm_base.xtm_tm;
	c2_mutex_lock(&tm->ntm_mutex);

	/* locate the passive buffer */
	struct c2_net_buffer *nb = NULL;
	struct c2_net_buffer *inb;
	c2_list_for_each_entry(&tm->ntm_q[in->sg_desc.sbd_qtype], inb,
			       struct c2_net_buffer,
			       nb_tm_linkage) {
		if (sunrpc_desc_equal(&inb->nb_desc, &in->sg_desc) &&
		    inb->nb_length == in->sg_desc.sbd_total) {
			nb = inb;
			break;
		}
	}
	if (nb == NULL) {
		rc = -ENOENT;
		goto done;
	}

	/* copy up to 1 segment from passive buffer into the reply,
	   and set sgr_eof if end of net buffer is reached.
	*/
	c2_bcount_t len = nb->nb_length;
	struct c2_bufvec_cursor cur;
	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
	bool eof = c2_bufvec_cursor_move(&cur, in->sg_offset);

	if (!eof && len > in->sg_offset) {
		len -= in->sg_offset;
		c2_bcount_t step = min32u(c2_bufvec_cursor_step(&cur), len);

		ex->sgr_buf.sb_len = step;
		C2_ALLOC_ARR(ex->sgr_buf.sb_buf, step);
		if (ex->sgr_buf.sb_buf == NULL)
			rc = -ENOMEM;
		else {
			struct c2_bufvec dtmp = {
				.ov_vec = {
					.v_nr = 1,
					.v_count = &step
				},
				.ov_buf = (void**) &ex->sgr_buf.sb_buf
			};
			struct c2_bufvec_cursor dcur;
			c2_bcount_t copied;

			c2_bufvec_cursor_init(&dcur, &dtmp);
			copied = c2_bufvec_cursor_copy(&dcur, &cur, step);
			C2_ASSERT(copied == step);
			eof = c2_bufvec_cursor_move(&cur, 0);
		}
	} else {
		eof = true;
		ex->sgr_buf.sb_len = 0;
		C2_ALLOC_ARR(ex->sgr_buf.sb_buf, 1);
		if (ex->sgr_buf.sb_buf == NULL)
			rc = -ENOMEM;
	}
	ex->sgr_eof = eof;

	if (eof || rc != 0)
		sunrpc_queue_passive_cb(nb, rc);

done:
	c2_mutex_unlock(&tm->ntm_mutex);
	ex->sgr_rc = rc;
	c2_net_reply_post(ctx->ft_service, reply, ctx->fc_cookie);
	return rc;
}

static int sunrpc_put_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx)
{
	struct sunrpc_put      *in = c2_fop_data(fop);
	struct sunrpc_put_resp *ex;
	struct c2_fop          *reply;
	int                     rc = 0;

	reply = c2_fop_alloc(&sunrpc_put_resp_fopt, NULL);
	C2_ASSERT(reply != NULL);
	ex = c2_fop_data(reply);

	struct c2_net_bulk_sunrpc_tm_pvt *tp =
	    container_of(ctx->ft_service,
			 struct c2_net_bulk_sunrpc_tm_pvt, xtm_service);
	struct c2_net_transfer_mc *tm = tp->xtm_base.xtm_tm;
	c2_mutex_lock(&tm->ntm_mutex);

	/* locate the passive buffer */
	struct c2_net_buffer *nb = NULL;
	struct c2_net_buffer *inb;
	c2_list_for_each_entry(&tm->ntm_q[in->sp_desc.sbd_qtype], inb,
			       struct c2_net_buffer,
			       nb_tm_linkage) {
		if (sunrpc_desc_equal(&inb->nb_desc, &in->sp_desc)) {
			nb = inb;
			break;
		}
	}
	if (nb == NULL) {
		rc = -ENOENT;
		goto done;
	}

	/* copy up the data from the wire buffer to the passive net buffer
	   starting at the specified offset.  Assumes the put operations
	   are performed sequentially, so the passive callback can be
	   made as soon as the final put operation is performed.
	*/
	c2_bcount_t len = in->sp_buf.sb_len;
	struct c2_bufvec_cursor cur;
	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);

	struct c2_bufvec stmp = {
		.ov_vec = {
			.v_nr = 1,
			.v_count = &len
		},
		.ov_buf = (void**) &in->sp_buf.sb_buf
	};
	struct c2_bufvec_cursor scur;
	c2_bcount_t copied;

	c2_bufvec_cursor_init(&scur, &stmp);
	c2_bufvec_cursor_move(&cur, in->sp_offset);
	copied = c2_bufvec_cursor_copy(&cur, &scur, len);
	if (copied < len) {
		rc = -EFBIG;
		sunrpc_queue_passive_cb(nb, rc);
	} else if (in->sp_offset + in->sp_buf.sb_len == in->sp_desc.sbd_total) {
		nb->nb_length = in->sp_desc.sbd_total;
		sunrpc_queue_passive_cb(nb, rc);
	}

done:
	c2_mutex_unlock(&tm->ntm_mutex);
	ex->spr_rc = rc;
	c2_net_reply_post(ctx->ft_service, reply, ctx->fc_cookie);
	return rc;
}

static int sunrpc_active_send(struct c2_net_buffer *nb,
			      struct sunrpc_buf_desc *sd,
			      struct c2_net_end_point *ep)
{
	int                     rc = 0;
	struct c2_net_conn      *conn;
	struct c2_fop           *f = NULL;
	struct c2_fop           *r = NULL;
	struct sunrpc_put       *fop;
	struct sunrpc_put_resp  *rep;
	struct c2_net_end_point *tm_ep;

	/* get a connection for this end point */
	rc = sunrpc_ep_make_conn(ep, &conn);
	if (rc != 0)
		return rc;

	f = c2_fop_alloc(&sunrpc_put_fopt, NULL);
	r = c2_fop_alloc(&sunrpc_put_resp_fopt, NULL);
	if (f == NULL || r == NULL) {
		rc = -ENOMEM;
		goto done;
	}
	fop = c2_fop_data(f);

	struct c2_net_call call = {
		.ac_arg = f,
		.ac_ret = r
	};

	tm_ep = nb->nb_tm->ntm_ep;
	fop->sp_sender.sep_addr = MEM_EP_ADDR(tm_ep); /* network byte */
	fop->sp_sender.sep_port = MEM_EP_PORT(tm_ep); /* order */

	/*
	  Walk each buf in our bufvec, sending data
	  to remote until complete bufvec is transferred.
	*/
	struct c2_bufvec_cursor cur;
	size_t len = nb->nb_length;
	size_t off = 0;
	c2_bcount_t step;

	fop->sp_desc = *sd;
	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
	while (len > 0 && rc == 0) {
		step = min32u(c2_bufvec_cursor_step(&cur), len);
		fop->sp_offset = off;
		fop->sp_buf.sb_len = step;
		fop->sp_buf.sb_buf = c2_bufvec_cursor_addr(&cur);
		rc = c2_net_cli_call(conn, &call);
		if (rc == 0) {
			rep = c2_fop_data(r);
			rc = rep->spr_rc;
		}
		off += step;
		len -= step;
		C2_ASSERT(len == 0 || !c2_bufvec_cursor_move(&cur, step));
	}

done:
	if (r != NULL)
		c2_fop_free(r);
	if (f != NULL)
		c2_fop_free(f);

	return rc;
}

static int sunrpc_active_recv(struct c2_net_buffer *nb,
			      struct sunrpc_buf_desc *sd,
			      struct c2_net_end_point *ep)
{
	int                      rc;
	struct c2_net_conn      *conn;
	struct c2_fop           *f = NULL;
	struct c2_fop           *r = NULL;
	struct sunrpc_get       *fop;
	struct sunrpc_get_resp  *rep;
	struct c2_net_end_point *tm_ep;

	/* get a connection for this end point */
	rc = sunrpc_ep_make_conn(ep, &conn);
	if (rc != 0)
		return rc;

	f = c2_fop_alloc(&sunrpc_get_fopt, NULL);
	r = c2_fop_alloc(&sunrpc_get_resp_fopt, NULL);
	if (f == NULL || r == NULL) {
		rc = -ENOMEM;
		goto done;
	}
	fop = c2_fop_data(f);

	struct c2_net_call call = {
		.ac_arg = f,
		.ac_ret = r
	};

	tm_ep = nb->nb_tm->ntm_ep;
	fop->sg_sender.sep_addr = MEM_EP_ADDR(tm_ep); /* network byte */
	fop->sg_sender.sep_port = MEM_EP_PORT(tm_ep); /* order */

	/*
	  Receive data from remote and copy to our bufvec
	  until complete bufvec is transferred.
	*/
	struct c2_bufvec_cursor cur;
	c2_bcount_t len;
	size_t off = 0;
	c2_bcount_t copied;
	bool eof = false;

	fop->sg_desc = *sd;
	c2_bufvec_cursor_init(&cur, &nb->nb_buffer);
	nb->nb_length = sd->sbd_total;
	while (!eof) {
		fop->sg_offset = off;
		rc = c2_net_cli_call(conn, &call);
		if (rc == 0) {
			rep = c2_fop_data(r);
			rc = rep->sgr_rc;
			eof = rep->sgr_eof;
		}
		if (rc != 0)
			break;

		len = rep->sgr_buf.sb_len;
		struct c2_bufvec stmp = {
			.ov_vec = {
				.v_nr = 1,
				.v_count = &len
			},
			.ov_buf = (void**) &rep->sgr_buf.sb_buf
		};
		struct c2_bufvec_cursor scur;

		c2_bufvec_cursor_init(&scur, &stmp);
		copied = c2_bufvec_cursor_copy(&cur, &scur, len);
		if (copied < len) {
			rc = -EFBIG;
			break;
		}
		off += len;
	}

done:
	if (r != NULL)
		c2_fop_free(r);
	if (f != NULL)
		c2_fop_free(f);

	return rc;
}

static void sunrpc_wf_active_bulk(struct c2_net_transfer_mc *tm,
				  struct c2_net_bulk_mem_work_item *wi)
{
	struct c2_net_buffer *nb = MEM_WI_TO_BUFFER(wi);
	C2_PRE(nb != NULL &&
	       (nb->nb_qtype == C2_NET_QT_ACTIVE_BULK_RECV ||
		nb->nb_qtype == C2_NET_QT_ACTIVE_BULK_SEND) &&
	       nb->nb_tm == tm &&
	       nb->nb_desc.nbd_len != 0 &&
	       nb->nb_desc.nbd_data != NULL);
	C2_PRE(nb->nb_flags & C2_NET_BUF_IN_USE);

	static enum c2_net_queue_type inverse_qt[C2_NET_QT_NR] = {
		[C2_NET_QT_MSG_RECV]          = C2_NET_QT_NR,
		[C2_NET_QT_MSG_SEND]          = C2_NET_QT_NR,
		[C2_NET_QT_PASSIVE_BULK_RECV] = C2_NET_QT_ACTIVE_BULK_SEND,
		[C2_NET_QT_PASSIVE_BULK_SEND] = C2_NET_QT_ACTIVE_BULK_RECV,
		[C2_NET_QT_ACTIVE_BULK_RECV]  = C2_NET_QT_NR,
		[C2_NET_QT_ACTIVE_BULK_SEND]  = C2_NET_QT_NR,
	};
	int rc;
	struct c2_net_end_point *match_ep = NULL;
	do {
		/* decode the descriptor */
		struct sunrpc_buf_desc sd;
		struct sockaddr_in si;

		rc = sunrpc_desc_decode(&nb->nb_desc, &sd);
		if (rc != 0)
			break;
		if (!sunrpc_ep_equals_addr(tm->ntm_ep, &sd.sbd_active_ep)) {
			rc = -EACCES;   /* wrong destination */
			break;
		}
		if (nb->nb_qtype != inverse_qt[sd.sbd_qtype]) {
			rc = -EPERM;    /* wrong operation */
			break;
		}

		/* Make a local end point matching the remote address */
		si.sin_addr.s_addr = sd.sbd_passive_ep.sep_addr;
		si.sin_port = sd.sbd_passive_ep.sep_port;
		c2_mutex_lock(&tm->ntm_dom->nd_mutex);
		rc = sunrpc_ep_create(&match_ep, tm->ntm_dom, &si);
		c2_mutex_unlock(&tm->ntm_dom->nd_mutex);
		if (rc != 0) {
			match_ep = NULL;
			break;
		}

		if (nb->nb_qtype == C2_NET_QT_ACTIVE_BULK_SEND)
			rc = sunrpc_active_send(nb, &sd, match_ep);
		else
			rc = sunrpc_active_recv(nb, &sd, match_ep);
	} while(0);

	/* free the local match end point */
	if (match_ep != NULL)
		c2_net_end_point_put(match_ep);

	/* post the send completion callback (will clear C2_NET_BUF_IN_USE) */
	C2_POST(rc <= 0);
	struct c2_net_event ev = {
		.nev_qtype   = nb->nb_qtype,
		.nev_tm      = tm,
		.nev_buffer  = nb,
		.nev_status  = rc,
		.nev_payload = wi
	};
	c2_time_now(&ev.nev_time);
	(void)c2_net_tm_event_post(tm, &ev);
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
