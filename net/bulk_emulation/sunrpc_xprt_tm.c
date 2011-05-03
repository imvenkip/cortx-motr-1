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
   Work function for the C2_NET_XOP_STATE_CHANGE work item.
   @param tm the corresponding transfer machine
   @param wi the work item, this will be freed
 */
static void sunrpc_wf_state_change(struct c2_net_transfer_mc *tm,
				   struct c2_net_bulk_mem_work_item *wi)
{
	struct c2_net_bulk_sunrpc_domain_pvt *dp = tm->ntm_dom->nd_xprt_private;
	struct c2_net_bulk_sunrpc_tm_pvt *tp = tm->ntm_xprt_private;
	struct c2_net_bulk_mem_end_point *mep;
	struct c2_net_bulk_sunrpc_end_point *sep;
	int rc;

	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));
	C2_ASSERT(wi->xwi_next_state == C2_NET_XTM_STARTED ||
		  wi->xwi_next_state == C2_NET_XTM_STOPPED);
	C2_ASSERT(sunrpc_ep_invariant(tm->ntm_ep));

	mep = container_of(tm->ntm_ep,
			   struct c2_net_bulk_mem_end_point, xep_ep);
	sep = container_of(mep, struct c2_net_bulk_sunrpc_end_point, xep_base);

	if (wi->xwi_next_state == C2_NET_XTM_STARTED) {
		/*
		  Application can call c2_net_tm_stop
		  before the C2_NET_XTM_STARTED work item gets processed.
		  If that happens, ignore the C2_NET_XTM_STARTED item.
		 */
		if (tp->xtm_base.xtm_state < C2_NET_XTM_STOPPING) {
			/* c2_service_id_init is done by sunrpc_ep_create */

			/* c2_service initialization */
			tp->xtm_service.s_table.not_start = s_fops[0]->ft_code;
			tp->xtm_service.s_table.not_nr = ARRAY_SIZE(s_fops);
			tp->xtm_service.s_table.not_fopt  = s_fops;
			tp->xtm_service.s_handler = &sunrpc_bulk_handler;

			rc = c2_service_start(&tp->xtm_service, &sep->xep_sid);
			C2_ASSERT(rc >= 0);
		}
	} else {
		if (tp->xtm_service.s_id != NULL)
			c2_service_stop(&tp->xtm_service);
		C2_SET0(&tp->xtm_service);

		/* c2_service_id_fini() is done when tm_ep is released */
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
