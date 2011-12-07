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
#include "rpc/rpc2.h"

/**
   @addtogroup reqh

   A colibri service is described to a request handler using a struct
   c2_reqh_service_type data structure.
   Every service should register its corresponding c2_reqh_service_type
   instance containing service specific initialisation method, service
   name. A c2_reqh_service_type instance can be declared using
   C2_REQH_SERVICE_TYPE_DECLARE macro. Once the service type is declared
   it should be registered using c2_reqh_service_type_register() and
   unregister using c2_reqh_service_type_unregister().
   During service type registration, the service type is added to the global
   list of the service types maintained by the request handler module.
   The request handler creates and initialises a service by invoking the
   constructor method of its service type, and obtains struct c2_reqh_service
   instance.The constructor should perform only internal house keeping tasks.
   Next, the service start method is invoked, it should properly initialise the
   internal state of the service, e.g. service fops. &tc.

   Request handler creates an rpcmachine for each specified end point per
   network domain. There could be multiple rpc machines running within a single
   request handler, resulting in the associated services being reachable through
   all of these end points.

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
	...
   }

   static int dummy_service_stop(struct c2_reqh_service *service)
   {
	...
        dummy_fops_fini();
	...
   }
   @endcode

   - declare service type using C2_REQH_SERVICE_TYPE_DECLARE macro,
   @code
   C2_REQH_SERVICE_TYPE_DECLARE(dummy_service_type, &dummy_service_type_ops,
                                                                     "dummy");
   @endcode

   - now, the above service type can be registered as below,
   @code
   c2_reqh_service_type_register(&dummy_service_type);
   @endcode

   - unregister service using c2_reqh_service_type_unregister().

   A typical service transitions through its phases as below,
   @verbatim

     cs_service_init()
          |
          v                 allocated
     rsto_service_alloc_and_init()+---->C2_RSPH_INITIALISING
                                            |rs_state = C2_RSS_UNDEFINED
                                            | c2_reqh_service_init()
                                            v
                                        C2_RSPH_INITIALISED
                                            | rs_state = C2_RSS_READY
                                            | rso_start()
                    start up failed         v
        +------------------------------+C2_RSPH_STARTING
        |              rc != 0              |
	|                                   | c2_reqh_service_start()
        v                                   v
   C2_RSPH_FAILED                          C2_RSPH_STARTED
        |                                   | rs_state = C2_RSS_RUNNING
        v                                   | rso_stop()
    rso_fini()                              v
        ^                               C2_RSPH_STOPPING
        |                                   |
        |                                   | c2_reqh_service_stop()
        |     c2_reqh_service_fini()        v rs_state = C2_RSS_STOPPED
	+------------------------------+C2_RSPH_STOPPED

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
	   A service is in C2_RSPH_INITIALISING state when it is allocated
	   in service specific start routine, once the service specific
	   initialisation is complete, generic c2_reqh_service_init() is invoked.
	 */
	C2_RSPH_INITIALISING,
	/**
	   A service transitions to C2_RSPH_INITIALISED state, once it is
	   successfully initialised by the generic service c2_reqh_service_init()
	   routine.

	   @see c2_reqh_service_type_ops
	 */
	C2_RSPH_INITIALISED,
	/**
	   A service transitions to C2_RSPH_STARTING phase before service
	   specific start routine is invoked from cs_service_init() while
	   configuring a service during colibri setup.

	   @see struct c2_reqh_service_ops
	 */
	C2_RSPH_STARTING,
	/**
	   A service transitions to C2_RSPH_STARTED phase after completing
	   generic part of service start up operations by c2_reqh_service_start().
	   once service specific start up operations are completed, generic
	   service start routine c2_reqh_service_start() is invoked.

	   @see c2_reqh_service_start()
	 */
	C2_RSPH_STARTED,
	/**
	   A service transitions to C2_RSPH_STOPPING phase before service
	   specific stop routine is invoked from cs_service_fini().
	   A service should be in C2_RSS_RUNNING state to proceed for
	   finalisation.

	   @see c2_reqh_service_state
	   @see c2_reqh_service_stop()
	 */
	C2_RSPH_STOPPING,
	/**
	   A service is transitions to C2_RSPH_STOPPED phase once the
	   generic service c2_reqh_service_stop() routine completes successfully.
	 */
	C2_RSPH_STOPPED,
	/**
	   A service transitions to C2_RSPH_FAILED phase if the service start up
	   fails.
	 */
	C2_RSPH_FAILED
};

/**
   States a service can be throughout its lifecycle.
 */
enum c2_reqh_service_state {
	/**
	   A service is in C2_RSS_UNDEFINED state after its allocation and before
	   it is initialised.
	 */
	C2_RSS_UNDEFINED,
	/**
	   A service is in C2_RSS_READY state once it is successfully initialised.
	 */
	C2_RSS_READY,
	/**
	   A service is in C2_RSS_RUNNING state once it is successfully started.
	 */
	C2_RSS_RUNNING,
	/**
	   A service is in C2_RSS_STOPPED state once it is successfully stopped.
	 */
	C2_RSS_STOPPED
};

/**
   Represents a service on node.
   Multiple services in a colibri address space share the same request handler.
   There exist a list of services in struct c2_reqh, c2_reqh::rh_services,
   sharing the same request handler.

 */
struct c2_reqh_service {
	/**
	   Service id that should be unique throughout the cluster.
	   Currently generating service uuid using the service name and local
	   time stamp.
	 */
	char                              rs_uuid[C2_REQH_SERVICE_UUID_SIZE];

	/**
	   Service type specific structure to hold service specific
	   implementations of its operations.
	   This can be used to initialise service specific objects such as fops.
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
	   Performs service specific startup operations.
	   Once started, incoming requests related to this service are ready
	   to be processed by the corresponding request handler.
	   Service startup can involve operations like initialising service
	   specific fops, &tc which may fail due to whichever reason, in that
	   case the service is finalised and appropriate error is returned.
	   Once the service specific startup operations are performed, the
	   service is added to the request handler's list of services by
	   the generic c2_reqh_service_start() routine invoked after this
	   routine returns successfully.

	   @param service Service to be started

	   @see c2_reqh_service_start()
	 */
	int (*rso_start)(struct c2_reqh_service *service);

	/**
	   Performs services specific finalisation of objects.
	   Once stopped, no incoming request related to this service
	   on a node will be processed further.
	   Stopping a service can involve operations like finalising
	   service specific fops, &tc.
	   Once the service specific objects are finalised, generic
	   c2_reqh_service_stop() routine is invoked after this routine
	   returns successfully, this removes the service from request
	   handler's list of services.

	   @param service Service to be started

	   @see c2_reqh_service_stop()
	 */
	void (*rso_stop)(struct c2_reqh_service *service);

	/**
	   Destroys a particular service.
	   Firstly a generic c2_reqh_service_fini() is invoked, from
	   colibri_setup service finalisation routine, which performs
	   generic part of service finalisation and then follows the
	   service specific finalisation.

	   @param service Service to be finalised

	   @see cs_service_fini()
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
	   Allocates and initialises a service with the given service type and
	   corresponding service operations vector. This is invoked during
	   colibri startup to configure a particular service. Once the service
	   specific initialisation is done, generic c2_reqh_service_init()
	   routine is invoked after this routine returns successfully.

	   @param service Service to be allocated and initialised
	   @param stype Type of service to be initialised

	   @see c2_reqh_service_init()
	 */
	int (*rsto_service_alloc_and_init)(struct c2_reqh_service_type *stype,
					struct c2_reqh_service **service);
};

/**
   Represents a particular service type.
   A c2_reqh_service_type instance is initialised and registered into a global
   list of service types as a part of corresponding module initiasation process.

   @see c2_reqh_service_type_init()
 */
struct c2_reqh_service_type {
	const char		              *rst_name;

	/** Service type operations.*/
	const struct c2_reqh_service_type_ops *rst_ops;

	/**
	    Linkage into global service types list.

	    @see c2_rstypes
	 */
	struct c2_tlink                        rst_linkage;
	uint64_t                               rst_magic;
};

/**
   Locates a particular type of service by traversing global list of service
   types maintained by request handler module.

   @param sname, name of the service to be searched in global list

   @pre sname != NULL

   @see c2_reqh_service_init()

 */
struct c2_reqh_service_type *c2_reqh_service_type_find(const char *sname);

/**
   Starts a particular service.
   Registers a service with the request handler and transitions the service
   into C2_RSPH_STARTED phase and changes service state to C2_RSS_RUNNING.
   This is invoked after service specific start routine returns successfully.

   @param service Service to be started

   @pre service != NULL

   @see struct c2_reqh_service_ops
   @see c2_service_init()

   @post c2_reqh_service_invariant(service)
 */
int c2_reqh_service_start(struct c2_reqh_service *service);

/**
   Stops a particular service.
   Unregisters a service from the request handler and transitions service to
   C2_RSPH_STOPPED phase and state is changed to C2_RSS_STOPPED.
   This is invoked after service specific stop routine returns successfully.

   @param service Service to be stopped

   @pre service != NULL

   @see struct c2_reqh_service_ops
   @see cs_service_fini()
 */
void c2_reqh_service_stop(struct c2_reqh_service *service);

/**
   Performs generic part of service initialisation.
   Transitions service into C2_RSPH_INITIALISED phase and changes service state to
   C2_RSS_READY.
   This is invoked after the service specific init routine returns successfully.

   @param service service to be initialised

   @pre service != NULL && reqh != NULL && service->rs_state == C2_RSS_UNDEFINED &&
	service->rs_phase == C2_RSPH_INITIALISING

   @see struct c2_reqh_service_type_ops
   @see cs_service_init()
 */
int c2_reqh_service_init(struct c2_reqh_service *service, struct c2_reqh *reqh);

/**
   Performs generic part of service finalisation.
   This is invoked before service specific finalisation routine.

   @param service Service to be finalised

   @pre service != NULL && service->rs_phase == C2_RSPH_STOPPED &&
        service->rs_state == C2_RSS_STOPPED

   @see struct c2_reqh_service_ops
   @see cs_service_fini()
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
   Unregisters a service type from a global service types list, i.e. rstypes.

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
