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

#include <linux/errno.h>

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/tlist.h"
#include "lib/bob.h"
#include "lib/memory.h"   /* m0_alloc() */
#include "rpc/service.h"
#include "rpc/rev_conn.h"
#include "rpc/rpc_internal.h"
#include "reqh/reqh.h"
#include "reqh/reqh_service.h"

/**
   @addtogroup rpc_service

   @{
 */

M0_TL_DESCR_DEFINE(rev_conn, "Reverse Connections", static,
		   struct m0_reverse_connection, rcf_link, rcf_magic,
		   M0_RM_REV_CONN_LIST_MAGIC, M0_RM_REV_CONN_LIST_HEAD_MAGIC);
M0_TL_DEFINE(rev_conn, static, struct m0_reverse_connection);

static const struct m0_bob_type rpc_svc_bob = {
	.bt_name = "rpc service",
	.bt_magix_offset = offsetof(struct m0_rpc_service, rps_magix),
	.bt_magix = M0_RPC_SERVICE_MAGIC,
	.bt_check = NULL
};
M0_BOB_DEFINE(M0_INTERNAL, &rpc_svc_bob, m0_rpc_service);

static int rpc_service_start(struct m0_reqh_service *service)
{
	struct m0_rpc_service *svc;

	svc = bob_of(service, struct m0_rpc_service, rps_svc, &rpc_svc_bob);
	rev_conn_tlist_init(&svc->rps_rev_conns);

	return 0;
}

static void rpc_service_stop(struct m0_reqh_service *service)
{
	struct m0_rpc_service *svc;

	svc = bob_of(service, struct m0_rpc_service, rps_svc, &rpc_svc_bob);
	m0_rpc_service_reverse_session_put(service);
	rev_conn_tlist_fini(&svc->rps_rev_conns);
}

static void rpc_service_fini(struct m0_reqh_service *service)
{
	struct m0_rpc_service *svc;

	svc = bob_of(service, struct m0_rpc_service, rps_svc, &rpc_svc_bob);
	m0_rpc_service_bob_fini(svc);
	m0_free(svc);
}

static int rpc_service_fop_accept(struct m0_reqh_service *service,
				  struct m0_fop *fop)
{
	return 0;
}
static const struct m0_reqh_service_ops rpc_ops = {
	.rso_start      = rpc_service_start,
	.rso_stop       = rpc_service_stop,
	.rso_fini       = rpc_service_fini,
	.rso_fop_accept = rpc_service_fop_accept
};

static int rpc_service_allocate(struct m0_reqh_service **service,
				struct m0_reqh_service_type *stype,
				struct m0_reqh_context *rctx)
{
	struct m0_rpc_service *svc;

	M0_PRE(stype != NULL && service != NULL);

	RPC_ALLOC_PTR(svc, SERVICE_ALLOC, NULL);
	if (svc == NULL)
		return -ENOMEM;

	m0_rpc_service_bob_init(svc);
	svc->rps_svc.rs_ops = &rpc_ops;
	*service = &svc->rps_svc;
	return 0;
}

static const struct m0_reqh_service_type_ops rpc_service_type_ops = {
	.rsto_service_allocate = rpc_service_allocate
};

M0_REQH_SERVICE_TYPE_DEFINE(m0_rpc_service_type, &rpc_service_type_ops,
			    "rpcservice", &m0_addb_ct_rpc_serv,
			    1);
M0_EXPORTED(m0_rpc_service_type);

M0_INTERNAL int m0_rpc_service_register(void)
{
	int rc;

	m0_addb_ctx_type_register(&m0_addb_ct_rpc_serv);
	m0_rev_conn_fom_type_init();
	rc = m0_reqh_service_type_register(&m0_rpc_service_type);
	return rc;
}

M0_INTERNAL void m0_rpc_service_unregister(void)
{
	m0_reqh_service_type_unregister(&m0_rpc_service_type);
}

M0_INTERNAL struct m0_rpc_session *
m0_rpc_service_reverse_session_lookup(struct m0_reqh_service   *service,
				      const struct m0_rpc_item *item)
{

	const char                   *rem_ep;
	struct m0_reverse_connection *revc;
	struct m0_rpc_service        *svc;

	M0_PRE(item != NULL && service->rs_type == &m0_rpc_service_type);

	svc    = bob_of(service, struct m0_rpc_service, rps_svc, &rpc_svc_bob);
	rem_ep = m0_rpc_item_remote_ep_addr(item);

	m0_tl_for (rev_conn, &svc->rps_rev_conns, revc) {
		if (strcmp(rem_ep, revc->rcf_rem_ep) == 0)
			return revc->rcf_sess;
	} m0_tl_endfor;

	return NULL;
}

M0_INTERNAL int
m0_rpc_service_reverse_session_get(struct m0_reqh_service   *service,
				   const struct m0_rpc_item *item,
				   struct m0_rpc_session    *session)
{
	int                           rc;
	const char                   *rem_ep;
	struct m0_reverse_connection *revc;
	struct m0_rpc_service        *svc;

	M0_PRE(item != NULL && service->rs_type == &m0_rpc_service_type);

	svc    = bob_of(service, struct m0_rpc_service, rps_svc, &rpc_svc_bob);
	rem_ep = m0_rpc_item_remote_ep_addr(item);

	M0_ALLOC_PTR(revc);
	if (revc == NULL) {
		rc = -ENOMEM;
		goto err_revc;
	}
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
	revc->rcf_sess    = session;
	revc->rcf_rpcmach = item->ri_rmachine;
	revc->rcf_ft      = M0_REV_CONNECT;
	m0_fom_init(&revc->rcf_fom, &rev_conn_fom_type, &rev_conn_fom_ops,
		    NULL, NULL, service->rs_reqh, service->rs_type);

	m0_fom_queue(&revc->rcf_fom, service->rs_reqh);
	rev_conn_tlink_init_at_tail(revc, &svc->rps_rev_conns);
	return M0_RC(0);

err_ep:
	m0_free(revc->rcf_rem_ep);
err_revc:
	m0_free(revc);
	return M0_RC(rc);
}

M0_INTERNAL void
m0_rpc_service_reverse_session_put(struct m0_reqh_service *service)
{
	int                           rc;
	struct m0_rpc_service        *svc;
	struct m0_reverse_connection *revc;

	M0_PRE(service->rs_type == &m0_rpc_service_type);

	svc = bob_of(service, struct m0_rpc_service, rps_svc, &rpc_svc_bob);

	/*
	 * It is assumed that following functions log errors internally
	 * and hence their return value is ignored.
	 */
	m0_tl_for (rev_conn, &svc->rps_rev_conns, revc) {
		rev_conn_tlink_del_fini(revc);
		rc = m0_rpc_session_destroy(revc->rcf_sess,
			m0_time_from_now(M0_REV_CONN_TIMEOUT, 0));
		if (rc != 0)
			M0_LOG(M0_ERROR, "Failed to terminate session %d", rc);

		rc = m0_rpc_conn_destroy(revc->rcf_conn,
			m0_time_from_now(M0_REV_CONN_TIMEOUT, 0));
		if (rc != 0)
			M0_LOG(M0_ERROR, "Failed to terminate "
			       "connection %d", rc);
		m0_free(revc->rcf_sess);
		m0_free(revc->rcf_rem_ep);
		m0_free(revc->rcf_conn);
		m0_free(revc);
	} m0_tl_endfor;
}

M0_INTERNAL struct m0_reqh_service *
m0_reqh_rpc_service_find(struct m0_reqh *reqh)
{
	return
		m0_reqh_service_find(m0_reqh_service_type_find("rpcservice"),
				     reqh);
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
