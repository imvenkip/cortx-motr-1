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
      - @ref MGMT-SVC-DLD-lspec-rh-sm
      - @ref MGMT-SVC-DLD-lspec-mgmt-svc
      - @ref MGMT-SVC-DLD-lspec-mgmt-foms
      - @ref MGMT-SVC-DLD-lspec-state
      - @ref MGMT-SVC-DLD-lspec-thread
      - @ref MGMT-SVC-DLD-lspec-numa
   - @ref MGMT-SVC-DLD-conformance
   - @ref MGMT-SVC-DLD-ut
   - @ref MGMT-SVC-DLD-st
   - @ref MGMT-SVC-DLD-O

   Individual FOPs are documented separately:
   - @subpage MGMT-SVC-DLD-FOP-SSR "Service Status Response FOP"
   - @subpage MGMT-SVC-DLD-FOP-SS "Service Status FOP"
   - @todo MGMT-SVC-DLD-FOP-SR "Service Run FOP"
   - @todo MGMT-SVC-DLD-FOP-ST "Service Terminate FOP"

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
   - A state machine is added to the request handler to address
   R.reqh.startup.synchronous, graceful shutdown and to support management
   operations.
   The m0_reqh::rh_shutdown flag will be eliminated.  Associated methods of the
   request handler object and its container object will need modification.
   This is explained in @ref MGMT-SVC-DLD-lspec-rh-sm.

   - The m0_reqh_service_ops object is extended to define a new optional method
   to be used by the request handler under circumstances described in
   @ref MGMT-SVC-DLD-lspec-rh-sm.

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
   - The m0_reqh_service object identifier must be modified to use a 128 bit
   field UUID instead of its current 64 bit identifier.  However, since this
   field is used as a 64 bit identifier in the IO service, an alternative field
   is introduced until such time as this can get resolved.
@code
   struct m0_reqh_service {
         struct m0_uint128 rs_service_uuid;
	 // deprecated
         uint64_t          rs_uuid;
         ...
   };
@endcode
   - The _args_parse() subroutine of mero/setup.c must be extended to
   accept an optional UUID field after the service type in the arguments
   of m0d.
@verbatim
         -s ServiceType[:UUID]
@endverbatim
   The service UUID can be parsed with m0_uuid_parse(). A new array field should
   be added to cs_reqh_context to store the list of service UUIDs parallel
   to the service type in rc_services.
@code
struct cs_reqh_context {
	...
	const char                 **rc_services;      // existing, malloc'd
	struct m0_uint128           *rc_service_uuids; // new, malloc'd
	...
};
@endcode
   The array must be allocated along with rc_services in cs_reqh_ctx_alloc(),
   and freed in cs_reqh_ctx_free().

   <hr>
   @section MGMT-SVC-DLD-highlights Design Highlights
   - A state machine added to the request handler to formally define when FOPs
   are to be accepted.
   - A special "management" request handler service is provided to
   expose a service management FOP interface.
     - It works in conjunction with the request handler state machine.
     - The m0mc command will be its primary client.
     - It is extensible to additional management tasks.

   <hr>
   @section MGMT-SVC-DLD-lspec Logical Specification
   - @ref MGMT-SVC-DLD-lspec-comps
   - @ref MGMT-SVC-DLD-lspec-rh-sm
   - @ref MGMT-SVC-DLD-lspec-mgmt-svc
   - @ref MGMT-SVC-DLD-lspec-mgmt-foms
   - @ref MGMT-SVC-DLD-lspec-state
   - @ref MGMT-SVC-DLD-lspec-thread
   - @ref MGMT-SVC-DLD-lspec-numa

   @subsection MGMT-SVC-DLD-lspec-comps Component Overview
   This design involves the following sub-components:
   - A state machine in the request handler
   - An associated management service

   @subsection MGMT-SVC-DLD-lspec-rh-sm Request Handler State Machine
   A state machine is introduced in the @ref reqh "request handler"
   to determine when incoming FOPs are to be accepted for processing.
   This is necessary because:
   - External management support requires that we precisely define when
   each management operations is permitted.
   - Currently incoming FOPs can be accepted even before all services
   have started.
   - Shutdown is very abrupt - it may be necessary to "drain" activities
   of some services on a case-by-case basis.

   The following must be taken into account when determining whether to
   accept an incoming FOP in the request handler:
   - The ability of the request handler to process incoming FOPs.  A formal
   state model is introduced, as described below.
   - The state of the request handler service that will animate the incoming
   FOP.  The decision must take into account the fact that a service could be
   starting or stopping in response to individual management operations.

   The request handler is extended as follows:
@code
   struct m0_reqh {
       ...
       struct m0_sm            rh_sm;       // state machine
       struct m0_reqh_service *rh_mgmt_svc; // the management service
   };
@endcode
   Note that the state machine works in conjunction with the management
   service, and hence a pointer to the service is maintained for this purpose.

   Additionally, a new method is added to m0_reqh_service_ops:
@code
   struct m0_reqh_service_ops {
       ...
       int (*rso_fop_accept)(struct m0_reqh_service *service,
                             struct m0_fop *fop);
   }
@endcode
   The method is optional, and will be used by the m0_reqh_fop_allow()
   subroutine described below.

   The request handler state machine is illustrated below. All subroutines
   mentioned in edge labels have their "m0_reqh_" prefix stripped for clarity.
   @dot
   digraph fa_sm {
       size = "6,7"
       label = "Request Handler State Machine"
       node [shape=record, fontname=Courier, fontsize=12]
       edge [fontsize=12]
       before [label="", shape="plaintext", layer=""]
       init   [label="M0_REQH_ST_INIT"]
       mstart [label="M0_REQH_ST_MGMT_START"]
       normal [label="M0_REQH_ST_NORMAL"]
       sdrain [label="M0_REQH_ST_DRAIN"]
       sstop  [label="M0_REQH_ST_SVCS_STOP"]
       mstop  [label="M0_REQH_ST_MGMT_STOP"]
       fini   [label="M0_REQH_ST_STOPPED"]
       after  [label="", shape="plaintext", layer=""]
       before -> init [label="init()"]
       init -> mstart [label="mgmt_service_start()"]
       init -> init [label="service_start()\nservice_stop()"]
       init -> sstop [label="services_terminate()"]
       init -> normal [label="start()"]
       mstart -> mstart [label="service_start()\nservice_stop()"]
       mstart -> normal [label="start()"]
       mstart -> sstop [label="services_terminate()"]
       normal -> normal [label="service_start()\nservice_stop()"]
       normal -> sdrain [label="shutdown_wait()"]
       normal -> sstop [label="services_terminate()"]
       sdrain -> sstop [label="services_terminate()"]
       sstop -> mstop [label="has management"]
       sstop -> fini [label="no managment"]
       mstop -> fini [label="mgmt_service_stop()"]
       fini -> after [label="fini()"]
   }
   @enddot
   The states and associated activities are as follows:
   - @b M0_REQH_ST_INIT The initial state when the request handler object has
     been initialized by m0_reqh_init().
     Services can be started and stopped but no FOPs are accepted in this state.
   - @b M0_REQH_ST_MGMT_START A management service is available in this state.
     Services can be started and stopped.
     Incoming management status query FOPs are accepted, but all other FOPs are
     rejected, as explained in the algorithm below.
   - @b M0_REQH_ST_NORMAL All FOPs are accepted.
     It is possible that individual services be started and stopped via
     management calls when the request handler is in this state, so care has to
     be taken to reject FOPs for services that are not in their ::M0_RST_STARTED
     state.
   - @b M0_REQH_ST_DRAIN Services are notified that they will be stopped.
     Incoming management status queries are accepted, but all other FOPs are
     rejected.
     - Note: The m0_reqh_shutdown_wait() subroutine uses
       m0_reqh_fom_domain_idle_wait() to wait for activity to cease. This
       implies that during this period it is possible that a stream of incoming
       management status requests could prevent the request handler from ever
       leaving the state. Some action has to be taken to sense such a condition
       and prevent it from happening; for example, we could wait on the FOM
       counters of the individual services instead of on the locality FOM
       counters, or reject management queries if they arrive too frequently or
       after some maximum count has been reached since transitioning to the
       state.
   - @b M0_REQH_ST_SVCS_STOP Services are stopped and finalized.  Incoming
     management status queries are accepted, but all other FOPs are rejected.
   - @b M0_REQH_ST_MGMT_STOP The management service is stopped.
     No FOPs are accepted in this state.
     - Note: A second call must be made to m0_reqh_fom_domain_idle_wait() to
       ensure that ongoing management FOMs terminate.
   - @b M0_REQH_ST_STOPPED The request handler object is stopped and may be
     finalized.

   New utility subroutines are provided to operate on the state machine:
   - m0_reqh_state_get() returns the state of this state machine.
   - m0_reqh_fop_allow() determines if an incoming FOP should be accepted.
   - m0_reqh_start() causes a transition to the NORMAL state. It is possible
   to transition to the NORMAL state even without a management service. This
   is provided mainly for unit tests.
   - m0_reqh_mgmt_service_start() starts the management service and transitions
   the state to MGMT_START.
   - m0_reqh_mgmt_service_stop() terminates the management service and
   transitions the state machine from MGMT_STOP to STOPPED.

   In addition, the following existing subroutines are extended to understand
   the request handler state:
   - m0_reqh_services_terminate() transitions the request handler to the
   SVCS_STOP state, and from there to either the MGMT_STOP or STOPPED states.
   - m0_reqh_shutdown_wait() transitions the request handler to the DRAIN state.

   The decision to accept an incoming FOP is made by the m0_reqh_fop_allow()
   subroutine.  It is intended to be used from the m0_reqh_fop_handle()
   subroutine, and will return an error code that can be provided to the
   underlying RPC subsystem, as follows:
@code
	m0_rwlock_read_lock(&reqh->rh_rwlock);
	rc = m0_reqh_fop_allow(reqh, fop);
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

   @todo Explore moving this call to later after FOM creation, so that
   a proper failure response FOP can be returned.  This may particularly
   apply to case @b C (see below).

   The m0_reqh_fop_allow() algorithm is illustrated by the following
   pseudo-code:
@code
   if (reqh state is INIT)
      return -EAGAIN;
   if (reqh state is MGMT_STOP or STOPPED)
      return -ESHUTDOWN;
   svc = m0_reqh_service_find(fop->f_type->ft_fom_type.ft_rstype, reqh);
   if (svc == NULL)
      return -ECONNREFUSED;
   if (reqh state is NORMAL) {
       if (svc->rs_state == M0_RST_STARTED)
          return 0; // case A
       if (svc->rs_state == M0_RST_STOPPING) {
          if (svc->rs_ops->rso_fop_accept != NULL)
             return (*svc->rs_ops->rso_fop_accept)(svc, fop); // case B
	  return -ESHUTDOWN;
       } else if (svc->rs_state == M0_RST_STARTING)
             return -EBUSY;  // case C
       return -ESHUTDOWN;
   } else if (reqh state is DRAIN) {
       if (svc->rs_ops->rso_fop_accept != NULL &&
           (svc->rs_state == M0_RST_STARTED ||
	    svc->rs_state == M0_RST_STOPPING))
	   return (*svc->rs_ops->rso_fop_accept)(svc, fop); // case D
       return -ESHUTDOWN;
   } else if (reqh state is MGMT_START or SVCS_STOP &&
              svc is the management service)
       return (*svc->rs_ops->rso_fop_accept)(svc, fop); // case E
   return -ESHUTDOWN;
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
   operation).  The returned error code must distinguish this case from others
   to allow higher layers the option of sending a failure response FOP.
   Note that this case cannot happen if the service in question was not
   previously terminated by a management FOP.
   - @b D In this case the request handler is draining operations. If the
   service defines an rso_fop_accept() method then, like in case @b B, the
   service is consulted.  Note that the management service itself should
   expect to get invoked in this manner for management operations issued
   during normal shutdown.
   - @b E In this case the request handler is either starting or stopping
   services. Only management service query operations are permitted, and
   the management service is consulted on whether to accept the FOP or not.

   The m0_reqh_service_find() subroutine is used to locate the service for
   the incoming FOP based on the service type.  This copies similar logic in the
   m0_fom_init() subroutine.

         @todo When the request handler manages services by their UUID instead
	 of their service type, all such occurences should be modified.  The
	 locker key should be moved to the service object and not the service
	 type.  Some work will need to be done to handle assignment of a FOP
	 to a specific service if multiple of the same type exist in the
	 request handler.  In turn, this will need modification to the
	 fto_create() method signature to pass the service object pointer in
	 directly.

   We have to ensure that any change to service state from management
   FOPs is done while holding the write lock on m0_reqh::rh_rwlock.

	@todo The locking model for FOM creation must be re-addressed.
	Currently a read-lock is held on the request handler, and the
	service mutex is acquired in the call to m0_fom_init().

   @subsection MGMT-SVC-DLD-lspec-mgmt-svc The Management Service
   The management service is a special request handler service that is
   implicitly created in the cs_services_init() subroutine by invoking
   m0_reqh_mgmt_service_start().  This subroutine will use
   m0_mgmt_service_allocate() to allocate an instance of the service and
   set m0_reqh::rh_mgmt_svc to point to this instance.
   It then starts the service using m0_reqh_service_start().
   The management service's rso_start() method will fail if this field
   matches its service instance value.  This ensures that the management
   service cannot be started explicitly.

   The management service should be terminated explicitly with
   m0_reqh_mgmt_service_stop().  It is not stopped by the
   m0_reqh_services_terminate() subroutine.

   @subsection MGMT-SVC-DLD-lspec-mgmt-foms Management FOPs and FOMs
   The management service defines FOPs for the following operations:
   - Query the state of services (m0_fop_mgmt_service_state_req)

   @todo Start services (m0_fop_mgmt_service_run_req)
   and Stop services (m0_fop_mgmt_service_terminate_req)

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

   A state machine is introduced in the @ref reqh "Request Handler" and
   is described in @ref MGMT-SVC-DLD-lspec-rh-sm.

   Each management FOM implements a "phase" state machine. See the individual
   FOMs for details.

   @subsection MGMT-SVC-DLD-lspec-thread Threading and Concurrency Model
   - The m0_reqh::rh_rwlock read-lock is held in the m0_reqh_fop_handle()
   subroutine when using m0_reqh::rh_fap::rfp_accept() method.  This
   is described in @ref MGMT-SVC-DLD-lspec-rh-sm.
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
     A state machine has been added to the request handler to
     address this issue.
   - @b I.reqh.mgmt-api.service.stop
     The m0_fop_mgmt_service_terminate_req FOP is provided for this.
   - @b I.reqh.mgmt-api.service.query
     The m0_fop_mgmt_service_status_req FOP is provided for this.
   - @b R.reqh.mgmt-api.shutdown
     The request handler state machine provides support for a more graceful
     shutdown.
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
   The m0_reqh_fop_allow() subroutine is involved with each FOP processed. The
   data it references is readily available and no major computation is involved.
   Service methods are invoked only in the rare cases of request handler or
   service state change while processing FOPs.

   <hr>
   @section MGMT-DLD-impl-plan Implementation Plan
   - Implement support to query all services, and not individual services.
   Do not implement support for individual service level control.
   - Implement the state machine in the request handler, instrumenting the
   existing startup and shutdown logic.

   Other features as required by the shipping product.

 */


/* This file is designed to be included by mgmt/mgmt.c only if the
   management service is to be built.
 */
#include "mgmt/svc/mgmt_svc_pvt.h"

/**
   @ingroup mgmt_svc_pvt
   @{
 */

static const struct m0_bob_type mgmt_svc_bob = {
	.bt_name = "mgmt svc",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct mgmt_svc, ms_magic),
	.bt_magix = M0_MGMT_SVC_MAGIC,
	.bt_check = NULL
};

M0_BOB_DEFINE(static, &mgmt_svc_bob, mgmt_svc);

/**
   UT handle to a started singleton service.  Every instance started will
   overwrite this without serialization.
 */
static struct mgmt_svc *the_mgmt_svc; /** @todo: Need this? */

/*
 ******************************************************************************
 * MGMT service
 ******************************************************************************
 */
static bool mgmt_svc_invariant(const struct mgmt_svc *svc)
{
	return mgmt_svc_bob_check(svc);
}

/**
   The rso_start method to start the MGMT service.
 */
static int mgmt_svc_rso_start(struct m0_reqh_service *service)
{
	struct mgmt_svc *svc;
	struct m0_reqh  *reqh;

	M0_LOG(M0_DEBUG, "starting");
	M0_PRE(service->rs_state == M0_RST_STARTING);

	svc = bob_of(service, struct mgmt_svc, ms_reqhs, &mgmt_svc_bob);

	/* There is only one management service per request handler
	   and it is special!
	 */
	reqh = service->rs_reqh;
	if (m0_reqh_state_get(reqh) != M0_REQH_ST_MGMT_START ||
	    reqh->rh_mgmt_svc != service)
		return -EPROTO;

	the_mgmt_svc = svc; /* UT */
	return 0;
}

/**
   The rso_prepare_to_stop method terminates the persistent FOMs.
 */
static void mgmt_svc_rso_prepare_to_stop(struct m0_reqh_service *service)
{
	struct mgmt_svc *svc;

	M0_LOG(M0_DEBUG, "preparing to stop");
	M0_PRE(service->rs_state == M0_RST_STARTED);
	svc = bob_of(service, struct mgmt_svc, ms_reqhs, &mgmt_svc_bob);
}

/**
   The rso_stop method to stop the MGMT service.
 */
static void mgmt_svc_rso_stop(struct m0_reqh_service *service)
{
	M0_LOG(M0_DEBUG, "stopping");
	M0_PRE(service->rs_state == M0_RST_STOPPING);
}

/**
   The rso_fini method to finalize the MGMT service.
 */
static void mgmt_svc_rso_fini(struct m0_reqh_service *service)
{
	struct mgmt_svc *svc;

	M0_LOG(M0_DEBUG, "done");
	M0_PRE(M0_IN(service->rs_state, (M0_RST_STOPPED, M0_RST_FAILED)));
	/** @todo Should assert reqh state */
	svc = bob_of(service, struct mgmt_svc, ms_reqhs, &mgmt_svc_bob);
	mgmt_svc_bob_fini(svc);
	the_mgmt_svc = NULL;
	m0_free(svc);
}

static int mgmt_svc_rso_fop_accept(struct m0_reqh_service *service,
				   struct m0_fop *fop)
{
	struct mgmt_svc *svc;

	M0_LOG(M0_DEBUG, "fop_accept");
	svc = bob_of(service, struct mgmt_svc, ms_reqhs, &mgmt_svc_bob);
	M0_PRE(fop != NULL);

	if (fop->f_type == &m0_fop_mgmt_service_state_req_fopt)
		return 0;

	return -ESHUTDOWN;
}

static const struct m0_reqh_service_ops mgmt_service_ops = {
	.rso_start           = mgmt_svc_rso_start,
	.rso_prepare_to_stop = mgmt_svc_rso_prepare_to_stop,
	.rso_stop            = mgmt_svc_rso_stop,
	.rso_fini            = mgmt_svc_rso_fini,
	.rso_fop_accept      = mgmt_svc_rso_fop_accept
};

/*
 ******************************************************************************
 * MGMT service type
 ******************************************************************************
 */

/**
   The rsto_service_allocate method to allocate an MGMT service instance.
 */
static int mgmt_svc_rsto_service_allocate(struct m0_reqh_service **service,
					  struct m0_reqh_service_type *stype,
					const char *arg __attribute__((unused)))
{
	struct mgmt_svc *svc;

	M0_ALLOC_PTR(svc);
	if (svc == NULL) {
		M0_LOG(M0_ERROR, "Unable to allocate memory for MGMT service");
		return -ENOMEM;
	}
	*service = &svc->ms_reqhs;
	(*service)->rs_type = stype;
	(*service)->rs_ops = &mgmt_service_ops;
	mgmt_svc_bob_init(svc);

	M0_POST(mgmt_svc_invariant(svc));

	return 0;
}

static struct m0_reqh_service_type_ops mgmt_service_type_ops = {
	.rsto_service_allocate = mgmt_svc_rsto_service_allocate,
};

M0_REQH_SERVICE_TYPE_DEFINE(m0_mgmt_svc_type, &mgmt_service_type_ops,
                            M0_MGMT_SVC_TYPE_NAME, &m0_addb_ct_mgmt_service);


/*
 ******************************************************************************
 * MGMT service initialization
 ******************************************************************************
 */

/**
   Initialize the management service.
 */
static int mgmt_svc_init(void)
{
	return m0_reqh_service_type_register(&m0_mgmt_svc_type);
}

/**
   Finalize the management service.
 */
static void mgmt_svc_fini(void)
{
        m0_reqh_service_type_unregister(&m0_mgmt_svc_type);
}

/** @} end group mgmt_svc_pvt */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
