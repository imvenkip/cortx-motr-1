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
   name and magic to check consistency.
   As part of service type registration, the service type instance is
   added to global list of the same.
   During colibri startup a particular service type is searched in the
   global list of service types based on its service name. Once located
   a c2_reqh_service object is allocated and service type specific
   initialisation method is invoked, this initialises specific type of
   service related objects (e.g. fops). This initialisation method also
   registers service specific operations vector containing its
   implementations for start, stop and fini.
   After initialsing the service it is added to the list of services in
   the given request handler.
   There is a rpcmachine created per end point per network domain, thus
   multiple rpc machines can be running within a request handler,
   thus all the services registered with that request handler are reachable
   through the registered end points.

   @{
 */

enum {
	C2_REQH_SERVICE_UUID = 64,
	C2_REQH_SERVICE_END_POINT = 128
};

enum c2_reqh_service_state {
	RH_SERVICE_STARTED,
	RH_SERVICE_STOPPED
};

/**
   Represents a service on node.
   This is built during c2_setup, as part of starting
   a particular service.
   Multiple services in an address space share the same
   request handler instance on a node. There exists a list
   of services in c2_reqh, sharing the same request handler.
 */
struct c2_reqh_service {
	/**
	   Service id that shold be unique throughout
	   the cluster.
	   Currently using a simple integer value.
	   @todo To have a generic uuid generation
		mechanism
	 */
	char                              rs_uuid[C2_REQH_SERVICE_UUID];
	/**
	   Service type specific structure to hold service
	   specific implementations of its operations.
	   This can be used to initialise service specific
	   objects such as fops, &tc.
	 */
	struct c2_reqh_service_type      *rs_type;

	/**
	   State of service (i.e. started or stopped)
	   This can be used to identify if further incoming
	   request related to this service should be processed
	   or not.
	   @see c2_reqh_service_state
	 */
	int                               rs_state;
	/**
	   Service specific operations vector
	 */
	const struct c2_reqh_service_ops *rs_ops;

	/**
	   Request handler this service belongs to.
	 */
	struct c2_reqh                   *rs_reqh;
	/**
	   Linkage into list of services in request handler
	 */
	struct c2_list_link               rs_linkage;
	/**
	   Service magic to check consistency
	 */
	uint64_t                          rs_magic;
};

/**
   Service specific operations vector.
 */
struct c2_reqh_service_ops {
	/**
	   Performs startup operations related to service
	   Once started, incoming requests related
	   to this service are ready to be processed by a
	   node.
	   Service startup can involve operations like
	   initialising service specific fops and also registers
	   the service with given request handler.
	   Sets the service state as started.

	   @param service Service to be started
	   @param reqh Request handler this service belongs to

	   @see c2_reqh_service_start()
	 */
	void (*rso_start)(struct c2_reqh_service *service,
				struct c2_reqh *reqh);
	/**
	   Stops a service and no incoming request related
	   to this service on a node should be processed
	   further.
	   Service stop can involve operations like finalising
	   service specific fops, also unregisters service from
	   the request handler.
	   This also sets service->rs_state to RH_SERVICE_STOPPED

	   @param service Service to be started

	   @see c2_reqh_service_stop()
	 */
	void (*rso_stop)(struct c2_reqh_service *service);
	/**
	   Finalises a particular service.

	   @param service Service to be finalised
	 */
	void (*rso_fini)(struct c2_reqh_service *service);
};

/**
   Service type operations vector.
 */
struct c2_reqh_service_type_ops {
	/**
	   Initialises a particular service.

	   @param service Service to be initialised
	   @param stype Type of service to be initialised

	   @retval 0 On success
	   	-errno On failure
	 */
	int (*rsto_service_init)(struct c2_reqh_service **service,
					struct c2_reqh_service_type *stype);
};

/**
   Represents a particular service type.
   A c2_reqh_service_type instance is initialised
   and registered into a global list of service types as
   a part of corresponding module initiasation process.

   @see c2_reqh_service_type_init()
 */
struct c2_reqh_service_type {
	const char		        *rst_name;
	struct c2_reqh_service_type_ops *rst_ops;
	struct c2_list_link              rst_linkage;
	uint64_t                         rst_magic;
};

/**
   Locates a particular type of service.
   This is invoked while initialising a particular service.

   @param sname, name of the service to be searched in global list

   @pre sname != NULL

   @post ergo(found == true, strcmp(stype->rst_name, sname) == 0 &&
                reqh_service_type_invariant(stype))

   @see c2_reqh_service_init()

 */
struct c2_reqh_service_type *c2_reqh_service_type_find(const char *sname);

/**
   Registers the service with the given request handler
   and sets its state to RH_SERVICE_STARTED, thus
   signifying that the service is now ready to process
   incoming requests related to it.

   @param service Service to be started

   @pre service != NULL && reqh != NULL
 */
void c2_reqh_service_start(struct c2_reqh_service *service, struct c2_reqh *reqh);

/**
   Stops a particular service.
   Unregisters the service from the request handler.

   @param service Service to be stopped

   @pre service != NULL
 */
void c2_reqh_service_stop(struct c2_reqh_service *service);
/**
   Initialises a particular service on a node.
   This is invoked during c2_setup.
   Allocates a service object. locates corresponding
   service type object using the service_name, and
   invokes init function for that service type and also
   starts a service.

   @param service, service to be allocated and initialised
   @param service_name, name of service to be initialised

   @retval 0 On success
	-errno On failure
 */
int c2_reqh_service_init(struct c2_reqh_service *service,
			const char *service_name);

/**
   Finalses and stops a particular service on a node.

   @param service Service to be finalised
 */
void c2_reqh_service_fini(struct c2_reqh_service *service);

/**
   Initialises and registers a particular type of service.
   This is invoked while initialising a particular module.

   @param rstype, c2_reqh_service_type instance to be initialsed
   @param rst_ops, service type specific operations vector
   @param service_name, name of the service to be initialised

   @retval 0 On success
	-errno On failure
 */
int c2_reqh_service_type_init(struct c2_reqh_service_type *rstype,
		struct c2_reqh_service_type_ops *rst_ops, const char *service_name);

/**
   Finalises a particular type of service on a node.
   This is invoked while finalising a module.

   @param rstype Type of service to be finalised
 */
void c2_reqh_service_type_fini(struct c2_reqh_service_type *rstype);

/**
   Initialises global list of service types and
   its corresponding mutex.
   This is invoked from c2_init().

   @retval 0 On success
	-errno On failure
 */
int c2_reqh_service_types_init();

/**
   Finalises global list of service types and its
   corresponding mutex.
 */
void c2_reqh_service_types_fini();

/**
   Checks consistency of a particular service.
 */
bool c2_reqh_service_invariant(struct c2_reqh_service *service);

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
