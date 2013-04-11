/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 11-Mar-2013
 */

/**
   @page MGMT-SVC-DLD-FOP-SSR Management Service Status Response FOM
   This FOP, defined by m0_fop_mgmt_service_state_res, returns the
   status of a list of services.  There is no FOM defined for this
   FOP.

   This FOP is used as the response FOP for a number of different request FOPs.

   The mgmt_fop_ssr_fill() subroutine is provided for use in the management
   service. It will fill in the FOP with the status of the indicated services
   or of all configured services.

   @note Only services with non-null UUIDs are returned.
 */


/* This file is designed to be included by mgmt/mgmt.c */

struct m0_fop_type m0_fop_mgmt_service_state_res_fopt;

#ifdef M0_MGMT_SERVICE_PRESENT
/**
   @addtogroup mgmt_svc_pvt
   @{
 */

/*
 ******************************************************************************
 * Utility subs
 ******************************************************************************
 */

static void mgmt_fop_ssr_fill(struct m0_fom *fom,
			      struct m0_mgmt_service_uuid_seq *services)
{
	struct m0_fop_mgmt_service_state_res *ssrfop;
	struct m0_reqh                       *reqh = m0_fom_reqh(fom);
	struct m0_reqh_service               *svc;
	int                                   nr_rh_services;
	int                                   i;

	M0_PRE(fom != NULL);
	M0_PRE(services != NULL);
	M0_PRE(fom->fo_rep_fop != NULL);
	ssrfop = m0_fop_data(fom->fo_rep_fop);
	M0_PRE(ssrfop != NULL);
	M0_PRE(ssrfop->msr_rc == 0);
	M0_PRE(ssrfop->msr_ss.msss_nr == 0);
	M0_PRE(ssrfop->msr_ss.msss_state == NULL);

	if (services->msus_nr != 0) {
		/** @todo We don't support specific services yet. */
		ssrfop->msr_rc = -ENOSYS;
		return;
	}

	ssrfop->msr_reqh_state = reqh->rh_sm.sm_state;

	nr_rh_services = m0_reqh_svc_tlist_length(&reqh->rh_services);
	M0_ASSERT(nr_rh_services > 0);
	M0_ALLOC_ARR_ADDB(ssrfop->msr_ss.msss_state, nr_rh_services,
			  &reqh->rh_addb_mc, M0_MGMT_ADDB_LOC_FOP_SSR_FILL,
			  &m0_mgmt_addb_ctx, &fom->fo_addb_ctx,
			  fom->fo_op_addb_ctx);
	if (ssrfop->msr_ss.msss_state == NULL) {
		ssrfop->msr_rc = -ENOMEM;
		return;
	}
	i = 0;
	m0_tl_for(m0_reqh_svc, &reqh->rh_services, svc) {
		M0_ASSERT(i < nr_rh_services);
		if (svc->rs_service_uuid.u_hi == 0 &&
		    svc->rs_service_uuid.u_lo == 0)
			continue;
		ssrfop->msr_ss.msss_state[i].mss_uuid = svc->rs_service_uuid;
		ssrfop->msr_ss.msss_state[i].mss_state =
			m0_reqh_service_state_get(svc);
		++i;
	} m0_tl_endfor;
	ssrfop->msr_ss.msss_nr = i;
	if (i == 0) {
		m0_free(ssrfop->msr_ss.msss_state);
		ssrfop->msr_ss.msss_state = NULL;
	}
}

#endif /* M0_MGMT_SERVICE_PRESENT */
/** @} end group mgmt_svc_pvt */

/**
   @addtogroup mgmt_pvt
   @{
 */

/*
 ******************************************************************************
 * FOP initialization logic
 ******************************************************************************
 */

static int mgmt_fop_ssr_init(void)
{
	return
	   M0_FOP_TYPE_INIT(&m0_fop_mgmt_service_state_res_fopt,
			    .name      = "Mgmt Service State Response",
			    .opcode    = M0_MGMT_SERVICE_STATE_REPLY_OPCODE,
			    .xt        = m0_fop_mgmt_service_state_res_xc,
			    .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
}

static void mgmt_fop_ssr_fini(void)
{
	m0_fop_type_fini(&m0_fop_mgmt_service_state_res_fopt);
}

/** @} end group mgmt_pvt */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
