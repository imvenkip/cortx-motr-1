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

/**
   @page MGMT-DLD Mero Management Interfaces

   - @ref MGMT-DLD-ovw
   - @ref MGMT-DLD-def
   - @ref MGMT-DLD-req
   - @ref MGMT-DLD-depends
   - @ref MGMT-DLD-highlights
   - @subpage MGMT-DLD-fspec "Functional Specification" <!-- Note @subpage -->
   - @ref MGMT-DLD-lspec
      - @ref MGMT-DLD-lspec-comps
      - @ref MGMT-DLD-lspec-state
      - @ref MGMT-DLD-lspec-thread
      - @ref MGMT-DLD-lspec-numa
   - @ref MGMT-DLD-conformance
   - @ref MGMT-DLD-ut
   - @ref MGMT-DLD-st
   - @ref MGMT-DLD-O
   - @ref MGMT-DLD-ref
   - @ref MGMT-DLD-impl-plan

   Additional design details are found in component DLDs:
   - @subpage MGMT-SVC-DLD "The Management Service Detailed Design"
   - @subpage MGMT-M0MC-DLD "The m0mc Command Detailed Design"

   <hr>
   @section MGMT-DLD-ovw Overview

   The Management module provides external interfaces with which to manage
   Mero.  The interfaces take the form of over-the-wire @ref fop "FOP"
   specifications that are exchanged with a management service, and
   command line utilities necessary for interaction with external
   subsystems such as the HA subsystem. @ref MGMT-DLD-ref-svc-plan "[0]".

   The Management module provides @i mechanisms by which Mero is managed;
   it does not provide @i policy.  This DLD and associated documents
   should aid in the development of middle-ware required to successfully
   deploy a Mero based product such as
   @ref MGMT-DLD-ref-mw-prod-plan "WOMO [1]".

   <hr>
   @section MGMT-DLD-def Definitions

   - @b LOMO Lustre Objects over Mero.
   - @b WOMO Web Objects over Mero.
   - @b Genders The @ref libgenders-3 "libgenders(3)" database subsystem that
   is used in cluster administration.  The module is in the public domain, and
   originated from LLNL.

   <hr>
   @section MGMT-DLD-req Requirements

   These requirements originate from @ref MGMT-DLD-ref-svc-plan "[0]".
   - @b R.reqh.mgmt-api.service.start Provide an external interface to
   start a service through an active request handler.
   - @b R.reqh.startup.synchronous During start-up, services should not
   respond to FOP requests until all services are ready.
   - @b R.reqh.mgmt-api.service.stop Provide an external interface to stop
   a service running under a request handler.
   - @b R.reqh.mgmt-api.service.query Provide an external interface to query
   the status of services running under a request handler.
   - @b R.reqh.mgmt-api.service.query-failed Provide an external interface to
   query the list of services that have failed.
   - @b R.reqh.mgmt-api.shutdown Provide an external interface to gracefully
   shut down all services of a request handler.  It should be possible to force
   the shutdown of all services.  It should be possible to gracefully shutdown
   some of the services even if some subset has failed.
   - @b R.cli.mgmt-api.services Provide support to manage local and remote Mero
   services through a CLI.  Should provide a timeout (default/configurable).
   - @b R.cli.mgmt-api.shutdown Provide support to shutdown local and remote
   Mero services through a CLI.

   These extensibility requirements also originate from
   @ref MGMT-DLD-ref-svc-plan "[0]":
   - @b R.reqh.mgmt-api.control Provide an external interface to control
   miscellaneous run time behaviors, such as trace levels, conditional logic,
   etc.
   - @b R.cli.mgmt-api.control Provide support to control miscellaneous local
   and remote Mero services through a CLI.

   <hr>
   @section MGMT-DLD-depends Dependencies

   Component DLDs will document their individual dependencies.

   - The @ref libgenders-3 "genders" database is crucial to the management of
   Mero.  The design relies on external agencies to set up this database and
   propagate it to all the hosts in the cluster.
   - The design relies on external agencies to set up the Lustre Network
   subsystem.
   - The design relies on external agencies to set up the cluster hosts
   database and assign names and addresses for both normal TCP/IP and LNet
   purposes.  The hosts database must be propagated to all hosts in the cluster.
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

   <hr>
   @section MGMT-DLD-highlights Design Highlights
   - Use the Genders database to obtain static run time component configuration
   information.
   - Provide the m0mc command line program to manage Mero
   - Automatically insert a Management service in every request handler
   - Interact with this service through Management FOPs for run time control

   <hr>
   @section MGMT-DLD-lspec Logical Specification

   - @ref MGMT-DLD-lspec-comps
   - @ref MGMT-DLD-lspec-genders
   - @ref MGMT-SVC-DLD "The management service"
   - @ref MGMT-M0MC-DLD "The management command (m0mc)"
   - @ref MGMT-DLD-lspec-state
   - @ref MGMT-DLD-lspec-thread
   - @ref MGMT-DLD-lspec-numa

   @subsection MGMT-DLD-lspec-comps Component Overview
   <i>Mandatory.
   This section describes the internal logical decomposition.
   A diagram of the interaction between internal components and
   between external consumers and the internal components is useful.</i>

   The Management module consists of the following components:
   - @b @ref MGMT-DLD-lspec-genders
   - @b @ref MGMT-DLD-lspec-svc "The Management Service"
   - @b @ref MGMT-M0MC-DLD "The management command (m0mc)"

   @subsubsection MGMT-DLD-lspec-genders Genders database

   @subsection MGMT-DLD-lspec-state State Specification
   <i>Mandatory.
   This section describes any formal state models used by the component,
   whether externally exposed or purely internal.</i>

   @subsection MGMT-DLD-lspec-thread Threading and Concurrency Model

   - Service management must operate within the locality model of a
   request handler, and must honor the existing request handler and
   service object locks.

   @subsection MGMT-DLD-lspec-numa NUMA optimizations

   - An atomic variable is used to collectively track the number of active FOPs
   per @ref reqhservice "request handler service" within the service object
   itself, rather than on a per-locality-per-service object basis.  This is
   because FOP creation and finalization events are relatively rare compared
   to FOP scheduling operations.

   <hr>
   @section MGMT-DLD-conformance Conformance
   <i>Mandatory.
   This section cites each requirement in the @ref MGMT-DLD-req section,
   and explains briefly how the DLD meets the requirement.</i>

   <hr>
   @section MGMT-DLD-ut Unit Tests
   <i>Mandatory. This section describes the unit tests that will be designed.
   </i>

   <hr>
   @section MGMT-DLD-st System Tests
   <i>Mandatory.
   This section describes the system testing done, if applicable.</i>

   <hr>
   @section MGMT-DLD-O Analysis
   <i>This section estimates the performance of the component, in terms of
   resource (memory, processor, locks, messages, etc.) consumption,
   ideally described in big-O notation.</i>

   <hr>
   @section MGMT-DLD-ref References

   - @anchor MGMT-DLD-ref-svc-plan [0] <a href="https://docs.google.com/a/
xyratex.com/document/d/10VtuJSH8gcMNjaS7wEvgDbn2v1vS7m2Czgi8nb857sQ/view">
Mero Service Interface Planning</a>
   - @anchor MGMT-DLD-ref-mw-prod-plan [1] <a href="https://docs.google.com/a/
xyratex.com/document/d/1OTmELk-rsABDONlsXCIFrR8q5NVlVlpQkiKEzgP0hl8/view">
Mero-WOMO Productization Planning</a>
   - @anchor libgenders-3 <a href="http://linux.die.net/man/3/libgenders">
   libgenders(3)</a>,
   @anchor nodeattr-1 <a href="http://linux.die.net/man/1/nodeattr">
   nodeattr(1)</a>

   <hr>
   @section MGMT-DLD-impl-plan Implementation Plan

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

/*  LocalWords:  XYRATEX XYRATEX'S Braganza MGMT DLD Mero subpage lspec numa
 */
/*  LocalWords:  DLDs SVC
 */
