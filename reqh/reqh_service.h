/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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

#ifndef __COLIBRI_REQH_REQH_SERVICE_H__
#define __COLIBRI_REQH_REQH_SERVICE_H__

#include "lib/tlist.h"
#include "lib/bob.h"
#include "lib/mutex.h"


/**
   @defgroup reqhservice Request handler service

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

   Request handler creates an rpc_machine for each specified end point per
   network domain. There could be multiple rpc machines running within a single
   request handler, resulting in the associated services being reachable through
   all of these end points.

   Services need to be registered before they can be started. Service
   registration can be done as below,

   First, we have to define service type operations,
   @code
   static const struct c2_reqh_service_type_ops dummy_service_type_ops = {
        .rsto_service_allocate = dummy_service_allocate
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

   A typical service transitions through its states as below,
   @verbatim

     cs_service_init()
          |
          v                 allocated
     rsto_service_allocate()+------------>C2_RST_INITIALISING
                                            |
                                            | c2_reqh_service_init()
                                            v
                                        C2_RST_INITIALISED
                                            |
                                            | rso_start()
                    start up failed         v
        +------------------------------+C2_RST_STARTING
        |              rc != 0              |
	|                                   | c2_reqh_service_start()
        v                                   v
   C2_RST_FAILED                          C2_RST_STARTED
        |                                   |
        v                                   | rso_stop()
    rso_fini()                              v
        ^                               C2_RST_STOPPING
        |                                   |
        |                                   | c2_reqh_service_stop()
        |     c2_reqh_service_fini()        v
	+------------------------------+C2_RST_STOPPED

   @endverbatim

   @{
 */

enum {
	C2_REQH_SERVICE_UUID_SIZE = 64,
};

/**
   Phases through which a service typically passes.
 */
enum c2_reqh_service_state {
	/**
	   A service is in C2_RST_INITIALISING state when it is created.
	   in service specific start routine, once the service specific
	   initialisation is complete, generic part of service is initialised.

           @see c2_reqh_service_allocate()
	 */
	C2_RST_INITIALISING,
	/**
	   A service transitions to C2_RST_INITIALISED state, once it is
	   successfully initialised.

	   @see c2_reqh_service_init()
	 */
	C2_RST_INITIALISED,
	/**
	   A service transitions to C2_RST_STARTING state before service
	   specific start routine is invoked.

	   @see c2_reqh_service_start()
	 */
	C2_RST_STARTING,
	/**
	   A service transitions to C2_RST_STARTED state after completing
	   generic part of service start up operations by c2_reqh_service_start().
	 */
	C2_RST_STARTED,
	/**
	   A service transitions to C2_RST_STOPPING state before service specific
           stop routine is invoked.

	   @see c2_reqh_service_stop()
	 */
	C2_RST_STOPPING,
	/**
	   A service transitions to C2_RST_STOPPED state, once service specific
           stop routine completes successfully and after it is unregistered from
           the request handler.
	 */
	C2_RST_STOPPED,
	/**
	   A service transitions to C2_RST_FAILED state if the service start up
	   fails.
	 */
	C2_RST_FAILED
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
	uint64_t                          rs_magix;
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
           This is invoked from c2_reqh_service_start(). Once the service
           specific startup operations are performed, the service is registered
           with the request handler.

	   @param service Service to be started

	   @see c2_reqh_service_start()
	 */
	int (*rso_start)(struct c2_reqh_service *service);

	/**
	   Performs service specific finalisation of objects.
	   Once stopped, no incoming request related to this service
	   on a node will be processed further.
	   Stopping a service can involve operations like finalising service
           specific fops, &tc. This is invoked from c2_reqh_service_stop().
           Once the service specific objects are finalised, the service is
           unregistered from request handler.

	   @param service Service to be started

	   @see c2_reqh_service_stop()
	 */
	void (*rso_stop)(struct c2_reqh_service *service);

	/**
	   Destroys a particular service.
           This is invoked from c2_reqh_service_fini(). Initialy generic part
           of the service is finalised, followed by the service specific
           finalisation.

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
	   Allocates and initialises a service for the given service type.
	   This also initialises the corresponding service operations vector.
           This is typically invoked  during colibri setup, but also can be
           invoked later, in-order to configure a particular service. Once the
           service specific initialisation is done, generic c2_reqh_service_init()
           routine is invoked.

	   @param stype Type of service to be located
	   @param service successfully located service
	 */
	int (*rsto_service_allocate)(struct c2_reqh_service_type *stype,
				     struct c2_reqh_service **service);
};

/**
   Represents a particular service type.  A c2_reqh_service_type instance is
   initialised and registered into a global list of service types as a part of
   corresponding module initialisation process.

   @see c2_reqh_service_type_init()
 */
struct c2_reqh_service_type {
	const char		              *rst_name;

	/** Service type operations.*/
	const struct c2_reqh_service_type_ops *rst_ops;

	/**
	 * Reqh key to store and locate c2_reqh_service instance.
	 * @see c2_reqh::rh_key
	 */
	unsigned                               rst_key;

	/**
	    Linkage into global service types list.

	    @see c2_rstypes
	 */
	struct c2_tlink                        rst_linkage;
	uint64_t                               rst_magix;
};

/**
   Locates a particular type of service.
   Invokes service type specific locate routine.

   @param stype Type of service to be located
   @param service out parameter containing located service

   @pre service != NULL
   @post c2_reqh_service_invariant(service)

   @see struct c2_reqh_service_type_ops
 */
C2_INTERNAL int c2_reqh_service_allocate(struct c2_reqh_service_type *stype,
					 struct c2_reqh_service **service);

/**
   Searches a particular type of service by traversing global list of service
   types maintained by request handler module.

   @param sname, name of the service to be searched in global list

   @pre sname != NULL

   @see c2_reqh_service_init()

 */
C2_INTERNAL struct c2_reqh_service_type *c2_reqh_service_type_find(const char
								   *sname);

/**
   Starts a particular service.
   Invokes service specific start routine, if service specific startup completes
   Successfully then the service is registered with the request handler and
   transitioned into C2_RST_STARTED state.

   @param service Service to be started

   @pre service != NULL
   @post c2_reqh_service_invariant(service)

   @see struct c2_reqh_service_ops
 */
C2_INTERNAL int c2_reqh_service_start(struct c2_reqh_service *service);

/**
   Stops a particular service.
   Unregisters a service from the request handler and transitions service to
   C2_RST_STOPPED state.
   This is invoked after service specific stop routine returns successfully.

   @param service Service to be stopped

   @pre service != NULL

   @see struct c2_reqh_service_ops
   @see cs_service_fini()
 */
C2_INTERNAL void c2_reqh_service_stop(struct c2_reqh_service *service);

/**
   Performs generic part of service initialisation.
   Transitions service into C2_RST_INITIALISED state
   This is invoked after the service specific init routine returns successfully.

   @param service service to be initialised

   @pre service != NULL && reqh != NULL && service->rs_state == C2_RST_INITIALISING

   @see struct c2_reqh_service_type_ops
   @see cs_service_init()
 */
C2_INTERNAL void c2_reqh_service_init(struct c2_reqh_service *service,
				      struct c2_reqh *reqh);

/**
   Performs generic part of service finalisation.
   This is invoked before service specific finalisation routine.

   @param service Service to be finalised

   @pre service != NULL && service->rs_state == C2_RST_STOPPED

   @see struct c2_reqh_service_ops
   @see cs_service_fini()
 */
C2_INTERNAL void c2_reqh_service_fini(struct c2_reqh_service *service);

#define C2_REQH_SERVICE_TYPE_DECLARE(stype, ops, name) \
struct c2_reqh_service_type stype = {                  \
        .rst_name  = (name),	                       \
	.rst_ops   = (ops),                            \
}                                                     \

/**
   Registers a service type in a global service types list,
   i.e. rstypes.

   @pre rstype != NULL && rstype->rst_magix == C2_RHS_MAGIC
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
C2_INTERNAL int c2_reqh_service_types_init(void);

/**
   Finalises global list of service types.
   This is invoked from c2_reqhs_fini();
 */
C2_INTERNAL void c2_reqh_service_types_fini(void);

/**
   Checks consistency of a particular service.
 */
C2_INTERNAL bool c2_reqh_service_invariant(const struct c2_reqh_service
					   *service);

/**
   Returns service intance of the given service type using the reqhkey
   framework.

   @see c2_reqh::rh_key
 */
C2_INTERNAL struct c2_reqh_service *c2_reqh_service_find(const struct
							 c2_reqh_service_type
							 *st,
							 struct c2_reqh *reqh);

C2_INTERNAL int c2_reqh_service_types_length(void);
C2_INTERNAL bool c2_reqh_service_is_registered(const char *sname);
C2_INTERNAL void c2_reqh_service_list_print(void);

/** @} endgroup reqhservice */

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
