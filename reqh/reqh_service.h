/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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

#ifndef __COLIBRI_REQH_REQH_SERVICE_H__
#define __COLIBRI_REQH_REQH_SERVICE_H__

#include "lib/list.h"
#include "net/net.h"
#include "rpc/rpccore.h"

/**
   @addtogroup reqh

   A simplistic representation of a service running on a particular
   node in cluster.
   Every module should register its corresponding c2_reqh_service_type
   instance containing service specific initialisation method, service
   name. A c2_reqh_service_type instance cna be declared using
   C2_REQH_SERVICE_TYPE_DECLARE macro. Once the service type is declared
   it should be registered using c2_reqh_service_type_register() and
   unregister using c2_reqh_service_type_unregister().
   As part of service type registration , the service type is added global
   list of the same.
   During initialisation of a service, a service type is located using the
   service name from the global list of service types. Once located, service
   specific init routine is invoked, which initialises the same. Once the
   service is successfuly initialised, its pre registered start routine is
   invoked which initialises service related objects (e.g. fops).
   As part of service start up the, service is registered with the request
   handler.
   There is a rpcmachine created per end point per network domain, thus
   multiple rpc machines can be running within a request handler, thus all
   the services registered with that request handler are reachable through
   the registered end points.

   Services need to be registered before they can be started. Service
   registration can be done as below,

   First, we have to define service type operations,
   @code
   static const struct c2_reqh_service_type_ops dummy_service_type_ops = {
        .rsto_service_alloc_and_init = dummy_service_alloc_init
   };
   @endcode

   - then define service operations,
   @code
   const struct c2_reqh_service_ops dummy_service_ops = {
        .rso_start = dummy_service_start,
        .rso_stop = dummy_service_stop,
        .rso_fini = dummy_service_fini
   };
   @endcode

   Typical service specific start and stop operations may look like below,
   @code
   static int dummy_service_start(struct c2_reqh_service *service)
   {
        ...
        rc = dummy_fops_init();
        if (rc != 0)
		c2_reqh_service_start(service);
	...
   }

   static int dummy_service_stop(struct c2_reqh_service *service)
   {
	...
        dummy_fops_fini();
        c2_reqh_service_stop(service);
	...
   }
   @endcode

   - declare service type using C2_REQH_SERVICE_TYPE_DECLARE macro,
   @code
   C2_REQH_SERVICE_TYPE_DECLARE(dummy_service_type, &dummy_service_type_ops, "dummy");
   @endcode

   - now, the above service type can be registered as below,
   @code
   c2_reqh_service_type_register(&dummy_service_type);
   @endcode

   - unregister service using c2_reqh_service_type_unregister().

   A typical service transitions through its phases as below,
   @verbatim
                                                    allocated                                         
   cs_service_init()---->rsto_service_alloc_and_init()+---->RH_SERVICE_INITIALISING
                                                                      | rs_state = RH_SERVICE_UNDEFINED
            							      | c2_reqh_service_init()
            					                      v  
                                                            RH_SERVICE_INITIALISED
      							              | rs_state = RH_SERVICE_READY
                                                                      | rso_start()
                    start up failed                                   v
      	+-------------------------------------------------+ RH_SERVICE_STARTING
      	|                       rc != 0                               |
	|                                                             | c2_reqh_servie_start()
        v                                                             v
   RH_SERVICE_FAILED                                          RH_SERVICE_STARTED
        |                                                             | rs_state = RH_SERVICE_RUNNING
        v                                                             | rso_stop()
    rso_fini()                                                        v
        ^                                                    RH_SERVICE_STOPPING
        |                                                             |
        |                                                             | c2_reqh_service_stop()
        |                                                             v
	+--------------------------------------------------+ RH_SERVICE_STOPPED

   @endverbatim

   @{ 
 */

enum {
	C2_REQH_SERVICE_UUID_SIZE = 64,
};

/**
   Magic for reqh service
 */
enum {
	/** Hex value for "reqhsvcs" */
	C2_RHS_MAGIC = 0x7265716873766373
};

/**
   Phases through which a service typically passes.
 */
enum c2_reqh_service_phase {
	/**
	   A service is in RH_SERVICE_INITIALISING state when it is allocated
	   in service specific start routine, once the service specific
	   initialisation is complete, generic c2_reqh_service_init() is invoked.
	 */
	RH_SERVICE_INITIALISING,
	/**
	   A service transitions to RH_SERVICE_INITIALISED state, once it is
	   successfuly initialised by the generic service c2_reqh_service_init()
	   routine.

	   @see c2_reqh_service_type_ops
	 */
	RH_SERVICE_INITIALISED,
	/**
	   A service transitions to RH_SERVICE_STARTING phase before service
	   specific start routine is invoked from cs_service_init() while
	   configuring a service during colibri setup.

	   @see struct c2_reqh_service_ops
	 */
	RH_SERVICE_STARTING,
	/**
	   A service transitions to RH_SERVICE_STARTED phase after completing
	   generic part of service start up operations by c2_reqh_service_start().
	   once service specific start up operations are completed, generic
	   service start routine c2_reqh_service_start() is invoked.

	   @see c2_reqh_service_start()
	 */
	RH_SERVICE_STARTED,
	/**
	   A service transitions to RH_SERVICE_STOPPING phase before service
	   specific stop routine is invoked from cs_service_fini().
	   A service should be in RH_SERVICE_RUNNING state to proceed for
	   finalisation.

	   @see c2_reqh_service_state
	   @see c2_reqh_service_stop()
	 */
	RH_SERVICE_STOPPING,
	/**
	   A service is transitions to RH_SERVICE_STOPPED phase once the
	   generic service c2_reqh_service_stop() routine completes successfuly.
	 */
	RH_SERVICE_STOPPED,
	/**
	   A service transitions to RH_SERVICE_FAILED phase if the service
	   start up fails.
	 */
	RH_SERVICE_FAILED
};

/**
   States a service can be throughout its lifecycle.
 */
enum c2_reqh_service_state {
	/**
	   A service is in RH_SERVICE_UNDEFINED state after its allocation
	   and before it is initialised.
	 */
	RH_SERVICE_UNDEFINED,
	/**
	   A service is in RH_SERVICE_READY state once it is successfuly
	   initialised.
	 */
	RH_SERVICE_READY,

	/**
	   A service is in RH_SERVICE_RUNNING state once it is successfuly
	   started.
	 */
	RH_SERVICE_RUNNING
};

/**
   Represents a service on node.
   Multiple services in a colibri address space share the same
   request handler. There exist a list of services in struct c2_reqh,
   c2_reqh::rh_services, sharing the same request handler.

 */
struct c2_reqh_service {
	/**
	   Service id that should be unique throughout the cluster.
	   Currently generating service uuid using the service name
	   and local time stamp.
	 */
	char                              rs_uuid[C2_REQH_SERVICE_UUID_SIZE];

	/**
	   Service type specific structure to hold service
	   specific implementations of its operations.
	   This can be used to initialise service specific
	   objects such as fops.
	 */
	struct c2_reqh_service_type      *rs_type;

	/**
	   Current phase of a service.

	   @see c2_reqh_service_phase
	 */
	enum c2_reqh_service_phase        rs_phase;

	/**
	   Current state of a service.

	   @see c2_reqh_service_state
	 */
	enum c2_reqh_service_state        rs_state;

	/**
	   Protects service state transitions.
	 */
	struct c2_mutex                   rs_mutex;

	/**
	   Service specific operations vector.
	 */
	const struct c2_reqh_service_ops *rs_ops;

	/**
	   Request handler this service belongs to.
	 */
	struct c2_reqh                   *rs_reqh;

	/**
	   Linkage into list of services in request handler.

	   @see c2_reqh::rh_services
	 */
	struct c2_tlink                   rs_linkage;

	/**
	   Service magic to check consistency of service instance.
	 */
	uint64_t                          rs_magic;
};

/**
   Service specific operations vector.
 */
struct c2_reqh_service_ops {
	/**
	   Performs service specific startup operations and invokes
	   generic c2_reqh_service_start(), which registers the service
	   with the given request handler.
	   Once started, incoming requests related to this service are
	   ready to be processed by the corresponding request handler.
	   Service startup can involve operations like initialising service
	   specific fops, which may fail due to whichever reason, in that
	   case the service is finalised and appropriate error is returned.

	   @param service Service to be started

	   @see c2_reqh_service_start()
	 */
	int (*rso_start)(struct c2_reqh_service *service);

	/**
	   Performs services specific finalisation of objects and invokes
	   generic c2_reqh_service_stop(), no incoming request related
	   to this service on a node should be processed further.
	   Stopping a service can involve operations like finalising
	   service specific fops.
	   A service should not proceed for finalisation until its
	   c2_reqh_service::rs_busy_ref_cnt is 0.

	   @param service Service to be started

	   @see c2_reqh_service_stop()
	 */
	int (*rso_stop)(struct c2_reqh_service *service);

	/**
	   Destroys a particular service.
	   Firstly a generic c2_reqh_service_fini() is invoked,
	   which performs generic part of service finalisation
	   and then follows the service specific finalisation.

	   @param service Service to be finalised

	   @see c2_reqh_service_fini()
	 */
	void (*rso_fini)(struct c2_reqh_service *service);
};

/**
   Service type operations vector.
 */
struct c2_reqh_service_type_ops {
	/**
	   Contructs a particular service.
	   Allocates and initialises a service with the
	   given service type and corresponding service
	   operations vector. This is invoked during
	   colibri startup to configure a particular service,
	   once the allocation and service specific initialisation
	   is done, this invokes a generic c2_reqh_service_init()
	   routine to initialise generic part of a service.

	   @param service Service to be allocated and initialised
	   @param stype Type of service to be initialised

	   @see c2_reqh_service_init()
	 */
	int (*rsto_service_alloc_and_init)(struct c2_reqh_service_type *stype,
			struct c2_reqh *reqh, struct c2_reqh_service **service);
};

/**
   Represents a particular service type.
   A c2_reqh_service_type instance is initialised
   and registered into a global list of service types as
   a part of corresponding module initiasation process.

   @see c2_reqh_service_type_init()
 */
struct c2_reqh_service_type {
	const char		              *rst_name;
	const struct c2_reqh_service_type_ops *rst_ops;
	struct c2_tlink                        rst_linkage;
	uint64_t                               rst_magic;
};

/**
   Locates a particular type of service.
   This is invoked while initialising a particular service.

   @param sname, name of the service to be searched in global list

   @pre sname != NULL

   @see c2_reqh_service_init()

 */
struct c2_reqh_service_type *c2_reqh_service_type_find(const char *sname);

/**
   Starts a particular service.
   Registers a service with the request handler and transitions
   the service into RH_SERVICE_STARTED phase and changes service
   state to RH_SERVICE_RUNNING. This is invoked from service
   specific start routine.

   @param service Service to be started

   @pre service != NULL

   @post c2_reqh_service_invariant(service)
 */
int c2_reqh_service_start(struct c2_reqh_service *service);

/**
   Stops a particular service.
   Unregisters a service from the request handler and
   transitions service to RH_SERVICE_STOPPED phase.
   This is invoked from service specific stop routine.

   @param service Service to be stopped

   @pre service != NULL
 */
void c2_reqh_service_stop(struct c2_reqh_service *service);

/**
   Performs generic part of service initialisation.
   Transitions service into RH_SERVICE_INITIALISED phase
   and changes service state to RH_SERVICE_READY.
   This is invoked from the service specific init routine.

   @param service service to be initialised

   @pre service != NULL && reqh != NULL &&
        service->rs_state == RH_SERVICE_UNDEFINED &&
	service->rs_phase == RH_SERVICE_INITIALISING

   @see struct c2_reqh_service_type_ops
 */
int c2_reqh_service_init(struct c2_reqh_service *service, struct c2_reqh *reqh);

/**
   Performs generic part of service finalisation.
   This is invoked from service specific fini routine.

   @param service Service to be finalised

   @pre service != NULL && service->rs_phase == RH_SERVICE_STOPPED

   @see struct c2_reqh_service_ops
 */
void c2_reqh_service_fini(struct c2_reqh_service *service);

#define C2_REQH_SERVICE_TYPE_DECLARE(stype, ops, name) \
struct c2_reqh_service_type stype = {                  \
        .rst_name  = (name),	                       \
	.rst_ops   = (ops),                            \
	.rst_magic = (C2_RHS_MAGIC)                    \
}                                                     \

/**
   Registers a service type in a global service types list,
   i.e. rstypes.

   @pre rstype != NULL && rstype->rst_magic == C2_RHS_MAGIC
 */
int c2_reqh_service_type_register(struct c2_reqh_service_type *rstype);

/**
   Unregisters a service type from a global service types list,
   i.e. rstypes.

   @pre rstype != NULL
 */
void c2_reqh_service_type_unregister(struct c2_reqh_service_type *rstype);

/**
   Initialises global list of service types.
   This is invoked from c2_reqhs_init().
 */
int c2_reqh_service_types_init();

/**
   Finalises global list of service types.
   This is invoked from c2_reqhs_fini();
 */
void c2_reqh_service_types_fini();

/**
   Checks consistency of a particular service.
 */
bool c2_reqh_service_invariant(const struct c2_reqh_service *service);

/** @} endgroup reqh */

/* __COLIBRI_REQH_REQH_SERVICE_H__ */

#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
