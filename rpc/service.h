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
 * Original author: Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 02/23/2012
 */

#ifndef __COLIBRI_RPC_SERVICE_H__
#define __COLIBRI_RPC_SERVICE_H__

/**
   @defgroup rpc_service RPC service

   @{
 */

#include "lib/tlist.h"
#include "lib/bob.h"

/* Imports */
struct c2_rpc_conn;

/* Exports */
struct c2_rpc_service_type;
struct c2_rpc_servie_type_ops;
struct c2_rpc_service;
struct c2_rpc_service_ops;

int  c2_rpc_service_module_init(void);
void c2_rpc_service_module_fini(void);

/** @todo XXX This is stub definition */
struct c2_uuid {
	char u_uuid[40];
};

enum {
	/** Value of c2_rpc_service_type::svt_magix */
	C2_RPC_SERVICE_TYPE_MAGIX = 0x5356435f54595045, /* "SVC_TYPE" */
};

struct c2_rpc_service_type {
	/** Numeric id that uniquely identifies a service type */
	uint32_t                              svt_type_id;

	/** Human readable name of service type e.g. "ioservice" */
	const char                           *svt_name;
	const struct c2_rpc_service_type_ops *svt_ops;

        /**
           Link to put c2_rpc_service_type instance in
           service.c:rpc_service_types tlist.
           tl descr: rpc_service_types_tl
        */
	struct c2_tlink                       svt_tlink;

	/** magic number. C2_RPC_SERVICE_TYPE_MAGIX */
	uint64_t                              svt_magix;
};

struct c2_rpc_service_type_ops {
        /**
         * Allocates and initalises c2_rpc_service instance.
         * Values pointed by ep_addr and uuid are copied in
         * c2_rpc_service instance.
         *
	 * @post ergo(result == 0, *out != NULL &&
	 *            (*out)->svc_state == C2_RPC_SERVICE_STATE_INITIALISED)
         */
	int (*rsto_alloc_and_init)(struct c2_rpc_service_type *service_type,
				   const char                 *ep_addr,
				   const struct c2_uuid       *uuid,
				   struct c2_rpc_service     **out);
};

/**
 * Defines a c2_rpc_service_type instance.
 *
 * @param obj_name Name of c2_rpc_service_type instance
 * @param hname    Human readable name of service type
 * @param type_id  numeric id that uniquely identifies a service-type
 * @param ops      Pointer to c2_rpc_service_type_ops instance
 */
#define C2_RPC_SERVICE_TYPE_DEFINE(scope, obj_name, hname, type_id, ops) \
scope struct c2_rpc_service_type (obj_name) = {                          \
	.svt_name     = (hname),                                         \
	.svt_type_id  = (type_id),                                       \
	.svt_ops      = (ops),                                           \
	.svt_magix    = C2_RPC_SERVICE_TYPE_MAGIX,                       \
}

/**
 * Registers a service type.
 *
 * Adds service_type to service.c:rpc_service_types list.
 *
 * @pre service_type != NULL &&
 *      c2_rpc_service_type_locate(service_type->svt_type_id) == NULL
 *
 * @post c2_rpc_service_type_locate(service_type->svt_type_id) == service_type
 */
void c2_rpc_service_type_register(struct c2_rpc_service_type *service_type);

/**
 * @return c2_rpc_service_type instance identified by type_id if found, NULL
 *         otherwise.
 * @post ergo(result != NULL, result->svt_type_id == type_id)
 */
struct c2_rpc_service_type * c2_rpc_service_type_locate(uint32_t type_id);

/**
 * Unregisters a service type.
 *
 * Removes service_type from service.c:rpc_service_types list.
 * @pre rpc_service_types_tlink_is_in(service_type)
 * @post !rpc_service_types_tlink_is_in(service_type)
 */
void c2_rpc_service_type_unregister(struct c2_rpc_service_type *service_type);

/**
 * Possible states in which a c2_rpc_service instance can be.
 *
 * The user need not have to be aware of these states.
 * The states are defined to make it easy to check the service invariants and
 * enforce proper api calling sequence.
 */
enum c2_rpc_service_state {
	/** Pre-initialisation state */
	C2_RPC_SERVICE_STATE_UNDEFINED,
	C2_RPC_SERVICE_STATE_INITIALISED,
	C2_RPC_SERVICE_STATE_CONN_ATTACHED,

	/** Not a state. Just to mark upper limit of valid state values */
	C2_RPC_SERVICE_STATE_NR,
};

enum {
	/** Value of c2_rpc_service::svc_magix */
	C2_RPC_SERVICE_MAGIX            = 0x7270635f737663,   /* "rpc_svc"  */
	C2_RPC_SERVICES_LIST_HEAD_MAGIX = 0x7270637376636864, /* "rpcsvchd" */
};

/**
 * Represents a service on sender node.
 *
 * A service is something a client can send rpc items for processing.
 * Service is located somewhere in the cluster. This location is identified
 * by network end-point. Each service is an instance of a service type.
 * E.g., "ioservice" is a service type. It might have multiple instances
 * running on different nodes or sharing nodes. Each service is uniquely
 * identified by UUID, which is a part of system configuration. A client
 * maintains a list of known services associated with c2_rpc_machine.
 * Multiple services (of the same or different types) might run on the same
 * endpoint.
 *
 * c2_rpc_service instance might be embedded in higher level
 * objects depending on service type.
 *
 * Concurrency and Existence:
 *
 * - c2_rpc_service instance is NOT reference counted. So it is responsibility
 *   of user to ensure there are no threads refering the service instance,
 *   before releasing the service instance.
 *
 * - Access to service instance in C2_RPC_SERVICE_STATE_CONN_ATTACHED state
 *   is serialised by service->svc_conn->c_rpc_machine->rm_session_mutex.
 *
 * - Yet, the user needs to ensure that no more than one thread call
 *   c2_rpc_service_conn_detach() on same service instance at the same time.
 *
 * @see c2_rpc_service_invariant()
 */
struct c2_rpc_service {
	struct c2_rpc_service_type      *svc_type;

	enum c2_rpc_service_state        svc_state;

	/** @todo XXX embed service configuration object in c2_rpc_service */
	char                            *svc_ep_addr;
	struct c2_uuid                   svc_uuid;

        /**
         * Rpc connection attached to the service instance. Valid only in
         * C2_RPC_SERVICE_STATE_CONN_ATTACHED state.
         */
	struct c2_rpc_conn              *svc_conn;

	const struct c2_rpc_service_ops *svc_ops;

        /**
         * Link in c2_rpc_machine::rm_services list.
         * tl descr: c2_rpc_services_tl
         */
	struct c2_tlink                  svc_tlink;

	/** magic == C2_RPC_SERVICE_MAGIX */
	uint64_t                         svc_magix;
};

C2_BOB_DECLARE(extern, c2_rpc_service);
C2_TL_DECLARE(c2_rpc_services, extern, struct c2_rpc_service);

struct c2_rpc_service_ops {
        /**
         * Finalises and frees service.
         *
         * Object pointed by service is not valid after call returns from
         * this routine.
         *
         * @pre service != NULL &&
         *      service->svc_state == C2_RPC_SERVICE_STATE_INITIALISED
         */
	void (*rso_fini_and_free)(struct c2_rpc_service *service);
};

bool c2_rpc_service_invariant(const struct c2_rpc_service *service);

const char *
c2_rpc_service_get_ep_addr(const struct c2_rpc_service *service);

/**
 * Associates service with conn
 *
 * @pre service != NULL &&
 *      service->svc_state == C2_RPC_SERVICE_STATE_INITIALISED
 * @pre conn != NULL &&
 *      conn->c_state == C2_RPC_CONN_ACTIVE &&
 *      strcmp(service->svc_ep_addr, conn->c_rpcchan->rc_destep->nep_addr) == 0
 *
 * @post service->svc_state == C2_RPC_SERVICE_STATE_CONN_ATTACHED
 */
void c2_rpc_service_conn_attach(struct c2_rpc_service *service,
				struct c2_rpc_conn    *conn);

/**
 * @return conn if service->svc_state == C2_RPC_SERVICE_STATE_CONN_ATTACHED,
 *         NULL otherwise.
 */
struct c2_rpc_conn *
c2_rpc_service_get_conn(const struct c2_rpc_service *service);

const struct c2_uuid *
c2_rpc_service_get_uuid(const struct c2_rpc_service *service);

/**
 * Removes association between service and service->svc_conn.
 *
 * @pre service != NULL &&
 *      service->svc_state == C2_RPC_SERVICE_STATE_CONN_ATTACHED
 * @post service->svc_state == C2_RPC_SERVICE_STATE_INITIALISED_
 */
void c2_rpc_service_conn_detach(struct c2_rpc_service *service);

/**
 * Releases service instance.
 *
 * Instance pointed by service will be freed at the discretion of confc.
 *
 * @pre service != NULL &&
 *      service->svc_state == C2_RPC_SERVICE_STATE_INITIALISED
 */
void c2_rpc_service_release(struct c2_rpc_service *service);

/*
 * APIs after this point are not to be used by end-user. These are helpers
 * for confc/service-configuration-object-type specific routines.
 */

/**
 * Wrapper over service_type->svt_ops->rsto_alloc_and_init()
 */
int
c2_rpc_service_alloc_and_init(struct c2_rpc_service_type *service_type,
			      const char                 *ep_addr,
			      const struct c2_uuid       *uuid,
			      struct c2_rpc_service     **out);

/**
 * Wrapper over service->svc_ops->rso_fini_and_free()
 */
void c2_rpc_service_fini_and_free(struct c2_rpc_service *service);

/**
 * Helper routine to initialise c2_rpc_service instance.
 *
 * Will be called from implementation of
 * c2_rpc_service_type::svt_ops::rsto_alloc_and_init().
 *
 * @pre service != NULL && service->svc_state == C2_RPC_SERVICE_STATE_UNDEFINED
 * @pre service_type != NULL && ep_addr != NULL && uuid != NULL && ops != NULL
 *
 * @post service->svc_state == C2_RPC_SERVICE_STATE_UNDEFINED
 */
int c2_rpc__service_init(struct c2_rpc_service           *service,
			 struct c2_rpc_service_type      *service_type,
			 const char                      *ep_addr,
			 const struct c2_uuid            *uuid,
			 const struct c2_rpc_service_ops *ops);

/**
 * Helper routine to finalise c2_rpc_service instance.
 *
 * Will be usable for implementation of service->svc_ops->rso_fini_and_free()
 */
void c2_rpc__service_fini(struct c2_rpc_service *service);

/**
   @} end of rpc_service group
 */
#endif /* __COLIBRI_RPC_SERVICE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

