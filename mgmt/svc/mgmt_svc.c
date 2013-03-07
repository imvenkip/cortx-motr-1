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
   - @ref MGMT-SVC-DLD-ovw
   - @ref MGMT-SVC-DLD-req
   - @ref MGMT-SVC-DLD-depends
   - @ref MGMT-SVC-DLD-highlights
   - @subpage MGMT-SVC-DLD-fspec "Functional Specification" <!-- @subpage -->
   - @ref MGMT-SVC-DLD-lspec
      - @ref MGMT-SVC-DLD-lspec-comps
      - @ref MGMT-SVC-DLD-lspec-state
      - @ref MGMT-SVC-DLD-lspec-thread
      - @ref MGMT-SVC-DLD-lspec-numa
   - @ref MGMT-SVC-DLD-conformance
   - @ref MGMT-SVC-DLD-ut
   - @ref MGMT-SVC-DLD-st
   - @ref MGMT-SVC-DLD-O

   <hr>
   @section MGMT-SVC-DLD-ovw Overview

   <hr>
   @section MGMT-SVC-DLD-req Requirements

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
   - The ::m0_reqh_fop_handle() subroutine must be modified to ensure that
   no external FOP requests are honored until all services have been started.

   <hr>
   @section MGMT-SVC-DLD-highlights Design Highlights

   <hr>
   @section MGMT-SVC-DLD-lspec Logical Specification
   - @ref MGMT-SVC-DLD-lspec-comps
   - @ref MGMT-SVC-DLD-lspec-state
   - @ref MGMT-SVC-DLD-lspec-thread
   - @ref MGMT-SVC-DLD-lspec-numa

   @subsection MGMT-SVC-DLD-lspec-comps Component Overview

   @subsection MGMT-SVC-DLD-lspec-state State Specification

   @subsection MGMT-SVC-DLD-lspec-thread Threading and Concurrency Model
   - Service management must operate within the locality model of a
   request handler, and must honor the existing request handler and
   service object locks.

   @subsection MGMT-SVC-DLD-lspec-numa NUMA optimizations
   - An atomic variable is used to collectively track the number of active FOPs
   per @ref reqhservice "request handler service" within the service object
   itself, rather than on a per-locality-per-service object basis.  This is
   because FOP creation and finalization events are relatively rare compared
   to FOP scheduling operations.

   <hr>
   @section MGMT-SVC-DLD-conformance Conformance
   <i>Mandatory.
   This section cites each requirement in the @ref MGMT-SVC-DLD-req section,
   and explains briefly how the DLD meets the requirement.</i>

   <hr>
   @section MGMT-SVC-DLD-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>

   <hr>
   @section MGMT-SVC-DLD-st System Tests
   <i>Mandatory.
   This section describes the system testing done, if applicable.</i>

   <hr>
   @section MGMT-SVC-DLD-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

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
