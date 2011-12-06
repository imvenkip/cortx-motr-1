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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 * Original creation date: 08/03/2011
 */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifdef HAVE_NETINET_IN_H
#  include <netinet/in.h>
#endif

#include <arpa/inet.h>
#include <netdb.h>

#include "lib/misc.h"		/* C2_SET0 */
#include "lib/errno.h"		/* ENOENT */
#include "console/console.h"

bool verbose;
uint32_t timeout;

/**
   @addtogroup console
   @{
 */

/**
 * @brief It creates the end point for any rpc connection.
 *        Initializes following members of c2_console
 *        - Transport
 *        - Network Domain
 *        - Network End point
 *        - RPC Connection
 *        - RPC Machine
 *        - COB Domain and ID
 *        - RPC Session.
 *
 * @param cons context information for rpc connection.
 *
 * @return 0 success, -errno failure.
 */
static int c2_cons_rpc_init_common(struct c2_console *cons)
{
	int  rc;

	C2_PRE(cons != NULL);

        rc = c2_net_xprt_init(cons->cons_xprt);
	if (rc != 0) {
		fprintf(stderr, "Failed to Init transport\n");
		return rc;
	}

	C2_SET0(&cons->cons_ndom);
        rc = c2_net_domain_init(&cons->cons_ndom, cons->cons_xprt);
	if (rc != 0) {
		fprintf(stderr, "Failed to Init network domain\n");
		goto xprt;
	}

        rc = c2_dbenv_init(&cons->cons_db, cons->cons_db_name, 0);
	if (rc != 0) {
		fprintf(stderr, "Failed to Init DBENV\n");
		goto ndom;
	}

        rc = c2_cob_domain_init(&cons->cons_cob_domain, &cons->cons_db,
				&cons->cons_cob_dom_id);
	if (rc != 0) {
		fprintf(stderr, "Failed to Init COB domain\n");
		goto db;
	}

	rc = c2_reqh_init(&cons->cons_reqh, NULL, NULL, NULL, NULL, NULL);
	if (rc != 0) {
		fprintf(stderr, "Failed to Init request handler\n");
		goto cdom;
	}

        rc = c2_rpcmachine_init(&cons->cons_rpc_mach, &cons->cons_cob_domain,
				&cons->cons_ndom, cons->cons_lepaddr,
				&cons->cons_reqh);
	if (rc != 0) {
		fprintf(stderr, "Failed to Init RPC machine\n");
		goto reqh;
	}

        cons->cons_trans_mc = &cons->cons_rpc_mach.cr_tm;

        rc = c2_net_end_point_create(&cons->cons_rendp, cons->cons_trans_mc,
				     cons->cons_repaddr);
	if (rc != 0) {
		fprintf(stderr, "Failed to create Remote Endpoint\n");
		goto rpc;
	}

	return 0;
rpc:
        c2_rpcmachine_fini(&cons->cons_rpc_mach);
reqh:
	c2_reqh_fini(&cons->cons_reqh);
cdom:
        c2_cob_domain_fini(&cons->cons_cob_domain);
db:
        c2_dbenv_fini(&cons->cons_db);
ndom:
        c2_net_domain_fini(&cons->cons_ndom);
xprt:
        c2_net_xprt_fini(cons->cons_xprt);

	return rc;
}

/**
 * @brief De-Initializes following members of c2_console
 *        - Transport
 *        - Network Domain
 *        - Network End Point
 *        - RPC Connection
 *        - RPC Machine
 *        - COB Domain and ID
 *        - RPC Session.
 *
 * @pre cons->cons_host == NULL &&
 *      cons->cons_port == 0
 * @post De-Initialization for each of member in c2_console
 */
static void c2_cons_rpc_fini_common(struct c2_console *cons)
{
        c2_net_end_point_put(cons->cons_rendp);

        c2_rpcmachine_fini(&cons->cons_rpc_mach);

	c2_reqh_fini(&cons->cons_reqh);

        c2_net_domain_fini(&cons->cons_ndom);

        c2_net_xprt_fini(cons->cons_xprt);

        c2_cob_domain_fini(&cons->cons_cob_domain);

        c2_dbenv_fini(&cons->cons_db);
}

int c2_cons_rpc_client_init(struct c2_console *cons)
{
	int rc;

	rc = c2_cons_rpc_init_common(cons);
	if (rc != 0)
		return rc;

        rc = c2_rpc_conn_init(&cons->cons_rconn, cons->cons_rendp,
			      &cons->cons_rpc_mach, cons->cons_items_in_flight);
	if (rc != 0) {
		fprintf(stderr, "Failed to Init RPC connection\n");
		goto common;
	}

	rc = c2_rpc_session_init(&cons->cons_rpc_session, &cons->cons_rconn,
				 cons->cons_nr_slots);
	if (rc != 0) {
		fprintf(stderr, "Failed to Init RPC session\n");
		goto conn;
	}

	return 0;
conn:
	c2_rpc_conn_fini(&cons->cons_rconn);
common:
	c2_cons_rpc_fini_common(cons);

	return rc;
}

int c2_cons_rpc_server_init(struct c2_console *cons)
{
	return  c2_cons_rpc_init_common(cons);
}

void c2_cons_rpc_client_fini(struct c2_console *cons)
{
	C2_PRE(cons != NULL);

	c2_rpc_session_fini(&cons->cons_rpc_session);

        c2_rpc_conn_fini(&cons->cons_rconn);

	c2_cons_rpc_fini_common(cons);
}

void c2_cons_rpc_server_fini(struct c2_console *cons)
{
	C2_PRE(cons != NULL);

	c2_cons_rpc_fini_common(cons);
}

c2_time_t c2_cons_timeout_construct(uint32_t timeout_secs)
{
	c2_time_t t1;
	c2_time_t t2;

	t1 = c2_time_now();
	c2_time_set(&t2, timeout_secs, 0);
	return c2_time_add(t1, t2);
}

int c2_cons_rpc_client_connect(struct c2_console *cons)
{
	struct c2_rpc_conn	*conn = &cons->cons_rconn;
	struct c2_rpc_session	*session = &cons->cons_rpc_session;
        c2_time_t		 deadline;
	int			 rc;
	bool			 rcb;

        rc = c2_rpc_conn_establish(conn);
	if (rc != 0) {
		fprintf(stderr, "Failed to create RPC Connection\n");
		return rc;
	}

	deadline = c2_cons_timeout_construct(timeout);
        /* Wait for session to become active */
	rcb = c2_rpc_conn_timedwait(conn, C2_RPC_CONN_ACTIVE |
				    C2_RPC_CONN_FAILED, deadline);
	if (rcb) {
		if (conn->c_state == C2_RPC_CONN_ACTIVE) {
			if (verbose)
				printf("console: Connection established\n");
		} else if (conn->c_state == C2_RPC_CONN_FAILED) {
			fprintf(stderr, "console: Connection create failed\n");
			return conn->c_rc;
		} else {
			fprintf(stderr, "console: Connection create INVALID\n");
			return conn->c_rc;
		}
	} else {
		fprintf(stderr, "console: Connection create timed out\n");
		return -ETIMEDOUT;
	}

        rc = c2_rpc_session_establish(session);
	if (rc != 0) {
		fprintf(stderr, "Failed to create session\n");
		goto cleanup;
	}

        /* Wait for session to become IDLE */
        rcb = c2_rpc_session_timedwait(session, C2_RPC_SESSION_IDLE, deadline);
	if (rcb) {
                if (session->s_state == C2_RPC_SESSION_IDLE) {
			if (verbose)
				printf("console: Session established\n");
		} else if (session->s_state == C2_RPC_SESSION_FAILED) {
                        fprintf(stderr, "console: Session create failed\n");
			rc = session->s_rc;
			goto cleanup;
		} else {
                        fprintf(stderr, "console: Session create INVALID\n");
			rc = session->s_rc;
			goto cleanup;
		}
	} else {
		fprintf(stderr, "console: Session create timed out\n");
		rc = -ETIMEDOUT;
		goto cleanup;
	}

	return 0;
cleanup:
        rc = c2_rpc_conn_terminate(conn);
        if (rc != 0) {
                fprintf(stderr, "Failed to terminate rpc connection\n");
                return rc;
        }

        rcb = c2_rpc_conn_timedwait(conn, C2_RPC_CONN_TERMINATED |
                                    C2_RPC_CONN_FAILED, deadline);
        if (rcb) {
                if (conn->c_state == C2_RPC_CONN_TERMINATED) {
			if (verbose)
				printf("console: Connection terminated\n");
		} else if (conn->c_state == C2_RPC_CONN_FAILED)
                        fprintf(stderr, "console: conn create failed\n");
                else
                        fprintf(stderr, "console: conn INVALID!!!\n");
        } else
		fprintf(stderr, "console: Connection terminate timed out\n");

	return rc;
}

int c2_cons_rpc_client_disconnect(struct c2_console *cons)
{
	struct c2_rpc_conn	*conn = &cons->cons_rconn;
	struct c2_rpc_session	*session = &cons->cons_rpc_session;
        c2_time_t		 deadline;
	int			 rc;
	bool			 rcb;

	do {
		deadline = c2_cons_timeout_construct(timeout);
		/* Wait for session to terminate */
		rcb = c2_rpc_session_timedwait(session, C2_RPC_SESSION_IDLE, deadline);
	} while (!rcb);

	rc = c2_rpc_session_terminate(session);
	if (rc != 0) {
		fprintf(stderr, "Failed to terminate session\n");
		return rc;
	}

	deadline = c2_cons_timeout_construct(timeout);
        /* Wait for session to terminate */
        rcb = c2_rpc_session_timedwait(session, C2_RPC_SESSION_TERMINATED |
				       C2_RPC_SESSION_FAILED, deadline);
        if (rcb) {
                if (session->s_state == C2_RPC_SESSION_TERMINATED) {
			if (verbose)
				printf("console: Session terminated\n");
		} else if (session->s_state == C2_RPC_SESSION_FAILED) {
                        fprintf(stderr, "console: session terminate failed\n");
			return session->s_rc;
		} else {
                        fprintf(stderr, "console: session terminate INVALID\n");
			return session->s_rc;
		}
	} else {
		fprintf(stderr, "console: session terminate timed out\n");
		return -ETIMEDOUT;
	}

        rc = c2_rpc_conn_terminate(conn);
	if (rc != 0) {
		fprintf(stderr, "Failed to terminate rpc connection\n");
		return rc;
	}

        rcb = c2_rpc_conn_timedwait(conn, C2_RPC_CONN_TERMINATED |
                                    C2_RPC_CONN_FAILED, deadline);
        if (rcb) {
                if (conn->c_state == C2_RPC_CONN_TERMINATED) {
			if (verbose)
				printf("console: Connection terminated\n");
		} else if (conn->c_state == C2_RPC_CONN_FAILED) {
                        fprintf(stderr, "console: conn create failed\n");
			return conn->c_rc;
                } else {
                        fprintf(stderr, "console: conn INVALID!!!\n");
			return conn->c_rc;
		}
        } else {
		fprintf(stderr, "console: Connection terminate timed out\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/** @} end of console group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
