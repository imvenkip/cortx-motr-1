/* -*- C -*- */

/* This file is included into sunrpc_xprt_xo.c */

/**
   @addtogroup bulksunrpc
   @{
 */


/**
   Work function for the C2_NET_XOP_STATE_CHANGE work item.
   @param tm the corresponding transfer machine
   @param wi the work item, this will be freed
 */
static void sunrpc_wf_state_change(struct c2_net_transfer_mc *tm,
				   struct c2_net_bulk_mem_work_item *wi)
{
	struct c2_net_bulk_sunrpc_domain_pvt *dp = tm->ntm_dom->nd_xprt_private;
	/*struct c2_net_bulk_sunrpc_tm_pvt *tp = tm->ntm_xprt_private;*/

	C2_PRE(c2_mutex_is_locked(&tm->ntm_mutex));
	C2_ASSERT(wi->xwi_next_state == C2_NET_XTM_STARTED ||
		  wi->xwi_next_state == C2_NET_XTM_STOPPED);

	if (wi->xwi_next_state == C2_NET_XTM_STARTED) {
		/* c2_service_id_init(TM EP) */
		/* c2_service initialization */
		/* c2_service_start(&tp->xtm_service, sid) */
	}
	else {
		/* c2_service_stop() */
		/* c2_service_id_fini() */
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
