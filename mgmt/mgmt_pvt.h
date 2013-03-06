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
     - @ref MGMT-DLD-fspec-uc-m0mc

   <hr>
   @section MGMT-DLD-fspec-ds Data Structures

   The following management FOPs are defined:
   - Query
   - Stop

   <hr>
   @section MGMT-DLD-fspec-sub Subroutines and Macros

   The following interfaces interact with the Mero initialization subsystem:
   - m0_mgmt_init()
   - m0_mgmt_fini()

   <hr>
   @section MGMT-DLD-fspec-cli Command Usage

   m0mc

   <hr>
   @section MGMT-DLD-fspec-usecases Recipes

   @subsection MGMT-DLD-fspec-uc-m0mc-local Easy local invocation
   @todo m0mc UC Local invocation requires no flags

   @subsection MGMT-DLD-fspec-uc-m0mc-local Easy remote invocation
   @todo m0mc UC Remote invocation only requires (IP) hostname

   @subsection MGMT-DLD-fspec-uc-m0mc-sall Start all Mero services
   @todo m0mc UC Start all Mero services (m0d)

   @subsection MGMT-DLD-fspec-uc-m0mc-Sall Stop all Mero services
   @todo m0mc UC Stop all Mero services (m0d)

   @subsection MGMT-DLD-fspec-uc-m0mc-s Start individual Mero services
   @todo m0mc UC Start a Mero service

   @subsection MGMT-DLD-fspec-uc-m0mc-S Stop individual Mero services
   @todo m0mc UC Stop a Mero service

   @subsection MGMT-DLD-fspec-uc-m0mc-q Query individual Mero service status
   @todo m0mc UC Query a Mero service

   @subsection MGMT-DLD-fspec-uc-m0mc-Q Query all Mero service status
   @todo m0mc UC Query all Mero services

   @see @ref MGMT-DLD "Management Detailed Design"

 */


/**
   @defgroup mgmt_pvt Management Private Interfaces
   @ingroup mgmt

   @see @ref mgmt "Management Interfaces"
   @see @ref MGMT-DLD-fspec "Management Functional Specification"
   @see @ref MGMT-DLD "Management Detailed Design"

   @{
 */

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
