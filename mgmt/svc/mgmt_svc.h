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
#ifndef __MERO_MGMT_SVC_H__
#define __MERO_MGMT_SVC_H__

#include "reqh/reqh.h"
#include "reqh/reqh_service.h"

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
	/** Embedded request handler service */
	struct m0_reqh_service ms_reqhs;
};

static int  mgmt_fom_service_state_req_init();
static void mgmt_fom_service_state_req_fini();

/** @} end mgmt_pvt group */
#endif /* __MERO_MGMT_SVC_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
