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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 12/10/2011
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/ut.h"    /* C2_UT_ASSERT */
#include "lib/misc.h"  /* C2_SET_ARR0 */
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/tlist.h"

#include "ut/rpc.h"
#include "rpc/rpclib.h"
#include "fop/fop.h"
#include "net/bulk_sunrpc.h"
#include "net/lnet/lnet.h"
#include "net/bulk_mem.h"
#include "reqh/reqh_service.h"
#include "colibri/colibri_setup.h"

#include "fop/fop_format_def.h"

#include "ut/cs_service.h"
#include "ut/cs_fop_foms.h"
#include "ut/cs_test_fops_u.h"
#include "ut/cs_test_fops.ff"
#include "rpc/rpc_opcodes.h"

extern const struct c2_tl_descr ndoms_descr;

/* Client context */
struct cl_ctx {
	/* Client network domain.*/
	struct c2_net_domain cl_ndom;
	/* Client db.*/
	struct c2_dbenv      cl_dbenv;
	/* Client cob domain.*/
	struct c2_cob_domain cl_cdom;
	/* Client rpc context.*/
	struct c2_rpc_client_ctx    cl_ctx;
};

/* Configures colibri environment with given parameters. */
static char *cs_ut_service_one_cmd[] = { "colibri_setup", "-r", "-T", "linux",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "bulk-sunrpc:127.0.0.1:34567:2",
                                "-s", "ds1"};

static char *cs_ut_services_many_cmd[] = { "colibri_setup", "-r", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "bulk-sunrpc:127.0.0.1:34567:2",
                                "-e", "bulk-mem:127.0.0.1:35678",
                                "-s", "ds1", "-s" "ds2"};

static char *cs_ut_reqhs_many_cmd[] = { "colibri_setup", "-r", "-T", "linux",
                                "-D", "cs_r1sdb", "-S", "cs_r1stob",
                                "-e", "bulk-sunrpc:127.0.0.1:34567:2",
                                "-s", "ds1",
				"-r", "-T", "AD",
                                "-D", "cs_r2sdb", "-S", "cs_r2stob",
                                "-e", "bulk-mem:127.0.0.1:35678",
                                "-s" "ds2"};

static char *cs_ut_opts_jumbled_cmd[] = { "colibri_setup", "-r", "-D",
                                "cs_sdb", "-T", "AD", "-s", "ds1",
                                "-e", "bulk-sunrpc:127.0.0.1:34567:2",
                                "-S", "cs_stob"};

static char *cs_ut_reqh_none_cmd[] = { "colibri_setup", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "bulk-sunrpc:127.0.0.1:34567:2",
                                "-s", "ds1"};

static char *cs_ut_stype_bad_cmd[] = { "colibri_setup", "-r", "-T", "asdadd",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "bulk-sunrpc:127.0.0.1:34567:2",
                                "-s", "ds1"};

static char *cs_ut_xprt_bad_cmd[] = { "colibri_setup", "-r", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "asdasdada:127.0.0.1:34567:2",
                                "-s", "ds1"};

static char *cs_ut_ep_bad_cmd[] = { "colibri_setup", "-r", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "bulk-sunrpc:asdad:asdsd:sadasd",
                                "-s", "ds1"};

static char *cs_ut_service_bad_cmd[] = { "colibri_setup", "-r", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "bulk-sunrpc:127.0.0.1:34567:2",
                                "-s", "dasdadasd"};

static char *cs_ut_args_bad_cmd[] = { "colibri_setup", "-r", "-D", "cs_sdb",
                                "-S", "cs_stob", "-e",
                                "bulk-sunrpc:127.0.0.1:34567:2"};

static char *cs_ut_buffer_pool_cmd[] = { "colibri_setup", "-r", "-T", "linux",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "bulk-sunrpc:127.0.0.1:34567:2",
                                "-s", "ds1", "-q 4", "-m 4096"};

static char *cs_ut_lnet_cmd[] = { "colibri_setup", "-r", "-T", "linux",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "lnet:127.0.0.1@tcp:12345:31:0",
                                "-s", "ds1", "-n 128", "-p 4096", "-b 128",
				"-t 16", "-m 64", "-k 4096", "-R 1"};

static const char *cl_ep_addrs[] = {
				"127.0.0.1:34567:1",
				"127.0.0.1:34569"};

static const char *srv_ep_addrs[] = {
				"127.0.0.1:34567:2",
				"127.0.0.1:35678"};

static const char *cl_lnet_ep_addrs[] = {
				"127.0.0.1@tcp:12345:31:0",
				"127.0.0.1@tcp:12345:31:*"};

static const char *srv_lnet_ep_addrs[] = {
				"127.0.0.1@tcp:12345:31:0",
				"127.0.0.1@tcp:12345:31:*"};


static const char *cdbnames[] = {
				"cdb1",
				"cdb2"};

/*
  Transports used in colibri a context.
 */
static struct c2_net_xprt *cs_xprts[] = {
	&c2_net_bulk_sunrpc_xprt,
	&c2_net_bulk_mem_xprt,
	&c2_net_lnet_xprt,
};

static int cl_cdom_id = 10001;

enum {
	MAX_RPCS_IN_FLIGHT = 10,
	MAX_RPC_SLOTS_NR   = 2,
	RPC_TIMEOUTS       = 5
};

enum {
	BULK_SUNRPC_XPRT,
	BULK_MEM_XPRT
};

#define SERVER_LOG_FILE_NAME	"cs_ut.errlog"

static int cs_ut_client_init(struct cl_ctx *cctx, const char *cl_ep_addr,
			const char *srv_ep_addr, const char* dbname,
			struct c2_net_xprt *xprt)
{
	int rc;
	struct c2_rpc_client_ctx *cl_ctx;

	C2_PRE(cctx != NULL && cl_ep_addr != NULL && srv_ep_addr != NULL &&
		dbname != NULL && xprt != NULL);

	rc = c2_net_domain_init(&cctx->cl_ndom, xprt);
	C2_UT_ASSERT(rc == 0);

	cl_ctx = &cctx->cl_ctx;

	cl_ctx->rcx_net_dom            = &cctx->cl_ndom;
	cl_ctx->rcx_local_addr         = cl_ep_addr;
	cl_ctx->rcx_remote_addr        = srv_ep_addr;
	cl_ctx->rcx_db_name            = dbname;
	cl_ctx->rcx_dbenv              = &cctx->cl_dbenv;
	cl_ctx->rcx_cob_dom_id         = ++cl_cdom_id;
	cl_ctx->rcx_cob_dom            = &cctx->cl_cdom;
	cl_ctx->rcx_nr_slots           = MAX_RPC_SLOTS_NR;
	cl_ctx->rcx_timeout_s          = RPC_TIMEOUTS;
	cl_ctx->rcx_max_rpcs_in_flight = MAX_RPCS_IN_FLIGHT;

	rc = c2_rpc_client_init(cl_ctx);
	C2_UT_ASSERT(rc == 0);

	return rc;
}

static void cs_ut_client_fini(struct cl_ctx *cctx)
{
	int rc;

	C2_PRE(cctx != NULL);

	rc = c2_rpc_client_fini(&cctx->cl_ctx);
	C2_UT_ASSERT(rc == 0);

	c2_net_domain_fini(&cctx->cl_ndom);
}

/*
  Sends fops to server.
 */
int c2_cs_ut_send_fops(struct c2_rpc_session *cl_rpc_session, int dstype)
{
	int                      rc;
        uint32_t                 i;
        struct c2_fop           *fop[10] = { 0 };
	struct cs_ds1_req_fop   *cs_ds1_fop;
	struct cs_ds2_req_fop   *cs_ds2_fop;

	C2_PRE(cl_rpc_session != NULL && dstype > 0);

	switch (dstype) {
	case CS_UT_SERVICE1:
		for (i = 0; i < 10; ++i) {
			fop[i] = c2_fop_alloc(&cs_ds1_req_fop_fopt, NULL);
			cs_ds1_fop = c2_fop_data(fop[i]);
			cs_ds1_fop->csr_value = i;
			rc = c2_rpc_client_call(fop[i], cl_rpc_session,
						&cs_ds_req_fop_rpc_item_ops, 60);
			C2_UT_ASSERT(rc == 0);
		}
		break;
	case CS_UT_SERVICE2:
		for (i = 0; i < 10; ++i) {
			fop[i] = c2_fop_alloc(&cs_ds2_req_fop_fopt, NULL);
			cs_ds2_fop = c2_fop_data(fop[i]);
			cs_ds2_fop->csr_value = i;
			rc = c2_rpc_client_call(fop[i], cl_rpc_session,
						&cs_ds_req_fop_rpc_item_ops, 60);
			C2_UT_ASSERT(rc == 0);
		}
		break;
	default:
		C2_ASSERT("Invalid service type" == 0);
	}

	return rc;
}

static int cs_ut_test_helper_success(struct cl_ctx *cctx, size_t cctx_nr,
				     char *cs_argv[], int cs_argc)
{
	int rc;
	int i;
	int stype;

	C2_RPC_SERVER_CTX_DECLARE(sctx, cs_xprts, ARRAY_SIZE(cs_xprts),
				  cs_argv, cs_argc, SERVER_LOG_FILE_NAME);

	rc = c2_rpc_server_start(&sctx);
	C2_UT_ASSERT(rc == 0);

	for (i = 0; i < cctx_nr; ++i) {
		rc = cs_ut_client_init(&cctx[i], cl_ep_addrs[i], srv_ep_addrs[i],
				       cdbnames[i], cs_xprts[i]);
		C2_UT_ASSERT(rc == 0);
	}

	stype = CS_UT_SERVICE1;
	for (i = 0; i < cctx_nr; ++i, ++stype)
		c2_cs_ut_send_fops(&cctx[i].cl_ctx.rcx_session, stype);

	for (i = 0; i < cctx_nr; ++i)
		cs_ut_client_fini(&cctx[i]);

	c2_rpc_server_stop(&sctx);

	return rc;
}

static void cs_ut_test_helper_failure(char *cs_argv[], int cs_argc)
{
	int rc;

	C2_RPC_SERVER_CTX_DECLARE(sctx, cs_xprts, ARRAY_SIZE(cs_xprts),
				  cs_argv, cs_argc, SERVER_LOG_FILE_NAME);

	rc = c2_rpc_server_start(&sctx);
	C2_UT_ASSERT(rc != 0);

	/*
	 * If, for some reason, c2_rpc_server_start() completed without error,
	 * we need to stop server.
	 */
	if (rc == 0)
		c2_rpc_server_stop(&sctx);
}

static void test_cs_ut_service_one(void)
{
	struct cl_ctx  cctx[1] = { };

	cs_ut_test_helper_success(cctx, ARRAY_SIZE(cctx), cs_ut_service_one_cmd,
				  ARRAY_SIZE(cs_ut_service_one_cmd));
}

static void test_cs_ut_services_many(void)
{
	struct cl_ctx  cctx[2] = { };

	cs_ut_test_helper_success(cctx, ARRAY_SIZE(cctx), cs_ut_services_many_cmd,
				  ARRAY_SIZE(cs_ut_services_many_cmd));
}

static void test_cs_ut_reqhs_many(void)
{
	struct cl_ctx  cctx[2] = { };

	cs_ut_test_helper_success(cctx, ARRAY_SIZE(cctx), cs_ut_reqhs_many_cmd,
				  ARRAY_SIZE(cs_ut_reqhs_many_cmd));
}

static void test_cs_ut_opts_jumbled(void)
{
	struct cl_ctx  cctx[1] = { };

	cs_ut_test_helper_success(cctx, ARRAY_SIZE(cctx), cs_ut_opts_jumbled_cmd,
				  ARRAY_SIZE(cs_ut_opts_jumbled_cmd));
}

/*
  Tests server side bad colibri setup commands.
 */
static void test_cs_ut_reqh_none(void)
{
	cs_ut_test_helper_failure(cs_ut_reqh_none_cmd,
				  ARRAY_SIZE(cs_ut_reqh_none_cmd));
}

static void test_cs_ut_stype_bad(void)
{
	cs_ut_test_helper_failure(cs_ut_stype_bad_cmd,
				  ARRAY_SIZE(cs_ut_stype_bad_cmd));
}

static void test_cs_ut_xprt_bad(void)
{
	cs_ut_test_helper_failure(cs_ut_xprt_bad_cmd,
				  ARRAY_SIZE(cs_ut_xprt_bad_cmd));
}

static void test_cs_ut_ep_bad(void)
{
	cs_ut_test_helper_failure(cs_ut_ep_bad_cmd,
				  ARRAY_SIZE(cs_ut_ep_bad_cmd));
}

static void test_cs_ut_service_bad(void)
{
	cs_ut_test_helper_failure(cs_ut_service_bad_cmd,
				  ARRAY_SIZE(cs_ut_service_bad_cmd));
}

static void test_cs_ut_args_bad(void)
{
	cs_ut_test_helper_failure(cs_ut_args_bad_cmd,
				  ARRAY_SIZE(cs_ut_args_bad_cmd));
}

static void test_cs_ut_buffer_pool(void)
{
	struct cl_ctx  cctx[1] = { };

	cs_ut_test_helper_success(cctx, ARRAY_SIZE(cctx), cs_ut_buffer_pool_cmd,
				  ARRAY_SIZE(cs_ut_buffer_pool_cmd));
}

static void test_cs_ut_lnet(void)
{
	return;
}

const struct c2_test_suite colibri_setup_ut = {
        .ts_name = "colibri_setup-ut",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "cs-single-service", test_cs_ut_service_one},
		{ "cs-multiple-services", test_cs_ut_services_many},
		{ "cs-multiple-request-handlers", test_cs_ut_reqhs_many},
		{ "cs-command-options-jumbled", test_cs_ut_opts_jumbled},
		{ "cs-missing-reqh-option", test_cs_ut_reqh_none},
		{ "cs-bad-storage-type", test_cs_ut_stype_bad},
		{ "cs-bad-network-xprt", test_cs_ut_xprt_bad},
		{ "cs-bad-network-ep", test_cs_ut_ep_bad},
		{ "cs-bad-service", test_cs_ut_service_bad},
		{ "cs-missing-options", test_cs_ut_args_bad},
		{ "cs-buffer_pool-options", test_cs_ut_buffer_pool},
		{ "cs-lnet-options", test_cs_ut_lnet},
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
