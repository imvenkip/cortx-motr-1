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
	struct c2_net_buffer *nb = mem_wi_to_buffer(wi);

	C2_PRE(c2_mutex_is_not_locked(&tm->ntm_mutex));
	C2_PRE(nb != NULL &&
	       (nb->nb_qtype == C2_NET_QT_PASSIVE_BULK_RECV ||
		nb->nb_qtype == C2_NET_QT_PASSIVE_BULK_SEND) &&
	       nb->nb_tm == tm);
	C2_PRE(nb->nb_flags & C2_NET_BUF_IN_USE);

	/* post the completion callback (will clear C2_NET_BUF_IN_USE) */
	mem_wi_post_buffer_event(wi);
	return;
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
	static const enum c2_net_queue_type inverse_qt[C2_NET_QT_NR] = {
		[C2_NET_QT_MSG_RECV]          = C2_NET_QT_NR,
		[C2_NET_QT_MSG_SEND]          = C2_NET_QT_NR,
		[C2_NET_QT_PASSIVE_BULK_RECV] = C2_NET_QT_ACTIVE_BULK_SEND,
		[C2_NET_QT_PASSIVE_BULK_SEND] = C2_NET_QT_ACTIVE_BULK_RECV,
		[C2_NET_QT_ACTIVE_BULK_RECV]  = C2_NET_QT_NR,
		[C2_NET_QT_ACTIVE_BULK_SEND]  = C2_NET_QT_NR,
	};
	struct c2_net_buffer *nb = mem_wi_to_buffer(wi);
	int rc;
	struct c2_net_transfer_mc *passive_tm = NULL;
	struct c2_net_end_point     *match_ep = NULL;

	C2_PRE(nb != NULL &&
	       (nb->nb_qtype == C2_NET_QT_ACTIVE_BULK_RECV ||
		nb->nb_qtype == C2_NET_QT_ACTIVE_BULK_SEND) &&
	       nb->nb_tm == tm &&
	       nb->nb_desc.nbd_len != 0 &&
	       nb->nb_desc.nbd_data != NULL);
	C2_PRE(nb->nb_flags & C2_NET_BUF_IN_USE);

	do { /* provide a break context */
		struct mem_desc *md;
		struct c2_net_buffer *passive_nb = NULL;
		struct c2_net_buffer *inb;
		struct c2_net_buffer *s_buf;
		struct c2_net_buffer *d_buf;
		c2_bcount_t datalen;
		struct c2_net_bulk_mem_work_item *passive_wi;
		struct c2_net_bulk_mem_tm_pvt *passive_tp;

		/* decode the descriptor */
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
		passive_wi = mem_buffer_to_wi(passive_nb);
		passive_wi->xwi_op = C2_NET_XOP_PASSIVE_BULK_CB;
		passive_wi->xwi_status = rc;
		passive_wi->xwi_nbe_length = datalen;

		passive_tp = passive_tm->ntm_xprt_private;
		mem_wi_add(passive_wi, passive_tp);

		/* active side gets same status */
		wi->xwi_status = rc;
		wi->xwi_nbe_length = datalen;
	} while (0);

	/* release the destination TM mutex */
	if (passive_tm != NULL)
		c2_mutex_unlock(&passive_tm->ntm_mutex);

	/* free the local match end point */
	if (match_ep != NULL)
		c2_net_end_point_put(match_ep);

	/* post the send completion callback (will clear C2_NET_BUF_IN_USE) */
	wi->xwi_status = rc;
	mem_wi_post_buffer_event(wi);
	return;
}

/**
   @} bulkmem
*/

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
