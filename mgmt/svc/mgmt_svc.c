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
   @page MGMT-SVC-DLD Management Service Design
   - @ref MGMT-SVC-DLD-ovw
   - @ref MGMT-SVC-DLD-req
   - @ref MGMT-SVC-DLD-depends
   - @ref MGMT-SVC-DLD-highlights
   - @ref MGMT-SVC-DLD-lspec
      - @ref MGMT-SVC-DLD-lspec-comps
      - @ref MGMT-SVC-DLD-lspec-fap
         - @ref MGMT-SVC-DLD-lspec-fap-sm
         - @ref MGMT-SVC-DLD-lspec-fap-mc
      - @ref MGMT-SVC-DLD-lspec-mgmt-svc
      - @ref MGMT-SVC-DLD-lspec-mgmt-foms
      - @ref MGMT-SVC-DLD-lspec-state
      - @ref MGMT-SVC-DLD-lspec-thread
      - @ref MGMT-SVC-DLD-lspec-numa
   - @ref MGMT-SVC-DLD-conformance
   - @ref MGMT-SVC-DLD-ut
   - @ref MGMT-SVC-DLD-st
   - @ref MGMT-SVC-DLD-O

   <hr>
   @section MGMT-SVC-DLD-ovw Overview
   This design provides external control and monitoring interfaces for the
   services executing under a request handler.  It also addresses issues
   involving the graceful start up and shutdown of the m0d process.

   The design operates within the standard deployment pattern parameters
   outlined in @ref MGMT-DLD-lspec-osif "Extensions for the service command".

   <hr>
   @section MGMT-SVC-DLD-req Requirements
   The management service will address the following requirements described
   in the @ref MGMT-DLD-req "Management DLD".
   - @b R.reqh.mgmt-api.service.start
   - @b R.reqh.startup.synchronous
   - @b R.reqh.mgmt-api.service.stop
   - @b R.reqh.mgmt-api.service.query
   - @b R.reqh.mgmt-api.shutdown
   - @b R.reqh.mgmt-api.control

   <hr>
   @section MGMT-SVC-DLD-depends Dependencies
   - A FOP acceptance policy engine is added to the request handler to address
   R.reqh.startup.synchronous, graceful shutdown and to support management
   operations.  This is explained in @ref MGMT-SVC-DLD-lspec-fap.
@code
   struct m0_reqh {
          ...
          struct m0_reqh_fop_policy rh_fap;
   };
@endcode
   The m0_reqh::rh_shutdown flag will be eliminated.  Associated methods of the
   request handler object and its container object will need modification.

   - The m0_reqh_service_ops object is extended to define a new optional method
   to be used by the FOP acceptance policy under circumstances described in
   @ref MGMT-SVC-DLD-lspec-fap-mc :
@code
   struct m0_reqh_service_ops {
       ...
       int (*rso_fop_accept)(struct m0_reqh_service *service,
                             struct m0_fop *fop);
   }
@endcode

   - The m0_reqh_service object is extended to add a counter to track the number
   of outstanding FOMs of a service.  This is needed to implement the graceful
   shutdown of an individual service or of a set of services.
@code
   struct m0_reqh_service {
         ...
         struct m0_atomic64 rs_nr_foms;
   };
@endcode
   The counter is operated from the m0_fom_init() and m0_fom_fini() subroutines,
   as explained in @ref MGMT-SVC-DLD-lspec-mgmt-foms.

   <hr>
   @section MGMT-SVC-DLD-highlights Design Highlights
   - A policy to control incoming FOP processing is added to the request
   handler to formally define when FOPs are to be accepted.
   - A special "management" request handler service is provided to
   expose a service management FOP interface.
     - It works in conjunction with the FOP acceptance policy.
     - The m0mc command will be its primary client.
     - It is extensible to additional management tasks.

   <hr>
   @section MGMT-SVC-DLD-lspec Logical Specification
   - @ref MGMT-SVC-DLD-lspec-comps
   - @ref MGMT-SVC-DLD-lspec-fap
      - @ref MGMT-SVC-DLD-lspec-fap-sm
      - @ref MGMT-SVC-DLD-lspec-fap-mc
   - @ref MGMT-SVC-DLD-lspec-mgmt-svc
   - @ref MGMT-SVC-DLD-lspec-mgmt-foms
   - @ref MGMT-SVC-DLD-lspec-state
   - @ref MGMT-SVC-DLD-lspec-thread
   - @ref MGMT-SVC-DLD-lspec-numa

   @subsection MGMT-SVC-DLD-lspec-comps Component Overview
   This design involves the following sub-components:
   - A FOP acceptance policy
   - An associated management service

   @subsection MGMT-SVC-DLD-lspec-fap FOP Acceptance Policy
   A policy engine is introduced in the @ref reqh "request handler"
   to determine when incoming FOPs are to be accepted for processing.
   This is necessary because:
   - External management support requires that we precisely define when
   each management operations is permitted.
   - Currently incoming FOPs can be accepted even before all services
   have started.
   - Shutdown is very abrupt - it may be necessary to "drain" activities
   of some services on a case-by-case basis.

   The policy must take the following into account when determining whether to
   accept an incoming FOP:
   - The willingness of the request handler to process incoming FOPs.  A formal
   state model is introduced, as described in @ref MGMT-SVC-DLD-lspec-fap-sm.
   - The state of the request handler service that will animate the incoming
   FOP.  The policy must take into account the fact that a service could be
   starting or stopping in response to individual management operations.

   The policy is represented the m0_reqh::rh_fap field, whose data type
   is defined as:
@code
   struct m0_reqh_fop_policy {
       struct m0_sm            rfp_sm;       // policy state machine
       struct m0_reqh_service *rfp_mgmt_svc; // the management service
   };
@endcode
   Note that the policy is designed to work in conjunction with the management
   service, and maintains a pointer to the service for this purpose.

   New subroutines are provided to initialize and finalize the policy:
   - m0_reqh_fp_init()
   - m0_reqh_fp_fini()

   The policy object can be extended in the future to maintain statistical
   information on its use.

   @subsubsection MGMT-SVC-DLD-lspec-fap-sm FOP Acceptance Policy State Machine
   The FOP acceptance policy state machine is as follows:
   @dot
   digraph fa_sm {
       size = "6,7"
       label = "FOP Acceptance State Machine"
       node [shape=record, fontname=Courier, fontsize=12]
       edge [fontsize=12]
       before [label="", shape="plaintext", layer=""]
       init   [label="INIT"]
       mstart [label="M0_REQH_FP_MGMT_START"]
       sstart [label="M0_REQH_FP_SVCS_START"]
       normal [label="M0_REQH_FP_NORMAL"]
       sdrain [label="M0_REQH_FP_DRAIN"]
       sstop  [label="M0_REQH_FP_SVCS_STOP"]
       mstop  [label="M0_REQH_FP_MGMT_STOP"]
       fini   [label="M0_REQH_FP_STOPPED"]
       before -> init [label="m0_reqh_init(reqh)"]
       init -> mstart [label="cs_services_init()"]
       init -> fini [label="fail"]
       mstart -> sstart [label="management started"]
       mstart -> fini [label="fail"]
       sstart -> normal [label="all services started"]
       sstart -> sstop [label="some service failed"]
       normal -> sdrain [label="m0_reqh_shutdown_wait()"]
       sdrain -> sstop [label="m0_reqh_services_terminate()"]
       sstop -> mstop [label="services stopped"]
       mstop -> fini [label="mgmt stopped"]
   }
   @enddot
   The states and associated activities are as follows:
   - @b M0_REQH_FP_INIT The initial state when the request handler object has
     been initialized by m0_reqh_init().
     No FOPs are accepted in this state.
   - @b M0_REQH_FP_MGMT_START The request handler starts the management service
     in this state.
     No FOPs are accepted in this state.
     The transition to this state will take place in cs_services_init().
   - @b M0_REQH_FP_SVCS_START The request handler starts other services in
     this state.
     Incoming management status queries are accepted, but all other FOPs are
     rejected.
     The transition to this state will take place in cs_services_init().
   - @b M0_REQH_FP_NORMAL All services are started, and all FOPs are accepted.
     It is possible that individual services be started and stopped via
     management calls when the request handler is in this state, so care has to
     be taken to reject FOPs for services that are not in their ::M0_RST_STARTED
     state.
     The transition to this state will take place in cs_services_init().
   - @b M0_REQH_FP_DRAIN Services are notified that they will be stopped.
     Incoming management status queries are accepted, but all other FOPs are
     rejected.
     The transition to this state happens within m0_reqh_shutdown_wait().
        - Note: The m0_reqh_shutdown_wait() subroutine uses
          m0_reqh_fom_domain_idle_wait() to wait for activity to cease. This
          implies that during this period it is possible that a stream of
          incoming management status requests could prevent the request handler
          from ever leaving the state. Some action has to be taken to sense such
          a condition and prevent it from happening; for example, we could wait
          on the FOM counters of the individual services instead of on the
          locality FOM counters, or reject management queries if they arrive
	  too frequently or after some maximum count has been reached since
	  transitioning to the state.
   - @b M0_REQH_FP_SVCS_STOP Services are stopped and finalized.  Incoming
     management status queries are accepted, but all other FOPs are rejected.
     The transition to this state happens within m0_reqh_services_terminate();
     care must be taken to not stop the management service until the other
     services are terminated.
   - @b M0_REQH_FP_MGMT_STOP The management service is stopped.
     The transition to this state happens within m0_reqh_services_terminate(),
     after services are terminated.
     No FOPs are accepted in this state.
     A second call must be made to m0_reqh_fom_domain_idle_wait() to ensure that
     ongoing management FOMs terminate and then the management service is
     stopped.
   - @b M0_REQH_FP_STOPPED The request handler object is stopped.
     The transition to this state is made in m0_reqh_services_terminate()
     after the management service is stopped.

   New utility subroutines are provided to operate on the policy state machine:
   - m0_reqh_fp_state_get() returns the state of this state machine.
   - m0_reqh_fp_state_set() sets the state of this state machine.

   @subsubsection MGMT-SVC-DLD-lspec-fap-mc FOP Acceptance Policy Engine
   The FOP acceptance policy decision is made by the m0_reqh_fp_accept()
   subroutine.  It is intended to be used from the m0_reqh_fop_handle()
   subroutine, and will return an error code that can be provided to the
   underlying RPC subsystem, as follows:
@code
	m0_rwlock_read_lock(&reqh->rh_rwlock);
	rc = m0_reqh_fp_accept(&reqh->rh_fp, reqh, fop);
	if (rc != 0) {
		REQH_ADDB_FUNCFAIL(rc, FOP_HANDLE_2, &reqh->rh_addb_ctx);
		m0_rwlock_read_unlock(&reqh->rh_rwlock);
		return;
	}
        // create and schedule FOM ...
	m0_rwlock_read_unlock(&reqh->rh_rwlock);
@endcode
   Note that the existing m0_reqh::rh_shutdown field which is currently
   tested in the above "if" statement is no longer necessary.

   The policy decision algorithm is illustrated by the following pseudo-code:
@code
   if (fp state is INIT or MGMT_START)
      return -EAGAIN;
   rc = -ESHUTDOWN;
   if (fp state is MGMT_STOP or STOPPED)
      return rc;
   svc = m0_reqh_service_find(fop->f_type->ft_fom_type.ft_rstype, reqh);
   if (svc == NULL)
      return -ECONNREFUSED;
   if (fp state is NORMAL) {
       if (svc->rs_state == M0_RST_STARTED)
          return 0; // case A
       else if (svc->rs_state == M0_RST_STOPPING) {
          if (svc->rs_ops->rso_fop_accept != NULL)
             rc = (*svc->rs_ops->rso_fop_accept)(svc, fop); // case B
       }
       else if (svc->rs_state == M0_RST_STARTING)
             rc = -EAGAIN;  // case C
   } else if (fp state is DRAIN) {
       if (svc->rs_ops->rso_fop_accept != NULL &&
           (svc->rs_state == M0_RST_STARTED ||
	    svc->rs_state == M0_RST_STOPPING))
	   rc = (*svc->rs_ops->rso_fop_accept)(svc, fop); // case D
   } else if (fp state is SVCS_START or SVCS_STOP &&
              svc is the management service)
       rc = (*svc->rs_ops->rso_fop_accept)(svc, fop); // case E
   return rc;
@endcode
   The interesting cases have been flagged:
   - @b A This is the normal operating case
   - @b B In this case the request handler is operating normally but the
   service concerned is stopping.  If the service has defined an
   rso_fop_accept() method then it is consulted to determine if the incoming
   FOP should be accepted or not.  This could be used, for example, for some
   sort of session tear down processing.
   - @b C In this case the request handler is operating normally but the
   service is in the process of starting (in response to an earlier management
   operation).  The error code indicates that the request could be retried
   later.
   - @b D In this case the request handler is draining operations. If the
   service defines an rso_fop_accept() method then, like in case @b B, the
   service is consulted.  Note that the management service itself should
   expect to get invoked in this manner for management operations issued
   during normal shutdown.
   - @b E In this case the request handler is either starting or stopping
   services. Only management service query operations are permitted, and
   the management service is consulted on whether to accept the FOP or not.

   Note the use of m0_reqh_service_find() to locate the service for the
   incoming FOP based on the service type.  This copies similar logic in
   the m0_fom_init() subroutine.

   Note that we have to ensure that any change to service state from management
   FOPs is done while holding the write lock on m0_reqh::rh_rwlock.

   @subsection MGMT-SVC-DLD-lspec-mgmt-svc The Management Service
   The management service is a special request handler service that is
   implicitly created in the cs_services_init() subroutine.
   The management service should never be created explicitly; this
   will be asserted by the service's rso_start() method, which is charged with
   setting the m0_reqh_fop_policy::rfp_mgmt_svc field in the FOP acceptance
   policy object in the request handler.

   @subsection MGMT-SVC-DLD-lspec-mgmt-foms Management FOPs and FOMs
   The management service defines FOPs for the following operations:
   - Query the state of services (m0_fop_mgmt_service_status_req)
   - Start services (m0_fop_mgmt_service_run_req)
   - Stop services (m0_fop_mgmt_service_terminate_req)

   Additional FOPs can be added in the future to control run time tracing, etc.

   The management service does not provide FOPs to shut down the m0d process;
   the existing shutdown mechanism is sufficient for the purpose.  See
   @ref MGMT-DLD-lspec-osif "Extensions for the service command" for more
   detail.  The FOP acceptance state machine introduced by this design does
   make the shutdown more organized.

   Service management FOMs utilize the methods offered by the existing
   @ref reqhservice "request handler service" object.  These methods rely on
   external serialization utilizing the m0_reqh::rh_rwlock, so it is expected
   that these FOPs could block during execution; they will sandwich such calls
   with m0_fom_block_enter() and m0_fom_block_leave().
   The service query FOP requires a read-lock, but the service start and stop
   FOPs require a write-lock.

   When stopping a service or a set of services, it is necessary for the
   management FOM to wait for the FOMs of the service to complete.  The design
   introduces the m0_reqh_service::rs_nr_foms counter to track the number of
   outstanding FOMs of a service.  The counter is incremented by the
   m0_fom_init() subroutine and decremented by the m0_fom_fini() subroutine.  As
   the FOMs of a given service could execute in multiple localities, the counter
   is defined as an atomic variable so that it can be updated without adverse
   impact to the locality operating on the counter.

   The m0_fom_fini() subroutine will be further extended to always signal on the
   m0_reqh::rh_sd_signal channel - currently it does so only when the locality
   counter FOM counter goes to 0.  This will have minimal impact on the existing
   user of the channel, m0_reqh_fom_domain_idle_wait(), as the subroutine is
   used only during shutdown.

   @subsection MGMT-SVC-DLD-lspec-state State Specification

   The FOP acceptance policy is influenced by two state machines:
   m0_reqh::rh_fhp::rfp_sm described in @ref MGMT-SVC-DLD-lspec-fap-sm, and
   m0_reqh_service::rs_state, described in
   @ref reqhservice "request handler service".
   The policy engine is described in @ref MGMT-SVC-DLD-lspec-fap-mc.

   Each management FOM implements a "phase" state machine. See the individual
   FOMs for details:
   - @todo Add references to the FOMs here

   @subsection MGMT-SVC-DLD-lspec-thread Threading and Concurrency Model
   - The m0_reqh::rh_rwlock read-lock is held in the m0_reqh_fop_handle()
   subroutine when using m0_reqh::rh_fap::rfp_accept() method.  This
   is described in @ref MGMT-SVC-DLD-lspec-fap-mc.
   - The m0_reqh::rh_rwlock is used to synchronize service management
   operations.  These are potentially blocking calls so FOM blocking support
   subroutines are used.
   - The m0_reqh_service::rs_nr_foms counter is an atomic that can be used from
   different locality threads without synchronization.  Decrementing this
   counter is always accompanied by signaling on the m0_reqh::rh_sd_signal
   channel to alert potential waiters.

   @subsection MGMT-SVC-DLD-lspec-numa NUMA optimizations
   - An atomic variable is used to track the number of request handler service
   FOMs outstanding, as the FOMs of a service could execute in multiple
   localities.  FOP creation and finalization events are relatively rare
   compared to FOP scheduling operations, so the impact of this atomic is low.

   <hr>
   @section MGMT-SVC-DLD-conformance Conformance
   - @b I.reqh.mgmt-api.service.start
     The m0_fop_mgmt_service_run_req FOP is provided for this.
   - @b I.reqh.startup.synchronous
     A FOP acceptance policy has been added to the request handler to
     address this issue.
   - @b I.reqh.mgmt-api.service.stop
     The m0_fop_mgmt_service_terminate_req FOP is provided for this.
   - @b I.reqh.mgmt-api.service.query
     The m0_fop_mgmt_service_status_req FOP is provided for this.
   - @b R.reqh.mgmt-api.shutdown
     The FOP acceptance policy provides support for a more graceful shutdown.
   - @b I.reqh.mgmt-api.control Provide an external interface to control
     New FOPs can be added to the management service to provide such
     control.
   <hr>
   @section MGMT-SVC-DLD-ut Unit Tests
   @todo Define unit tests

   <hr>
   @section MGMT-SVC-DLD-st System Tests
   @todo Define system tests

   <hr>
   @section MGMT-SVC-DLD-O Analysis
   The FOP acceptance policy is involved with each FOP processed. The data it
   references is readily available and no major computation is involved.
   Service methods are invoked only in the rare cases of request handler or
   service state change while processing FOPs.

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
