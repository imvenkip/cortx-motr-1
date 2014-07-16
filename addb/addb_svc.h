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
 * Original creation date: 12/09/2012
 */

#pragma once

#ifndef __MERO_ADDB_ADDB_SVC_H__
#define __MERO_ADDB_ADDB_SVC_H__

#include "reqh/reqh.h"
#include "reqh/reqh_service.h"
#ifndef __KERNEL__
#include "mero/setup.h"
#endif
/**
   @defgroup addb_svc_pvt ADDB Service Internal Interfaces
   @ingroup addb_svc
   @{
 */

extern struct m0_reqh_service_type m0_addb_svc_type;

/**
   ADDB statistics posting FOM
 */
struct addb_post_fom {
	uint64_t                pf_magic;
	/** Periodicity of the statistics post. */
	m0_time_t               pf_period;
	/** Tolerance limit in epoch calculation */
	m0_time_t               pf_tolerance;
	/** Next post time. */
	m0_time_t               pf_next_post;
	/** trap used to get into the locality to interact with the fom */
	struct m0_sm_ast        pf_ast;
	/** The FOM timer */
	struct m0_fom_timeout   pf_timeout;
	/** Last scanned monitor from the reqh mon list */
	struct m0_addb_monitor *pf_mon;
	/** Number of unprocessed monitors for single invocation of
	 *  m0_addb_monitor_summaries_post().
	 */
	uint32_t                pf_mon_unprocessed_nr;
	/** Embedded FOM object. */
	struct m0_fom           pf_fom;
};

/**
 * ADDB fom created for ADDB fop
 */
struct addb_fom {
	uint64_t       af_magic;
	struct m0_fom  af_fom;
};

/**
   ADDB request handler service
 */
struct addb_svc {
	uint64_t               as_magic;
	/**
	   The statistics posting FOM.
	 */
        struct addb_post_fom   as_pfom;
	/**
	   Service condition variable
	 */
	struct m0_cond         as_cond;
	/**
	   Embedded request handler service object.
	 */
	struct m0_reqh_service as_reqhs;
};

/** @} end group addb_svc_pvt */

#endif /* __MERO_ADDB_ADDB_SVC_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
