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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 12/10/2011
 */

#include "ut/ut.h"     /* M0_UT_ASSERT */
#include "lib/misc.h"  /* M0_SET_ARR0 */
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/tlist.h"

#include "rpc/rpclib.h"        /* m0_rpc_server_ctx */
#include "rpc/rpc_opcodes.h"
#include "fop/fop.h"
#include "net/bulk_mem.h"
#include "net/lnet/lnet.h"
#include "reqh/reqh_service.h"
#include "mero/setup.h"
#include "ut/cs_fop.h"
#include "ut/cs_fop_xc.h"      /* cs_ds1_{req,rep}_fop, cs_ds2_{req,rep}_fop */
#include "ut/cs_service.h"     /* m0_cs_default_stypes */
#include "ut/file_helpers.h"   /* M0_UT_CONF_PATH */

#include "mero/setup.c"

extern const struct m0_tl_descr ndoms_descr;

/* Client context */
struct cl_ctx {
	/* Client network domain.*/
	struct m0_net_domain	 cl_ndom;
	/* Client rpc context.*/
	struct m0_rpc_client_ctx cl_ctx;
};

/* Configures mero environment with given parameters. */
static char *cs_ut_service_one_cmd[] = { "m0d", "-T", "linux",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-A", "linuxstob:cs_addb_stob",
                                "-e", "lnet:0@lo:12345:34:1",
				"-w", "10",
                                "-s", "ds1", "-s", "confd",
				"-P", M0_UT_CONF_PROFILE,
				"-c", M0_UT_CONF_PATH("conf-str.txt")};

static char *cs_ut_services_many_cmd[] = { "m0d", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-A", "linuxstob:cs_addb_stob",
				"-w", "10",
                                "-e", "lnet:0@lo:12345:34:1",
                                "-e", "bulk-mem:127.0.0.1:35678",
                                "-s", "ds1", "-s" "ds2", "-s", "confd",
				"-P", M0_UT_CONF_PROFILE,
				"-c", M0_UT_CONF_PATH("conf-str.txt")};

static char *cs_ut_opts_jumbled_cmd[] = { "m0d", "-D",
                                "cs_sdb", "-T", "AD", "-s", "ds1",
				"-w", "10",
                                "-e", "lnet:0@lo:12345:34:1",
                                "-S", "cs_stob", "-A", "linuxstob:cs_addb_stob",
				"-s", "confd", "-P", M0_UT_CONF_PROFILE,
				"-c", M0_UT_CONF_PATH("conf-str.txt")};

static char *cs_ut_dev_stob_cmd[] = { "m0d", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-A", "linuxstob:cs_addb_stob",
				"-w", "10",
                                "-d", "devices.conf",
                                "-e", "lnet:0@lo:12345:34:1",
                                "-s", "ds1", "-s", "confd",
				"-P", M0_UT_CONF_PROFILE,
				"-c", M0_UT_CONF_PATH("conf-str.txt")};

static char *cs_ut_stype_bad_cmd[] = { "m0d", "-T", "asdadd",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-A", "linuxstob:cs_addb_sdb",
				"-w", "10",
                                "-e", "lnet:0@lo:12345:34:1",
                                "-s", "ds1", "-s", "confd",
				"-P", M0_UT_CONF_PROFILE,
				"-c", M0_UT_CONF_PATH("conf-str.txt")};

static char *cs_ut_xprt_bad_cmd[] = { "m0d", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-A", "linuxstob:cs_addb_sdb",
				"-w", "10",
                                "-e", "asdasdada:172.18.50.40@o2ib1:34567:2",
                                "-s", "ds1", "-s", "confd",
				"-P", M0_UT_CONF_PROFILE,
				"-c", M0_UT_CONF_PATH("conf-str.txt")};

static char *cs_ut_ep_bad_cmd[] = { "m0d", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-A", "linuxstob:cs_addb_sdb",
				"-w", "10",
                                "-e", "lnet:asdad:asdsd:sadasd",
                                "-s", "ds1", "-s", "confd",
				"-P", M0_UT_CONF_PROFILE,
				"-c", M0_UT_CONF_PATH("conf-str.txt")};

static char *cs_ut_service_bad_cmd[] = { "m0d", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-A", "linuxstob:cs_addb_sdb",
				"-w", "10",
                                "-e", "lnet:172.18.50.40@o2ib1:12345:34:1",
                                "-s", "dasdadasd", "-s", "confd",
				"-P", M0_UT_CONF_PROFILE,
				"-c", M0_UT_CONF_PATH("conf-str.txt")};

static char *cs_ut_args_bad_cmd[] = { "m0d", "-D", "cs_sdb",
                                "-S", "cs_stob",
				"-A", "linuxstob:cs_addb_sdb", "-w", "10",
                                "-e", "lnet:172.18.50.40@o2ib1:12345:34:1",
				"-s", "confd", "-P", M0_UT_CONF_PROFILE,
				"-c", M0_UT_CONF_PATH("conf-str.txt")};

static char *cs_ut_buffer_pool_cmd[] = { "m0d", "-T", "linux",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-A", "linuxstob:cs_addb_stob",
				"-w", "10",
                                "-e", "lnet:0@lo:12345:34:1",
                                "-s", "ds1", "-q", "4", "-m", "4096",
				"-s", "confd",
				"-P", M0_UT_CONF_PROFILE,
				"-c", M0_UT_CONF_PATH("conf-str.txt")};

static char *cs_ut_lnet_cmd[] = { "m0d", "-T", "linux",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-A", "linuxstob:cs_addb_sdb",
				"-w", "10",
                                "-e", "lnet:0@lo:12345:34:1",
                                "-s", "ds1", "-s", "confd",
				"-P", M0_UT_CONF_PROFILE,
				"-c", M0_UT_CONF_PATH("conf-str.txt")};

static char *cs_ut_lnet_mult_if_cmd[] = { "m0d", "-T", "linux",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-A", "linuxstob:cs_addb_stob",
				"-w", "10",
                                "-e", "lnet:172.18.50.40@tcp:12345:30:101",
                                "-e", "lnet:172.18.50.40@o2ib0:12345:34:101",
                                "-s", "ioservice", "-s", "confd",
				"-P", M0_UT_CONF_PROFILE,
				"-c", M0_UT_CONF_PATH("conf-str.txt")};

static char *cs_ut_ep_mixed_dup_cmd[] = { "m0d", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-A", "linuxstob:cs_addb_stob",
				"-w", "10",
                                "-e", "lnet:172.18.50.40@tcp:12345:30:101",
                                "-e", "lnet:172.18.50.40@o2ib0:12345:34:101",
                                "-e", "lnet:172.18.50.40@o2ib1:12345:30:101",
                                "-e", "lnet:172.18.50.40@o2ib1:12345:30:101",
                                "-s", "ioservice", "-s", "confd",
				"-P", M0_UT_CONF_PROFILE,
				"-c", M0_UT_CONF_PATH("conf-str.txt")};

/* Duplicate tcp interfaces in a request handler context. */
static char *cs_ut_lnet_dup_tcp_if_cmd[] = { "m0d", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-A", "linuxstob:cs_addb_stob",
				"-w", "10",
                                "-e", "lnet:172.18.50.40@tcp:12345:30:101",
                                "-e", "lnet:172.18.50.40@tcp:12345:32:105",
                                "-s", "ds1", "-s", "confd",
				"-P", M0_UT_CONF_PROFILE,
				"-c", M0_UT_CONF_PATH("conf-str.txt")};

static char *cs_ut_lnet_ep_bad_cmd[] = { "m0d", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-A", "linuxstob:cs_addb_stob",
				"-w", "10",
                                "-e", "lnet:asdad:asdsd:sadasd",
                                "-s", "ds1", "-s", "confd",
				"-P", M0_UT_CONF_PROFILE,
				"-c", M0_UT_CONF_PATH("conf-str.txt")};

/* Missing configuration profile (-P) specification. */
static char *cs_ut_no_profile_cmd[] = {
	"m0d", "-T", "AD", "-D", "cs_sdb", "-S", "cs_stob",
	"-A", "linuxstob:cs_addb.stob", "-w", "10",
	"-e", "lnet:0@lo:12345:34:1",
	"-s", "ds1", "-s", "ds2", "-s", "ioservice",
	"-q", "4", "-m", "4096"
};

/* confd service missing configuration file. */
static char *cs_ut_confd_no_confstr_cmd[] = {
	"m0d", "-T", "AD", "-D", "cs_sdb", "-S", "cs_stob",
	"-A", "linuxstob:cs_addb.stob", "-w", "10",
	"-e", "lnet:0@lo:12345:34:1", "-q", "4", "-m", "4096",
        "-s", "confd", "-P", M0_UT_CONF_PROFILE
};

/* mero server missing configuration service endpoint. */
static char *cs_ut_no_confd_ep_cmd[] = {
	"m0d", "-T", "AD", "-D", "cs_sdb", "-S", "cs_stob",
	"-A", "linuxstob:cs_addb.stob", "-w", "10",
	"-e", "lnet:0@lo:12345:34:1", "-q", "4", "-m", "4096",
        "-s", "ds1", "-P", M0_UT_CONF_PROFILE
};

static const char *cdbnames[] = { "cdb1", "cdb2" };
static const char *cl_ep_addrs[] = { "0@lo:12345:34:2", "127.0.0.1:34569" };
static const char *srv_ep_addrs[] = { "0@lo:12345:34:1", "127.0.0.1:35678" };

/*
  Transports used in mero a context.
 */
static struct m0_net_xprt *cs_xprts[] = {
	&m0_net_lnet_xprt,
	&m0_net_bulk_mem_xprt
};

enum { MAX_RPCS_IN_FLIGHT = 10 };

#define SERVER_LOG_FILE_NAME "cs_ut.errlog"

static int cs_ut_client_init(struct cl_ctx *cctx, const char *cl_ep_addr,
			     const char *srv_ep_addr, const char* dbname,
			     struct m0_net_xprt *xprt)
{
	int                       rc;
	struct m0_rpc_client_ctx *cl_ctx;

	M0_PRE(cctx != NULL && cl_ep_addr != NULL && srv_ep_addr != NULL &&
	       dbname != NULL && xprt != NULL);

	rc = m0_net_domain_init(&cctx->cl_ndom, xprt);
	M0_UT_ASSERT(rc == 0);

	cl_ctx = &cctx->cl_ctx;

	cl_ctx->rcx_net_dom            = &cctx->cl_ndom;
	cl_ctx->rcx_local_addr         = cl_ep_addr;
	cl_ctx->rcx_remote_addr        = srv_ep_addr;
	cl_ctx->rcx_max_rpcs_in_flight = MAX_RPCS_IN_FLIGHT;

	rc = m0_rpc_client_start(cl_ctx);
	M0_UT_ASSERT(rc == 0);

	return rc;
}

static void cs_ut_client_fini(struct cl_ctx *cctx)
{
	int rc;

	rc = m0_rpc_client_stop(&cctx->cl_ctx);
	M0_UT_ASSERT(rc == 0);

	m0_net_domain_fini(&cctx->cl_ndom);
}

/** Sends fops to server. */
int m0_cs_ut_send_fops(struct m0_rpc_session *cl_rpc_session, int dstype)
{
	int                      rc;
        uint32_t                 i;
        struct m0_fop           *fop[10] = { 0 };
	struct cs_ds1_req_fop   *cs_ds1_fop;
	struct cs_ds2_req_fop   *cs_ds2_fop;

	M0_PRE(cl_rpc_session != NULL && dstype > 0);

	switch (dstype) {
	case CS_UT_SERVICE1:
		for (i = 0; i < 10; ++i) {
			fop[i] = m0_fop_alloc_at(cl_rpc_session,
						 &cs_ds1_req_fop_fopt);
			cs_ds1_fop = m0_fop_data(fop[i]);
			cs_ds1_fop->csr_value = i;
			rc = m0_rpc_post_sync(fop[i], cl_rpc_session,
					      &cs_ds_req_fop_rpc_item_ops,
					      0 /* deadline */);
			M0_UT_ASSERT(rc == 0);
			m0_fop_put_lock(fop[i]);
		}
		break;
	case CS_UT_SERVICE2:
		for (i = 0; i < 10; ++i) {
			fop[i] = m0_fop_alloc_at(cl_rpc_session,
						 &cs_ds2_req_fop_fopt);
			cs_ds2_fop = m0_fop_data(fop[i]);
			cs_ds2_fop->csr_value = i;
			rc = m0_rpc_post_sync(fop[i], cl_rpc_session,
					      &cs_ds_req_fop_rpc_item_ops,
					      0 /* deadline */);
			M0_UT_ASSERT(rc == 0);
			m0_fop_put_lock(fop[i]);
		}
		break;
	default:
		M0_ASSERT("Invalid service type" == 0);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int cs_ut_test_helper_success(struct cl_ctx *cctx, size_t cctx_nr,
				     char *cs_argv[], int cs_argc)
{
	int rc;
	int i;
	int stype;
	struct m0_rpc_server_ctx sctx = {
		.rsx_xprts            = cs_xprts,
		.rsx_xprts_nr         = ARRAY_SIZE(cs_xprts),
		.rsx_argv             = cs_argv,
		.rsx_argc             = cs_argc,
		.rsx_log_file_name    = SERVER_LOG_FILE_NAME
	};

	rc = m0_rpc_server_start(&sctx);
	M0_UT_ASSERT(rc == 0);
	for (i = 0; i < cctx_nr; ++i) {
		rc = cs_ut_client_init(&cctx[i], cl_ep_addrs[i],
					srv_ep_addrs[i], cdbnames[i],
					cs_xprts[i]);
		M0_UT_ASSERT(rc == 0);
	}

	stype = CS_UT_SERVICE1;
	for (i = 0; i < cctx_nr; ++i, ++stype)
		m0_cs_ut_send_fops(&cctx[i].cl_ctx.rcx_session, stype);

	for (i = 0; i < cctx_nr; ++i)
		cs_ut_client_fini(&cctx[i]);

	m0_rpc_server_stop(&sctx);

	return rc;
}

static void cs_ut_test_helper_failure(char *cs_argv[], int cs_argc)
{
	int rc;
	struct m0_rpc_server_ctx sctx = {
		.rsx_xprts            = cs_xprts,
		.rsx_xprts_nr         = ARRAY_SIZE(cs_xprts),
		.rsx_argv             = cs_argv,
		.rsx_argc             = cs_argc,
		.rsx_log_file_name    = SERVER_LOG_FILE_NAME
	};

	rc = m0_rpc_server_start(&sctx);
	M0_UT_ASSERT(rc != 0);

	/*
	 * If, for some reason, m0_rpc_server_start() completed without error,
	 * we need to stop server.
	 */
	if (rc == 0)
		m0_rpc_server_stop(&sctx);
}

static void test_cs_ut_service_one(void)
{
	struct cl_ctx cctx[1] = {};

	cs_ut_test_helper_success(cctx, ARRAY_SIZE(cctx), cs_ut_service_one_cmd,
				  ARRAY_SIZE(cs_ut_service_one_cmd));
}

static void dev_conf_file_create(void)
{
	FILE *f;
	char  cwd[MAXPATHLEN];
	char *path;

	path = getcwd(cwd, ARRAY_SIZE(cwd));
	M0_UT_ASSERT(path != NULL);

	/* touch d1 d2 */
	f = fopen("d1", "w");
	M0_UT_ASSERT(f != NULL);
	fclose(f);
	f = fopen("d2", "w");
	M0_UT_ASSERT(f != NULL);
	fclose(f);

	f = fopen("devices.conf", "w+");
	M0_UT_ASSERT(f != NULL);
	fprintf(f, "Devices:\n");
	fprintf(f, "        - id: 0\n");
	fprintf(f, "          filename: %s/d1\n", path);
	fprintf(f, "        - id: 1\n");
	fprintf(f, "          filename: %s/d2\n", path);
	fclose(f);
}

static void test_cs_ut_dev_stob(void)
{
	struct cl_ctx cctx[1] = {};

	dev_conf_file_create();
	cs_ut_test_helper_success(cctx, ARRAY_SIZE(cctx), cs_ut_dev_stob_cmd,
				  ARRAY_SIZE(cs_ut_dev_stob_cmd));
}

static void test_cs_ut_services_many(void)
{
	struct cl_ctx cctx[2] = {};

	cs_ut_test_helper_success(cctx, ARRAY_SIZE(cctx),
				  cs_ut_services_many_cmd,
				  ARRAY_SIZE(cs_ut_services_many_cmd));
}

static void test_cs_ut_opts_jumbled(void)
{
	struct cl_ctx cctx[1] = {};

	cs_ut_test_helper_success(cctx, ARRAY_SIZE(cctx),
				  cs_ut_opts_jumbled_cmd,
				  ARRAY_SIZE(cs_ut_opts_jumbled_cmd));
}

/** Tests m0d failure paths using fault injection. */
static void test_cs_ut_linux_stob_cleanup(void)
{
	dev_conf_file_create();
	m0_fi_enable_once("cs_ad_stob_create", "ad_domain_locate_fail");
	cs_ut_test_helper_failure(cs_ut_dev_stob_cmd,
				  ARRAY_SIZE(cs_ut_dev_stob_cmd));
}

static void test_cs_ut_failures(void)
{
#define _TEST(cmd) cs_ut_test_helper_failure(cmd, ARRAY_SIZE(cmd))

	_TEST(cs_ut_stype_bad_cmd);
	_TEST(cs_ut_xprt_bad_cmd);
	_TEST(cs_ut_ep_bad_cmd);
	_TEST(cs_ut_lnet_ep_bad_cmd);
	_TEST(cs_ut_lnet_dup_tcp_if_cmd);
	_TEST(cs_ut_service_bad_cmd);
	_TEST(cs_ut_args_bad_cmd);
	_TEST(cs_ut_no_profile_cmd);
	_TEST(cs_ut_confd_no_confstr_cmd);
	_TEST(cs_ut_no_confd_ep_cmd);
#undef _TEST
}

static void test_cs_ut_lnet_multiple_if(void)
{
	struct m0_mero mero_ctx;
	int            rc;

	rc = m0_cs_init(&mero_ctx, cs_xprts, ARRAY_SIZE(cs_xprts), stderr, true);
	M0_UT_ASSERT(rc == 0);

	rc = cs_args_parse(&mero_ctx, ARRAY_SIZE(cs_ut_lnet_mult_if_cmd),
			   cs_ut_lnet_mult_if_cmd);
	M0_UT_ASSERT(rc == 0);

	rc = reqh_ctx_validate(&mero_ctx);
	M0_UT_ASSERT(rc == 0);

	m0_cs_fini(&mero_ctx);
}

static void test_cs_ut_lnet_ep_mixed_dup(void)
{
	struct m0_mero mero_ctx;
	FILE          *out;
	int            rc;

	out = fopen("temp", "w");

	rc = m0_cs_init(&mero_ctx, cs_xprts, ARRAY_SIZE(cs_xprts), out, true);
	M0_UT_ASSERT(rc == 0);

	rc = cs_args_parse(&mero_ctx, ARRAY_SIZE(cs_ut_ep_mixed_dup_cmd),
			   cs_ut_ep_mixed_dup_cmd);
	M0_UT_ASSERT(rc == 0);

	rc = reqh_ctx_validate(&mero_ctx);
	M0_UT_ASSERT(rc != 0);

	m0_cs_fini(&mero_ctx);
	fclose(out);
}

static void test_cs_ut_buffer_pool(void)
{
	struct cl_ctx cctx[1] = {};

	cs_ut_test_helper_success(cctx, ARRAY_SIZE(cctx), cs_ut_buffer_pool_cmd,
				  ARRAY_SIZE(cs_ut_buffer_pool_cmd));
}

static void test_cs_ut_lnet(void)
{
	struct cl_ctx cctx[1] = {};

	cs_ut_test_helper_success(cctx, ARRAY_SIZE(cctx), cs_ut_lnet_cmd,
				  ARRAY_SIZE(cs_ut_lnet_cmd));
}

struct m0_ut_suite m0d_ut = {
        .ts_name = "m0d-ut",
        .ts_tests = {
                { "cs-single-service", test_cs_ut_service_one},
		{ "cs-multiple-services", test_cs_ut_services_many},
		{ "cs-command-options-jumbled", test_cs_ut_opts_jumbled},
		{ "cs-device-stob", test_cs_ut_dev_stob},
		{ "cs-fail-linux-stob-cleanup", test_cs_ut_linux_stob_cleanup},
		{ "cs-buffer_pool-options", test_cs_ut_buffer_pool},
		{ "cs-duplicate-lnet-mixed-ep", test_cs_ut_lnet_ep_mixed_dup},
		{ "cs-lnet-multiple-interfaces", test_cs_ut_lnet_multiple_if},
		{ "cs-lnet-options", test_cs_ut_lnet},
		{ "cs-failures", test_cs_ut_failures},
                { NULL, NULL },
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
