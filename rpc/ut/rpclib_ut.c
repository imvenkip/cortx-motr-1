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
#include "lib/processor.h"
#include "lib/trace.h"
#include "addb/addb.h"
#include "fop/fop.h"
#include "reqh/reqh.h"

#include "rpc/session.h"
#include "rpc/it/ping_fop.h"
#ifdef __KERNEL__
#include "rpc/it/ping_fop_k.h"
#else
#include "rpc/it/ping_fop_u.h"
#endif
#include "rpc/rpclib.h"

#include "ut/rpc.h"
#include "ut/cs_service.h"
#include "ut/cs_fop_foms.h"
#include "ut/cs_test_fops_u.h"


#define CLIENT_ENDPOINT_ADDR	"127.0.0.1:12345:2"
#define CLIENT_DB_NAME		"rpclib_ut_client.db"

#define SERVER_ENDPOINT_ADDR	"127.0.0.1:12345:1"
#define SERVER_ENDPOINT		"bulk-sunrpc:" SERVER_ENDPOINT_ADDR
#define SERVER_DB_FILE_NAME	"rpclib_ut_server.db"
#define SERVER_STOB_FILE_NAME	"rpclib_ut_server.stob"
#define SERVER_LOG_FILE_NAME	"rpclib_ut_server.log"

enum {
	CLIENT_COB_DOM_ID	= 16,
	SESSION_SLOTS		= 1,
	MAX_RPCS_IN_FLIGHT	= 1,
	CONNECT_TIMEOUT		= 5,
};

extern struct c2_net_xprt c2_net_bulk_sunrpc_xprt;


static int send_fop(struct c2_rpc_session *session)
{
	int                   rc;
	struct c2_fop         *fop;
	struct cs_ds2_req_fop *cs_ds2_fop;

	fop = c2_fop_alloc(&cs_ds2_req_fop_fopt, NULL);
	C2_UT_ASSERT(fop != NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	cs_ds2_fop = c2_fop_data(fop);
	cs_ds2_fop->csr_value = 0xaaf5;

	rc = c2_rpc_client_call(fop, session, &cs_ds_req_fop_rpc_item_ops,
				CONNECT_TIMEOUT);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(fop->f_item.ri_error == 0);
	C2_UT_ASSERT(fop->f_item.ri_reply != 0);

	/* FIXME: freeing fop here will lead to endless loop in
	 * nr_active_items_count(), which is called from
	 * c2_rpc_session_terminate() */
	/*c2_fop_free(fop);*/
out:
	return rc;
}

static void test_rpclib(void)
{
	int rc;
	struct c2_net_xprt    *xprt = &c2_net_bulk_sunrpc_xprt;
	struct c2_net_domain  client_net_dom = { };
	struct c2_dbenv       client_dbenv;
	struct c2_cob_domain  client_cob_dom;

	char *server_argv[] = {
		"rpclib_ut", "-r", "-T", "AD", "-D", SERVER_DB_FILE_NAME,
		"-S", SERVER_STOB_FILE_NAME, "-e", SERVER_ENDPOINT,
		"-s", "ds1", "-s", "ds2"
	};

	C2_RPC_SERVER_CTX_DECLARE_SIMPLE(sctx, xprt, server_argv,
				  SERVER_LOG_FILE_NAME);

	struct c2_rpc_client_ctx cctx = {
		.rcx_net_dom               = &client_net_dom,
		.rcx_local_addr            = CLIENT_ENDPOINT_ADDR,
		.rcx_remote_addr           = SERVER_ENDPOINT_ADDR,
		.rcx_db_name               = CLIENT_DB_NAME,
		.rcx_dbenv                 = &client_dbenv,
		.rcx_cob_dom_id            = CLIENT_COB_DOM_ID,
		.rcx_cob_dom               = &client_cob_dom,
		.rcx_nr_slots              = SESSION_SLOTS,
		.rcx_timeout_s             = CONNECT_TIMEOUT,
		.rcx_max_rpcs_in_flight    = MAX_RPCS_IN_FLIGHT,
		.rcx_recv_queue_min_length = C2_NET_TM_RECV_QUEUE_DEF_LEN,
	};

	/*
	 * There is no need to initialize xprt explicitly if client and server
	 * run withing a single process, because in this case transport is
	 * initialized by c2_rpc_server_start().
	 */

	rc = c2_net_domain_init(&client_net_dom, xprt);
	C2_UT_ASSERT(rc == 0);
	if (rc != 0)
		goto out;

	rc = c2_rpc_server_start(&sctx);
	C2_UT_ASSERT(rc == 0);
	if (rc != 0)
		goto net_dom_fini;

	rc = c2_rpc_client_init(&cctx);
	C2_UT_ASSERT(rc == 0);
	if (rc != 0)
		goto server_fini;

	rc = send_fop(&cctx.rcx_session);
	C2_UT_ASSERT(rc == 0);

	rc = c2_rpc_client_fini(&cctx);
	C2_UT_ASSERT(rc == 0);

server_fini:
	c2_rpc_server_stop(&sctx);
net_dom_fini:
	c2_net_domain_fini(&client_net_dom);
out:
	return;
}

static int test_rpclib_init(void)
{
	/* set ADDB leve to AEL_WARN to see ADDB messages on STDOUT */
	/*c2_addb_choose_default_level(AEL_WARN);*/

	return 0;
}

static int test_rpclib_fini(void)
{
	return 0;
}

const struct c2_test_suite rpclib_ut = {
	.ts_name = "rpclib-ut",
	.ts_init = test_rpclib_init,
	.ts_fini = test_rpclib_fini,
	.ts_tests = {
		{ "rpclib", test_rpclib },
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
