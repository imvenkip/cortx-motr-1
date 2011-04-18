/* -*- C -*- */

/* This file is included into mem_xprt_xo.c */

/**
   @addtogroup bulkmem
   @{
 */

/**
 */
static void mem_wf_msg_recv_cb(struct c2_net_transfer_mc *tm,
			       struct c2_net_bulk_mem_work_item *wi)
{
	C2_PRE(!c2_mutex_is_locked(&tm->ntm_mutex));

	struct c2_net_buffer *nb = MEM_WI_TO_BUFFER(wi);
	C2_PRE(nb != NULL && 
	       nb->nb_qtype == C2_NET_QT_MSG_RECV &&
	       nb->nb_tm == tm &&
	       (nb->nb_status < 0 ||  /* failed or we have a non-zero msg*/
		(nb->nb_ep != NULL && nb->nb_length >0)));
	C2_PRE(nb->nb_flags & C2_NET_BUF_IN_USE);

	/* post the recv completion callback (will clear C2_NET_BUF_IN_USE) */
	struct c2_net_event ev = {
		.nev_qtype   = nb->nb_qtype,
		.nev_tm      = tm,
		.nev_buffer  = nb,
		.nev_status  = nb->nb_status,
		.nev_payload = wi
	};
	(void)c2_net_tm_event_post(tm, &ev);
	return;
}

/**
   This item involves copying the buffer to the appropriate receive buffer (if
   available) of the target transfer machine in the destination domain, adding
   a C2_NET_XOP_MSG_RECV_CB item on that queue, and then invoking the
   completion callback on the send buffer.  The senders end point reference
   will be incremented for the additional reference from the receiving buffer.

   The c2_net_mutex will be obtained to traverse the list of domains.
   Each domain lock will be held to traverse the transfer machines in the
   domain.
   Once the correct transfer machine is found, its lock is obtained and the
   other locks released.

   If a suitable transfer machine is not found then the message send fails.

 */
static void mem_wf_msg_send(struct c2_net_transfer_mc *tm,
			    struct c2_net_bulk_mem_work_item *wi)
{
	struct c2_net_domain *mydom = tm->ntm_dom;
	C2_PRE(!c2_mutex_is_locked(&tm->ntm_mutex));
	C2_PRE(!c2_mutex_is_locked(&mydom->nd_mutex));
	C2_PRE(!c2_mutex_is_locked(&c2_net_mutex));

	struct c2_net_buffer *nb = MEM_WI_TO_BUFFER(wi);
	C2_PRE(nb != NULL && 
	       nb->nb_qtype == C2_NET_QT_MSG_SEND &&
	       nb->nb_tm == tm &&
	       nb->nb_ep != NULL);
	C2_PRE(nb->nb_flags & C2_NET_BUF_IN_USE);

	/* iterate over in-mem domains to find the destination TM */
	struct c2_net_transfer_mc *dest_tm = NULL;
	c2_mutex_lock(&c2_net_mutex);
	struct c2_net_bulk_mem_domain_pvt *dp;
	c2_list_for_each_entry(&mem_domains, dp,
			       struct c2_net_bulk_mem_domain_pvt,
			       xd_dom_linkage) {
		if (dp->xd_dom == mydom)
			continue; /* skip self */
		/* iterate over TM's in domain */
		c2_mutex_lock(&dp->xd_dom->nd_mutex);
		struct c2_net_transfer_mc *itm;
		c2_list_for_each_entry(&dp->xd_dom->nd_tms, itm,
				       struct c2_net_transfer_mc,
				       ntm_dom_linkage) {
			if (mem_ep_is_equal(itm->ntm_ep, nb->nb_ep)) {
				/* found matching TM; lock its mutex */
				dest_tm = itm;
				c2_mutex_lock(&dest_tm->ntm_mutex);
				C2_ASSERT(mem_tm_invariant(dest_tm));
				break;
			}
		}
		c2_mutex_unlock(&dp->xd_dom->nd_mutex);
		if (dest_tm != NULL)
			break;
	}
	c2_mutex_unlock(&c2_net_mutex);
	C2_ASSERT(dest_tm == NULL || c2_mutex_is_locked(&dest_tm->ntm_mutex));

	int rc;
	do {
		if (dest_tm == NULL ||
		    dest_tm->ntm_state != C2_NET_TM_STARTED) {
			rc = -ENETUNREACH;
			break;
		}
		/* we're now operating on the destination TM */
		struct c2_list_link *link;
		link = c2_list_first(&dest_tm->ntm_q[C2_NET_QT_MSG_RECV]);
		if (link == NULL) {
			rc = -ENOBUFS;
			break;
		}
		struct c2_net_buffer *dest_nb = 
			container_of(link, struct c2_net_buffer, nb_tm_linkage);
		if( nb->nb_length > mem_buffer_length(dest_nb)) {
			rc = -ENOBUFS;
			break;
		}
		C2_ASSERT(mem_buffer_invariant(dest_nb));
		c2_list_del(&dest_nb->nb_tm_linkage);
		rc = mem_copy_buffer(dest_nb, nb, nb->nb_length);
		/* receive buffer needs sender EP in destination DOM */
		if(!rc) {
			struct c2_net_bulk_mem_end_point *mep;
			mep = container_of(nb->nb_ep, 
					   struct c2_net_bulk_mem_end_point, 
					   xep_ep);
			rc = mem_ep_create(&dest_nb->nb_ep, dest_tm->ntm_dom,
					   &mep->xep_sa);
		}
		dest_nb->nb_status = rc; /* recv error code */
	
		/* schedule the receive msg callback */
		struct c2_net_bulk_mem_work_item *dest_wi = 
			MEM_BUFFER_TO_WI(dest_nb);
		dest_wi->xwi_op = C2_NET_XOP_MSG_RECV_CB;

		struct c2_net_bulk_mem_tm_pvt *dest_tp = 
			dest_tm->ntm_xprt_private;
		mem_wi_add(dest_wi, dest_tp);

		/* send succeeded, even if receive may have failed */
		rc = 0;

		/* release the destination TM mutex */
		c2_mutex_unlock(&dest_tm->ntm_mutex);
	} while(0);

	/* Back to sender's TM */

	/* post the send completion callback (will clear C2_NET_BUF_IN_USE) */
	struct c2_net_event ev = {
		.nev_qtype   = nb->nb_qtype,
		.nev_tm      = tm,
		.nev_buffer  = nb,
		.nev_status  = rc,
		.nev_payload = wi
	};
	(void)c2_net_tm_event_post(tm, &ev);
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
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
