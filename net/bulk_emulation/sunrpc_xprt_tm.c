/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Carl Braganza, Dave Cohrs
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
		if (++sunrpc_server_active_tms > 1) {
			/* previously started - match address */
			struct c2_net_bulk_sunrpc_end_point *sep;
			sep = sunrpc_ep_to_pvt(ep);
			if (strcmp(sunrpc_server_id.si_uuid,
				   sep->xep_sid.si_uuid) != 0)
				rc = -EADDRNOTAVAIL;
			break;
		}
		/* initialize the domain */
#ifdef __KERNEL__
		rc = c2_net_domain_init(dom, &c2_net_ksunrpc_xprt);
#else
		rc = c2_net_domain_init(dom, &c2_net_usunrpc_minimal_xprt);
#endif
		if (rc != 0)
			break;
		dom_init = true;

		/* initialize the sid using supplied EP */
		rc = sunrpc_ep_init_sid(&sunrpc_server_id, dom, ep);
		if (rc != 0)
			break;
		sid_init = true;

		/* initialize the service */
		C2_SET0(svc);
		svc->s_table.not_start = s_fops[0]->ft_code;
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
static void sunrpc_stop_service()
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
	C2_ASSERT(sunrpc_ep_invariant(tm->ntm_ep));

	if (wi->xwi_next_state == C2_NET_XTM_STARTED) {
		/*
		  Application can call c2_net_tm_stop
		  before the C2_NET_XTM_STARTED work item gets processed.
		  If that happens, ignore the C2_NET_XTM_STARTED item.
		 */
		if (tp->xtm_base.xtm_state < C2_NET_XTM_STOPPING) {
			rc = sunrpc_start_service(tm->ntm_ep);
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
