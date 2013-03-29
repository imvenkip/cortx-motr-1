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
 * Original creation date: 5-Mar-2013
 */
#pragma once
#ifndef __MERO_MGMT_MGMT_PVT_H__
#define __MERO_MGMT_MGMT_PVT_H__

/**
   @page MGMT-DLD-fspec Functional Specification
   - @ref MGMT-DLD-fspec-ds
   - @ref MGMT-DLD-fspec-sub
   - @ref MGMT-DLD-fspec-cli
   - @ref MGMT-DLD-fspec-usecases

   <hr>
   @section MGMT-DLD-fspec-ds Data Structures

   The following management FOPs are defined:
   - Query
   - Stop

   The following existing data structures are modified by this design:
   - m0_reqh
   - m0_reqh_service_ops

   <hr>
   @section MGMT-DLD-fspec-sub Subroutines and Macros

   The following interfaces interact with the Mero initialization subsystem:
   - m0_mgmt_init()
   - m0_mgmt_fini()

   The following interfaces are added to the Request handler to support
   proper management of services:
   - m0_reqh_fop_allow()
   - m0_reqh_mgmt_service_start()
   - m0_reqh_mgmt_service_stop()
   - m0_reqh_start()
   - m0_reqh_state_get()

   In addition, the following existing subroutines are extended to understand
   the request handler state:
   - m0_reqh_services_terminate()
   - m0_reqh_shutdown_wait()

   <hr>
   @section MGMT-DLD-fspec-cli Command Usage

   m0ctl

   <hr>
   @section MGMT-DLD-fspec-usecases Recipes

   @subsection MGMT-DLD-fspec-uc-m0ctl-local Easy local invocation
   m0ctl will default to using /etc/mero/genders to determine configuration.

   @subsection MGMT-DLD-fspec-uc-m0ctl-local Easy remote invocation
   @todo m0ctl UC Remote invocation only requires (IP) hostname

   @subsection MGMT-DLD-fspec-uc-m0ctl-sall Start all Mero services
   At present, "service mero start" is the exposed interface to start all
   services.

   @todo m0ctl UC Start all Mero services (m0d)

   @subsection MGMT-DLD-fspec-uc-m0ctl-Sall Stop all Mero services
   At present, "service mero stop" is the exposed interface to start all
   services.

   @todo m0ctl UC Stop all Mero services (m0d)

   @subsection MGMT-DLD-fspec-uc-m0ctl-s Start individual Mero services
   @todo m0ctl UC Start a Mero service

   @subsection MGMT-DLD-fspec-uc-m0ctl-S Stop individual Mero services
   @todo m0ctl UC Stop a Mero service

   @subsection MGMT-DLD-fspec-uc-m0ctl-q Query individual Mero service status
   @todo m0ctl UC Query a Mero service

   @subsection MGMT-DLD-fspec-uc-m0ctl-Q Query all Mero service status
   Either "service mero status" can be used as a front-end to query status of
   all mero services, or, alternatively "m0ctl query-all" can be used.

   @see @ref MGMT-DLD "Management Detailed Design"
   @see @ref MGMT-CTL-DLD "Management CLI Design"

 */


/**
   @defgroup mgmt_pvt Management Private Interfaces
   @ingroup mgmt

   @see @ref mgmt "Management Interfaces"
   @see @ref MGMT-DLD-fspec "Management Functional Specification"
   @see @ref MGMT-DLD "Management Detailed Design"

   @{
 */

/** Management module global ADDB context */
extern struct m0_addb_ctx m0_mgmt_addb_ctx;

#define MGMT_ADDB_FUNCFAIL(rc, loc)					\
M0_ADDB_FUNC_FAIL(&m0_addb_gmc, M0_MGMT_ADDB_LOC_##loc, rc, &m0_mgmt_addb_ctx)

#define MGMT_ALLOC_PTR(ptr, loc, ...)					\
M0_ALLOC_PTR_ADDB(ptr, &m0_addb_gmc, M0_MGMT_ADDB_LOC_##loc, &m0_mgmt_addb_ctx)

#define MGMT_ALLOC_ARR(ptr, nr, loc)					\
M0_ALLOC_ARR_ADDB(ptr, nr, &m0_addb_gmc, M0_MGMT_ADDB_LOC_##loc,	\
		  &m0_mgmt_addb_ctx)

/** @} end mgmt_pvt group */
#endif /* __MERO_MGMT_MGMT_PVT_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
