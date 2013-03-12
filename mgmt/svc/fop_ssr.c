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
   This FOP, defined by m0_fop_mgmt_service_status_res, returns the
   status of a list of services.  There is no FOM defined for this
   FOP.

   This FOP is used as the response FOP for a number of different request FOPs.
   As such it is associated with the following utilities:
   - mgmt_fop_ssr_fill()
 */


/* This file is designed to be included by mgmt/svc/mgmt_svc.c */

/**
   @ingroup mgmt_svc_pvt
   @{
 */

/*
 ******************************************************************************
 * FOP initialization logic
 ******************************************************************************
 */
static int mgmt_fop_ssr_init()
{
	return
	   M0_FOP_TYPE_INIT(&m0_fop_mgmt_service_status_res_fopt,
			    .name      = "Mgmt Service Status Response",
			    .opcode    = M0_MGMT_SERVICE_STATUS_REPLY_OPCODE,
			    .xt        = m0_fop_mgmt_service_status_res_xc,
			    .rpc_flags = M0_RPC_ITEM_TYPE_REPLY);
}

static void mgmt_fop_ssr_fini()
{
	m0_fop_type_fini(&m0_fop_mgmt_service_state_res_fopt);
}

/** @} end group mgmt_svc_pvt */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
