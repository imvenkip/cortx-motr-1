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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 08/06/2011
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "lib/ut.h"
#include "lib/memory.h"
#include "lib/processor.h"
#include "fop/fop.h"
#include "reqh/reqh.h"
#include "rpc/rpclib.h"
#include "net/lnet/lnet.h"
#include "ut/rpc.h"

#include "fop/ut/long_lock/rdwr_fop.h"
#include "fop/ut/long_lock/rdwr_fom.h"
#include "fop/ut/long_lock/rdwr_fop_u.h"
#include "fop/ut/long_lock/rdwr_test_bench.h"

enum {
	CLIENT_COB_DOM_ID	= 16,
	SESSION_SLOTS		= RDWR_REQUEST_MAX,
	MAX_RPCS_IN_FLIGHT	= RDWR_REQUEST_MAX,
	CONNECT_TIMEOUT		= 5,
};

#define CLIENT_ENDPOINT_ADDR    "0@lo:12345:34:*"
#define CLIENT_DB_NAME		"libfop_lock_ut_client.db"

#define SERVER_ENDPOINT_ADDR	"0@lo:12345:34:1"
#define SERVER_ENDPOINT		"lnet:" SERVER_ENDPOINT_ADDR
#define SERVER_DB_FILE_NAME	"libfop_lock_ut_server.db"
#define SERVER_STOB_FILE_NAME	"libfop_lock_ut_server.stob"
#define SERVER_LOG_FILE_NAME	"libfop_lock_ut_server.log"

static struct c2_net_xprt    *xprt = &c2_net_lnet_xprt;
static struct c2_net_domain   client_net_dom = { };
static struct c2_dbenv        client_dbenv;
static struct c2_cob_domain   client_cob_dom;

static struct c2_rpc_client_ctx cctx = {
	.rcx_net_dom		   = &client_net_dom,
	.rcx_local_addr            = CLIENT_ENDPOINT_ADDR,
	.rcx_remote_addr           = SERVER_ENDPOINT_ADDR,
	.rcx_db_name		   = CLIENT_DB_NAME,
	.rcx_dbenv		   = &client_dbenv,
	.rcx_cob_dom_id		   = CLIENT_COB_DOM_ID,
	.rcx_cob_dom		   = &client_cob_dom,
	.rcx_nr_slots		   = SESSION_SLOTS,
	.rcx_timeout_s		   = CONNECT_TIMEOUT,
	.rcx_max_rpcs_in_flight	   = MAX_RPCS_IN_FLIGHT,
	.rcx_recv_queue_min_length = C2_NET_TM_RECV_QUEUE_DEF_LEN,
};

static char *server_argv[] = {
	"libfop_lock_ut", "-r", "-T", "AD", "-D", SERVER_DB_FILE_NAME,
	"-S", SERVER_STOB_FILE_NAME, "-e", SERVER_ENDPOINT,
	"-s", "ds1", "-s", "ds2"
};

static struct c2_rpc_server_ctx sctx = {
	.rsx_xprts            = &xprt,
	.rsx_xprts_nr         = 1,
	.rsx_argv             = server_argv,
	.rsx_argc             = ARRAY_SIZE(server_argv),
	.rsx_service_types    = cs_default_stypes,
	.rsx_service_types_nr = 2,
	.rsx_log_file_name    = SERVER_LOG_FILE_NAME,
};

static void test_long_lock(void)
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

	c2_rdwr_send_fop(&cctx.rcx_session);

	rc = c2_rpc_client_fini(&cctx);
	C2_UT_ASSERT(rc == 0);

server_fini:
	c2_rpc_server_stop(&sctx);

	return;
}

static int test_long_lock_init(void)
{
	int rc;

	/* set ADDB leve to AEL_WARN to see ADDB messages on STDOUT */
	/*c2_addb_choose_default_level(AEL_WARN);*/

	rc = c2_rdwr_fop_init();
	C2_ASSERT(rc == 0);

	rc = c2_net_xprt_init(xprt);
	C2_ASSERT(rc == 0);

	rc = c2_net_domain_init(&client_net_dom, xprt);
	C2_ASSERT(rc == 0);

	return rc;
}

static int test_long_lock_fini(void)
{
	c2_net_domain_fini(&client_net_dom);
	c2_net_xprt_fini(xprt);
	c2_rdwr_fop_fini();
	return 0;
}

const struct c2_test_suite fop_lock_ut = {
	.ts_name = "fop-lock-ut",
	.ts_init = test_long_lock_init,
	.ts_fini = test_long_lock_fini,
	.ts_tests = {
		{ "fop-lock", test_long_lock },
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
