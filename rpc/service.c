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

#include <linux/string.h>
#include <linux/errno.h>

#include "rpc/service.h"
#include "rpc/session.h"
#include "rpc/rpc2.h"
#include "lib/tlist.h"
#include "lib/bob.h"
#include "lib/rwlock.h"
#include "lib/memory.h"

/**
   @addtogroup rpc_service

   @{
 */
static struct c2_bob_type rpc_service_type_bob = {
	.bt_name         = "rpc_service_type",
	.bt_magix_offset = offsetof(struct c2_rpc_service_type, svt_magix),
	.bt_magix        = C2_RPC_SERVICE_TYPE_MAGIX,
	.bt_check        = NULL,
};

static void c2_rpc_service_type_bob_init(struct c2_rpc_service_type *)
				__attribute__((unused));

static void c2_rpc_service_type_bob_fini(struct c2_rpc_service_type *)
				__attribute__((unused));

C2_BOB_DEFINE(static, &rpc_service_type_bob, c2_rpc_service_type);

enum {
	RPC_SERVICE_TYPES_LIST_HEAD_MAGIX = 0x5356435459504844, /* "SVCTYPHD" */
};

C2_TL_DESCR_DEFINE(rpc_service_types, "rpc_service_type", static,
		   struct c2_rpc_service_type, svt_tlink, svt_magix,
		   C2_RPC_SERVICE_TYPE_MAGIX,
		   RPC_SERVICE_TYPES_LIST_HEAD_MAGIX);

C2_TL_DEFINE(rpc_service_types, static, struct c2_rpc_service_type);

static struct c2_tl     rpc_service_types;
static struct c2_rwlock rpc_service_types_lock;

int c2_rpc_service_module_init(void)
{
	rpc_service_types_tlist_init(&rpc_service_types);
	c2_rwlock_init(&rpc_service_types_lock);
	return 0;
}

void c2_rpc_service_module_fini(void)
{
	c2_rwlock_init(&rpc_service_types_lock);
	rpc_service_types_tlist_fini(&rpc_service_types);
}

void c2_rpc_service_type_register(struct c2_rpc_service_type *service_type)
{
	C2_PRE(service_type != NULL);
	C2_ASSERT(c2_rpc_service_type_bob_check(service_type));
	C2_PRE(c2_rpc_service_type_locate(service_type->svt_type_id) == NULL);

	c2_rwlock_write_lock(&rpc_service_types_lock);

	rpc_service_types_tlink_init_at_tail(service_type, &rpc_service_types);

	c2_rwlock_write_unlock(&rpc_service_types_lock);

	C2_POST(c2_rpc_service_type_locate(service_type->svt_type_id) ==
			service_type);
}

void c2_rpc_service_type_unregister(struct c2_rpc_service_type *service_type)
{
	C2_PRE(service_type != NULL);
	C2_ASSERT(c2_rpc_service_type_bob_check(service_type));
	C2_PRE(rpc_service_types_tlink_is_in(service_type));

	c2_rwlock_write_lock(&rpc_service_types_lock);

	rpc_service_types_tlink_del_fini(service_type);

	c2_rwlock_write_unlock(&rpc_service_types_lock);

	C2_POST(!rpc_service_types_tlink_is_in(service_type));
	C2_POST(c2_rpc_service_type_locate(service_type->svt_type_id) ==
			NULL);
}

struct c2_rpc_service_type * c2_rpc_service_type_locate(uint32_t type_id)
{
	struct c2_rpc_service_type *service_type;

	c2_rwlock_read_lock(&rpc_service_types_lock);
	c2_tlist_for(&rpc_service_types_tl, &rpc_service_types, service_type) {

		C2_ASSERT(c2_rpc_service_type_bob_check(service_type));

		if (service_type->svt_type_id == type_id) {
			c2_rwlock_read_unlock(&rpc_service_types_lock);
			return service_type;
		}

	} c2_tlist_endfor;

	c2_rwlock_read_unlock(&rpc_service_types_lock);
	return NULL;
}

static struct c2_bob_type rpc_service_bob = {
	.bt_name         = "rpc_service",
	.bt_magix_offset = offsetof(struct c2_rpc_service, svc_magix),
	.bt_magix        = C2_RPC_SERVICE_MAGIX,
	.bt_check        = NULL,
};

/** @todo XXX make following definition static */
C2_BOB_DEFINE(/* global scope */, &rpc_service_bob, c2_rpc_service);

C2_TL_DESCR_DEFINE(c2_rpc_services, "rpc_service", static,
                   struct c2_rpc_service, svc_tlink, svc_magix,
                   C2_RPC_SERVICE_MAGIX,
                   C2_RPC_SERVICES_LIST_HEAD_MAGIX);

C2_TL_DEFINE(c2_rpc_services, , struct c2_rpc_service);

bool c2_rpc_service_invariant(const struct c2_rpc_service *service)
{
	bool valid;

	if (service == NULL || !c2_rpc_service_bob_check(service))
		return false;

	if (service->svc_state < C2_RPC_SERVICE_STATE_INITIALISED ||
	    service->svc_state >= C2_RPC_SERVICE_STATE_NR)
		return false;

	valid = service->svc_type != NULL &&
		service->svc_ep_addr != NULL &&
		service->svc_ops != NULL;

	if (!valid)
		return false;

	switch (service->svc_state) {
	case C2_RPC_SERVICE_STATE_INITIALISED:
	case C2_RPC_SERVICE_STATE_CONN_DETACHED:
		return	service->svc_conn == NULL &&
			!c2_rpc_services_tlink_is_in(service);

	case C2_RPC_SERVICE_STATE_CONN_ATTACHED:
		return  service->svc_conn != NULL &&
			c2_rpc_services_tlink_is_in(service);
	default:
		return false;
	}
	/* Unreachable */
	C2_ASSERT(0);
	return false;
}

struct c2_rpc_service *
c2_rpc_service_alloc_and_init(struct c2_rpc_service_type *service_type,
			      const char                 *ep_addr,
			      const struct c2_uuid       *uuid)
{
	struct c2_rpc_service *service;

	C2_PRE(service_type != NULL &&
	       service_type->svt_ops != NULL &&
	       service_type->svt_ops->rsto_alloc_and_init != NULL);

	service = service_type->svt_ops->rsto_alloc_and_init(service_type,
					ep_addr, uuid);

	C2_ASSERT(ergo(service != NULL, c2_rpc_service_bob_check(service)));
	C2_ASSERT(ergo(service != NULL,
		     c2_rpc_service_invariant(service) &&
		     service->svc_state == C2_RPC_SERVICE_STATE_INITIALISED));

	return service;
}

void c2_rpc_service_fini_and_free(struct c2_rpc_service *service)
{
	C2_PRE(service != NULL && c2_rpc_service_bob_check(service));
	C2_PRE(service->svc_ops != NULL &&
	       service->svc_ops->rso_fini_and_free != NULL);

	service->svc_ops->rso_fini_and_free(service);
	/* Do not dereference @service after this point */
}
int c2_rpc__service_init(struct c2_rpc_service            *service,
			 struct c2_rpc_service_type       *service_type,
			 const char                       *ep_addr,
			 const struct c2_uuid             *uuid,
			 const struct c2_rpc_service_ops  *ops)
{
	char *copy_of_ep_addr;
	int   rc;

	C2_PRE(service != NULL && ep_addr != NULL);
	C2_PRE(service_type != NULL);
	C2_PRE(uuid != NULL && ops != NULL && ops->rso_fini_and_free != NULL);
	C2_PRE(service->svc_state == C2_RPC_SERVICE_STATE_UNDEFINED);

	copy_of_ep_addr = c2_alloc(strlen(ep_addr) + 1);
	if (copy_of_ep_addr == NULL) {
		rc = -ENOMEM;
		goto out;
	}
	strcpy(copy_of_ep_addr, ep_addr);

	service->svc_type    = service_type;
	service->svc_ep_addr = copy_of_ep_addr;
	service->svc_uuid    = *uuid;
	service->svc_ops     = ops;
	service->svc_conn    = NULL;

	c2_rpc_services_tlink_init(service);
	c2_rpc_service_bob_init(service);

	rc = 0;

out:
	/*
 	 * Leave in UNDEFINED state. The caller will set service->svc_state to
 	 * INITIALISED when it successfully initalises service-type specific
 	 * fields.
 	 */
	C2_POST(service->svc_state == C2_RPC_SERVICE_STATE_UNDEFINED);
	return rc;
}

void c2_rpc__service_fini(struct c2_rpc_service *service)
{
	C2_PRE(service != NULL && c2_rpc_service_bob_check(service));
	C2_PRE(c2_rpc_service_invariant(service) &&
		(service->svc_state == C2_RPC_SERVICE_STATE_INITIALISED ||
		 service->svc_state == C2_RPC_SERVICE_STATE_CONN_DETACHED));

	c2_free(service->svc_ep_addr);

	service->svc_type = NULL;

	c2_rpc_services_tlink_fini(service);
	c2_rpc_service_bob_fini(service);

	/* Caller of this routine will move service to UNDEFINED state */
}

const char *
c2_rpc_service_get_ep_addr(const struct c2_rpc_service *service)
{
	C2_PRE(service != NULL && c2_rpc_service_bob_check(service));

	return service->svc_ep_addr;
}

const struct c2_uuid *
c2_rpc_service_get_uuid(const struct c2_rpc_service *service)
{
	C2_PRE(service != NULL && c2_rpc_service_bob_check(service));

	return &service->svc_uuid;
}

void c2_rpc_service_attach_conn(struct c2_rpc_service *service,
				struct c2_rpc_conn    *conn)
{
	struct c2_rpcmachine *machine;

	C2_PRE(service != NULL && c2_rpc_service_bob_check(service));
	C2_PRE(c2_rpc_service_invariant(service) &&
		service->svc_state == C2_RPC_SERVICE_STATE_INITIALISED);

	c2_mutex_lock(&conn->c_mutex);
	C2_PRE(conn->c_state == C2_RPC_CONN_ACTIVE);

	machine = conn->c_rpcmachine;
	c2_mutex_lock(&machine->cr_session_mutex);

	C2_PRE(service->svc_state == C2_RPC_SERVICE_STATE_INITIALISED);

	service->svc_conn = conn;
	conn->c_service   = service;
	c2_rpc_services_tlink_init_at_tail(service, &machine->cr_services);
	service->svc_state = C2_RPC_SERVICE_STATE_CONN_ATTACHED;

	c2_mutex_unlock(&machine->cr_session_mutex);
	c2_mutex_unlock(&conn->c_mutex);
}

void c2_rpc_service_detach_conn(struct c2_rpc_service *service)
{
	struct c2_rpc_conn   *conn;
	struct c2_rpcmachine *machine;

	C2_PRE(service != NULL && c2_rpc_service_bob_check(service));
	C2_PRE(service->svc_conn != NULL);

	conn = service->svc_conn;
	c2_mutex_lock(&conn->c_mutex);
	C2_PRE(conn->c_state == C2_RPC_CONN_ACTIVE);

	machine = conn->c_rpcmachine;
	c2_mutex_lock(&machine->cr_session_mutex);

	C2_PRE(service->svc_state == C2_RPC_SERVICE_STATE_CONN_ATTACHED);

	service->svc_conn = NULL;
	conn->c_service   = NULL;
	c2_rpc_services_tlist_del(service);
	service->svc_state = C2_RPC_SERVICE_STATE_CONN_DETACHED;

	c2_mutex_unlock(&machine->cr_session_mutex);
	c2_mutex_unlock(&conn->c_mutex);
}

/** @} end of rpc_service group */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

