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
#include <config.h>
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
static FILE  *cs_ut_outfile;

/* Client context */
struct cl_ctx {
	/* Client network domain.*/
	struct c2_net_domain cl_ndom;
	/* Client db.*/
	struct c2_dbenv      cl_dbenv;
	/* Client cob domain.*/
	struct c2_cob_domain cl_cdom;
	/* Client rpc context.*/
	struct c2_rpc_ctx    cl_rctx;
};

/* Server context */
struct srv_ctx {
	/* Destination end point for server i.e. client end point.*/
	struct c2_net_end_point      *sc_nep;
	/* Server side transfer machine to create destination end point.*/
	struct c2_net_transfer_mc    *sc_tm;
	/* Destination end point address for server i.e. client end point.*/
	const char                   *sc_cl_epaddr;
	/* Network transport used by the server. */
	const struct c2_net_xprt     *sc_xprt;
};

enum {
	MAX_RPCS_IN_FLIGHT = 10,
};

/* Configures colibri environment with given parameters. */
static char *cs_ut_service_one_cmd[] = { "colibri_setup", "-r", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "bulk-sunrpc:127.0.0.1:34567:2",
                                "-s", "ds1"};

static char *cs_ut_services_many_cmd[] = { "colibri_setup", "-r", "-T", "AD",
                                "-D", "cs_sdb", "-S", "cs_stob",
                                "-e", "bulk-sunrpc:127.0.0.1:34567:2",
                                "-e", "bulk-mem:127.0.0.1:35678",
                                "-s", "ds1", "-s" "ds2"};

static char *cs_ut_reqhs_many_cmd[] = { "colibri_setup", "-r", "-T", "AD",
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

static const char *cl_ep_addrs[] = {
				"127.0.0.1:34567:1",
				"127.0.0.1:34569"};

static const char *srv_ep_addrs[] = {
				"127.0.0.1:34567:2",
				"127.0.0.1:35678"};

static const char *cdbnames[] = {
				"cdb1",
				"cdb2"};

enum {
	MAX_RPC_SLOTS_NR = 2,
	RPC_TIMEOUTS     = 5
};

static int cl_cdom_id = 10001;

enum {
	BULK_SUNRPC_XPRT,
	BULK_MEM_XPRT
};

/*
  Transports used in colibri a context.
 */
static struct c2_net_xprt *cs_xprts[] = {
	&c2_net_bulk_sunrpc_xprt,
	&c2_net_bulk_mem_xprt
};

static struct c2_reqh_service_type *stypes[] = {
			&ds1_service_type,
			&ds2_service_type
};

static struct c2_colibri srv_colibri_ctx;

static void cs_ut_srv_ctx_setup(struct srv_ctx *sctx, int sctx_nr)
{
	int i;

	C2_PRE(sctx != NULL && sctx_nr > 0);

	for (i = 0; i < sctx_nr; ++i) {
		sctx[i].sc_cl_epaddr = srv_ep_addrs[i];
		sctx[i].sc_xprt = cs_xprts[i];
	}
}

/*
  Initialises server side colibri environment using colibri_setup.

  param stypes Types of services supported in a colibri context
  param stypes_nr Number of supported service types
  param sc_ctx Server side context, containing destination end point
               for server i.e. client end point, reference to server
               side c2_net_transfer_mc and c2_net_xprt
 */
static int cs_ut_server_init(char **cs_cmdv, int cs_cmdc, struct srv_ctx *sctx,
								int sctx_nr)
{
        int rc;
	int i;

	C2_PRE(cs_cmdv != NULL && cs_cmdc > 0);

	cs_ut_outfile = fopen("cs_ut.errlog", "w+");
	C2_UT_ASSERT(cs_ut_outfile != NULL);

	/*
	  Server context is NULL during failure test cases.
	 */
	if (sctx != NULL)
		cs_ut_srv_ctx_setup(sctx, sctx_nr);

        errno = 0;
        /* Register the service type. */
	for (i = 0; i < ARRAY_SIZE(stypes); ++i) {
		rc = c2_reqh_service_type_register(stypes[i]);
		C2_UT_ASSERT(rc == 0);
	}

        rc = c2_cs_init(&srv_colibri_ctx, cs_xprts, ARRAY_SIZE(cs_xprts), cs_ut_outfile);
	C2_UT_ASSERT(rc == 0);

        rc = c2_cs_setup_env(&srv_colibri_ctx, cs_cmdc, cs_cmdv);
        if (rc != 0)
		return rc;

        rc = c2_cs_start(&srv_colibri_ctx);
	if (rc != 0)
		return rc;

	for (i = 0; i < sctx_nr; ++i) {
		sctx[i].sc_tm = c2_cs_tm_get(&srv_colibri_ctx,
						sctx[i].sc_xprt,
						stypes[i]->rst_name);
		C2_UT_ASSERT(sctx[i].sc_tm != NULL);

		/* Create destination endpoint for server i.e client endpoint */
		rc = c2_net_end_point_create(&sctx[i].sc_nep,
					sctx[i].sc_tm,
					sctx[i].sc_cl_epaddr);
		C2_UT_ASSERT(rc == 0);
	}

	return rc;
}

/*
  Finalises server side colibri environent.
 */
static void cs_ut_server_fini(struct srv_ctx *sctx, int sctx_nr)
{
	int i;

	if (sctx_nr > 0)
		C2_PRE(sctx != NULL);

	/*
	   In failure test cases server context i.e. sctx is NULL, but we still
	   need to cleanup colibri context.
	 */

	/* Fini the server side destination endpoints */
	for (i = 0; i < sctx_nr; ++i)
		c2_net_end_point_put(sctx[i].sc_nep);

        c2_cs_fini(&srv_colibri_ctx);

        /* Unregister service type */
	for (i = 0; i < ARRAY_SIZE(stypes); ++i)
		c2_reqh_service_type_unregister(stypes[i]);

	C2_UT_ASSERT(cs_ut_outfile != NULL);
	fclose(cs_ut_outfile);
}

static int cs_ut_client_init(struct cl_ctx *cctx, const char *cl_ep_addr,
			const char *srv_ep_addr, const char* dbname,
			struct c2_net_xprt *xprt)
{
	int                rc;
	struct c2_rpc_ctx *cl_rctx;

	C2_PRE(cctx != NULL && cl_ep_addr != NULL && srv_ep_addr != NULL &&
		dbname != NULL && xprt != NULL);

	rc = c2_net_xprt_init(xprt);
	C2_UT_ASSERT(rc == 0);

	rc = c2_net_domain_init(&cctx->cl_ndom, xprt);
	C2_UT_ASSERT(rc == 0);

	cl_rctx = &cctx->cl_rctx;

	cl_rctx->rx_net_dom            = &cctx->cl_ndom;
	cl_rctx->rx_reqh               = NULL;
	cl_rctx->rx_local_addr         = cl_ep_addr;
	cl_rctx->rx_remote_addr        = srv_ep_addr;
	cl_rctx->rx_db_name            = dbname;
	cl_rctx->rx_dbenv              = &cctx->cl_dbenv;
	cl_rctx->rx_cob_dom_id         = ++cl_cdom_id;
	cl_rctx->rx_cob_dom            = &cctx->cl_cdom;
	cl_rctx->rx_nr_slots           = MAX_RPC_SLOTS_NR;
	cl_rctx->rx_timeout_s          = RPC_TIMEOUTS;
	cl_rctx->rx_max_rpcs_in_flight = MAX_RPCS_IN_FLIGHT;

	rc = c2_rpc_client_init(cl_rctx);
	C2_UT_ASSERT(rc == 0);

	return rc;
}

static void cs_ut_client_fini(struct cl_ctx *cctx)
{
	int rc;

	C2_PRE(cctx != NULL);

	rc = c2_rpc_client_fini(&cctx->cl_rctx);
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
        struct c2_fop           *fop[10];
	struct cs_ds1_req_fop   *cs_ds1_fop;
	struct cs_ds2_req_fop   *cs_ds2_fop;
	struct c2_rpc_item      *item;

	C2_PRE(cl_rpc_session != NULL && dstype > 0);

	C2_SET_ARR0(fop);
	switch (dstype) {
	case CS_UT_SERVICE1:
		for (i = 0; i < 10; ++i) {
			fop[i] = c2_fop_alloc(&cs_ds1_req_fop_fopt, NULL);
			item = &fop[i]->f_item;
			item->ri_ops = &cs_ds_req_fop_rpc_item_ops;
			cs_ds1_fop = c2_fop_data(fop[i]);
			cs_ds1_fop->csr_value = i;
			rc = c2_rpc_client_call(fop[i], cl_rpc_session, 60);
			C2_UT_ASSERT(rc == 0);
		}
		break;
	case CS_UT_SERVICE2:
		for (i = 0; i < 10; ++i) {
			fop[i] = c2_fop_alloc(&cs_ds2_req_fop_fopt, NULL);
			item = &fop[i]->f_item;
			item->ri_ops = &cs_ds_req_fop_rpc_item_ops;
			cs_ds2_fop = c2_fop_data(fop[i]);
			cs_ds2_fop->csr_value = i;
			rc = c2_rpc_client_call(fop[i], cl_rpc_session, 60);
			C2_UT_ASSERT(rc == 0);
		}
		break;
	default:
		C2_ASSERT("Invalid service type" == 0);
	}

	return rc;
}

static void test_cs_ut_service_one(void)
{
	int            rc;
	struct cl_ctx  cctx    = { };      
	struct srv_ctx sctx[1] = { };

	rc = cs_ut_server_init(cs_ut_service_one_cmd,
			ARRAY_SIZE(cs_ut_service_one_cmd), sctx,
						ARRAY_SIZE(sctx));
	C2_UT_ASSERT(rc == 0);

	rc = cs_ut_client_init(&cctx, cl_ep_addrs[0], srv_ep_addrs[0],
				cdbnames[0], cs_xprts[0]);
	C2_UT_ASSERT(rc == 0);

	c2_cs_ut_send_fops(&cctx.cl_rctx.rx_session, CS_UT_SERVICE1);

	cs_ut_client_fini(&cctx);

	cs_ut_server_fini(sctx, ARRAY_SIZE(sctx));
}

static void test_cs_ut_services_many(void)
{
        int            rc;
	int            i;
	uint64_t       stype;
	struct cl_ctx  cctx[2] = { };
	struct srv_ctx sctx[2] = { };

        rc = cs_ut_server_init(cs_ut_services_many_cmd,
			ARRAY_SIZE(cs_ut_services_many_cmd), sctx,
						ARRAY_SIZE(sctx));
        C2_UT_ASSERT(rc == 0);

	for (i = 0; i < ARRAY_SIZE(cctx); ++i) {
		rc = cs_ut_client_init(&cctx[i], cl_ep_addrs[i],
					srv_ep_addrs[i], cdbnames[i],
					cs_xprts[i]);
		C2_UT_ASSERT(rc == 0);
	}

	stype = CS_UT_SERVICE1;
	for (i = 0; i < ARRAY_SIZE(cctx); ++i, ++stype)
		c2_cs_ut_send_fops(&cctx[i].cl_rctx.rx_session, stype);

	for (i = 0; i < ARRAY_SIZE(cctx); ++i)
		cs_ut_client_fini(&cctx[i]);
        cs_ut_server_fini(sctx, ARRAY_SIZE(sctx));
}

static void test_cs_ut_reqhs_many(void)
{
        int            rc;
        int            i;
	uint64_t       stype;
        struct srv_ctx sctx[2] = { };
        struct cl_ctx  cctx[2] = { };

        rc = cs_ut_server_init(cs_ut_reqhs_many_cmd,
			ARRAY_SIZE(cs_ut_reqhs_many_cmd), sctx,
						ARRAY_SIZE(sctx));
        C2_UT_ASSERT(rc == 0);

	for (i = 0; i < ARRAY_SIZE(cctx); ++i) {
		rc = cs_ut_client_init(&cctx[i], cl_ep_addrs[i],
					srv_ep_addrs[i], cdbnames[i],
					cs_xprts[i]);
		C2_UT_ASSERT(rc == 0);
	}

	stype = CS_UT_SERVICE1;
        for (i = 0; i < ARRAY_SIZE(cctx); ++i, ++stype)
                c2_cs_ut_send_fops(&cctx[i].cl_rctx.rx_session, stype);

        for (i = 0; i < ARRAY_SIZE(cctx); ++i, ++stype) {
		cs_ut_client_fini(&cctx[i]);
		C2_UT_ASSERT(rc == 0);
	}
        cs_ut_server_fini(sctx, ARRAY_SIZE(sctx));
}

static void test_cs_ut_opts_jumbled(void)
{
        int            rc;
	struct cl_ctx  cctx    = { };
        struct srv_ctx sctx[1] = { };

        rc = cs_ut_server_init(cs_ut_opts_jumbled_cmd,
			ARRAY_SIZE(cs_ut_opts_jumbled_cmd), sctx,
						ARRAY_SIZE(sctx));
        C2_UT_ASSERT(rc == 0);

	rc = cs_ut_client_init(&cctx, cl_ep_addrs[0], srv_ep_addrs[0], cdbnames[0],
				cs_xprts[0]);
        C2_UT_ASSERT(rc == 0);

        rc = c2_cs_ut_send_fops(&cctx.cl_rctx.rx_session, CS_UT_SERVICE1);
	C2_UT_ASSERT(rc == 0);

	cs_ut_client_fini(&cctx);

        cs_ut_server_fini(sctx, ARRAY_SIZE(sctx));
}

/*
  Tests server side bad colibri setup commands.
 */
static void test_cs_ut_reqh_none(void)
{
        int rc;

        rc = cs_ut_server_init(cs_ut_reqh_none_cmd,
				ARRAY_SIZE(cs_ut_reqh_none_cmd), NULL, 0);
        C2_UT_ASSERT(rc != 0);

	cs_ut_server_fini(NULL, 0);
}

static void test_cs_ut_stype_bad(void)
{
	int rc;

        rc = cs_ut_server_init(cs_ut_stype_bad_cmd,
				ARRAY_SIZE(cs_ut_stype_bad_cmd), NULL, 0);
        C2_UT_ASSERT(rc != 0);

	cs_ut_server_fini(NULL, 0);
}

static void test_cs_ut_xprt_bad(void)
{
        int rc;

        rc = cs_ut_server_init(cs_ut_xprt_bad_cmd,
				ARRAY_SIZE(cs_ut_xprt_bad_cmd), NULL, 0);
        C2_UT_ASSERT(rc != 0);

	cs_ut_server_fini(NULL, 0);
}

static void test_cs_ut_ep_bad(void)
{
        int rc;

        rc = cs_ut_server_init(cs_ut_ep_bad_cmd,
				ARRAY_SIZE(cs_ut_ep_bad_cmd), NULL, 0);
        C2_UT_ASSERT(rc != 0);

	cs_ut_server_fini(NULL, 0);
}

static void test_cs_ut_service_bad(void)
{
        int rc;

        rc = cs_ut_server_init(cs_ut_service_bad_cmd,
			ARRAY_SIZE(cs_ut_service_bad_cmd), NULL, 0);
        C2_UT_ASSERT(rc != 0);

	cs_ut_server_fini(NULL, 0);
}

static void test_cs_ut_args_bad(void)
{
        int rc;

        rc = cs_ut_server_init(cs_ut_args_bad_cmd,
			ARRAY_SIZE(cs_ut_args_bad_cmd), NULL, 0);
        C2_UT_ASSERT(rc != 0);

	cs_ut_server_fini(NULL, 0);
}

const struct c2_test_suite colibri_setup_ut = {
        .ts_name = "colibri_setup-ut... this takes some time",
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
