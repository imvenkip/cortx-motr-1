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

/* This file is included into sunrpc_xprt_xo.c */

/**
   @addtogroup bulksunrpc
   @{
 */

/* only call fops listed here, not reply fops */
static struct c2_fop_type *s_fops[] = {
	&sunrpc_msg_fopt,
	&sunrpc_get_fopt,
	&sunrpc_put_fopt,
};

/**
   The c2_service handler for sunrpc-based bulk emulation.
 */
static int sunrpc_bulk_handler(struct c2_service *service, struct c2_fop *fop,
			       void *cookie)
{
	struct c2_fop_ctx ctx = {
		.ft_service = service,
		.fc_cookie = cookie
	};
	int rc;

	rc = fop->f_type->ft_ops->fto_execute(fop, &ctx);
	/* C2_ADDB_ADD */
	return rc;
}

/**
   Starts the common service if necessary.
   @param ep End point of TM in context.
   @retva 0 on success
   @retval -EADDRNOTAVAIL if the address of this TM does not match the
   address of the service.
   @retval -errno Other errors.
 */
static int sunrpc_start_service(struct c2_net_end_point *ep)
{
	int rc = 0;
	bool dom_init = false;
	bool sid_init = false;
	c2_mutex_lock(&sunrpc_server_mutex);
	do {
		struct c2_net_domain *dom = &sunrpc_server_domain;
		struct c2_service    *svc = &sunrpc_server_service;
		struct c2_net_bulk_sunrpc_end_point *sep;
		sep = sunrpc_ep_to_pvt(ep);
		if (++sunrpc_server_active_tms > 1) {
			/* previously started - match address */
			if (!mem_sa_eq(&sunrpc_server_in,
				       &sep->xep_base.xep_sa))
				rc = -EADDRNOTAVAIL;
			break;
		}
		/* initialize the domain */
#ifdef __KERNEL__
		rc = c2_net_domain_init(dom, &c2_net_ksunrpc_minimal_xprt);
#else
		/* disable SIGPIPE because sunrpc does not set MSG_NOSIGNAL
		   when writing on socket, causing process to exit. */
		{
			struct sigaction new_action;
			new_action.sa_handler = SIG_IGN;
			sigemptyset(&new_action.sa_mask);
			new_action.sa_flags = 0;
			sigaction(SIGPIPE, &new_action, NULL);
		}
		rc = c2_net_domain_init(dom, &c2_net_usunrpc_minimal_xprt);
#endif
		if (rc != 0)
			break;
		dom_init = true;

		/* remember the address */
		sunrpc_server_in = sep->xep_base.xep_sa;

		/* initialize the sid using supplied EP */
		rc = sunrpc_ep_init_sid(&sunrpc_server_id, dom, ep);
		if (rc != 0)
			break;
		sid_init = true;

		/* initialize the service */
		C2_SET0(svc);
		svc->s_table.not_start = s_fops[0]->ft_rpc_item_type.rit_opcode;
		svc->s_table.not_nr    = ARRAY_SIZE(s_fops);
		svc->s_table.not_fopt  = s_fops;
		svc->s_handler         = &sunrpc_bulk_handler;
		rc = c2_service_start(svc, &sunrpc_server_id);
	} while(0);
	if (rc != 0) {
		sunrpc_server_active_tms--;
		if (sid_init)
			c2_service_id_fini(&sunrpc_server_id);
		if (dom_init)
			c2_net_domain_fini(&sunrpc_server_domain);
	}
	c2_mutex_unlock(&sunrpc_server_mutex);
	return rc;
}

/**
   Stops the common service if necessary.
 */
static void sunrpc_stop_service(void)
{
	c2_mutex_lock(&sunrpc_server_mutex);
	do {
		if (--sunrpc_server_active_tms > 0)
			break; /* started */
		c2_service_stop(&sunrpc_server_service);
		sunrpc_server_service.s_id = NULL;
		c2_service_id_fini(&sunrpc_server_id);
		c2_net_domain_fini(&sunrpc_server_domain);
	} while(0);
	c2_mutex_unlock(&sunrpc_server_mutex);
	return;
}

/**
   Work function for the C2_NET_XOP_STATE_CHANGE work item.
   @param tm the corresponding transfer machine
   @param wi the work item, this will be freed
 */
static void sunrpc_wf_state_change(struct c2_net_transfer_mc *tm,
				   struct c2_net_bulk_mem_work_item *wi)
{
	struct c2_net_bulk_sunrpc_domain_pvt *dp =
		sunrpc_dom_to_pvt(tm->ntm_dom);
	struct c2_net_bulk_sunrpc_tm_pvt *tp = sunrpc_tm_to_pvt(tm);
	int rc = 0;

	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));
	C2_ASSERT(wi->xwi_next_state == C2_NET_XTM_STARTED ||
		  wi->xwi_next_state == C2_NET_XTM_STOPPED);

	if (wi->xwi_next_state == C2_NET_XTM_STARTED) {
		/*
		  Application can call c2_net_tm_stop
		  before the C2_NET_XTM_STARTED work item gets processed.
		  If that happens, ignore the C2_NET_XTM_STARTED item.
		 */
		if (tp->xtm_base.xtm_state < C2_NET_XTM_STOPPING) {
			C2_ASSERT(sunrpc_ep_invariant(wi->xwi_nbe_ep));
			rc = sunrpc_start_service(wi->xwi_nbe_ep);
			if (rc != 0)
				wi->xwi_status = rc; /* fail TM */
		}
	} else {
		sunrpc_stop_service();
	}
	/* invoke the base work function */
	(*dp->xd_base_ops->bmo_work_fn[C2_NET_XOP_STATE_CHANGE])(tm, wi);
}

/**
   Inherit the cancel callback method.
 */
static void sunrpc_wf_cancel_cb(struct c2_net_transfer_mc *tm,
				struct c2_net_bulk_mem_work_item *wi)
{
	struct c2_net_bulk_sunrpc_domain_pvt *dp;
	C2_PRE(sunrpc_dom_invariant(tm->ntm_dom));
	dp = sunrpc_dom_to_pvt(tm->ntm_dom);
	(*dp->xd_base_ops->bmo_work_fn[C2_NET_XOP_CANCEL_CB])(tm, wi);
}

/**
   Inherit the error callback method.
 */
static void sunrpc_wf_error_cb(struct c2_net_transfer_mc *tm,
				struct c2_net_bulk_mem_work_item *wi)
{
	struct c2_net_bulk_sunrpc_domain_pvt *dp;
	C2_PRE(sunrpc_dom_invariant(tm->ntm_dom));
	dp = sunrpc_dom_to_pvt(tm->ntm_dom);
	(*dp->xd_base_ops->bmo_work_fn[C2_NET_XOP_ERROR_CB])(tm, wi);
}

/**
   Inherit the post error method.
 */
static void sunrpc_post_error(struct c2_net_transfer_mc *tm, int32_t status)
{
	struct c2_net_bulk_sunrpc_domain_pvt *dp;
	C2_PRE(sunrpc_dom_invariant(tm->ntm_dom));
	dp = sunrpc_dom_to_pvt(tm->ntm_dom);
	(*dp->xd_base_ops->bmo_post_error)(tm, status);
}

/**
   Skulker method to time out TM buffers
   @param dom Domain pointer (mutex is held)
   @param now Current time
 */
static void sunrpc_skulker_timeout_buffers(struct c2_net_domain *dom,
					   c2_time_t now)
{
	struct c2_net_transfer_mc *tm;
	enum c2_net_queue_type     qt;
	struct c2_net_buffer      *nb;

	C2_PRE(c2_mutex_is_locked(&dom->nd_mutex));

	/* iterate over TM's in domain */
	c2_list_for_each_entry(&dom->nd_tms, tm,
			       struct c2_net_transfer_mc, ntm_dom_linkage) {
		c2_mutex_lock(&tm->ntm_mutex);
		/* iterate over buffers in each queue */
		for (qt = C2_NET_QT_MSG_RECV; qt < C2_NET_QT_NR; ++qt) {
			c2_tl_for(c2_net_tm, &tm->ntm_q[qt], nb) {
				if (nb->nb_timeout == C2_TIME_NEVER)
					continue;
				if (c2_time_after(nb->nb_timeout, now))
					continue;
				/* mark as timed out */
				nb->nb_flags |= C2_NET_BUF_TIMED_OUT;
				/* Attempt to cancel - active calls
				   may not always notice or honor this flag.
				   The cancel wf supplies the proper error
				   code.
				 */
				sunrpc_xo_buf_del(nb);
			} c2_tl_endfor;
		}
		c2_mutex_unlock(&tm->ntm_mutex);
	}
	return;
}

/** @} */ /* bulksunrpc */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
