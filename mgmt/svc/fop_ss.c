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
   @page MGMT-SVC-DLD-FOP-SS Management Service Status FOM
   This FOP, defined by m0_fop_mgmt_service_status_req, conveys a request to
   return the status of a specified list of services, or of all services.
   The response is through a @ref MGMT-SVC-DLD-FOP-SS "Service Status FOP".

   The FOP defines a FOM with the following phases:
   @dot
   digraph ss_fop_phases {
        r_lock -> r_lock [label="Read lock wait (blocking)"]
	r_lock -> validate [label="Got read lock"]
	validate -> r_unlock [label="Validation failed"]
	validate -> get_status
	get_status -> r_unlock [label="Release read lock"]
	r_unlock -> fini
   }
   @enddot
   Note that the generic FOM phases do not apply here.

 */


/* This file is designed to be included by mgmt/svc/mgmt_svc.c */
#include "mgmt/svc/mgmt_svc.h"

/**
   @ingroup mgmt_svc_pvt
   @{
 */

/*
 ******************************************************************************
 * FOM Phase state machine
 ******************************************************************************
 */
enum mgmt_pfom_phase {
	MGMT_FOM_SS_PHASE_R_LOCK     = M0_FOM_PHASE_INIT,
	MGMT_FOM_SS_PHASE_FINI       = M0_FOM_PHASE_FINISH,
	MGMT_FOM_SS_PHASE_VALIDATE   = M0_FOM_PHASE_NR,
	MGMT_FOM_SS_PHASE_GET_STATUS,
	MGMT_FOM_SS_PHASE_R_UNLOCK,
};

static const struct m0_sm_state_descr mgmt_pfom_state_descr[] = {
        [MGMT_FOM_SS_PHASE_R_LOCK] = {
                .sd_flags       = M0_SDF_INITIAL,
                .sd_name        = "ReadLock",
                .sd_allowed     = M0_BITS(MGMT_FOM_SS_PHASE_VALIDATE)
        },
        [MGMT_FOM_SS_PHASE_VALIDATE] = {
                .sd_flags       = 0,
                .sd_name        = "Validate",
                .sd_allowed     = M0_BITS(MGMT_FOM_SS_PHASE_GET_STATUS,
					  MGMT_FOM_SS_PHASE_R_UNLOCK)
        },
        [MGMT_FOM_SS_PHASE_GET_STATUS] = {
                .sd_flags       = 0,
                .sd_name        = "GetStatus",
                .sd_allowed     = M0_BITS(MGMT_FOM_SS_PHASE_R_UNLOCK)
        },
        [MGMT_FOM_SS_PHASE_R_UNLOCK] = {
                .sd_flags       = M0_SDF_TERMINAL,
                .sd_name        = "ReadUnlock",
                .sd_allowed     = 0
        },
};

/*
 ******************************************************************************
 * FOM type ops
 ******************************************************************************
 */
static int mgmt_fop_ss_fto_create(struct m0_fop *fop, struct m0_fom **out,
				  struct m0_reqh *reqh)
{
	return 0;
}

static const struct m0_fom_type_ops mgmt_fop_ss_fom_ops = {
        .fto_create   = mgmt_fop_ss_fto_create
};

/*
 ******************************************************************************
 * FOP initialization logic
 ******************************************************************************
 */
static int mgmt_fom_ss_init()
{
	return
	   M0_FOP_TYPE_INIT(&m0_fop_mgmt_service_status_req_fopt,
			    .name      = "Mgmt Service Status Request",
			    .opcode    = M0_MGMT_SERVICE_STATUS_OPCODE,
			    .xt        = m0_fop_mgmt_service_status_req_xc,
			    .rpc_flags = M0_RPC_ITEM_TYPE_REQUEST,
			    .sm        = &mgmt_fop_ss_sm,
#ifndef __KERNEL__ /* for now */
			    .fom_ops   = &mgmt_fop_ss_fom_ops,
			    .svc_type  = &m0_mgmt_svc_type,
#endif
			    );

}

static void mgmt_fop_ss_fini()
{
	m0_fop_type_fini(&m0_fop_mgmt_service_state_req_fopt);
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
