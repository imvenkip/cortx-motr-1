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
 * Original creation date: 6-Mar-2013
 */

/**
   @page MGMT-SVC-DLD Mero Management Service

   <hr>
   @section MGMT-SVC-DLD-ovw Overview

   <hr>
   @section MGMT-SVC-DLD-depends Dependencies

   - A per @ref reqhservice "request handler service" activity counter is
   required to track active FOPs.  This is needed to implement a graceful
   shutdown of a service.
@code
   struct m0_reqh_service {
         ...
         struct m0_atomic64 rs_fop_count;
   };
@endcode
   The counter is operated from the ::m0_reqh_fop_handle() subroutine.
   - The @ref reqh "request handler" module requires modifications to always
   configure the management service.
   - Both the @ref reqh "request handler" object and the
   @ref reqhservice "request handler service" object may need to
   be extended or otherwise modified to support the necessary service
   management functionality.

 */


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
