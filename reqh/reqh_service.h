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

#ifndef __MERO_REQH_REQH_SERVICE_H__
#define __MERO_REQH_REQH_SERVICE_H__

#include "lib/tlist.h"
#include "lib/bob.h"
#include "lib/mutex.h"


/**
   @defgroup reqhservice Request handler service

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
   static const struct m0_reqh_service_type_ops dummy_stype_ops = {
        .rsto_service_allocate = dummy_service_allocate
   };
   @endcode

   - then define service operations,
   @code
   const struct m0_reqh_service_ops dummy_service_ops = {
        .rso_start = dummy_service_start,
        .rso_stop = dummy_service_stop,
        .rso_fini = dummy_service_fini
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
   M0_REQH_SERVICE_TYPE_DEFINE(dummy_stype, &dummy_stype_ops, "dummy");
   @endcode

   - now, the above service type can be registered as below,
   @code
   m0_reqh_service_type_register(&dummy_stype);
   @endcode

   - unregister service using m0_reqh_service_type_unregister().

   A typical service transitions through its states as below,
   @verbatim

     cs_service_init()
          |
          v                 allocated
     rsto_service_allocate()+------------>M0_RST_INITIALISING
                                            |
                                            | m0_reqh_service_init()
                                            v
                                        M0_RST_INITIALISED
                                            |
                                            | rso_start()
                    start up failed         v
        +------------------------------+M0_RST_STARTING
        |              rc != 0              |
	|                                   | m0_reqh_service_start()
        v                                   v
   M0_RST_FAILED                          M0_RST_STARTED
        |                                   |
        v                                   | rso_stop()
    rso_fini()                              v
        ^                               M0_RST_STOPPING
        |                                   |
        |                                   | m0_reqh_service_stop()
        |     m0_reqh_service_fini()        v
	+------------------------------+M0_RST_STOPPED

   @endverbatim

   @{
 */

enum {
	M0_REQH_SERVICE_UUID_SIZE = 64,
};

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

	   @see m0_reqh_service_start()
	 */
	M0_RST_STARTING,
	/**
	   A service transitions to M0_RST_STARTED state after completing
	   generic part of service start up operations by m0_reqh_service_start().
	 */
	M0_RST_STARTED,
	/**
	   A service transitions to M0_RST_STOPPING state before service specific
           stop routine is invoked.

	   @see m0_reqh_service_stop()
	 */
	M0_RST_STOPPING,
	/**
	   A service transitions to M0_RST_STOPPED state, once service specific
           stop routine completes successfully and after it is unregistered from
           the request handler.
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
	   Currently generating service uuid using the service name and local
	   time stamp.
	 */
	char                              rs_uuid[M0_REQH_SERVICE_UUID_SIZE];

	/**
	   Service type specific structure to hold service specific
	   implementations of its operations.
	   This can be used to initialise service specific objects such as fops.
	 */
	struct m0_reqh_service_type      *rs_type;

	/**
	   Current state of a service.

	   @see m0_reqh_service_state
	 */
	enum m0_reqh_service_state        rs_state;

	/**
	   Protects service state transitions.
	 */
	struct m0_mutex                   rs_mutex;

	/**
	   Service specific operations vector.
	 */
	const struct m0_reqh_service_ops *rs_ops;

	/**
	   Request handler this service belongs to.
	 */
	struct m0_reqh                   *rs_reqh;

	/**
	   Linkage into list of services in request handler.

	   @see m0_reqh::rh_services
	 */
	struct m0_tlink                   rs_linkage;

	/**
	   Service magic to check consistency of service instance.
	 */
	uint64_t                          rs_magix;
};

/**
   Service specific operations vector.
 */
struct m0_reqh_service_ops {
	/**
	   Performs service specific startup operations.
	   Once started, incoming requests related to this service are ready
	   to be processed by the corresponding request handler.
	   Service startup can involve operations like initialising service
	   specific fops, &tc which may fail due to whichever reason, in that
	   case the service is finalised and appropriate error is returned.
           This is invoked from m0_reqh_service_start(). Once the service
           specific startup operations are performed, the service is registered
           with the request handler.

	   @param service Service to be started

	   @see m0_reqh_service_start()
	 */
	int (*rso_start)(struct m0_reqh_service *service);

	/**
	   Performs service specific finalisation of objects.
	   Once stopped, no incoming request related to this service
	   on a node will be processed further.
	   Stopping a service can involve operations like finalising service
           specific fops, &tc. This is invoked from m0_reqh_service_stop().
           Once the service specific objects are finalised, the service is
           unregistered from request handler.

	   @param service Service to be started

	   @see m0_reqh_service_stop()
	 */
	void (*rso_stop)(struct m0_reqh_service *service);

	/**
	   Destroys a particular service.
           This is invoked from m0_reqh_service_fini(). Initialy generic part
           of the service is finalised, followed by the service specific
           finalisation.

	   @param service Service to be finalised

	   @see m0_reqh_service_fini()
	 */
	void (*rso_fini)(struct m0_reqh_service *service);
};

/**
   Service type operations vector.
 */
struct m0_reqh_service_type_ops {
	/**
	   Allocates and initialises a service for the given service type.
	   This also initialises the corresponding service operations vector.
           This is typically invoked  during mero setup, but also can be
           invoked later, in-order to configure a particular service. Once the
           service specific initialisation is done, generic m0_reqh_service_init()
           routine is invoked.

	   @param stype Type of service to be located
	   @param service successfully located service
	 */
	int (*rsto_service_allocate)(struct m0_reqh_service_type *stype,
				     struct m0_reqh_service **service);
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

	/**
	    Linkage into global service types list.

	    @see m0_rstypes
	 */
	struct m0_tlink                        rst_linkage;
	uint64_t                               rst_magix;
};

/**
   Locates a particular type of service.
   Invokes service type specific locate routine.

   @param stype Type of service to be located
   @param service out parameter containing located service

   @pre service != NULL
   @post m0_reqh_service_invariant(service)

   @see struct m0_reqh_service_type_ops
 */
M0_INTERNAL int m0_reqh_service_allocate(struct m0_reqh_service_type *stype,
					 struct m0_reqh_service **service);

/**
   Searches a particular type of service by traversing global list of service
   types maintained by request handler module.

   @param sname, name of the service to be searched in global list

   @pre sname != NULL

   @see m0_reqh_service_init()

 */
M0_INTERNAL struct m0_reqh_service_type *m0_reqh_service_type_find(const char
								   *sname);

/**
   Starts a particular service.
   Invokes service specific start routine, if service specific startup completes
   Successfully then the service is registered with the request handler and
   transitioned into M0_RST_STARTED state.

   @param service Service to be started

   @pre service != NULL
   @post m0_reqh_service_invariant(service)

   @see struct m0_reqh_service_ops
 */
M0_INTERNAL int m0_reqh_service_start(struct m0_reqh_service *service);

/**
   Stops a particular service.
   Unregisters a service from the request handler and transitions service to
   M0_RST_STOPPED state.
   This is invoked after service specific stop routine returns successfully.

   @param service Service to be stopped

   @pre service != NULL

   @see struct m0_reqh_service_ops
   @see cs_service_fini()
 */
M0_INTERNAL void m0_reqh_service_stop(struct m0_reqh_service *service);

/**
   Performs generic part of service initialisation.
   Transitions service into M0_RST_INITIALISED state
   This is invoked after the service specific init routine returns successfully.

   @param service service to be initialised

   @pre service != NULL && reqh != NULL && service->rs_state == M0_RST_INITIALISING

   @see struct m0_reqh_service_type_ops
   @see cs_service_init()
 */
M0_INTERNAL void m0_reqh_service_init(struct m0_reqh_service *service,
				      struct m0_reqh *reqh);

/**
   Performs generic part of service finalisation.
   This is invoked before service specific finalisation routine.

   @param service Service to be finalised

   @pre service != NULL && service->rs_state == M0_RST_STOPPED

   @see struct m0_reqh_service_ops
   @see cs_service_fini()
 */
M0_INTERNAL void m0_reqh_service_fini(struct m0_reqh_service *service);

#define M0_REQH_SERVICE_TYPE_DEFINE(stype, ops, name) \
struct m0_reqh_service_type stype = {                 \
	.rst_name = (name),                           \
	.rst_ops  = (ops)                             \
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
   Returns service intance of the given service type using the reqhkey
   framework.

   @see m0_reqh::rh_key
 */
M0_INTERNAL struct m0_reqh_service *m0_reqh_service_find(const struct
							 m0_reqh_service_type
							 *st,
							 struct m0_reqh *reqh);

M0_INTERNAL int m0_reqh_service_types_length(void);
M0_INTERNAL bool m0_reqh_service_is_registered(const char *sname);
M0_INTERNAL void m0_reqh_service_list_print(void);

/** @} endgroup reqhservice */

/* __MERO_REQH_REQH_SERVICE_H__ */

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
