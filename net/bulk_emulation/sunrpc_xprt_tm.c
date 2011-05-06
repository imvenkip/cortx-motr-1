/* -*- C -*- */

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
			struct c2_net_bulk_mem_end_point *mep;
			struct c2_net_bulk_sunrpc_end_point *sep;
			mep = container_of(ep, struct c2_net_bulk_mem_end_point,
					   xep_ep);
			sep = container_of(mep,
					   struct c2_net_bulk_sunrpc_end_point,
					   xep_base);
			if (!c2_services_are_same(&sunrpc_server_id,
						  &sep->xep_sid))
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
	struct c2_net_bulk_sunrpc_domain_pvt *dp = tm->ntm_dom->nd_xprt_private;
	struct c2_net_bulk_sunrpc_tm_pvt *tp = tm->ntm_xprt_private;
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
				wi->xwi_state_change_status = rc; /* fail TM */
		}
	} else {
		sunrpc_stop_service();
	}
	/* invoke the base work function */
	(*dp->xd_base_work_fn[C2_NET_XOP_STATE_CHANGE])(tm, wi);
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
