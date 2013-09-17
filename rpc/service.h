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

#pragma once

#ifndef __MERO_RPC_SERVICE_H__
#define __MERO_RPC_SERVICE_H__

/**
   @defgroup rpc_service RPC service

   @{
 */

#include "lib/types.h"       /* m0_uint128 */
#include "lib/tlist.h"
#include "lib/bob.h"
#include "mero/magic.h"
#include "rpc/session.h"
#include "rpc/item.h"

/* Imports */
struct m0_rpc_conn;

/* Exports */
struct m0_rpc_service_type;
struct m0_rpc_servie_type_ops;
struct m0_rpc_service;
struct m0_rpc_service_ops;

M0_INTERNAL int m0_rpc_service_module_init(void);
M0_INTERNAL void m0_rpc_service_module_fini(void);

struct m0_rpc_service_type {
	/** Numeric id that uniquely identifies a service type */
	uint32_t                              svt_type_id;

	/** Human readable name of service type e.g. "ioservice" */
	const char                           *svt_name;
	const struct m0_rpc_service_type_ops *svt_ops;

        /**
           Link to put m0_rpc_service_type instance in
           service.c:rpc_service_types tlist.
           tl descr: rpc_service_types_tl
        */
	struct m0_tlink                       svt_tlink;

	/** magic number. M0_RPC_SERVICE_TYPE_MAGIC */
	uint64_t                              svt_magix;
};

struct m0_rpc_service_type_ops {
        /**
         * Allocates and initialises m0_rpc_service instance.
         * Values pointed by ep_addr and uuid are copied in
         * m0_rpc_service instance.
         *
	 * @post ergo(result == 0, *out != NULL &&
	 *            (*out)->svc_state == M0_RPC_SERVICE_STATE_INITIALISED)
         */
	int (*rsto_alloc_and_init)(struct m0_rpc_service_type *service_type,
				   const char                 *ep_addr,
				   const struct m0_uint128    *uuid,
				   struct m0_rpc_service     **out);
};

/**
 * Defines a m0_rpc_service_type instance.
 *
 * @param obj_name Name of m0_rpc_service_type instance
 * @param hname    Human readable name of service type
 * @param type_id  numeric id that uniquely identifies a service-type
 * @param ops      Pointer to m0_rpc_service_type_ops instance
 */
#define M0_RPC_SERVICE_TYPE_DEFINE(scope, obj_name, hname, type_id, ops) \
scope struct m0_rpc_service_type (obj_name) = {                          \
	.svt_name     = (hname),                                         \
	.svt_type_id  = (type_id),                                       \
	.svt_ops      = (ops),                                           \
	.svt_magix    = M0_RPC_SERVICE_TYPE_MAGIC,                       \
}

/**
 * Registers a service type.
 *
 * Adds service_type to service.c:rpc_service_types list.
 *
 * @pre service_type != NULL &&
 *      m0_rpc_service_type_locate(service_type->svt_type_id) == NULL
 *
 * @post m0_rpc_service_type_locate(service_type->svt_type_id) == service_type
 */
M0_INTERNAL void m0_rpc_service_type_register(struct m0_rpc_service_type
					      *service_type);

/**
 * @return m0_rpc_service_type instance identified by type_id if found, NULL
 *         otherwise.
 * @post ergo(result != NULL, result->svt_type_id == type_id)
 */
M0_INTERNAL struct m0_rpc_service_type *
m0_rpc_service_type_locate(uint32_t type_id);

/**
 * Unregisters a service type.
 *
 * Removes service_type from service.c:rpc_service_types list.
 * @pre rpc_service_types_tlink_is_in(service_type)
 * @post !rpc_service_types_tlink_is_in(service_type)
 */
M0_INTERNAL void m0_rpc_service_type_unregister(struct m0_rpc_service_type
						*service_type);

/**
 * Possible states in which a m0_rpc_service instance can be.
 *
 * The user need not have to be aware of these states.
 * The states are defined to make it easy to check the service invariants and
 * enforce proper api calling sequence.
 */
enum m0_rpc_service_state {
	/** Pre-initialisation state */
	M0_RPC_SERVICE_STATE_UNDEFINED,
	M0_RPC_SERVICE_STATE_INITIALISED,
	M0_RPC_SERVICE_STATE_CONN_ATTACHED,

	/** Not a state. Just to mark upper limit of valid state values */
	M0_RPC_SERVICE_STATE_NR,
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
 * maintains a list of known services associated with m0_rpc_machine.
 * Multiple services (of the same or different types) might run on the same
 * endpoint.
 *
 * m0_rpc_service instance might be embedded in higher level
 * objects depending on service type.
 *
 * Concurrency and Existence:
 *
 * - m0_rpc_service instance is NOT reference counted. So it is responsibility
 *   of user to ensure there are no threads referring the service instance,
 *   before releasing the service instance.
 *
 * - Access to service instance in M0_RPC_SERVICE_STATE_CONN_ATTACHED state
 *   is serialised by service->svc_conn->c_rpc_machine->rm_session_mutex.
 *
 * - Yet, the user needs to ensure that no more than one thread call
 *   m0_rpc_service_conn_detach() on same service instance at the same time.
 *
 * @see m0_rpc_service_invariant()
 */
struct m0_rpc_service {
	struct m0_rpc_service_type      *svc_type;

	enum m0_rpc_service_state        svc_state;

	/** @todo XXX embed service configuration object in m0_rpc_service */
	char                            *svc_ep_addr;
	struct m0_uint128                svc_uuid;

        /**
         * Rpc connection attached to the service instance. Valid only in
         * M0_RPC_SERVICE_STATE_CONN_ATTACHED state.
         */
	struct m0_rpc_conn              *svc_conn;

	const struct m0_rpc_service_ops *svc_ops;

        /**
         * Link in m0_rpc_machine::rm_services list.
         * tl descr: m0_rpc_services_tl
         */
	struct m0_tlink                  svc_tlink;

	/** List maintaining reverse connections to clients */
	struct m0_tl                     svc_rev_conn;

	/** magic == M0_RPC_SERVICE_MAGIC */
	uint64_t                         svc_magix;
};

M0_BOB_DECLARE(M0_EXTERN, m0_rpc_service);
M0_TL_DECLARE(m0_rpc_services, M0_EXTERN, struct m0_rpc_service);

struct m0_rpc_service_ops {
        /**
         * Finalises and frees service.
         *
         * Object pointed by service is not valid after call returns from
         * this routine.
         *
         * @pre service != NULL &&
         *      service->svc_state == M0_RPC_SERVICE_STATE_INITIALISED
         */
	void (*rso_fini_and_free)(struct m0_rpc_service *service);
};

M0_INTERNAL bool m0_rpc_service_invariant(const struct m0_rpc_service *service);

M0_INTERNAL const char *m0_rpc_service_get_ep_addr(const struct m0_rpc_service
						   *service);

/**
 * Associates service with conn
 *
 * @pre service != NULL &&
 *      service->svc_state == M0_RPC_SERVICE_STATE_INITIALISED
 * @pre conn != NULL &&
 *      conn->c_state == M0_RPC_CONN_ACTIVE &&
 *      strcmp(service->svc_ep_addr, conn->c_rpcchan->rc_destep->nep_addr) == 0
 *
 * @post service->svc_state == M0_RPC_SERVICE_STATE_CONN_ATTACHED
 */
M0_INTERNAL void m0_rpc_service_conn_attach(struct m0_rpc_service *service,
					    struct m0_rpc_conn *conn);

/**
 * @return conn if service->svc_state == M0_RPC_SERVICE_STATE_CONN_ATTACHED,
 *         NULL otherwise.
 */
M0_INTERNAL struct m0_rpc_conn *m0_rpc_service_get_conn(const struct
							m0_rpc_service
							*service);

M0_INTERNAL const struct m0_uint128
m0_rpc_service_get_uuid(const struct m0_rpc_service *service);

/**
 * Removes association between service and service->svc_conn.
 *
 * @pre service != NULL &&
 *      service->svc_state == M0_RPC_SERVICE_STATE_CONN_ATTACHED
 * @post service->svc_state == M0_RPC_SERVICE_STATE_INITIALISED_
 */
M0_INTERNAL void m0_rpc_service_conn_detach(struct m0_rpc_service *service);

/**
 * Releases service instance.
 *
 * Instance pointed by service will be freed at the discretion of confc.
 *
 * @pre service != NULL &&
 *      service->svc_state == M0_RPC_SERVICE_STATE_INITIALISED
 */
M0_INTERNAL void m0_rpc_service_release(struct m0_rpc_service *service);

/*
 * APIs after this point are not to be used by end-user. These are helpers
 * for confc/service-configuration-object-type specific routines.
 */

/**
 * Wrapper over service_type->svt_ops->rsto_alloc_and_init()
 */
M0_INTERNAL int
m0_rpc_service_alloc_and_init(struct m0_rpc_service_type *service_type,
			      const char *ep_addr,
			      const struct m0_uint128 *uuid,
			      struct m0_rpc_service **out);

/**
 * Wrapper over service->svc_ops->rso_fini_and_free()
 */
M0_INTERNAL void m0_rpc_service_fini_and_free(struct m0_rpc_service *service);

/**
 * Helper routine to initialise m0_rpc_service instance.
 *
 * Will be called from implementation of
 * m0_rpc_service_type::svt_ops::rsto_alloc_and_init().
 *
 * @pre service != NULL && service->svc_state == M0_RPC_SERVICE_STATE_UNDEFINED
 * @pre service_type != NULL && ep_addr != NULL && uuid != NULL && ops != NULL
 *
 * @post service->svc_state == M0_RPC_SERVICE_STATE_UNDEFINED
 */
M0_INTERNAL int m0_rpc__service_init(struct m0_rpc_service *service,
				     struct m0_rpc_service_type *service_type,
				     const char *ep_addr,
				     const struct m0_uint128 *uuid,
				     const struct m0_rpc_service_ops *ops);

/**
 * Helper routine to finalise m0_rpc_service instance.
 *
 * Will be usable for implementation of service->svc_ops->rso_fini_and_free()
 */
M0_INTERNAL void m0_rpc__service_fini(struct m0_rpc_service *service);

/**
 * Return reverse session to given item.
 *
 * @pre svc != NULL
 * @pre item != NULL && session != NULL
 */
M0_INTERNAL int
m0_rpc_service_reverse_session_get(struct m0_rpc_service    *svc,
				   const struct m0_rpc_item *item,
				   struct m0_rpc_session    *session);

M0_INTERNAL void
m0_rpc_service_reverse_session_put(struct m0_rpc_service *svc);

M0_INTERNAL struct m0_rpc_session *
m0_rpc_service_reverse_session_lookup(struct m0_rpc_service    *svc,
				      const struct m0_rpc_item *item);

/**
   @} end of rpc_service group
 */
#endif /* __MERO_RPC_SERVICE_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
