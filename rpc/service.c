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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/tlist.h"
#include "lib/bob.h"
#include "lib/rwlock.h"
#include "lib/memory.h"   /* m0_alloc() */
#include "rpc/rpc_internal.h"
#include "rpc/rev_conn.h"
#include "reqh/reqh.h"

/**
   @addtogroup rpc_service

   @{
 */
static struct m0_bob_type rpc_service_type_bob;

static void m0_rpc_service_type_bob_init(struct m0_rpc_service_type *)
	__attribute__((unused));

static void m0_rpc_service_type_bob_fini(struct m0_rpc_service_type *)
	__attribute__((unused));

M0_BOB_DEFINE(static, &rpc_service_type_bob, m0_rpc_service_type);

M0_TL_DESCR_DEFINE(service_type, "rpc_service_type", static,
		   struct m0_rpc_service_type, svt_tlink, svt_magix,
		   M0_RPC_SERVICE_TYPE_MAGIC,
		   M0_RPC_SERVICE_TYPES_HEAD_MAGIC);

M0_TL_DEFINE(service_type, static, struct m0_rpc_service_type);

M0_TL_DESCR_DEFINE(rev_conn, "Reverse Connections", static,
		   struct m0_reverse_connection, rcf_link, rcf_magic,
		   M0_RM_REV_CONN_LIST_MAGIC, M0_RM_REV_CONN_LIST_HEAD_MAGIC);
M0_TL_DEFINE(rev_conn, static, struct m0_reverse_connection);

static struct m0_tl     service_type_tlist;
static struct m0_rwlock service_type_tlist_lock;

static struct m0_bob_type rpc_service_bob;

M0_BOB_DEFINE(M0_INTERNAL, &rpc_service_bob, m0_rpc_service);

M0_TL_DESCR_DEFINE(m0_rpc_services, "rpc_service", M0_INTERNAL,
                   struct m0_rpc_service, svc_tlink, svc_magix,
                   M0_RPC_SERVICE_MAGIC,
                   M0_RPC_SERVICES_HEAD_MAGIC);

M0_TL_DEFINE(m0_rpc_services, M0_INTERNAL, struct m0_rpc_service);

M0_INTERNAL int m0_rpc_service_module_init(void)
{
	m0_bob_type_tlist_init(&rpc_service_type_bob, &service_type_tl);
	m0_bob_type_tlist_init(&rpc_service_bob, &m0_rpc_services_tl);

	service_type_tlist_init(&service_type_tlist);
	m0_rwlock_init(&service_type_tlist_lock);
	m0_rev_conn_fom_type_init();
	return 0;
}

M0_INTERNAL void m0_rpc_service_module_fini(void)
{
	m0_rwlock_fini(&service_type_tlist_lock);
	service_type_tlist_fini(&service_type_tlist);
}

M0_INTERNAL void m0_rpc_service_type_register(struct m0_rpc_service_type
					      *service_type)
{
	M0_PRE(service_type != NULL);
	M0_ASSERT(m0_rpc_service_type_bob_check(service_type));
	M0_PRE(m0_rpc_service_type_locate(service_type->svt_type_id) == NULL);

	m0_rwlock_write_lock(&service_type_tlist_lock);

	service_type_tlink_init_at_tail(service_type, &service_type_tlist);

	m0_rwlock_write_unlock(&service_type_tlist_lock);

	M0_POST(m0_rpc_service_type_locate(service_type->svt_type_id) ==
		service_type);
}

M0_INTERNAL void m0_rpc_service_type_unregister(struct m0_rpc_service_type
						*service_type)
{
	M0_PRE(service_type != NULL);
	M0_ASSERT(m0_rpc_service_type_bob_check(service_type));
	M0_PRE(service_type_tlink_is_in(service_type));

	m0_rwlock_write_lock(&service_type_tlist_lock);

	service_type_tlink_del_fini(service_type);

	m0_rwlock_write_unlock(&service_type_tlist_lock);

	M0_POST(!service_type_tlink_is_in(service_type));
	M0_POST(m0_rpc_service_type_locate(service_type->svt_type_id) ==
		NULL);
}

M0_INTERNAL struct m0_rpc_service_type *
m0_rpc_service_type_locate(uint32_t type_id)
{
	struct m0_rpc_service_type *service_type;

	m0_rwlock_read_lock(&service_type_tlist_lock);
	m0_tl_for(service_type, &service_type_tlist, service_type) {

		M0_ASSERT(m0_rpc_service_type_bob_check(service_type));

		if (service_type->svt_type_id == type_id)
			break;

	} m0_tl_endfor;

	m0_rwlock_read_unlock(&service_type_tlist_lock);

	M0_ASSERT(ergo(service_type != NULL,
		       service_type->svt_type_id == type_id));
	return service_type;
}

M0_INTERNAL bool m0_rpc_service_invariant(const struct m0_rpc_service *service)
{
	return
		service != NULL && m0_rpc_service_bob_check(service) &&
		service->svc_state >= M0_RPC_SERVICE_STATE_INITIALISED &&
		service->svc_state <  M0_RPC_SERVICE_STATE_NR &&

		service->svc_type != NULL &&
		service->svc_ep_addr != NULL &&
		service->svc_ops != NULL &&

		ergo(service->svc_state == M0_RPC_SERVICE_STATE_CONN_ATTACHED,
		     service->svc_conn != NULL &&
		     m0_rpc_services_tlink_is_in(service)) &&

		ergo(service->svc_state == M0_RPC_SERVICE_STATE_INITIALISED,
		     service->svc_conn == NULL &&
		     !m0_rpc_services_tlink_is_in(service));
}

M0_INTERNAL int m0_rpc_service_alloc_and_init(struct m0_rpc_service_type
					      *service_type,
					      const char *ep_addr,
					      const struct m0_uint128 *uuid,
					      struct m0_rpc_service **out)
{
	int rc;

	M0_PRE(service_type != NULL &&
	       service_type->svt_ops != NULL &&
	       service_type->svt_ops->rsto_alloc_and_init != NULL &&
	       out != NULL);

	rc = service_type->svt_ops->rsto_alloc_and_init(service_type,
							ep_addr, uuid, out);

	M0_POST(ergo(rc == 0, m0_rpc_service_invariant(*out) &&
		     (*out)->svc_state == M0_RPC_SERVICE_STATE_INITIALISED));
	M0_POST(ergo(rc != 0, *out == NULL));

	return rc;
}

M0_INTERNAL void m0_rpc_service_fini_and_free(struct m0_rpc_service *service)
{
	M0_PRE(m0_rpc_service_invariant(service));
	M0_PRE(service->svc_ops != NULL &&
	       service->svc_ops->rso_fini_and_free != NULL);

	service->svc_ops->rso_fini_and_free(service);
	/* Do not dereference @service after this point */
}

M0_INTERNAL int m0_rpc__service_init(struct m0_rpc_service *service,
				     struct m0_rpc_service_type *service_type,
				     const char *ep_addr,
				     const struct m0_uint128 *uuid,
				     const struct m0_rpc_service_ops *ops)
{
	char *copy_of_ep_addr;
	int   rc;

	M0_PRE(service != NULL && ep_addr != NULL);
	M0_PRE(service_type != NULL);
	M0_PRE(uuid != NULL && ops != NULL && ops->rso_fini_and_free != NULL);
	M0_PRE(service->svc_state == M0_RPC_SERVICE_STATE_UNDEFINED);

	copy_of_ep_addr = m0_alloc(strlen(ep_addr) + 1);
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

	m0_rpc_services_tlink_init(service);
	m0_rpc_service_bob_init(service);
	rev_conn_tlist_init(&service->svc_rev_conn);

	rc = 0;

out:
        /*
         * Leave in UNDEFINED state. The caller will set service->svc_state to
         * INITIALISED when it successfully initalises service-type specific
         * fields.
         */
	M0_POST(service->svc_state == M0_RPC_SERVICE_STATE_UNDEFINED);
	return rc;
}

M0_INTERNAL void m0_rpc__service_fini(struct m0_rpc_service *service)
{
	M0_PRE(m0_rpc_service_invariant(service) &&
	       service->svc_state == M0_RPC_SERVICE_STATE_INITIALISED);

	m0_free(service->svc_ep_addr);

	service->svc_type = NULL;
	rev_conn_tlist_fini(&service->svc_rev_conn);
	m0_rpc_services_tlink_fini(service);
	m0_rpc_service_bob_fini(service);

	/* Caller of this routine will move service to UNDEFINED state */
}

M0_INTERNAL const char *m0_rpc_service_get_ep_addr(const struct m0_rpc_service
						   *service)
{
	M0_PRE(m0_rpc_service_invariant(service));

	return service->svc_ep_addr;
}

M0_INTERNAL const struct m0_uint128
m0_rpc_service_get_uuid(const struct m0_rpc_service *service)
{
	M0_PRE(m0_rpc_service_invariant(service));

	return service->svc_uuid;
}

M0_INTERNAL void m0_rpc_service_conn_attach(struct m0_rpc_service *service,
					    struct m0_rpc_conn *conn)
{
	struct m0_rpc_machine *machine;

	M0_PRE(m0_rpc_service_invariant(service) &&
	       service->svc_state == M0_RPC_SERVICE_STATE_INITIALISED);

	machine = conn->c_rpc_machine;

	m0_rpc_machine_lock(machine);

	/* M0_ASSERT(m0_rpc_conn_invariant(conn)); */
	M0_PRE(conn_state(conn) == M0_RPC_CONN_ACTIVE);
	/*
         * Destination address of conn must match with end-point address of
         * service.
	 */
	M0_PRE(strcmp(service->svc_ep_addr,
		      conn->c_rpcchan->rc_destep->nep_addr) == 0);

	service->svc_conn = conn;
	conn->c_service   = service;
	m0_rpc_services_tlink_init_at_tail(service, &machine->rm_services);
	service->svc_state = M0_RPC_SERVICE_STATE_CONN_ATTACHED;

	m0_rpc_machine_unlock(machine);
}

M0_INTERNAL void m0_rpc_service_conn_detach(struct m0_rpc_service *service)
{
	struct m0_rpc_conn    *conn;
	struct m0_rpc_machine *machine;

	M0_PRE(service != NULL &&
	       service->svc_conn != NULL &&
	       service->svc_conn->c_rpc_machine != NULL);

	conn    = service->svc_conn;
	machine = conn->c_rpc_machine;

	m0_rpc_machine_lock(machine);

	M0_ASSERT(m0_rpc_service_invariant(service));
	M0_ASSERT(conn_state(conn) == M0_RPC_CONN_ACTIVE);
	M0_ASSERT(service->svc_state == M0_RPC_SERVICE_STATE_CONN_ATTACHED);

	service->svc_conn = NULL;
	conn->c_service   = NULL;
	m0_rpc_services_tlist_del(service);
	service->svc_state = M0_RPC_SERVICE_STATE_INITIALISED;

	m0_rpc_machine_unlock(machine);
}

M0_INTERNAL struct m0_rpc_session *
m0_rpc_service_reverse_session_lookup(struct m0_rpc_service    *svc,
				      const struct m0_rpc_item *item)
{
	const char                   *rem_ep;
	struct m0_reverse_connection *revc;

	M0_PRE(svc != NULL && item != NULL);

	rem_ep = m0_rpc_item_remote_ep_addr(item);

	m0_tl_for (rev_conn, &svc->svc_rev_conn, revc) {
		if (strcmp(rem_ep, revc->rcf_rem_ep) == 0)
			return revc->rcf_sess;
	} m0_tl_endfor;

	return NULL;
}

M0_INTERNAL int
m0_rpc_service_reverse_session_get(struct m0_rpc_service    *svc,
				   const struct m0_rpc_item *item,
				   struct m0_rpc_session    *session)
{
	int                           rc;
	const char                   *rem_ep;
	struct m0_reverse_connection *revc;
	struct m0_reqh_service       *reqhsvc;

	M0_PRE(svc != NULL && item != NULL);

	rem_ep = m0_rpc_item_remote_ep_addr(item);

	M0_ALLOC_PTR(revc);
	if (revc == NULL) {
		rc = -ENOMEM;
		goto err_revc;
	}
	reqhsvc = container_of(svc, struct m0_reqh_service, rs_rpc_svc);
	revc->rcf_rem_ep = m0_alloc(strlen(rem_ep) + 1);
	if (revc->rcf_rem_ep == NULL) {
		rc = -ENOMEM;
		goto err_ep;
	}
	strcpy(revc->rcf_rem_ep, rem_ep);
	M0_ALLOC_PTR(revc->rcf_sess);
	if (revc->rcf_sess == NULL) {
		rc = -ENOMEM;
		goto err_ep;
	}
	revc->rcf_sess = session;

	revc->rcf_rpcmach = item->ri_rmachine;
	revc->rcf_ft = M0_REV_CONNECT;
	m0_fom_init(&revc->rcf_fom, &rev_conn_fom_type, &rev_conn_fom_ops,
		    NULL, NULL, reqhsvc->rs_reqh, reqhsvc->rs_type);

	m0_fom_queue(&revc->rcf_fom, reqhsvc->rs_reqh);
	rev_conn_tlink_init_at_tail(revc, &svc->svc_rev_conn);
	M0_RETURN(0);

err_ep:
	m0_free(revc->rcf_rem_ep);
err_revc:
	m0_free(revc);
	M0_RETURN(rc);
}

M0_INTERNAL void
m0_rpc_service_reverse_session_put(struct m0_rpc_service *svc)
{
	int                           i;
	struct m0_clink              *clinks;
	size_t                        nr_clinks = 0;
	size_t                        nr_sessions;
	struct m0_reqh_service       *reqhsvc;
	struct m0_reverse_connection *revc;

	M0_PRE(svc != NULL);

	reqhsvc = container_of(svc, struct m0_reqh_service, rs_rpc_svc);

	/*
	 * We create a clink group to wait for completion of reverse
	 * disconnection fom.
	 */
	nr_sessions = rev_conn_tlist_length(&reqhsvc->rs_rpc_svc.svc_rev_conn);
	M0_ALLOC_ARR(clinks, nr_sessions);
	M0_ASSERT(clinks != NULL);
	/*
	 * It is assumed that following functions log errors internally
	 * and hence their return value is ignored.
	 */
	m0_tl_for (rev_conn, &svc->svc_rev_conn, revc) {
		revc->rcf_ft = M0_REV_DISCONNECT;
		M0_SET(&revc->rcf_fom);
		m0_fom_init(&revc->rcf_fom, &rev_conn_fom_type,
			    &rev_conn_fom_ops, NULL, NULL, reqhsvc->rs_reqh,
			    reqhsvc->rs_type);
		m0_mutex_init(&revc->rcf_mutex);
		m0_chan_init(&revc->rcf_chan, &revc->rcf_mutex);
		if (nr_clinks == 0)
			m0_clink_init(&clinks[nr_clinks], NULL);
		else
			m0_clink_attach(&clinks[nr_clinks], &clinks[0], NULL);
		m0_clink_add_lock(&revc->rcf_chan, &clinks[nr_clinks]);
		++nr_clinks;
		m0_fom_queue(&revc->rcf_fom, reqhsvc->rs_reqh);
	} m0_tl_endfor;

	while (nr_clinks) {
		m0_chan_wait(&clinks[0]);
		--nr_clinks;
	}

	for (i = nr_sessions - 1; i >= 0; --i) {
		m0_clink_del_lock(&clinks[i]);
		m0_clink_fini(&clinks[i]);
	}
	m0_free(clinks);

	m0_tl_for (rev_conn, &svc->svc_rev_conn, revc) {
		rev_conn_tlink_del_fini(revc);
		m0_chan_fini_lock(&revc->rcf_chan);
		m0_mutex_fini(&revc->rcf_mutex);
		m0_free(revc->rcf_sess);
		m0_free(revc->rcf_rem_ep);
		m0_free(revc->rcf_conn);
		m0_free(revc);
	} m0_tl_endfor;
}

#undef M0_TRACE_SUBSYSTEM

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
