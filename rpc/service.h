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
 * Original author: Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 02/23/2012
 */

#ifndef __COLIBRI_RPC_SERVICE_H__
#define __COLIBRI_RPC_SERVICE_H__

#include "lib/atomic.h"
#include "lib/tlist.h"
#include "lib/mutex.h"

/* Imports */
struct c2_rpc_conn;
struct c2_rpcmachine;
struct c2_uuid;

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

/**
 * @todo XXX
 * Remove this definition when c2_cfg_service_type definition is
 * available from cfg/cfg.h.
 */
enum c2_rpc_service_type_id {
	C2_RPC_IO_SERVICE = 1,
	C2_RPC_MD_SERVICE,
};

enum {
	C2_RPC_SERVICE_TYPE_MAGIX = 0x5356435f54595045, /* "SVC_TYPE" */
};

struct c2_rpc_service_type {
	uint32_t                              svt_type_id;
	const char                           *svt_name;
	const struct c2_rpc_service_type_ops *svt_ops;

	uint64_t                              svt_magix;
	struct c2_tlink                       svt_tlink;
};

struct c2_rpc_service_type_ops {
	/** @todo XXX define parameters */
	struct c2_rpc_service * (*rsto_alloc_and_init)(
			struct c2_rpc_service_type *service_type);
};

#define C2_RPC_SERVICE_TYPE_DEFINE(scope, obj_name, hname, type_id, ops) \
scope struct c2_rpc_service_type (obj_name) = {                          \
	.svt_name     = (hname),                                         \
	.svt_type_id  = (type_id),                                          \
	.svt_ops      = (ops),                                           \
	.svt_magix = C2_RPC_SERVICE_TYPE_MAGIX,                          \
}

void c2_rpc_service_type_register(struct c2_rpc_service_type *service_type);

struct c2_rpc_service_type * c2_rpc_service_type_locate(uint32_t type_id);

void c2_rpc_service_type_unregister(struct c2_rpc_service_type *service_type);

enum c2_rpc_service_state {
	C2_RPC_SERVICE_STATE_UNDEFINED,
	C2_RPC_SERVICE_STATE_INITIALISED,
	C2_RPC_SERVICE_STATE_CONN_ATTACHED,
	C2_RPC_SERVICE_STATE_CONN_DETACHED,
};

struct c2_rpc_service {
	struct c2_rpc_service_type      *svc_type;

	enum c2_rpc_service_state        svc_state;
	uint64_t                         svc_nr_refs;
	struct c2_mutex                  svc_mutex;

	/** @todo XXX embed service configuration object in c2_rpc_service */
	char                            *svc_ep_addr;
	struct c2_uuid                   svc_uuid;
	struct c2_rpc_conn              *svc_conn;

	const struct c2_rpc_service_ops *svc_ops;

	struct c2_tlink                  svc_tlink;
};

struct c2_rpc_service_ops {
	void (*rso_fini_and_free)(struct c2_rpc_service *service);
};

/*
 * struct c2_rpc_service *
 * c2_confc_service_to_rpc_service(const struct c2_conf_service *confc_service);
 */

const char *
c2_rpc_service_get_ep_addr(const struct c2_rpc_service *service);

void c2_rpc_service_attach_conn(struct c2_rpc_service *service,
				struct c2_rpc_conn    *conn);

struct c2_rpc_conn *
c2_rpc_service_get_conn(const struct c2_rpc_service *service);

const struct c2_uuid *
c2_rpc_service_get_uuid(const struct c2_rpc_service *service);

void c2_rpc_service_detach_conn(struct c2_rpc_service *service);

void c2_rpc_service_release(struct c2_rpc_service *service);

struct c2_rpc_service *
c2_rpc_service_alloc_and_init(struct c2_rpc_service_type *service_type);

#endif /* __COLIBRI_RPC_SERVICE_H__ */
