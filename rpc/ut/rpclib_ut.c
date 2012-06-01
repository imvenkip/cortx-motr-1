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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 09/28/2011
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/ut.h"
#include "lib/memory.h"
#include "lib/processor.h"
#include "lib/trace.h"
#include "lib/finject.h"
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
#include "net/lnet/lnet.h"

#include "ut/rpc.h"
#include "ut/cs_service.h"
#include "ut/cs_fop_foms.h"
#include "ut/cs_test_fops_u.h"

#define CLIENT_DB_NAME		"rpclib_ut_client.db"

#define SERVER_ENDPOINT_ADDR	"127.0.0.1@tcp:12345:34:1"
#define SERVER_ENDPOINT		"lnet:" SERVER_ENDPOINT_ADDR
#define SERVER_DB_FILE_NAME	"rpclib_ut_server.db"
#define SERVER_STOB_FILE_NAME	"rpclib_ut_server.stob"
#define SERVER_LOG_FILE_NAME	"rpclib_ut_server.log"

enum {
	CLIENT_COB_DOM_ID	= 16,
	SESSION_SLOTS		= 1,
	MAX_RPCS_IN_FLIGHT	= 1,
	CONNECT_TIMEOUT		= 5,
};

struct c2_net_xprt    *xprt = &c2_net_lnet_xprt;
struct c2_net_domain   client_net_dom = { };
struct c2_dbenv        client_dbenv;
struct c2_cob_domain   client_cob_dom;
char client_addr[C2_NET_LNET_XEP_ADDR_LEN] = "127.0.0.1@tcp:12345:34:2";
char server_addr[C2_NET_LNET_XEP_ADDR_LEN] = "127.0.0.1@tcp:12345:34:1";

struct c2_rpc_client_ctx cctx = {
	.rcx_net_dom		   = &client_net_dom,
	.rcx_db_name		   = CLIENT_DB_NAME,
	.rcx_dbenv		   = &client_dbenv,
	.rcx_cob_dom_id		   = CLIENT_COB_DOM_ID,
	.rcx_cob_dom		   = &client_cob_dom,
	.rcx_nr_slots		   = SESSION_SLOTS,
	.rcx_timeout_s		   = CONNECT_TIMEOUT,
	.rcx_max_rpcs_in_flight	   = MAX_RPCS_IN_FLIGHT,
	.rcx_recv_queue_min_length = C2_NET_TM_RECV_QUEUE_DEF_LEN,
};

char *server_argv[] = {
	"rpclib_ut", "-r", "-T", "AD", "-D", SERVER_DB_FILE_NAME,
	"-S", SERVER_STOB_FILE_NAME, "-e", SERVER_ENDPOINT,
	"-s", "ds1", "-s", "ds2"
};

struct c2_rpc_server_ctx sctx = {
	.rsx_xprts            = &xprt,
	.rsx_xprts_nr         = 1,
	.rsx_argv             = server_argv,
	.rsx_argc             = ARRAY_SIZE(server_argv),
	.rsx_service_types    = cs_default_stypes,
	.rsx_service_types_nr = 2,
	.rsx_log_file_name    = SERVER_LOG_FILE_NAME,
};

#ifdef ENABLE_FAULT_INJECTION
static void test_c2_rpc_server_start(void)
{
	c2_fi_enable_once("c2_reqh_service_type_register", "fake_error");
	C2_UT_ASSERT(c2_rpc_server_start(&sctx) != 0);

	c2_fi_enable_once("c2_cs_init", "fake_error");
	C2_UT_ASSERT(c2_rpc_server_start(&sctx) != 0);

	c2_fi_enable_once("c2_cs_setup_env", "fake_error");
	C2_UT_ASSERT(c2_rpc_server_start(&sctx) != 0);
}

static void test_c2_rpc_client_start(void)
{
	int rc;

	rc = c2_rpc_server_start(&sctx);
	C2_UT_ASSERT(rc == 0);
	if (rc != 0)
		return;

	c2_fi_enable_once("c2_rpc_machine_init", "fake_error");
	C2_UT_ASSERT(c2_rpc_client_init(&cctx) != 0);

	c2_fi_enable_once("c2_net_end_point_create", "fake_error");
	C2_UT_ASSERT(c2_rpc_client_init(&cctx) != 0);

	c2_fi_enable_once("c2_rpc_conn_create", "fake_error");
	C2_UT_ASSERT(c2_rpc_client_init(&cctx) != 0);

	c2_fi_enable_once("c2_rpc_conn_establish", "fake_error");
	C2_UT_ASSERT(c2_rpc_client_init(&cctx) != 0);

	c2_fi_enable_once("c2_rpc_session_establish", "fake_error");
	C2_UT_ASSERT(c2_rpc_client_init(&cctx) != 0);

	c2_rpc_server_stop(&sctx);
}

static void test_rpclib_error_paths(void)
{
	test_c2_rpc_server_start();
	test_c2_rpc_client_start();
}
#else
static void test_rpclib_error_paths(void)
{
}
#endif

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
	int                    rc;

	/*
	 * There is no need to initialize xprt explicitly if client and server
	 * run withing a single process, because in this case transport is
	 * initialized by c2_rpc_server_start().
	 */

	rc = c2_rpc_server_start(&sctx);
	C2_UT_ASSERT(rc == 0);
	if (rc != 0)
		return;

	rc = c2_rpc_client_init(&cctx);
	C2_UT_ASSERT(rc == 0);
	if (rc != 0)
		goto server_fini;

	rc = send_fop(&cctx.rcx_session);
	C2_UT_ASSERT(rc == 0);

	rc = c2_rpc_client_fini(&cctx);
server_fini:
	c2_rpc_server_stop(&sctx);
	return;
}

static int test_rpclib_init(void)
{
	int rc;

	/* set ADDB leve to AEL_WARN to see ADDB messages on STDOUT */
	/*c2_addb_choose_default_level(AEL_WARN);*/

	rc = c2_net_xprt_init(xprt);
	C2_ASSERT(rc == 0);

	rc = c2_net_domain_init(&client_net_dom, xprt);
	C2_ASSERT(rc == 0);

	rc = c2_lut_lhost_lnet_conv(&client_net_dom, client_addr);
	C2_ASSERT(rc == 0);
	rc = c2_lut_lhost_lnet_conv(&client_net_dom, server_addr);
	C2_ASSERT(rc == 0);
	cctx.rcx_local_addr  = client_addr;
	cctx.rcx_remote_addr = server_addr;

	return rc;
}

static int test_rpclib_fini(void)
{
	c2_net_domain_fini(&client_net_dom);
	c2_net_xprt_fini(xprt);
	return 0;
}

const struct c2_test_suite rpclib_ut = {
	.ts_name = "rpclib-ut",
	.ts_init = test_rpclib_init,
	.ts_fini = test_rpclib_fini,
	.ts_tests = {
		{ "rpclib", test_rpclib },
		{ "rpclib_error_paths", test_rpclib_error_paths },
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
