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
#include "lib/time.h"
#include "lib/processor.h"
#include "rpc/rpccore.h"
#include "reqh/reqh.h"
#include "rpc/rpc_helper.h"

#include "utils/rpc.h"


pid_t ut_rpc_server_start(struct c2_rpc_helper_rpcmachine_ctx *rctx,
			  const char *client_addr)
{
	int                       rc;
	int                       cleanup_rc;
	pid_t                     server_pid;
	struct c2_reqh            *request_handler;
	struct c2_rpcmachine      *rpc_machine;
	struct c2_net_end_point   *client_endpoint;
	struct c2_net_transfer_mc *transfer_machine;

	server_pid = fork();
	if (server_pid != 0) {
		c2_time_t sleep_time;

		/* give the child process some time to finish server
		 * initialization */
		/* TODO: implement proper synchronization using signals */
		/*sleep(1);*/
		c2_time_set(&sleep_time, 1, 0);
		c2_nanosleep(sleep_time, NULL);

		return server_pid;
	}


	rc = c2_processors_init();
	if (rc != 0)
		goto out;

	/* Init request handler */
	C2_ALLOC_PTR(request_handler);
	if (request_handler == NULL) {
		rc = -ENOMEM;
		goto proc_fini;
	}

	rc = c2_reqh_init(request_handler, NULL, NULL, NULL, NULL);
	if (rc != 0)
		goto reqh_free;

	rctx->request_handler = request_handler;

	C2_ALLOC_PTR(rpc_machine);
	if (rpc_machine == NULL) {
		rc = -ENOMEM;
		goto reqh_fini;
	}

	rc = c2_rpc_helper_init_machine(rctx, rpc_machine);
	C2_UT_ASSERT(rc == 0);
	if (rc != 0)
		goto rpc_mach_free;

	transfer_machine = &rpc_machine->cr_tm;

	/* Create destination endpoint for server (client endpoint) */
	rc = c2_net_end_point_create(&client_endpoint, transfer_machine,
					client_addr);
	C2_UT_ASSERT(rc == 0);
	if (rc != 0)
		goto cleanup;

	/* wait for signal from parent to terminate */
	pause();

cleanup:
	cleanup_rc = c2_rpc_helper_cleanup(rpc_machine);
	C2_UT_ASSERT(cleanup_rc == 0);
rpc_mach_free:
	c2_free(rpc_machine);
reqh_fini:
	c2_reqh_fini(request_handler);
reqh_free:
	c2_free(request_handler);
proc_fini:
	c2_processors_fini();
out:
	/* if rc contains error code - return it,
	 * otherwise return cleanup_rc error code, if any */
	return rc ? rc : cleanup_rc;
}

int ut_rpc_server_stop(pid_t server_pid)
{
	int rc;

	rc = kill(server_pid, SIGTERM);
	C2_UT_ASSERT(rc == 0);

	return rc;
}

int ut_rpc_connect_to_server(struct c2_rpc_helper_rpcmachine_ctx *rctx,
			     struct c2_rpc_helper_client_ctx *cctx,
			     struct c2_rpc_session **rpc_session)
{
	int                  rc;
	int                  cleanup_rc;
	struct c2_rpcmachine *rpc_machine;

	C2_ALLOC_PTR(rpc_machine);
	C2_UT_ASSERT(rpc_machine != NULL);
	if (rpc_machine == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	rc = c2_rpc_helper_init_machine(rctx, rpc_machine);
	C2_UT_ASSERT(rc == 0);
	if (rc != 0)
		goto free;

	cctx->rpc_machine = rpc_machine;

	rc = c2_rpc_helper_client_connect(cctx, rpc_session);
	C2_UT_ASSERT(rc == 0);
	if (rc != 0)
		goto cleanup;
	C2_UT_ASSERT(*rpc_session != NULL);

out:
	return rc;

cleanup:
	cleanup_rc = c2_rpc_helper_cleanup(rpc_machine);
	C2_UT_ASSERT(cleanup_rc == 0);
free:
	c2_free(rpc_machine);
	return rc;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
