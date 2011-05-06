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
	C2_POST(nb->nb_status <= 0);
	struct c2_net_event ev = {
		.nev_qtype   = nb->nb_qtype,
		.nev_tm      = tm,
		.nev_buffer  = nb,
		.nev_status  = nb->nb_status,
		.nev_payload = wi
	};
	c2_time_now(&ev.nev_time);
	(void)c2_net_tm_event_post(tm, &ev);
	return;
}

/**
   Find a remote TM for a msg send or active buffer operation.

   The c2_net_mutex will be obtained to traverse the list of domains.
   Each domain lock will be held to traverse the transfer machines in the
   domain.
   Once the correct transfer machine is found, its lock is obtained and the
   other locks released.

   @param tm Local TM
   @param match_ep End point of remote TM
   @param p_dest_tm Returns remote TM pointer, with TM mutex held.
   @param p_dest_ep Returns end point in remote TM's domain, with local
   TM's address. Optional - only msg send requires this.
   The option exists because the end point object has
   to be created while holding the remote DOM's mutex.
   @retval 0 On success
   @retval -errno On error
 */
static int mem_find_remote_tm(struct c2_net_transfer_mc  *tm,
			      struct c2_net_end_point    *match_ep,
			      struct c2_net_transfer_mc **p_dest_tm,
			      struct c2_net_end_point   **p_dest_ep)
{
	struct c2_net_domain *mydom = tm->ntm_dom;
	C2_PRE(!c2_mutex_is_locked(&tm->ntm_mutex));
	C2_PRE(!c2_mutex_is_locked(&mydom->nd_mutex));
	C2_PRE(!c2_mutex_is_locked(&c2_net_mutex));

	/* iterate over in-mem domains to find the destination TM */
	struct c2_net_transfer_mc *dest_tm = NULL;
	struct c2_net_end_point   *dest_ep = NULL;

	int rc = 0;
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
			c2_mutex_lock(&itm->ntm_mutex);
			do {
				if (itm->ntm_state != C2_NET_TM_STARTED)
					break; /* ignore */
				if (!mem_eps_are_equal(itm->ntm_ep, match_ep))
					break;

				/* Found the matching TM. */
				dest_tm = itm;
				if (p_dest_ep == NULL)
					break;
				/* We need to create an EP for the local TM
				   address in the remote DOM. Do this now,
				   before giving up the DOM mutex.
				*/
				struct c2_net_bulk_mem_end_point *mep;
				mep = container_of(tm->ntm_ep,
						   struct
						   c2_net_bulk_mem_end_point,
						   xep_ep);
				rc = MEM_EP_CREATE(&dest_ep, dest_tm->ntm_dom,
						   &mep->xep_sa,
						   mep->xep_service_id);
			} while(0);
			if (dest_tm != NULL) {
				/* found the TM */
				if (rc != 0) {
					/* ... but failed on EP */
					c2_mutex_unlock(&dest_tm->ntm_mutex);
					dest_tm = NULL;
				}
				break;
			}
			c2_mutex_unlock(&itm->ntm_mutex);
		}
		c2_mutex_unlock(&dp->xd_dom->nd_mutex);
		if (dest_tm != NULL || rc != 0)
			break;
	}
	if (rc == 0 && dest_tm == NULL)
		rc = -ENETUNREACH; /* search exhausted */
	c2_mutex_unlock(&c2_net_mutex);
	C2_ASSERT(rc != 0 ||
		  (dest_tm != NULL &&
		   c2_mutex_is_locked(&dest_tm->ntm_mutex) &&
		   dest_tm->ntm_state == C2_NET_TM_STARTED));
	if (!rc) {
		C2_ASSERT(mem_tm_invariant(dest_tm));
		*p_dest_tm = dest_tm;
		if (p_dest_ep != NULL) {
			C2_ASSERT(dest_ep != NULL);
			*p_dest_ep = dest_ep;
		}
	}
	return rc;
}


/**
   This item involves copying the buffer to the appropriate receive buffer (if
   available) of the target transfer machine in the destination domain, adding
   a C2_NET_XOP_MSG_RECV_CB item on that queue, and then invoking the
   completion callback on the send buffer.
   An end point object describing the sender's address will be referenced
   from the receiving buffer.


   If a suitable transfer machine is not found then the message send fails.

 */
static void mem_wf_msg_send(struct c2_net_transfer_mc *tm,
			    struct c2_net_bulk_mem_work_item *wi)
{
	struct c2_net_buffer *nb = MEM_WI_TO_BUFFER(wi);
	C2_PRE(nb != NULL &&
	       nb->nb_qtype == C2_NET_QT_MSG_SEND &&
	       nb->nb_tm == tm &&
	       nb->nb_ep != NULL);
	C2_PRE(nb->nb_flags & C2_NET_BUF_IN_USE);

	int rc;
	struct c2_net_transfer_mc *dest_tm = NULL;
	struct c2_net_end_point   *dest_ep = NULL;
	do {
		/* Search for a remote TM matching the destination address,
		   and if found, create an EP in the remote TM's domain with
		   the local TM's address.
		*/
		rc = mem_find_remote_tm(tm, nb->nb_ep, &dest_tm, &dest_ep);
		if (rc != 0)
			break;

		/* We're now operating in the destination TM while holding
		   its mutex.  The destination TM is operative.
		*/

		/* get the first receive buffer */
		struct c2_list_link *link;
		link = c2_list_first(&dest_tm->ntm_q[C2_NET_QT_MSG_RECV]);
		if (link == NULL) {
			dest_tm->ntm_qstats[C2_NET_QT_MSG_RECV].nqs_num_f_events
				++;
			rc = -ENOBUFS;
			break;
		}
		struct c2_net_buffer *dest_nb =
			container_of(link, struct c2_net_buffer, nb_tm_linkage);
		C2_ASSERT(mem_buffer_invariant(dest_nb));
		c2_list_del(&dest_nb->nb_tm_linkage);
		if( nb->nb_length > mem_buffer_length(dest_nb)) {
			rc = -EMSGSIZE;
			dest_nb->nb_length = nb->nb_length; /* desired length */
		} else
			rc = mem_copy_buffer(dest_nb, nb, nb->nb_length);
		if(rc == 0) {
			/* commit to using the destination EP */
			dest_nb->nb_ep = dest_ep;
			dest_ep = NULL; /* do not release below */
		}
		dest_nb->nb_status = rc; /* recv error code */

		/* schedule the receive msg callback */
		struct c2_net_bulk_mem_work_item *dest_wi =
			MEM_BUFFER_TO_WI(dest_nb);
		dest_wi->xwi_op = C2_NET_XOP_MSG_RECV_CB;

		struct c2_net_bulk_mem_tm_pvt *dest_tp =
			dest_tm->ntm_xprt_private;
		mem_wi_add(dest_wi, dest_tp);
	} while(0);

	/* release the destination TM mutex */
	if (dest_tm != NULL)
		c2_mutex_unlock(&dest_tm->ntm_mutex);

	/* release the destination EP, if still referenced,
	   outside of any mutex
	*/
	if (dest_ep != NULL)
		c2_net_end_point_put(dest_ep);

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
