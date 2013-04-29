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
 * Original creation date: 8-Mar-2013
 */
#pragma once
#ifndef __MERO_MGMT_SVC_PVT_H__
#define __MERO_MGMT_SVC_PVT_H__

/**
   @defgroup mgmt_svc_pvt Management Service Private
   @ingroup mgmt_pvt
   @{
 */

extern struct m0_reqh_service_type m0_mgmt_svc_type;

/**
   The Management service.  There is one instance of this service in every
   request handler.  It is created by the cs_services_init() subroutine.
 */
struct mgmt_svc {
	uint64_t               ms_magic;
	/**
	   Tracks active RUN foms.
	   Decrements signalled on the rh_sm_grp channel.
	 */
	struct m0_atomic64     ms_run_foms;
	/** Embedded request handler service */
	struct m0_reqh_service ms_reqhs;
};

/**
   Utility to fill in a service state response fop.
   @param fom The current FOM with a m0_fop_mgmt_service_state_res
   reply fop allocated.
   @param services Pointer to sequence of service UUIDs
   @pre The request handler rwlock must be held across this call.
 */
static void mgmt_fop_ssr_fill(struct m0_fom *fom,
			      struct m0_mgmt_service_uuid_seq *services);

/** @} end mgmt_svc_pvt group */
#endif /* __MERO_MGMT_SVC_PVT_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
