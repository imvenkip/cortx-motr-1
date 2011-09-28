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

#include <unistd.h> /* fork(2) */

#include "lib/ut.h"
#include "lib/memory.h"
#include "lib/processor.h"
#include "rpc/rpccore.h"
#include "reqh/reqh.h"

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


#define CLIENT_ENDPOINT_ADDR	"127.0.0.1:12345:1"
#define SERVER_ENDPOINT_ADDR	"127.0.0.1:54321:1"
#define CLIENT_DB_NAME		"rpc_helper_ut_db_client"
#define SERVER_DB_NAME		"rpc_helper_ut_db_server"
#define CLIENT_COB_DOM_ID	12
#define SERVER_COB_DOM_ID	13
#define CLIENT_SESSION_SLOTS	1
#define CLIENT_CONNECT_TIMEOUT	30


extern struct c2_net_xprt c2_net_bulk_sunrpc_xprt;

static struct c2_net_xprt *transport = &c2_net_bulk_sunrpc_xprt;


static pid_t start_server(void)
{
	int rc;
	int cleanup_rc;
	pid_t server_pid;
	struct c2_reqh *request_handler;
	struct c2_rpcmachine *rpc_machine;
	static struct c2_net_end_point *client_endpoint;
	static struct c2_net_transfer_mc *transfer_machine;

	server_pid = fork();
	if (server_pid) {
		DBG("server pid %d\n", server_pid);
		if (server_pid < 0)
			DBG_PERROR("failed to start server");
		return server_pid;
	}


	rc = c2_processors_init();
	if (rc)
		goto out;

	/* Init request handler */
	C2_ALLOC_PTR(request_handler);
	if (!request_handler) {
		rc = -ENOMEM;
		goto proc_fini;
	}

	c2_reqh_init(request_handler, NULL, NULL, NULL, NULL);

	C2_ALLOC_PTR(rpc_machine);
	C2_UT_ASSERT(rpc_machine != NULL);
	if (!rpc_machine) {
		rc = -ENOMEM;
		goto free_reqh;
	}

	rc = c2_rpc_helper_init_machine(transport, SERVER_ENDPOINT_ADDR,
					SERVER_DB_NAME, SERVER_COB_DOM_ID,
					request_handler, rpc_machine);
	C2_UT_ASSERT(rc == 0);
	if (rc)
		goto free_rpc_mach;

        transfer_machine = &rpc_machine->cr_tm;

	/* Create destination endpoint for server (client endpoint) */
	rc = c2_net_end_point_create(&client_endpoint, transfer_machine,
					CLIENT_ENDPOINT_ADDR);
	C2_UT_ASSERT(rc == 0);
	if (rc) {
		DBG("failed to create client endpoint\n");
		goto cleanup;
	}

	/* wait for signal from parent to terminate */
	DBG("wait for signal to termiate\n");
	pause();
	DBG("server terminated\n");

cleanup:
	cleanup_rc = c2_rpc_helper_cleanup(rpc_machine);
	C2_UT_ASSERT(cleanup_rc == 0);
free_rpc_mach:
	c2_free(rpc_machine);
free_reqh:
	c2_free(request_handler);
proc_fini:
	c2_processors_fini();
out:
	/* if rc contains error code - return it,
	 * otherwise return cleanup_rc error code, if any */
	return rc ? rc : cleanup_rc;
}

static int stop_server(pid_t server_pid)
{
	int rc;

	DBG("kill server process, pid %d\n", server_pid);
	rc = kill(server_pid, SIGTERM);
	if (rc)
		DBG_PERROR("failed to kill server");
	C2_UT_ASSERT(rc == 0);

	return rc;
}

static int connect_to_server(struct c2_rpc_session **rpc_session)
{
	int rc;
	int cleanup_rc;
	struct c2_rpcmachine *rpc_machine;

	C2_ALLOC_PTR(rpc_machine);
	C2_UT_ASSERT(rpc_machine != NULL);
	if (!rpc_machine) {
		rc = -ENOMEM;
		goto out;
	}

	rc = c2_rpc_helper_init_machine(transport, CLIENT_ENDPOINT_ADDR,
					CLIENT_DB_NAME, CLIENT_COB_DOM_ID,
					NULL, rpc_machine);
	C2_UT_ASSERT(rc == 0);
	if (rc)
		goto free;

	rc = c2_rpc_helper_client_connect(rpc_machine, SERVER_ENDPOINT_ADDR,
					  CLIENT_SESSION_SLOTS,
					  CLIENT_CONNECT_TIMEOUT,
					  rpc_session);
	C2_UT_ASSERT(rc == 0);
	if (rc)
		goto cleanup;
	C2_UT_ASSERT(*rpc_session != NULL);

out:
	return rc;
cleanup:
	cleanup_rc = c2_rpc_helper_cleanup(rpc_machine);
	C2_UT_ASSERT(cleanup_rc == 0);
free:
	c2_free(rpc_machine);
	goto out;
}

static void test_rpc_helper(void)
{
	int rc;
	pid_t server_pid;
	struct c2_rpc_session *rpc_session;

	server_pid = start_server();
	if (server_pid < 0)
		goto out;

	sleep(1);

	rc = connect_to_server(&rpc_session);
	if (rc)
		goto out;

	rc = c2_rpc_helper_cleanup(rpc_session->s_conn->c_rpcmachine);
	C2_UT_ASSERT(rc == 0);

	rc = stop_server(server_pid);
	if (rc)
		goto out;
out:
	return;
}

const struct c2_test_suite rpc_helper_ut = {
	.ts_name = "rpc-helper-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "rpc-helper", test_rpc_helper },
		{ NULL, NULL }
	}
};


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
