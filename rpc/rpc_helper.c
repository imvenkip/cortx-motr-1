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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 09/28/2011
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>

#include "lib/cdefs.h"
#include "lib/types.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "addb/addb.h"
#include "rpc/rpccore.h"
#include "net/net.h"
#include "fop/fop.h"

#include "rpc/rpc_helper.h"


static const struct c2_addb_loc rpc_helper_addb_loc = {
        .al_name = "rpc-helper"
};

#define ADDB_REPORT_FUNC_FAIL(ctx, name, rc)			\
		C2_ADDB_ADD((ctx), &rpc_helper_addb_loc,	\
			c2_addb_func_fail, (name), (rc))

enum {
	MAX_RPCS_IN_FLIGHT = 32,
	SESSION_CLEANUP_TIMEOUT = 5,
};


int c2_rpc_helper_init_machine(struct c2_rpc_helper_rpcmachine_ctx *rctx,
			       struct c2_rpcmachine *rpc_machine)
{
	int                      rc;
	struct c2_addb_ctx       *addb_ctx;
	struct c2_net_domain     *net_domain;
	struct c2_dbenv          *dbenv;
	struct c2_cob_domain     *cob_domain;
	struct c2_cob_domain_id  cob_domain_id_struct = {
					.id = rctx->cob_domain_id
				 };

	/* Init transport */
	rc = c2_net_xprt_init(rctx->xprt);
	if (rc != 0)
		goto out;

	/* Init network domain */
	C2_ALLOC_PTR(net_domain);
	if (net_domain == NULL) {
		rc = -ENOMEM;
		goto xprt_fini;
	}

	rc = c2_net_domain_init(net_domain, rctx->xprt);
	if (rc != 0)
		goto net_dom_free;

	addb_ctx = &net_domain->nd_addb;

	/* Init dbenv */
	C2_ALLOC_PTR(dbenv);
	if (dbenv == NULL) {
		rc = -ENOMEM;
		goto net_dom_fini;
	}

	rc = c2_dbenv_init(dbenv, rctx->db_name, 0);
	if (rc != 0) {
		ADDB_REPORT_FUNC_FAIL(addb_ctx, "c2_dbenv_init", rc);
		goto dbenv_free;
	}

	/* Init cob domain */
	C2_ALLOC_PTR(cob_domain);
	if (cob_domain == NULL) {
		rc = -ENOMEM;
		goto dbenv_fini;
	}

	rc = c2_cob_domain_init(cob_domain, dbenv, &cob_domain_id_struct);
	if (rc != 0) {
		ADDB_REPORT_FUNC_FAIL(addb_ctx, "c2_cob_domain_init", rc);
		goto cob_dom_free;
	}

	/* Init the rpcmachine */
	rc = c2_rpcmachine_init(rpc_machine, cob_domain, net_domain,
				rctx->local_addr, rctx->request_handler);
	if (rc != 0) {
		ADDB_REPORT_FUNC_FAIL(addb_ctx, "c2_rpcmachine_init", rc);
		goto cob_dom_fini;
	}

out:
	return rc;

cob_dom_fini:
	c2_cob_domain_fini(cob_domain);
cob_dom_free:
	c2_free(cob_domain);
dbenv_fini:
	c2_dbenv_fini(dbenv);
dbenv_free:
	c2_free(dbenv);
net_dom_fini:
	c2_net_domain_fini(net_domain);
net_dom_free:
	c2_free(net_domain);
xprt_fini:
	c2_net_xprt_fini(rctx->xprt);
	return rc;
}
C2_EXPORTED(c2_rpc_helper_init_machine);

int c2_rpc_helper_client_connect(struct c2_rpc_helper_client_ctx *cctx,
				 struct c2_rpc_session **rpc_session)
{
	int                              rc;
	int                              cleanup_rc;
	c2_time_t                        timeout;
	static struct c2_net_end_point   *server_endpoint;
	static struct c2_net_transfer_mc *transfer_machine;
	struct c2_addb_ctx               *addb_ctx;
	struct c2_rpc_conn               *connection;
	struct c2_rpc_session            *session;

	transfer_machine = &cctx->rpc_machine->cr_tm;
	addb_ctx = &cctx->rpc_machine->cr_rpc_machine_addb;

	/* Create destination endpoint for client (server endpoint) */
	rc = c2_net_end_point_create(&server_endpoint, transfer_machine,
					cctx->remote_addr);
	if (rc != 0) {
		ADDB_REPORT_FUNC_FAIL(addb_ctx, "c2_net_end_point_create", rc);
		goto out;
	}

	/* Init connection structure */
	C2_ALLOC_PTR(connection);
	if (connection == NULL) {
		rc = -ENOMEM;
		goto ep_clean;
	}

	rc = c2_rpc_conn_init(connection, server_endpoint, cctx->rpc_machine,
			MAX_RPCS_IN_FLIGHT);
	if (rc != 0) {
		ADDB_REPORT_FUNC_FAIL(addb_ctx, "c2_rpc_conn_init", rc);
		goto conn_free;
	}

	/* Create RPC connection */
	rc = c2_rpc_conn_establish(connection);
	if (rc != 0) {
		ADDB_REPORT_FUNC_FAIL(addb_ctx, "c2_rpc_conn_establish", rc);
		goto conn_fini;
	}

	timeout = c2_time_now();
	c2_time_set(&timeout, c2_time_seconds(timeout) + cctx->timeout_s,
		    c2_time_nanoseconds(timeout));

	rc = c2_rpc_conn_timedwait(connection, C2_RPC_CONN_ACTIVE |
				   C2_RPC_CONN_FAILED, timeout);
	if (rc != 0) {
		switch (connection->c_state) {
		case C2_RPC_CONN_ACTIVE:
			rc = 0;
			break;
		case C2_RPC_CONN_FAILED:
			rc = -ECONNREFUSED;
			goto conn_fini;
		default:
			C2_ASSERT("internal logic error in c2_rpc_conn_timedwait()" == 0);
		}
	} else {
		rc = -ETIMEDOUT;
		goto conn_fini;
	}

	/* Init RPC session */
	C2_ALLOC_PTR(session);
	if (session == NULL) {
		rc = -ENOMEM;
		goto conn_terminate;
	}

	rc = c2_rpc_session_init(session, connection, cctx->nr_slots);
	if (rc != 0) {
		ADDB_REPORT_FUNC_FAIL(addb_ctx, "c2_rpc_session_init", rc);
		goto session_free;
	}

	/* Create RPC session */
	rc = c2_rpc_session_establish(session);
	if (rc != 0) {
		ADDB_REPORT_FUNC_FAIL(addb_ctx, "c2_rpc_session_establish", rc);
		goto session_fini;
	}

	timeout = c2_time_now();
	c2_time_set(&timeout, c2_time_seconds(timeout) + cctx->timeout_s,
		    c2_time_nanoseconds(timeout));
	/* Wait for session to become active */
	rc = c2_rpc_session_timedwait(session, C2_RPC_SESSION_IDLE, timeout);
	if (rc != 0) {
		switch (session->s_state) {
		case C2_RPC_SESSION_IDLE:
			rc = 0;
			break;
		case C2_RPC_SESSION_FAILED:
			rc = -ECONNREFUSED;
			goto session_fini;
		default:
			C2_ASSERT("internal logic error in c2_rpc_session_timedwait()" == 0);
		}
	} else {
		rc = -ETIMEDOUT;
		goto session_fini;
	}

	*rpc_session = session;

out:
	return rc;

session_fini:
	c2_rpc_session_fini(session);
session_free:
	c2_free(session);
conn_terminate:
	/* Terminate RPC connection */
	cleanup_rc = c2_rpc_conn_terminate(connection);
	if (cleanup_rc) {
		ADDB_REPORT_FUNC_FAIL(addb_ctx, "c2_rpc_conn_terminate",
				      cleanup_rc);
		return rc;
	}

	timeout = c2_time_now();
	c2_time_set(&timeout, c2_time_seconds(timeout) + SESSION_CLEANUP_TIMEOUT,
		    c2_time_nanoseconds(timeout));

	cleanup_rc = c2_rpc_conn_timedwait(connection, C2_RPC_CONN_TERMINATED |
				   C2_RPC_CONN_FAILED, timeout);
	if (cleanup_rc) {
		switch (connection->c_state) {
		case C2_RPC_CONN_TERMINATED:
			cleanup_rc = 0;
			break;
		case C2_RPC_CONN_FAILED:
			cleanup_rc = -ECONNREFUSED;
			goto out;
		default:
			C2_ASSERT("internal logic error in c2_rpc_conn_timedwait()" == 0);
		}
	} else {
		cleanup_rc = -ETIMEDOUT;
	}
conn_fini:
	c2_rpc_conn_fini(connection);
conn_free:
	c2_free(connection);
ep_clean:
	c2_net_end_point_put(server_endpoint);
	return rc;
}
C2_EXPORTED(c2_rpc_helper_client_connect);

int c2_rpc_helper_client_call(struct c2_fop *fop, struct c2_rpc_session *session,
			      uint32_t timeout_s)
{
	int                 rc;
	c2_time_t           timeout;
	struct c2_clink     clink;
	struct c2_rpc_item  *item;

	C2_PRE(fop != NULL);
	C2_PRE(session != NULL);

	item = &fop->f_item;
	c2_rpc_item_init(item);

	item->ri_session = session;
	item->ri_type = fop->f_type->ft_ri_type;
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&item->ri_chan, &clink);
	c2_time_set(&timeout, timeout_s, 0);
	timeout = c2_time_add(c2_time_now(), timeout);

	rc = c2_rpc_post(item);
	if (rc != 0)
		goto clean;

	rc = c2_rpc_reply_timedwait(&clink, timeout);
clean:
	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	return rc;
}
C2_EXPORTED(c2_rpc_helper_client_call);

static int rpc_session_clean(struct c2_rpc_session *session)
{
	int       rc;
	c2_time_t timeout;

	C2_PRE(session != NULL);

	/* Wait for session to become IDLE */
	timeout = c2_time_now();
	c2_time_set(&timeout, c2_time_seconds(timeout) + SESSION_CLEANUP_TIMEOUT,
		    c2_time_nanoseconds(timeout));

	rc = c2_rpc_session_timedwait(session, C2_RPC_SESSION_IDLE, timeout);
	C2_ASSERT(session->s_state == C2_RPC_SESSION_IDLE ||
		  session->s_state == C2_RPC_SESSION_TERMINATED);

	/* Terminate session */
	rc = c2_rpc_session_terminate(session);
	if (rc != 0)
		goto out;

	/* Wait for session to terminate */
	timeout = c2_time_now();
	c2_time_set(&timeout, c2_time_seconds(timeout) + SESSION_CLEANUP_TIMEOUT,
		    c2_time_nanoseconds(timeout));

	rc = c2_rpc_session_timedwait(session, C2_RPC_SESSION_TERMINATED |
				      C2_RPC_SESSION_FAILED, timeout);
	if (rc != 0) {
		switch (session->s_state) {
		case C2_RPC_SESSION_TERMINATED:
			rc = 0;
			break;
		case C2_RPC_SESSION_FAILED:
			rc = -ECONNREFUSED;
			goto out;
		default:
			C2_ASSERT("internal logic error in c2_rpc_session_timedwait()" == 0);
		}
	} else {
		rc = -ETIMEDOUT;
	}

	c2_rpc_session_fini(session);
	c2_free(session);
out:
	return rc;
}

static int rpc_connection_clean(struct c2_rpc_conn *connection)
{
	int                   rc;
	c2_time_t             timeout;
	struct c2_rpc_session *session;
	struct c2_rpc_session *session_next;

	/* Terminate all registered sessions.
	 * Don't use c2_rpc_for_each_session() here because it doesn't support
	 * safe session deletion from connection's c_sessions list */
	c2_list_for_each_entry_safe(&connection->c_sessions, session,
				    session_next, struct c2_rpc_session, s_link)
	{
		/* skip session_0 */
		if (session->s_session_id == SESSION_ID_0)
			continue;

		rc = rpc_session_clean(session);
		if (rc != 0)
			goto out;
	}

	/* Terminate RPC connection */
	rc = c2_rpc_conn_terminate(connection);
	if (rc != 0)
		goto out;

	timeout = c2_time_now();
	c2_time_set(&timeout, c2_time_seconds(timeout) + SESSION_CLEANUP_TIMEOUT,
		    c2_time_nanoseconds(timeout));

	rc = c2_rpc_conn_timedwait(connection, C2_RPC_CONN_TERMINATED |
				   C2_RPC_CONN_FAILED, timeout);
	if (rc != 0) {
		switch (connection->c_state) {
		case C2_RPC_CONN_TERMINATED:
			rc = 0;
			break;
		case C2_RPC_CONN_FAILED:
			rc = -ECONNREFUSED;
			goto out;
		default:
			C2_ASSERT("internal logic error in c2_rpc_conn_timedwait()" == 0);
		}
	} else {
		rc = -ETIMEDOUT;
	}

	c2_rpc_conn_fini(connection);
out:
	return rc;
}

int c2_rpc_helper_cleanup(struct c2_rpcmachine *rpc_machine)
{
	int                       rc;
	struct c2_net_xprt        *xprt;
	struct c2_net_domain      *net_domain;
	struct c2_dbenv           *dbenv;
	struct c2_cob_domain      *cob_domain;
	struct c2_net_transfer_mc *transfer_machine;
	struct c2_net_end_point   *endpoint;
	struct c2_net_end_point   *endpoint_next;
	struct c2_net_end_point   *endpoint_first;
	struct c2_rpc_conn        *connection;
	struct c2_rpc_conn        *connection_next;

	net_domain       = rpc_machine->cr_tm.ntm_dom;
	xprt             = net_domain->nd_xprt;
	cob_domain       = rpc_machine->cr_dom;
	dbenv            = cob_domain->cd_dbenv;
	transfer_machine = &rpc_machine->cr_tm;

	/* Terminate all registered connections with all sessions */
	c2_list_for_each_entry_safe(&rpc_machine->cr_outgoing_conns, connection,
				    connection_next, struct c2_rpc_conn, c_link)
	{
		rc = rpc_connection_clean(connection);
		if (rc != 0)
			goto out;
	}

	/* Fini remote net endpoint. */
	endpoint_first = c2_list_entry(
				c2_list_first(&transfer_machine->ntm_end_points),
				       struct c2_net_end_point, nep_tm_linkage);

	c2_list_for_each_entry_safe(&transfer_machine->ntm_end_points, endpoint,
				    endpoint_next, struct c2_net_end_point,
				    nep_tm_linkage)
	{
		/* skip first endpoint, which is rpc machine's local endpoint */
		if (endpoint == endpoint_first)
			continue;

		c2_net_end_point_put(endpoint);
	}

	/* Fini rpcmachine */
	c2_rpcmachine_fini(rpc_machine);

	/* Fini cob domain */
	c2_cob_domain_fini(cob_domain);
	c2_free(cob_domain);

	/* Fini db */
	c2_dbenv_fini(dbenv);
	c2_free(dbenv);

	/* Fini net domain */
	c2_net_domain_fini(net_domain);
	c2_free(net_domain);

	/* Fini transport */
	c2_net_xprt_fini(xprt);
out:
	return rc;
}
C2_EXPORTED(c2_rpc_helper_cleanup);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
