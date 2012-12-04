/* -*- c -*- */
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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 23-Oct-2012
 */

#include "fop/fop.h"
#include "conf/onwire.h"
#include "conf/onwire_xc.h"
#include "conf/conf_fop.h"
#include "net/lnet/lnet.h"
#include "rpc/rpclib.h"
#include "lib/memory.h"
#include "lib/cdefs.h"
#include "lib/arith.h"
#include "lib/misc.h" /* M0_SET0 */
#include "lib/buf.h"
#include "lib/ut.h"
#include "ut/rpc.h"
#include <stdlib.h>

#define CLIENT_ENDPOINT_ADDR  "0@lo:12345:34:*"
#define CLIENT_DB_NAME        "conf_ut_client.db"

#define SERVER_ENDPOINT_ADDR  "0@lo:12345:34:1"
#define SERVER_ENDPOINT       "lnet:" SERVER_ENDPOINT_ADDR
#define SERVER_DB_FILE_NAME   "conf_ut_server.db"
#define SERVER_STOB_FILE_NAME "conf_ut_server.stob"
#define SERVER_LOG_FILE_NAME  "conf_ut_server.log"

enum {
	CLIENT_COB_DOM_ID  = 16,
	SESSION_SLOTS      = 10,
	MAX_RPCS_IN_FLIGHT = 1,
	TIMEOUT            = 5
};

static struct m0_net_xprt  *xprt = &m0_net_lnet_xprt;
static struct m0_net_domain client_net_dom = {};
static struct m0_dbenv      client_dbenv;
static struct m0_cob_domain client_cob_dom;

static struct m0_rpc_client_ctx cctx = {
	.rcx_net_dom               = &client_net_dom,
	.rcx_local_addr            = CLIENT_ENDPOINT_ADDR,
	.rcx_remote_addr           = SERVER_ENDPOINT_ADDR,
	.rcx_db_name               = CLIENT_DB_NAME,
	.rcx_dbenv                 = &client_dbenv,
	.rcx_cob_dom_id            = CLIENT_COB_DOM_ID,
	.rcx_cob_dom               = &client_cob_dom,
	.rcx_nr_slots              = SESSION_SLOTS,
	.rcx_timeout_s             = TIMEOUT,
	.rcx_max_rpcs_in_flight    = MAX_RPCS_IN_FLIGHT,
	.rcx_recv_queue_min_length = M0_NET_TM_RECV_QUEUE_DEF_LEN,
};

static char *server_argv[] = {
	"conf_ut", "-r", "-p", "-T", "AD", "-D", SERVER_DB_FILE_NAME,
	"-S", SERVER_STOB_FILE_NAME, "-e", SERVER_ENDPOINT, "-s", "confd"
};

static M0_RPC_SERVER_CTX_DEFINE(sctx, &xprt, 1, server_argv,
				ARRAY_SIZE(server_argv), NULL, 0,
				SERVER_LOG_FILE_NAME);

static void conf_net_init(void)
{
	int rc;

	rc = m0_net_xprt_init(xprt);
	M0_UT_ASSERT(rc == 0);

	rc = m0_net_domain_init(&client_net_dom, xprt);
	M0_UT_ASSERT(rc == 0);

	rc = m0_rpc_server_start(&sctx);
	M0_UT_ASSERT(rc == 0);

	rc = m0_rpc_client_init(&cctx);
	M0_UT_ASSERT(rc == 0);
}

static void conf_net_fini(void)
{
	M0_UT_ASSERT(m0_rpc_client_fini(&cctx) == 0);
	m0_rpc_server_stop(&sctx);
	m0_net_domain_fini(&client_net_dom);
	m0_net_xprt_fini(xprt);
}

static void conf_net_ping(void)
{
	struct m0_fop             *req;
	struct m0_rpc_item        *item;
	struct m0_conf_fetch_resp *resp;
	int                        rc;

	req = m0_fop_alloc(&m0_conf_fetch_fopt, NULL);
	M0_UT_ASSERT(req != NULL);
	{
		struct m0_conf_fetch *r = m0_fop_data(req);

		r->f_origin.oi_type = 999;
		r->f_origin.oi_id = (const struct m0_buf)M0_BUF_INIT0;
		r->f_path.ab_count = 0;
	}

	rc = m0_rpc_client_call(req, &cctx.rcx_session,
				&m0_fop_default_item_ops, 0, TIMEOUT);
	M0_UT_ASSERT(rc == 0);

	item = &req->f_item;
	M0_UT_ASSERT(item->ri_error == 0);
	M0_UT_ASSERT(item->ri_reply != NULL);

	resp = m0_fop_data(m0_rpc_item_to_fop(item->ri_reply));
	M0_UT_ASSERT(resp != NULL);
}

void test_conf_net(void)
{
	conf_net_init();
	conf_net_ping();
	conf_net_fini();
}
