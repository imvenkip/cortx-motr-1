/* -*- C -*- */
/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Rajanikant Chirmade <rajanikant_chirmade@xyratex.com>
 * Original creation date: 30-May-2014
 */

#pragma once
#ifndef __MERO_SSS_SS_SVC_H__
#define __MERO_SSS_SS_SVC_H__

#include "reqh/reqh.h"
#include "reqh/reqh_service.h"
#include "fop/fom_generic.h"

/**
 * @defgroup ss_svc Start_Stop Service
 * @{
 */

enum {
        MAX_SERVICE_NAME_LEN = 128,
};

#define M0_START_STOP_SVC_NAME "sss"
extern struct m0_reqh_service_type m0_ss_svc_type;

/** Start Stop Service */
struct ss_svc {
	struct m0_reqh_service sss_reqhs;
};

/** Start Stop fom */
struct ss_fom {
	uint64_t                                ssf_magic;
	struct m0_fom                           ssf_fom;
	struct m0_reqh_service_start_async_ctx  ssf_ctx;
	/** reqh service type */
	struct m0_reqh_service_type            *ssf_stype;
	/** reqh service */
	struct m0_reqh_service                 *ssf_svc;
	/** reqh service name */
	char                                    ssf_sname[MAX_SERVICE_NAME_LEN];
};

enum ss_fom_phases {
	SS_FOM_INIT = M0_FOPH_NR + 1,
	SS_FOM_SVC_ALLOC,
	SS_FOM_START,
	SS_FOM_START_WAIT,
	SS_FOM_STOP,
	SS_FOM_STOP_WAIT,
	SS_FOM_STATUS,
};

M0_INTERNAL int m0_ss_svc_init(void);
M0_INTERNAL void m0_ss_svc_fini(void);

/** @} end group ss_svc */

#endif /* __MERO_SSS_SS_SVC_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
