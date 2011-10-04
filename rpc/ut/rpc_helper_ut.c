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

#include "lib/ut.h"
#include "lib/memory.h"
#include "addb/addb.h"
#include "rpc/session.h"
#include "rpc/it/ping_fop.h"
#include "fop/fop.h"

#ifdef __KERNEL__
#include "rpc/it/ping_fop_k.h"
#else
#include "rpc/it/ping_fop_u.h"
#endif

#include "rpc/rpc_helper.h"
#include "utils/rpc.h"


#define CLIENT_ENDPOINT_ADDR	"127.0.0.1:12345:1"
#define SERVER_ENDPOINT_ADDR	"127.0.0.1:54321:1"
#define CLIENT_DB_NAME		"rpc_helper_ut_db_client"
#define SERVER_DB_NAME		"rpc_helper_ut_db_server"

enum {
	CLIENT_COB_DOM_ID	= 12,
	SERVER_COB_DOM_ID	= 13,
	CLIENT_SESSION_SLOTS	= 1,
	CLIENT_CONNECT_TIMEOUT	= 5,
	NR_PING_BYTES		= 8,
};

extern struct c2_net_xprt c2_net_bulk_sunrpc_xprt;


static int send_fop(struct c2_rpc_session *session)
{
	int                rc;
	int                i;
	struct c2_fop      *fop;
	struct c2_fop_ping *ping_fop;

	fop = c2_fop_alloc(&c2_fop_ping_fopt, NULL);
	C2_UT_ASSERT(fop != NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	ping_fop = c2_fop_data(fop);
	ping_fop->fp_arr.f_count = NR_PING_BYTES / sizeof(ping_fop->fp_arr.f_data);

	C2_ALLOC_ARR(ping_fop->fp_arr.f_data, ping_fop->fp_arr.f_count);
	C2_UT_ASSERT(ping_fop->fp_arr.f_data != NULL);
	if (ping_fop->fp_arr.f_data == NULL) {
		rc = -ENOMEM;
		goto free_fop;
	}

	for (i = 0; i < ping_fop->fp_arr.f_count; i++) {
		ping_fop->fp_arr.f_data[i] = i+100;
	}

	rc = c2_rpc_helper_client_call(fop, session, CLIENT_CONNECT_TIMEOUT);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(fop->f_item.ri_error == 0);
	C2_UT_ASSERT(fop->f_item.ri_reply != 0);

	c2_free(ping_fop->fp_arr.f_data);
free_fop:
	/* FIXME: freeing fop here will lead to endless loop in
	 * nr_active_items_count(), which is called from
	 * c2_rpc_session_terminate() */
	/*c2_free(fop);*/
out:
	return rc;
}

static void test_rpc_helper(void)
{
	int                    rc;
	pid_t                  server_pid;
	struct c2_rpc_session  *session;

	struct c2_rpc_helper_rpcmachine_ctx rctx_server = {
		.xprt            = &c2_net_bulk_sunrpc_xprt,
		.local_addr      = SERVER_ENDPOINT_ADDR,
		.db_name         = SERVER_DB_NAME,
		.cob_domain_id   = SERVER_COB_DOM_ID,
		.request_handler = NULL,
	};

	struct c2_rpc_helper_rpcmachine_ctx rctx_client = {
		.xprt            = &c2_net_bulk_sunrpc_xprt,
		.local_addr      = CLIENT_ENDPOINT_ADDR,
		.db_name         = CLIENT_DB_NAME,
		.cob_domain_id   = CLIENT_COB_DOM_ID,
		.request_handler = NULL,
	};

	struct c2_rpc_helper_client_ctx cctx = {
		.rpc_machine = NULL,
		.remote_addr = SERVER_ENDPOINT_ADDR,
		.nr_slots    = CLIENT_SESSION_SLOTS,
		.timeout_s   = CLIENT_CONNECT_TIMEOUT,
	};

	server_pid = ut_rpc_server_start(&rctx_server, CLIENT_ENDPOINT_ADDR);
	if (server_pid < 0)
		goto out;

	rc = ut_rpc_connect_to_server(&rctx_client, &cctx, &session);
	if (rc != 0)
		goto stop_server;

	rc = send_fop(session);

	rc = c2_rpc_helper_cleanup(session->s_conn->c_rpcmachine);
	C2_UT_ASSERT(rc == 0);

stop_server:
	rc = ut_rpc_server_stop(server_pid);
out:
	return;
}

static int test_rpc_helper_init(void)
{
	int rc;

	/* set ADDB leve to AEL_WARN to see ADDB messages on STDOUT */
	/*c2_addb_choose_default_level(AEL_WARN);*/

	rc = c2_ping_fop_init();

	return rc;
}

static int test_rpc_helper_fini(void)
{
	c2_ping_fop_fini();
	return 0;
}

const struct c2_test_suite rpc_helper_ut = {
	.ts_name = "rpc-helper-ut",
	.ts_init = test_rpc_helper_init,
	.ts_fini = test_rpc_helper_fini,
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
