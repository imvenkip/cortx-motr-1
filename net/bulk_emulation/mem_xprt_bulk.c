/* -*- C -*- */

/* This file is included into mem_xprt_xo.c */

/**
   @addtogroup bulkmem
   @{
 */

/**
   Invoke the completion callback on a passive bulk transfer buffer.
 */
static void mem_wf_passive_bulk_cb(struct c2_net_transfer_mc *tm,
				   struct c2_net_bulk_mem_work_item *wi)
{
	C2_PRE(c2_mutex_is_not_locked(&tm->ntm_mutex));

	struct c2_net_buffer *nb = MEM_WI_TO_BUFFER(wi);
	C2_PRE(nb != NULL &&
	       (nb->nb_qtype == C2_NET_QT_PASSIVE_BULK_RECV ||
		nb->nb_qtype == C2_NET_QT_PASSIVE_BULK_SEND) &&
	       nb->nb_tm == tm);
	C2_PRE(nb->nb_flags & C2_NET_BUF_IN_USE);

	/* post the completion callback (will clear C2_NET_BUF_IN_USE) */
	C2_PRE(nb->nb_status <= 0);
	struct c2_net_event ev = {
		.nev_type    = C2_NET_EV_BUFFER_RELEASE,
		.nev_tm      = tm,
		.nev_buffer  = nb,
		.nev_status  = nb->nb_status,
		.nev_payload = wi
	};
	c2_time_now(&ev.nev_time);
	c2_net_tm_event_post(&ev);
}

/**
   Perform a bulk data transfer, and invoke the completion
   callback on the active buffer.
   @param tm The active TM
   @param wi The work item.
 */
static void mem_wf_active_bulk(struct c2_net_transfer_mc *tm,
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
	struct c2_net_transfer_mc *passive_tm = NULL;
	struct c2_net_end_point     *match_ep = NULL;
	do {
		/* decode the descriptor */
		struct mem_desc *md;
		rc = mem_desc_decode(&nb->nb_desc, &md);
		if (rc != 0)
			break;

		if (!mem_ep_equals_addr(tm->ntm_ep, &md->md_active)) {
			rc = -EACCES;   /* wrong destination */
			break;
		}
		if (nb->nb_qtype != inverse_qt[md->md_qt]) {
			rc = -EPERM;    /* wrong operation */
			break;
		}

		/* Make a local end point matching the passive address.*/
		c2_mutex_lock(&tm->ntm_dom->nd_mutex);
		rc = MEM_EP_CREATE(&match_ep, tm->ntm_dom, &md->md_passive, 0);
		c2_mutex_unlock(&tm->ntm_dom->nd_mutex);
		if (rc != 0) {
			match_ep = NULL;
			break;
		}

		/* Search for a remote TM matching this EP address. */
		rc = mem_find_remote_tm(tm, match_ep, &passive_tm, NULL);
		if (rc != 0)
			break;

		/* We're now operating on the destination TM while holding
		   its mutex.  The destination TM is operative.
		 */

		/* locate the passive buffer */
		struct c2_net_buffer *passive_nb = NULL;
		struct c2_net_buffer *inb;
		c2_list_for_each_entry(&passive_tm->ntm_q[md->md_qt], inb,
				       struct c2_net_buffer,
				       nb_tm_linkage) {
			if(!mem_desc_equal(&inb->nb_desc, &nb->nb_desc))
				continue;
			passive_nb = inb;
			break;
		}
		if (passive_nb == NULL) {
			rc = -ENOENT;
			break;
		}

		struct c2_net_buffer *s_buf;
		struct c2_net_buffer *d_buf;
		c2_bcount_t datalen;
		if (nb->nb_qtype == C2_NET_QT_ACTIVE_BULK_SEND) {
			s_buf = nb;
			d_buf = passive_nb;
			datalen = nb->nb_length;
		} else {
			s_buf = passive_nb;
			d_buf = nb;
			datalen = md->md_len;
		}
		/*
		   Copy the buffer.
		   The length check was delayed until here so both buffers
		   can get released with appropriate error code.
		*/
		rc = mem_copy_buffer(d_buf, s_buf, datalen);

		/* schedule the passive callback */
		passive_nb->nb_status = rc;
		struct c2_net_bulk_mem_work_item *passive_wi =
			MEM_BUFFER_TO_WI(passive_nb);
		passive_wi->xwi_op = C2_NET_XOP_PASSIVE_BULK_CB;

		struct c2_net_bulk_mem_tm_pvt *passive_tp =
			passive_tm->ntm_xprt_private;
		mem_wi_add(passive_wi, passive_tp);

		/* active side gets same status */
	} while (0);

	/* release the destination TM mutex */
	if (passive_tm != NULL)
		c2_mutex_unlock(&passive_tm->ntm_mutex);

	/* free the local match end point */
	if (match_ep != NULL)
		c2_net_end_point_put(match_ep);

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

/**
   @} bulkmem
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
