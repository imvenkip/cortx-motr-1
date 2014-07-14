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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 05/08/2011
 */

#pragma once

#ifndef __MERO_REQH_REQH_SERVICE_H__
#define __MERO_REQH_REQH_SERVICE_H__

#include "lib/atomic.h"
#include "lib/chan.h"
#include "lib/tlist.h"
#include "lib/bob.h"
#include "lib/mutex.h"
#include "lib/types.h"
#include "sm/sm.h"
#include "addb/addb.h"

struct m0_fop;
struct m0_fom;

/**
   @defgroup reqhservice Request handler service
   @ingroup reqh

   A mero service is described to a request handler using a struct
   m0_reqh_service_type data structure.
   Every service should register its corresponding m0_reqh_service_type
   instance containing service specific initialisation method, service
   name. A m0_reqh_service_type instance can be defined using
   M0_REQH_SERVICE_TYPE_DEFINE macro. Once the service type is defined
   it should be registered using m0_reqh_service_type_register() and
   unregister using m0_reqh_service_type_unregister().
   During service type registration, the service type is added to the global
   list of the service types maintained by the request handler module.
   The request handler creates and initialises a service by invoking the
   constructor method of its service type, and obtains struct m0_reqh_service
   instance. The constructor should perform only internal house keeping tasks.
   Next, the service start method is invoked, it should properly initialise the
   internal state of the service, e.g. service fops, &c.

   Request handler creates an rpc_machine for each specified end point per
   network domain. There could be multiple rpc machines running within a single
   request handler, resulting in the associated services being reachable through
   all of these end points.

   Services need to be registered before they can be started. Service
   registration can be done as below,

   First, we have to define service type operations,
   @code
   static const struct m0_reqh_service_type_ops dummy_stype_ops = {
        .rsto_service_allocate = dummy_service_allocate
   };
   @endcode

   - then define service operations,
   @code
   const struct m0_reqh_service_ops service_ops = {
        .rso_start = service_start,
        .rso_stop = service_stop,
        .rso_fini = service_fini,
	.rso_stats_post_addb = optional_service_stats,
   };
   @endcode

   Typical service specific start and stop operations may look like below,
   @code
   static int dummy_service_start(struct m0_reqh_service *service)
   {
        ...
        rc = dummy_fops_init();
	...
   }

   static int dummy_service_stop(struct m0_reqh_service *service)
   {
	...
        dummy_fops_fini();
	...
   }
   @endcode

   - define service type using M0_REQH_SERVICE_TYPE_DEFINE macro,
   @code
   M0_REQH_SERVICE_TYPE_DEFINE(m0_ios_type, &ios_type_ops, "ioservice",
                               &m0_addb_ct_ios_serv, 2);
   @endcode

   - now, the above service type can be registered as below,
   @code
   m0_reqh_service_type_register(&dummy_stype);
   @endcode

   - unregister service using m0_reqh_service_type_unregister().

   @anchor reqhservice_state_dia <b>Request Handler Service State Diagram</b>
   A typical service transitions through its states as illustrated below.
   The triggering subroutine is identified, and the service method or
   service type method invoked is shown within square braces.
   @verbatim
  m0_reqh_service_allocate()
       <<START>> ---------------------> M0_RST_INITIALISING
   [rsto_service_allocate()]                |
                                            | m0_reqh_service_init()
             m0_reqh_service_failed()       v
        +------------------------------ M0_RST_INITIALISED
        |                                   | m0_reqh_service_start_async()/
        |                                   |     m0_reqh_service_start()
        v    m0_reqh_service_failed()       v
        +------------------------------ M0_RST_STARTING [rso_start [_async]()]
        |                                   | m0_reqh_service_started()/
        v                                   |     m0_reqh_service_start()
   M0_RST_FAILED                            v
        |                              M0_RST_STARTED
        | m0_reqh_service_fini()            | m0_reqh_service_prepare_to_stop()/
        v                                   |     m0_reqh_service_stop()
     <<END>> [rso_fini()]                   v
        ^                               M0_RST_STOPPING [rso_prepare_to_stop()]
        |                                   |
        |                                   | m0_reqh_service_stop()
        |     m0_reqh_service_fini()        v
	+------------------------------ M0_RST_STOPPED [rso_stop()]
   @endverbatim

   @{
 */

/**
   Phases through which a service typically passes.
 */
enum m0_reqh_service_state {
	/**
	   A service is in M0_RST_INITIALISING state when it is created.
	   in service specific start routine, once the service specific
	   initialisation is complete, generic part of service is initialised.

           @see m0_reqh_service_allocate()
	 */
	M0_RST_INITIALISING,
	/**
	   A service transitions to M0_RST_INITIALISED state, once it is
	   successfully initialised.

	   @see m0_reqh_service_init()
	 */
	M0_RST_INITIALISED,
	/**
	   A service transitions to M0_RST_STARTING state before service
	   specific start routine is invoked.

	   @see m0_reqh_service_start_async(), m0_reqh_service_start()
	 */
	M0_RST_STARTING,
	/**
	   A service transitions to M0_RST_STARTED state when it is ready
	   to process FOP requests.
	   @see m0_reqh_service_started(), m0_reqh_service_start()
	 */
	M0_RST_STARTED,
	/**
	   A service transitions to M0_RST_STOPPING state before the service
           specific rso_stop() routine is invoked.
	   The optional rso_prepare_to_stop() method will be called
	   when this state is entered.  This gives a service a chance
	   to trigger FOM termination before its rso_stop() method is
	   invoked.
	   @see m0_reqh_service_prepare_to_stop(), m0_reqh_service_stop()
	 */
	M0_RST_STOPPING,
	/**
	   A service transitions to M0_RST_STOPPED state, once service specific
           rso_stop() routine completes successfully and after it is
           unregistered from the request handler.
	   @see m0_reqh_service_stop()
	 */
	M0_RST_STOPPED,
	/**
	   A service transitions to M0_RST_FAILED state if the service start up
	   fails.
	 */
	M0_RST_FAILED
};

/**
   Represents a service on node.
   Multiple services in a mero address space share the same request handler.
   There exist a list of services in struct m0_reqh, m0_reqh::rh_services,
   sharing the same request handler.

 */
struct m0_reqh_service {
	/**
	   Service id that should be unique throughout the cluster.
	 */
	struct m0_uint128                  rs_service_uuid;

	/**
	   Service type specific structure to hold service specific
	   implementations of its operations.
	   This can be used to initialise service specific objects such as fops.
	 */
	const struct m0_reqh_service_type *rs_type;

	/**
	   Same type of services may have different levels to manage their
	   cleanup order.
	   If this level is 0 during service creation,
	   m0_reqh_service_type::rst_level is copied by
	   m0_reqh_service_allocate().
	 */
	unsigned                           rs_level;

	/**
	   Service state machine.

	   @see m0_reqh_service_state
	 */
	struct m0_sm                       rs_sm;

	/**
	   Protects service state transitions.
	 */
	struct m0_mutex                    rs_mutex;

	/**
	   Service specific operations vector.
	 */
	const struct m0_reqh_service_ops  *rs_ops;

	/**
	   Request handler this service belongs to.
	 */
	struct m0_reqh                    *rs_reqh;

	/**
	   Linkage into list of services in request handler.

	   @see m0_reqh::rh_services
	 */
	struct m0_tlink                    rs_linkage;

	/**
	   ADDB context for this service
	 */
	struct m0_addb_ctx                 rs_addb_ctx;

	/** Channel to wait till reverse session is established */
	struct m0_chan                     rs_rev_conn_wait;

	/**
	 * service context
	 */
	struct m0_reqh_context            *rs_reqh_ctx;

	/**
	 * The parameter specifying whatever information the service needs
	 * to start up.
	 *
	 * Currently this buffer is interpreted by confd_start().
	 *
	 * @see m0_sssservice_req::ss_param
	 */
	struct m0_buf                     rs_ss_param;

	/**
	   Service magic to check consistency of service instance.
	 */
	uint64_t                           rs_magix;
};

/**
   Asynchronous service startup context.
 */
struct m0_reqh_service_start_async_ctx {
	struct m0_reqh_service *sac_service;
	/** SSS (start/stop service) FOM. */
	struct m0_fom          *sac_fom;
	/** Result of startup activity. */
	int                     sac_rc;
};

/**
   Service specific operations vector.
 */
struct m0_reqh_service_ops {
	/**
	   Optional operation to asynchronously ready to transition a service
	   to the M0_RST_STARTED state. Either this method or rso_start()
	   must be provided.

           This method is invoked by m0_reqh_service_start_async().

	   @pre m0_reqh_service_state_get(asc->sac_service) == M0_RST_STARTING
	   @see m0_reqh_service_start_async(), m0_reqh_service_started()
	 */
	int (*rso_start_async)(struct m0_reqh_service_start_async_ctx *asc);

	/**
	   Optional operation to perform service specific startup operations
	   synchronously.  Either this method or rso_start_async() must be
	   provided.

	   Once started, incoming requests related to this service are ready
	   to be processed by the corresponding request handler.
	   Service startup can involve operations like initialising service
	   specific fops, &c which may fail due to whichever reason, in that
	   case the service is finalised and appropriate error is returned.

           This is invoked from m0_reqh_service_start(). Once the service
           specific startup operations are performed, the service is registered
           with the request handler.

	   @param service Service to be started

	   @see m0_reqh_service_start()
	 */
	int (*rso_start)(struct m0_reqh_service *service);

	/**
	   Optional method to notify the service that the request handler
	   is preparing to shut down.  After this call is made the request
	   handler will block waiting for FOM termination, so any long
	   lived service FOMs should notified that they should stop by this
	   method.
	   The service will be in the M0_RST_STOPPING state when the method
	   is invoked.
	 */
	void (*rso_prepare_to_stop)(struct m0_reqh_service *service);

	/**
	   Performs service specific finalisation of objects.
	   Once stopped, no incoming request related to this service
	   on a node will be processed further.
	   Stopping a service can involve operations like finalising service
           specific fops, &c. This is invoked from m0_reqh_service_stop().
           Once the service specific objects are finalised, the service is
           unregistered from request handler.

	   The service will be in the M0_RST_STOPPED state when the method
	   is invoked.
	   @param service Service to be stopped

	   @see m0_reqh_service_stop()
	 */
	void (*rso_stop)(struct m0_reqh_service *service);

	/**
	   Destroys a particular service.
           This is invoked from m0_reqh_service_fini(). Initially generic part
           of the service is finalised, followed by the service specific
           finalisation.

	   @param service Service to be finalised

	   @see m0_reqh_service_fini()
	 */
	void (*rso_fini)(struct m0_reqh_service *service);

	/**
	   Method to periodically record ADDB statistics on the operation
	   of the service.
	   No statistics should be posted if there was no activity in the
	   service since the last invocation of the method.
	   The method is optional and need not be specified.
	 */
	void (*rso_stats_post_addb)(struct m0_reqh_service *service);

	/**
	   Method to determine if an incoming FOP for this service should
	   be accepted when the service is stopping or will shortly be
	   stopped.
	   The method is optional and need not be specified.
	   @see m0_reqh_fop_policy
	 */
	int (*rso_fop_accept)(struct m0_reqh_service *service,
			      struct m0_fop *fop);

};

/**
   Service type operations vector.
 */
struct m0_reqh_service_type_ops {
	/**
	   Allocates and initialises a service for the given service type.
	   This also initialises the corresponding service operations vector.
	   This is typically invoked during mero setup, but also can be
	   invoked later, in order to configure a particular service.
	   Once the service specific initialisation is done, generic
	   m0_reqh_service_init() routine is invoked.

	   @param service  Resulted service.
	   @param stype    Type of service being allocated.
	 */
	int (*rsto_service_allocate)(struct m0_reqh_service **service,
				     const struct m0_reqh_service_type *stype);
};

/**
   Represents a particular service type.  A m0_reqh_service_type instance is
   initialised and registered into a global list of service types as a part of
   corresponding module initialisation process.

   @see m0_reqh_service_type_init()
 */
struct m0_reqh_service_type {
	const char		              *rst_name;

	/** Service type operations.*/
	const struct m0_reqh_service_type_ops *rst_ops;

	/**
	 * Reqh key to store and locate m0_reqh_service instance.
	 * @see m0_reqh::rh_key
	 */
	unsigned                               rst_key;

	/** Key for per-locality-per-svc fom count. */
	unsigned                               rst_fomcnt_key;

	unsigned                               rst_level;

	/**
	   Pointer to ADDB context type for this service type
	 */
	struct m0_addb_ctx_type               *rst_addb_ct;

	/**
	    Linkage into global service types list.

	    @see m0_rstypes
	 */
	struct m0_tlink                        rst_linkage;
	uint64_t                               rst_magix;
};

/**
   Allocates and initialises service of given type.

   @pre  service != NULL && stype != NULL
   @post ergo(retval == 0, m0_reqh_service_invariant(service))

   @see struct m0_reqh_service_type_ops
 */
M0_INTERNAL int
m0_reqh_service_allocate(struct m0_reqh_service **service,
			 const struct m0_reqh_service_type *stype,
			 struct m0_reqh_context *rctx);

/**
   Searches a particular type of service by traversing global list of service
   types maintained by request handler module.

   @param sname, name of the service to be searched in global list

   @pre sname != NULL

   @see m0_reqh_service_init()

 */
M0_INTERNAL struct m0_reqh_service_type *
m0_reqh_service_type_find(const char *sname);

/**
   Transition a service into the starting state and initiate the
   asynchrous initialization of the service with the rso_start_async()
   operation.

   @param asc Asynchronous service context, duly initialized.
   @pre m0_reqh_service_state_get(service) == M0_RST_INITIALIZED
   @pre asc->sac_service->rs_ops->rso_start_async != NULL
   @post m0_reqh_service_state_get(service) == M0_RST_STARTING
 */
M0_INTERNAL int m0_reqh_service_start_async(struct
					    m0_reqh_service_start_async_ctx
					    *asc);

/**
   Complete the transition to the started state.
   The service gets registered with the request handler.
   @param service The service that has completed startup activities initiated
   with m0_reqh_service_start_async().
   @pre m0_reqh_service_state_get(service) == M0_RST_STARTING
   @post m0_reqh_service_state_get(service) == M0_RST_STARTED
 */
M0_INTERNAL void m0_reqh_service_started(struct m0_reqh_service *service);

/**
   Fail the service because it could not initialize itself.
   @pre m0_reqh_service_state_get(service) == M0_RST_STARTING
   @post m0_reqh_service_state_get(service) == M0_RST_FAILED
 */
M0_INTERNAL void m0_reqh_service_failed(struct m0_reqh_service *service);

/**
   Starts a particular service synchronously.
   Invokes service specific start routine, if service specific startup completes
   Successfully then the service is registered with the request handler and
   transitioned into M0_RST_STARTED state.

   @pre m0_reqh_service_state_get(service) == M0_RST_INITIALIZED
   @post m0_reqh_service_state_get(service) == M0_RST_STARTED

   @see struct m0_reqh_service_ops
 */
M0_INTERNAL int m0_reqh_service_start(struct m0_reqh_service *service);

/**
   Transitions the service to the M0_RST_STOPPING state and invoke its
   rso_prepare_to_stop() method if it is defined.
   It is a no-op if the state was M0_RST_STOPPING on entry.
   @pre service != NULL
   @pre M0_IN(m0_reqh_service_state_get(service),
                   (M0_RST_STARTED, M0_RST_STOPPING))
   @post m0_reqh_service_state_get(service) == M0_RST_STOPPING
 */
M0_INTERNAL void m0_reqh_service_prepare_to_stop(struct m0_reqh_service
						 *service);

/**
   Stops a particular service.
   Transitions the service to M0_RST_STOPPED state.  The service is still
   registered with the request handler.

   @param service Service to be stopped

   @pre service != NULL
   @pre M0_IN(m0_reqh_service_state_get(service),
                   (M0_RST_STARTED, M0_RST_STOPPING))
   @post m0_reqh_service_state_get(service) == M0_RST_STOPPED

   @see struct m0_reqh_service_ops
   @see m0_reqh_service_prepare_to_stop()
   @see m0_reqh_services_terminate()
 */
M0_INTERNAL void m0_reqh_service_stop(struct m0_reqh_service *service);

/**
   Performs generic part of service initialisation.
   Transitions service into M0_RST_INITIALISED state
   This is invoked after the service specific init routine returns successfully.

   @param service service to be initialised
   @param reqh Request handler
   @param uuid Pointer to service UUID or NULL if not known.

   @pre service != NULL && reqh != NULL &&
        service->rs_sm.sm_state == M0_RST_INITIALISING

   @see struct m0_reqh_service_type_ops
   @see cs_service_init()
 */
M0_INTERNAL void m0_reqh_service_init(struct m0_reqh_service *service,
				      struct m0_reqh *reqh,
				      const struct m0_uint128 *uuid);

/**
   Performs generic part of service finalisation, including deregistering
   the service with its request handler.
   This is invoked before service specific finalisation routine.

   @param service Service to be finalised

   @pre service != NULL

   @see struct m0_reqh_service_ops
   @see m0_reqh_services_terminate()
 */
M0_INTERNAL void m0_reqh_service_fini(struct m0_reqh_service *service);

/** @todo Add NULL check for 'ct' the below comment
    when each service type has addb_ctx_type defined
    ADDB context type has to be registered before
    invoking m0_reqh_service_type_register()
*/
#define M0_REQH_SERVICE_TYPE_DEFINE(stype, ops, name, ct, level)  \
struct m0_reqh_service_type stype = {                             \
	.rst_name    = (name),	                                  \
	.rst_ops     = (ops),                                     \
	.rst_addb_ct = (ct),                                      \
	.rst_level   = (level),                                   \
}

/**
   Registers a service type in a global service types list,
   i.e. rstypes.

   @pre rstype != NULL && rstype->rst_magix == M0_RHS_MAGIC
 */
int m0_reqh_service_type_register(struct m0_reqh_service_type *rstype);

/**
   Unregisters a service type from a global service types list, i.e. rstypes.

   @pre rstype != NULL
 */
void m0_reqh_service_type_unregister(struct m0_reqh_service_type *rstype);

/**
   Initialises global list of service types.
   This is invoked from m0_reqhs_init().
 */
M0_INTERNAL int m0_reqh_service_types_init(void);

/**
   Finalises global list of service types.
   This is invoked from m0_reqhs_fini();
 */
M0_INTERNAL void m0_reqh_service_types_fini(void);

/**
   Checks consistency of a particular service.
 */
M0_INTERNAL bool m0_reqh_service_invariant(const struct m0_reqh_service
					   *service);

/**
   Returns service instance of the given service type.
 */
M0_INTERNAL struct m0_reqh_service *
m0_reqh_service_find(const struct m0_reqh_service_type *st,
		     const struct m0_reqh              *reqh);

M0_INTERNAL int m0_reqh_service_types_length(void);
M0_INTERNAL bool m0_reqh_service_is_registered(const char *sname);
M0_INTERNAL void m0_reqh_service_list_print(void);
M0_INTERNAL int m0_reqh_service_state_get(const struct m0_reqh_service *s);

/**
 * A helper function that allocates, initialises and starts a service of the
 * given type.
 */
M0_INTERNAL int m0_reqh_service_setup(struct m0_reqh_service **out,
				      struct m0_reqh_service_type *stype,
				      struct m0_reqh *reqh,
				      struct m0_reqh_context *rctx,
				      const struct m0_uint128 *uuid);

/** Dual to m0_reqh_service_setup(), stops and finalises the service. */
M0_INTERNAL void m0_reqh_service_quit(struct m0_reqh_service *svc);

/**
 * Implementation of simple .rso_start_async().
 *
 * @note:
 * Currently all services uses simple async service start
 * which is not really async implementation. But later it
 * may require to implement async service startup.
 */
M0_INTERNAL int
m0_reqh_service_async_start_simple(struct m0_reqh_service_start_async_ctx *asc);

/** @} endgroup reqhservice */
#endif /* __MERO_REQH_REQH_SERVICE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
