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

#include <errno.h>

#include "lib/cdefs.h"
#include "lib/types.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "rpc/rpccore.h"
#include "rpc/session_internal.h"
#include "net/net.h"
/*#include "reqh/reqh.h"*/

#include "rpc/rpc_helper.h"


#define DEBUG	1

#if DEBUG
#include <stdio.h>
#include <stdlib.h>
#define DBG(fmt, args...) printf("%s:%d " fmt, __func__, __LINE__, ##args)
#define DBG_PERROR(msg)   perror(msg)
#else
#define DBG(fmt, args...) ({ })
#define DBG_PERROR(msg)   ({ })
#endif /* DEBUG */

enum {
	MAX_RPCS_IN_FLIGHT = 32,
	SESSION_CLEANUP_TIMEOUT = 5,
};


int c2_rpc_helper_init_machine(struct c2_net_xprt *xprt, const char *addr_local,
			       const char *db_name, uint32_t cob_domain_id,
			       struct c2_reqh *request_handler,
			       struct c2_rpcmachine *rpc_machine)
{
	int rc;
	struct c2_net_domain	 *net_domain;
	struct c2_dbenv		 *dbenv;
	struct c2_cob_domain	 *cob_domain;
	struct c2_cob_domain_id  cob_domain_id_struct = { .id = cob_domain_id };

	/* Init transport */
	rc = c2_net_xprt_init(xprt);
	if(rc) {
		DBG("failed to init transport\n");
		goto cleanup;
	} else {
		DBG("bulk sunrpc transport init completed \n");
	}

	/* Init network domain */
	C2_ALLOC_PTR(net_domain);
	if (!net_domain) {
		rc = -ENOMEM;
		goto out;
	}

	rc = c2_net_domain_init(net_domain, xprt);
	if(rc) {
		DBG("failed to init domain\n");
		goto cleanup;
	} else {
		DBG("domain init completed\n");
	}

	/* Init dbenv */
	C2_ALLOC_PTR(dbenv);
	if (!dbenv) {
		rc = -ENOMEM;
		goto out;
	}

	rc = c2_dbenv_init(dbenv, db_name, 0);
	if(rc != 0){
		DBG("failed to init dbenv\n");
		goto cleanup;
	} else {
		DBG("dbenv init completed \n");
	}

	/* Init cob domain */
	C2_ALLOC_PTR(cob_domain);
	if (!cob_domain) {
		rc = -ENOMEM;
		goto out;
	}

	rc = c2_cob_domain_init(cob_domain, dbenv, &cob_domain_id_struct);
	if (rc) {
		DBG("failed to init cob domain\n");
		goto cleanup;
	} else {
		DBG("cob domain init completed\n");
	}

	/* Init the rpcmachine */
	rc = c2_rpcmachine_init(rpc_machine, cob_domain, net_domain, addr_local,
				request_handler);
	if(rc != 0){
		DBG("failed to init rpcmachine\n");
		goto cleanup;
	} else {
		DBG("rpc machine init completed \n");
	}

cleanup:
	/* TODO */
out:
	return rc;
}
C2_EXPORTED(c2_rpc_helper_init_machine);

int c2_rpc_helper_client_connect(struct c2_rpcmachine *rpc_machine,
				 const char *addr_remote, uint32_t nr_slots,
				 uint32_t timeout_s,
				 struct c2_rpc_session **rpc_session)
{
	int rc;
        c2_time_t timeout;
	static struct c2_net_end_point *server_endpoint;
	static struct c2_net_transfer_mc *transfer_machine;
	struct c2_rpc_conn *connection;
	struct c2_rpc_session *session;

        transfer_machine = &rpc_machine->cr_tm;

	/* Create destination endpoint for client (server endpoint) */
	rc = c2_net_end_point_create(&server_endpoint, transfer_machine,
					addr_remote);
	if (rc) {
		DBG("failed to create server endpoint\n");
		goto cleanup;
	}

	/* Init connection structure */
	C2_ALLOC_PTR(connection);
	if (!connection) {
		rc = -ENOMEM;
		goto out;
	}

	rc = c2_rpc_conn_init(connection, server_endpoint, rpc_machine,
			MAX_RPCS_IN_FLIGHT);
	if (rc) {
		DBG("failed to init rpc connection\n");
		goto cleanup;
	} else {
		DBG("rpc connection init completed\n");
	}

	/* Create RPC connection */
	rc = c2_rpc_conn_establish(connection);
	if (rc) {
		DBG("failed to create rpc connection\n");
		goto cleanup;
	} else {
		DBG("rpc connection created\n");
	}

        timeout = c2_time_now();
        c2_time_set(&timeout, c2_time_seconds(timeout) + timeout_s,
                                c2_time_nanoseconds(timeout));

	rc = c2_rpc_conn_timedwait(connection, C2_RPC_CONN_ACTIVE |
				   C2_RPC_CONN_FAILED, timeout);
	if (rc) {
		switch (connection->c_state) {
		case C2_RPC_CONN_ACTIVE:
			DBG("connection established\n");
			rc = 0;
			break;
		case C2_RPC_CONN_FAILED:
			DBG("connection create failed\n");
			rc = -1;
			goto cleanup;
		default:
			C2_ASSERT("internal logic error in c2_rpc_conn_timedwait()" == 0);
		}
	} else {
		DBG("timeout for conn create\n");
		rc = -ETIMEDOUT;
		goto cleanup;
	}

	/* Init RPC session */
	C2_ALLOC_PTR(session);
	if (!session) {
		rc = -ENOMEM;
		goto out;
	}

	rc = c2_rpc_session_init(session, connection, nr_slots);
	if (rc) {
		DBG("failed to init rpc session\n");
		goto cleanup;
	} else {
		DBG("rpc session init completed\n");
	}

	/* Create RPC session */
	rc = c2_rpc_session_establish(session);
	if (rc) {
		DBG("failed to create session\n");
		goto cleanup;
	} else {
		DBG("rpc session created\n");
	}

        timeout = c2_time_now();
        c2_time_set(&timeout, c2_time_seconds(timeout) + timeout_s,
                                c2_time_nanoseconds(timeout));
	/* Wait for session to become active */
	rc = c2_rpc_session_timedwait(session, C2_RPC_SESSION_IDLE, timeout);
	if (rc) {
		switch (session->s_state) {
		case C2_RPC_SESSION_IDLE:
			DBG("session established\n");
			rc = 0;
			break;
		case C2_RPC_SESSION_FAILED:
			DBG("session create failed\n");
			rc = -1;
			goto cleanup;
		default:
			C2_ASSERT("internal logic error in c2_rpc_session_timedwait()" == 0);
		}
	} else {
		DBG("timeout for session create\n");
	}

	*rpc_session = session;

cleanup:
	/* TODO */
out:
	return rc;
}
C2_EXPORTED(c2_rpc_helper_client_connect);

int c2_rpc_helper_client_call(struct c2_rpc_item *item,
			      struct c2_rpc_session *rpc_session,
			      uint32_t timeout_s)
{
	int rc;

	rc = 0;

	return rc;
}
C2_EXPORTED(c2_rpc_helper_client_call);

static int rpc_session_clean(struct c2_rpc_session *session)
{
	int rc;
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
	if (rc) {
		DBG("failed to terminate session\n");
		goto out;
	} else {
		DBG("rpc session terminate call successful\n");
	}

	/* Wait for session to terminate */
        timeout = c2_time_now();
        c2_time_set(&timeout, c2_time_seconds(timeout) + SESSION_CLEANUP_TIMEOUT,
                                c2_time_nanoseconds(timeout));

	rc = c2_rpc_session_timedwait(session, C2_RPC_SESSION_TERMINATED |
				      C2_RPC_SESSION_FAILED, timeout);
	if (rc) {
		switch (session->s_state) {
		case C2_RPC_SESSION_TERMINATED:
			DBG("session terminated\n");
			rc = 0;
			break;
		case C2_RPC_SESSION_FAILED:
			DBG("session terminate failed\n");
			rc = -1;
			goto out;
		default:
			C2_ASSERT("internal logic error in c2_rpc_session_timedwait()" == 0);
		}
	} else {
		DBG("timeout for session terminate\n");
	}

	c2_rpc_session_fini(session);
	c2_free(session);
out:
	return rc;
}

static int rpc_connection_clean(struct c2_rpc_conn *connection)
{
	int rc;
        c2_time_t timeout;
        struct c2_rpc_session *session;

	DBG("session list size %zu\n", c2_list_length(&connection->c_sessions));

	/* Terminate all registered sessions */
	c2_rpc_for_each_session(connection, session)
	{
		/* skip session_0 */
		if (session->s_session_id == SESSION_ID_0)
			continue;

		rc = rpc_session_clean(session);
		if (rc)
			goto out;

		/* FIXME: it will fail on 2nd session for some reason,
		 *        so we just stop after 1st session cleaning */
		break;
	}

	/* Terminate RPC connection */
	rc = c2_rpc_conn_terminate(connection);
	if (rc) {
		DBG("failed to terminate rpc connection\n");
		goto out;
	} else {
		DBG("rpc connection terminate call successful\n");
	}

        timeout = c2_time_now();
        c2_time_set(&timeout, c2_time_seconds(timeout) + SESSION_CLEANUP_TIMEOUT,
                                c2_time_nanoseconds(timeout));

	rc = c2_rpc_conn_timedwait(connection, C2_RPC_CONN_TERMINATED |
				   C2_RPC_CONN_FAILED, timeout);
	if (rc) {
		switch (connection->c_state) {
		case C2_RPC_CONN_TERMINATED:
			DBG("connection terminated\n");
			rc = 0;
			break;
		case C2_RPC_CONN_FAILED:
			DBG("failed to terminate connection\n");
			rc = -1;
			goto out;
		default:
			C2_ASSERT("internal logic error in c2_rpc_conn_timedwait()" == 0);
		}
	} else {
		DBG("timeout for conn terminate\n");
	}

	c2_rpc_conn_fini(connection);
out:
	return rc;
}

int c2_rpc_helper_cleanup(struct c2_rpcmachine *rpc_machine)
{
	int rc;
	struct c2_net_xprt        *xprt;
	struct c2_net_domain      *net_domain;
	struct c2_dbenv           *dbenv;
	struct c2_cob_domain      *cob_domain;
	/*struct c2_reqh            *request_handler;*/
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
        c2_list_for_each_entry_safe(&rpc_machine->cr_incoming_conns, connection,
				    connection_next, struct c2_rpc_conn, c_link)
	{
		rc = rpc_connection_clean(connection);
		if (rc)
			goto out;
        }

        c2_list_for_each_entry_safe(&rpc_machine->cr_outgoing_conns, connection,
				    connection_next, struct c2_rpc_conn, c_link)
	{
		rc = rpc_connection_clean(connection);
		if (rc)
			goto out;
        }

	/* Fini remote net endpoint. */
	DBG("tm ep list size %zu\n", c2_list_length(&transfer_machine->ntm_end_points));

	endpoint_first = c2_list_entry(transfer_machine->ntm_end_points.l_head,
				       struct c2_net_end_point, nep_tm_linkage);

        c2_list_for_each_entry_safe(&transfer_machine->ntm_end_points, endpoint,
				    endpoint_next, struct c2_net_end_point,
				    nep_tm_linkage)
	{
		/* skipt first endpoint, which is rpc machine's local endpoint */
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
