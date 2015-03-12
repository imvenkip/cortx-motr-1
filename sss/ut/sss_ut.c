/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Igor Perelyotov <igor.m.perelyotov@seagate.com>
 * Original creation date: 04-Mar-2015
 */


#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"
#include "lib/assert.h"
#include "lib/buf.h"           /* m0_buf_init */
#include "lib/finject.h"       /* m0_fi_enable, m0_fi_disable */
#include "lib/memory.h"        /* M0_ALLOC_PTR */
#include "fid/fid.h"           /* m0_fid */
#include "net/net.h"
#include "net/lnet/lnet.h"     /* m0_net_lnet_xprt */
#include "fop/fop.h"
#include "rpc/item.h"
#include "rpc/rpc_opcodes.h"
#include "rpc/rpclib.h"
#include "reqh/reqh_service.h" /* m0_service_health */
#include "ut/ut.h"
#include "ut/file_helpers.h"   /* M0_UT_CONF_PATH */

#include "sss/ss_fops.h"

#define SERVER_DB_NAME        "sss_ut_server.db"
#define SERVER_STOB_NAME      "sss_ut_server.stob"
#define SERVER_ADDB_STOB_NAME "linuxstob:sss_ut_server.addb_stob"
#define SERVER_LOG_NAME       "sss_ut_server.log"
#define SERVER_ENDPOINT_ADDR  "0@lo:12345:34:1"
#define SERVER_ENDPOINT       "lnet:" SERVER_ENDPOINT_ADDR

#define CLIENT_DB_NAME        "sss_ut_client.db"
#define CLIENT_ENDPOINT_ADDR  "0@lo:12345:34:*"

#define SERVICE_NAME "mdservice"

enum {
	MAX_RPCS_IN_FLIGHT = 1,
};

static const struct m0_fid ut_fid = {
	.f_container = 8286623314361712755,
	.f_key       = 1
};
static struct m0_net_domain    client_net_dom;
static struct m0_net_xprt     *xprt = &m0_net_lnet_xprt;

static char *server_argv[] = {
	"sss_ut", "-T", "AD", "-D", SERVER_DB_NAME,
	"-S", SERVER_STOB_NAME, "-A", SERVER_ADDB_STOB_NAME,
	"-e", SERVER_ENDPOINT, "-w", "10",
	"-c", M0_UT_CONF_PATH("conf-str.txt"), "-P", M0_UT_CONF_PROFILE
};

static struct m0_rpc_server_ctx sctx = {
	.rsx_xprts         = &xprt,
	.rsx_xprts_nr      = 1,
	.rsx_argv          = server_argv,
	.rsx_argc          = ARRAY_SIZE(server_argv),
	.rsx_log_file_name = SERVER_LOG_NAME,
};

static struct m0_rpc_client_ctx cctx = {
	.rcx_net_dom            = &client_net_dom,
	.rcx_local_addr         = CLIENT_ENDPOINT_ADDR,
	.rcx_remote_addr        = SERVER_ENDPOINT_ADDR,
	.rcx_max_rpcs_in_flight = MAX_RPCS_IN_FLIGHT,
};

static void rpc_client_and_server_start(void)
{
	int rc;

	rc = m0_net_domain_init(&client_net_dom, xprt);
	M0_ASSERT(rc == 0);
	/* Test case: memory error on service allocate */
	m0_fi_enable("ss_svc_rsto_service_allocate", "fail_allocation");
	rc = m0_rpc_server_start(&sctx);
	M0_UT_ASSERT(rc != 0);
	m0_fi_disable("ss_svc_rsto_service_allocate", "fail_allocation");
	m0_rpc_server_stop(&sctx);
	/* Normal start */
	rc = m0_rpc_server_start(&sctx);
	M0_ASSERT(rc == 0);
	rc = m0_rpc_client_start(&cctx);
	M0_ASSERT(rc == 0);
}

static void rpc_client_and_server_stop(void)
{
	int rc;
	M0_LOG(M0_DEBUG, "stop");
	rc = m0_rpc_client_stop(&cctx);
	M0_ASSERT(rc == 0);
	m0_rpc_server_stop(&sctx);
	m0_net_domain_fini(&client_net_dom);
}

static struct m0_fop *sss_ut_fop_alloc(const char *name, uint32_t cmd)
{
	struct m0_fop     *fop;
	struct m0_sss_req *ss_fop;

	M0_ALLOC_PTR(fop);
	M0_ASSERT(fop != NULL);

	M0_ALLOC_PTR(ss_fop);
	M0_ASSERT(ss_fop != NULL);

	m0_buf_init(&ss_fop->ss_name, (void *)name, strlen(name));
	ss_fop->ss_cmd = cmd;
	ss_fop->ss_id  = ut_fid;

	m0_fop_init(fop, &m0_fop_ss_fopt, (void *)ss_fop, m0_ss_fop_release);

	return fop;
}

static void sss_ut_req(uint32_t cmd,
		       int32_t  expected_rc,
		       uint32_t expected_state)
{
	int                 rc;
	struct m0_fop      *fop;
	struct m0_fop      *rfop;
	struct m0_rpc_item *item;
	struct m0_sss_rep  *ss_rfop;

	fop = sss_ut_fop_alloc(SERVICE_NAME, cmd);
	item = &fop->f_item;
	rc = m0_rpc_post_sync(fop, &cctx.rcx_session, NULL, 0);
	M0_UT_ASSERT(rc == 0);

	rfop = m0_rpc_item_to_fop(item->ri_reply);
	M0_UT_ASSERT(rfop != NULL);

	ss_rfop = m0_fop_data(rfop);
	M0_UT_ASSERT(ss_rfop->ssr_rc == expected_rc);

	if (expected_state != 0)
		M0_UT_ASSERT(ss_rfop->ssr_state == expected_state);

	m0_fop_put_lock(fop);
}

static int sss_ut_init(void)
{
	M0_ENTRY();
	rpc_client_and_server_start();
	M0_LEAVE();
	return M0_RC(0);
}

static int sss_ut_fini(void)
{
	M0_ENTRY();
	rpc_client_and_server_stop();
	M0_LEAVE();
	return M0_RC(0);
}

static void sss_commands_test(void)
{
	/* init */
	sss_ut_req(M0_SERVICE_STATUS, -ENOENT, 0);
	sss_ut_req(M0_SERVICE_INIT, 0, M0_RST_INITIALISED);
	sss_ut_req(M0_SERVICE_STATUS, 0, M0_RST_INITIALISED);

	/* start */
	sss_ut_req(M0_SERVICE_START, 0, M0_RST_STARTED);
	sss_ut_req(M0_SERVICE_STATUS, 0, M0_RST_STARTED);

	/* health */
	/* health is not implemented for mdservice now */
	sss_ut_req(M0_SERVICE_HEALTH, M0_HEALTH_UNKNOWN, M0_RST_STARTED);

	/* quiesce */
	sss_ut_req(M0_SERVICE_QUIESCE, 0, M0_RST_STOPPING);
	sss_ut_req(M0_SERVICE_STATUS, 0, M0_RST_STOPPING);

	/* stop */
	sss_ut_req(M0_SERVICE_STOP, 0, M0_RST_STOPPED);
	sss_ut_req(M0_SERVICE_STATUS, -ENOENT, 0);
}

const struct m0_ut_suite sss_ut = {
	.ts_name = "sss-ut",
	.ts_init = sss_ut_init,
	.ts_fini = sss_ut_fini,
	.ts_tests = {
		{ "commands", sss_commands_test },
		{ NULL, NULL },
	},
};
M0_EXPORTED(sss_ut);

#undef M0_TRACE_SUBSYSTEM


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
