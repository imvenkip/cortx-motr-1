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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>    /* pause */
#include <errno.h>     /* errno */

#include "lib/ut.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/tlist.h"

#include "net/bulk_sunrpc.h"
#include "net/bulk_mem.h"
#include "reqh/reqh_service.h"
#include "colibri/colibri_setup.h"

extern void send_fops(struct c2_rpc_session *cl_rpc_session, int dstype);
extern struct c2_reqh_service_type ds1_service_type;
extern struct c2_reqh_service_type ds2_service_type;
extern const struct c2_tl_descr ndoms_descr;

/* Client context */
struct client_ctx {
	const char                   *cl_dbname;
	const char                   *cl_epaddr;
	const char                   *cl_srv_epaddr;
	int                           cl_xprt_type;
        int                           cl_cdom_id;
	int                           cl_svc_type;
	struct c2_net_domain          cl_ndom;
	struct c2_cob_domain          cl_cob_domain;
	struct c2_cob_domain_id       cl_cob_dom_id;
	struct c2_rpcmachine          cl_rpc_mach;
	struct c2_net_end_point      *cl_nep;
	struct c2_rpc_conn            cl_conn;
	struct c2_dbenv               cl_db;
	struct c2_rpc_session         cl_rpc_session;
};

/* Server context */
struct srv_ctx {
	struct c2_net_end_point      *sc_nep;
	struct c2_net_transfer_mc    *sc_tm;
	const char                   *sc_cl_epaddr;
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
                                "-s", "ds1", "-r", "-T", "AD",
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

static int cl_cdom_id = 10001;

enum {
	BULK_SUNRPC_XPRT,
	BULK_MEM_XPRT
};

enum {
        DS_ONE = 1,
        DS_TWO,
};

/*
  Transports used in colibri a context.
 */
static struct c2_net_xprt *cs_xprts[] = {
	&c2_net_bulk_sunrpc_xprt,
	&c2_net_bulk_mem_xprt
};

static struct c2_colibri srv_colibri_ctx;

/*
  Initialises client.
 */
static int client_init(struct client_ctx *cl_ctx)
{
        int                                rc = 0;
        c2_time_t                          timeout;
        struct c2_net_transfer_mc         *cl_tm;

        /* Init client side network domain */
        rc = c2_net_domain_init(&cl_ctx->cl_ndom,
			cs_xprts[cl_ctx->cl_xprt_type]);
        C2_UT_ASSERT(rc == 0);

        cl_ctx->cl_cob_dom_id.id = cl_ctx->cl_cdom_id;

        /* Init the db */
        rc = c2_dbenv_init(&cl_ctx->cl_db, cl_ctx->cl_dbname, 0);
        C2_UT_ASSERT(rc == 0);

        /* Init the cob domain */
        rc = c2_cob_domain_init(&cl_ctx->cl_cob_domain, &cl_ctx->cl_db,
						&cl_ctx->cl_cob_dom_id);
        C2_UT_ASSERT(rc == 0);

        /* Init the rpcmachine */
        rc = c2_rpcmachine_init(&cl_ctx->cl_rpc_mach, &cl_ctx->cl_cob_domain,
                        &cl_ctx->cl_ndom, cl_ctx->cl_epaddr, NULL);
        C2_UT_ASSERT(rc == 0);

        cl_tm = &cl_ctx->cl_rpc_mach.cr_tm;
        /* Create destination endpoint for client i.e server endpoint */
        rc = c2_net_end_point_create(&cl_ctx->cl_nep, cl_tm,
					cl_ctx->cl_srv_epaddr);
        C2_UT_ASSERT(rc == 0);
        /* Init the connection structure */
        rc = c2_rpc_conn_init(&cl_ctx->cl_conn, cl_ctx->cl_nep,
			&cl_ctx->cl_rpc_mach, MAX_RPCS_IN_FLIGHT);
        C2_UT_ASSERT(rc == 0);

        /* Create RPC connection */
        rc = c2_rpc_conn_establish(&cl_ctx->cl_conn);
        C2_UT_ASSERT(rc == 0);


        timeout = c2_time_now();
        c2_time_set(&timeout, c2_time_seconds(timeout) + 3000,
                                c2_time_nanoseconds(timeout));

        C2_UT_ASSERT(c2_rpc_conn_timedwait(&cl_ctx->cl_conn, C2_RPC_CONN_ACTIVE,
                                                                 timeout));

        /* Init session */
        rc = c2_rpc_session_init(&cl_ctx->cl_rpc_session, &cl_ctx->cl_conn, 2);
        C2_UT_ASSERT(rc == 0);

        /* Create RPC session */
        rc = c2_rpc_session_establish(&cl_ctx->cl_rpc_session);
        C2_UT_ASSERT(rc == 0);

        timeout = c2_time_now();
        c2_time_set(&timeout, c2_time_seconds(timeout) + 3000,
                                c2_time_nanoseconds(timeout));
        /* Wait for session to become active */
        C2_UT_ASSERT(c2_rpc_session_timedwait(&cl_ctx->cl_rpc_session,
                        C2_RPC_SESSION_IDLE, timeout));

        return rc;
}

/*
  Finalises client.
 */
static void client_fini(struct client_ctx *cl_ctx)
{
	int       rc;
        c2_time_t timeout;

        rc = c2_rpc_session_terminate(&cl_ctx->cl_rpc_session);
        C2_UT_ASSERT(rc == 0);

        timeout = c2_time_now();
        c2_time_set(&timeout, c2_time_seconds(timeout) + 3000,
                                c2_time_nanoseconds(timeout));
        /* Wait for session to terminate */
        C2_UT_ASSERT(c2_rpc_session_timedwait(&cl_ctx->cl_rpc_session,
				C2_RPC_SESSION_TERMINATED,
				timeout));

        /* Terminate RPC connection */
        rc = c2_rpc_conn_terminate(&cl_ctx->cl_conn);
        C2_UT_ASSERT(rc == 0);

        timeout = c2_time_now();
        c2_time_set(&timeout, c2_time_seconds(timeout) + 3000,
                                c2_time_nanoseconds(timeout));

        C2_UT_ASSERT(c2_rpc_conn_timedwait(&cl_ctx->cl_conn, C2_RPC_CONN_TERMINATED,
									timeout));
        c2_rpc_session_fini(&cl_ctx->cl_rpc_session);
        c2_rpc_conn_fini(&cl_ctx->cl_conn);
        /* Fini the remote net endpoint. */
        c2_net_end_point_put(cl_ctx->cl_nep);

        /* Fini the rpcmachine */
        c2_rpcmachine_fini(&cl_ctx->cl_rpc_mach);

        /* Fini the net domain */
        c2_net_domain_fini(&cl_ctx->cl_ndom);

        /* Fini the cob domain */
        c2_cob_domain_fini(&cl_ctx->cl_cob_domain);

        /* Fini the db */
        c2_dbenv_fini(&cl_ctx->cl_db);
}

/*
  Initialises server side colibri environment using colibri_setup.
 */
static int server_init(char **cs_cmdv, int cs_cmdc,
		struct c2_reqh_service_type **stypes, int stypes_nr,
		struct srv_ctx *sc_ctx)
{
        int    rc;
	int    i;
        FILE  *outfile;

	C2_PRE(cs_cmdv != NULL && cs_cmdc > 0 && stypes != NULL &&
						stypes_nr > 0);
        outfile = fopen("cs.errlog", "w+");
	C2_UT_ASSERT(outfile != NULL);

        errno = 0;
        /* Register the service type. */
	for (i = 0; i < stypes_nr; ++i) {
		rc = c2_reqh_service_type_register(stypes[i]);
		C2_UT_ASSERT(rc == 0);
	}

        rc = c2_cs_init(&srv_colibri_ctx, cs_xprts, ARRAY_SIZE(cs_xprts), outfile);
	C2_UT_ASSERT(rc == 0);

        rc = c2_cs_setup_env(&srv_colibri_ctx, cs_cmdc, cs_cmdv);
        if (rc != 0)
		goto out;

        rc = c2_cs_start(&srv_colibri_ctx);
	if (rc != 0)
		goto out;

	for (i = 0; i < stypes_nr; ++i) {
		sc_ctx[i].sc_tm = c2_cs_tm_get(&srv_colibri_ctx,
						sc_ctx[i].sc_xprt,
						stypes[i]->rst_name);
		C2_UT_ASSERT(sc_ctx[i].sc_tm != NULL);

		/* Create destination endpoint for server i.e client endpoint */
		rc = c2_net_end_point_create(&sc_ctx[i].sc_nep,
					sc_ctx[i].sc_tm,
					sc_ctx[i].sc_cl_epaddr);
		C2_UT_ASSERT(rc == 0);
	}

out:
	return rc;
}

/*
  Finalises server side colibri environent.
 */
void server_fini(struct srv_ctx *sc_ctx, int sc_ctx_nr,
		struct c2_reqh_service_type **stypes, int stypes_nr)
{
	int i;

	/* Fini the server side destination endpoints */
	for (i = 0; i < sc_ctx_nr; ++i)
		c2_net_end_point_put(sc_ctx[i].sc_nep);

        c2_cs_fini(&srv_colibri_ctx);

        /* Unregister service type */
	for (i = 0; i < stypes_nr; ++i)
		c2_reqh_service_type_unregister(stypes[i]);
}

static void cs_ut_service_one(void)
{
	int                          rc;
	struct c2_reqh_service_type *stypes[] = {
			&ds1_service_type
	};
	struct srv_ctx               sc_ctx[1] = {
				{.sc_cl_epaddr = "127.0.0.1:34567:1",
				.sc_xprt = cs_xprts[BULK_SUNRPC_XPRT] }
	};
	struct client_ctx            cl_ctx = {
				.cl_dbname = "test1cdb",
				.cl_epaddr = "127.0.0.1:34567:1",
				.cl_srv_epaddr = "127.0.0.1:34567:2",
				.cl_xprt_type = BULK_SUNRPC_XPRT,
				.cl_cdom_id = cl_cdom_id,
				.cl_svc_type = DS_ONE };

	rc = server_init(cs_ut_service_one_cmd, ARRAY_SIZE(cs_ut_service_one_cmd),
			stypes, ARRAY_SIZE(stypes), sc_ctx);
	C2_UT_ASSERT(rc == 0);

	rc = client_init(&cl_ctx);
	C2_UT_ASSERT(rc == 0);

	send_fops(&cl_ctx.cl_rpc_session, cl_ctx.cl_svc_type);

	client_fini(&cl_ctx);
	server_fini(sc_ctx, ARRAY_SIZE(sc_ctx), stypes, ARRAY_SIZE(stypes));
}

static void cs_ut_services_many(void)
{
        int                          rc;
	int                          i;
        struct c2_reqh_service_type *stypes[] = {
				&ds1_service_type,
				&ds2_service_type
        };
        struct srv_ctx               sc_ctx[2] = {
				{.sc_cl_epaddr = "127.0.0.1:34567:1",
				.sc_xprt = cs_xprts[BULK_SUNRPC_XPRT] },

				{.sc_cl_epaddr = "127.0.0.1:34569",
				.sc_xprt = cs_xprts[BULK_MEM_XPRT] }
	};
        struct client_ctx            cl_ctx[2] = {
                                {.cl_dbname = "test2cdb1",
                                .cl_epaddr = "127.0.0.1:34567:1",
                                .cl_srv_epaddr = "127.0.0.1:34567:2",
                                .cl_xprt_type = BULK_SUNRPC_XPRT,
                                .cl_cdom_id = cl_cdom_id,
				.cl_svc_type = DS_ONE },

                                {.cl_dbname = "test2cdb2",
                                .cl_epaddr = "127.0.0.1:34569",
                                .cl_srv_epaddr = "127.0.0.1:35678",
                                .cl_xprt_type = BULK_MEM_XPRT,
                                .cl_cdom_id = ++cl_cdom_id,
				.cl_svc_type = DS_TWO }
	};

        rc = server_init(cs_ut_services_many_cmd,
			ARRAY_SIZE(cs_ut_services_many_cmd), stypes,
			ARRAY_SIZE(stypes), sc_ctx);
        C2_UT_ASSERT(rc == 0);

	for (i = 0; i < ARRAY_SIZE(cl_ctx); ++i) {
		rc = client_init(&cl_ctx[i]);
		C2_UT_ASSERT(rc == 0);
		send_fops(&cl_ctx[i].cl_rpc_session, cl_ctx[i].cl_svc_type);
		client_fini(&cl_ctx[i]);
	}
        server_fini(sc_ctx, ARRAY_SIZE(sc_ctx), stypes, ARRAY_SIZE(stypes));
}

static void cs_ut_reqhs_many(void)
{
        int                          rc;
        int                          i;
        struct c2_reqh_service_type *stypes[] = {
                                &ds1_service_type,
                                &ds2_service_type
        };
        struct srv_ctx               sc_ctx[2] = {
				{.sc_cl_epaddr = "127.0.0.1:34567:1",
				.sc_xprt = cs_xprts[BULK_SUNRPC_XPRT] },

				{.sc_cl_epaddr = "127.0.0.1:34569",
				.sc_xprt = cs_xprts[BULK_MEM_XPRT] }
	};
        struct client_ctx            cl_ctx[2] = {
                                {.cl_dbname = "test3cdb1",
                                .cl_epaddr = "127.0.0.1:34567:1",
                                .cl_srv_epaddr = "127.0.0.1:34567:2",
                                .cl_xprt_type = BULK_SUNRPC_XPRT,
                                .cl_cdom_id = cl_cdom_id,
				.cl_svc_type = DS_ONE },

                                {.cl_dbname = "test3cdb2",
                                .cl_epaddr = "127.0.0.1:34569",
                                .cl_srv_epaddr = "127.0.0.1:35678",
                                .cl_xprt_type = BULK_MEM_XPRT,
                                .cl_cdom_id = ++cl_cdom_id,
				.cl_svc_type = DS_TWO }
	};

        rc = server_init(cs_ut_reqhs_many_cmd,
			ARRAY_SIZE(cs_ut_reqhs_many_cmd), stypes,
			ARRAY_SIZE(stypes), sc_ctx);
        C2_UT_ASSERT(rc == 0);

        for (i = 0; i < ARRAY_SIZE(cl_ctx); ++i) {
                rc = client_init(&cl_ctx[i]);
                C2_UT_ASSERT(rc == 0);
                send_fops(&cl_ctx[i].cl_rpc_session, cl_ctx[i].cl_svc_type);
                client_fini(&cl_ctx[i]);
        }
        server_fini(sc_ctx, ARRAY_SIZE(sc_ctx), stypes, ARRAY_SIZE(stypes));
}

static void cs_ut_opts_jumbled(void)
{
        int                          rc;
        struct c2_reqh_service_type *stypes[] = {
                        &ds1_service_type
        };
        struct srv_ctx               sc_ctx[1] = {
				{.sc_cl_epaddr = "127.0.0.1:34567:1",
				.sc_xprt = cs_xprts[BULK_SUNRPC_XPRT]}
	};
        struct client_ctx            cl_ctx = {
                                .cl_dbname = "test4cdb",
                                .cl_epaddr = "127.0.0.1:34567:1",
                                .cl_srv_epaddr = "127.0.0.1:34567:2",
                                .cl_xprt_type = BULK_SUNRPC_XPRT,
                                .cl_cdom_id = cl_cdom_id,
				.cl_svc_type = DS_ONE
	};

        rc = server_init(cs_ut_opts_jumbled_cmd,
			ARRAY_SIZE(cs_ut_opts_jumbled_cmd), stypes,
			ARRAY_SIZE(stypes), sc_ctx);
        C2_UT_ASSERT(rc == 0);

        rc = client_init(&cl_ctx);
        C2_UT_ASSERT(rc == 0);

        send_fops(&cl_ctx.cl_rpc_session, cl_ctx.cl_svc_type);

        client_fini(&cl_ctx);
        server_fini(sc_ctx, ARRAY_SIZE(sc_ctx), stypes, ARRAY_SIZE(stypes));
}

/*
  Tests success scenarios.
 */
void test_success(void)
{
	cs_ut_service_one();
	cs_ut_services_many();
	cs_ut_reqhs_many();
	cs_ut_opts_jumbled();
}

/*
  Tests server side bad colibri setup commands.
 */
static void cs_ut_reqh_none(void)
{
        int                          rc;
        struct c2_reqh_service_type *stypes[] = {
                        &ds1_service_type
        };

        rc = server_init(cs_ut_reqh_none_cmd, ARRAY_SIZE(cs_ut_reqh_none_cmd),
			stypes, ARRAY_SIZE(stypes), NULL);
        C2_UT_ASSERT(rc != 0);
	server_fini(NULL, 0, stypes, ARRAY_SIZE(stypes));
}

static void cs_ut_stype_bad(void)
{
	int                          rc;
        struct c2_reqh_service_type *stypes[] = {
                        &ds1_service_type
        };

        rc = server_init(cs_ut_stype_bad_cmd, ARRAY_SIZE(cs_ut_stype_bad_cmd),
			stypes, ARRAY_SIZE(stypes), NULL);
        C2_UT_ASSERT(rc != 0);
	server_fini(NULL, 0, stypes, ARRAY_SIZE(stypes));
}

static void cs_ut_xprt_bad(void)
{
        int                          rc;
        struct c2_reqh_service_type *stypes[] = {
                        &ds1_service_type
        };

        rc = server_init(cs_ut_xprt_bad_cmd, ARRAY_SIZE(cs_ut_xprt_bad_cmd),
			stypes, ARRAY_SIZE(stypes), NULL);
        C2_UT_ASSERT(rc != 0);
	server_fini(NULL, 0, stypes, ARRAY_SIZE(stypes));
}

static void cs_ut_ep_bad(void)
{
        int                          rc;
        struct c2_reqh_service_type *stypes[] = {
                        &ds1_service_type
        };

        rc = server_init(cs_ut_ep_bad_cmd, ARRAY_SIZE(cs_ut_ep_bad_cmd),
                        stypes, ARRAY_SIZE(stypes), NULL);
        C2_UT_ASSERT(rc != 0);
	server_fini(NULL, 0, stypes, ARRAY_SIZE(stypes));
}

static void cs_ut_service_bad(void)
{
        int                          rc;
        struct c2_reqh_service_type *stypes[] = {
                        &ds1_service_type,
			&ds2_service_type
        };

        rc = server_init(cs_ut_service_bad_cmd, ARRAY_SIZE(cs_ut_service_bad_cmd),
                        stypes, ARRAY_SIZE(stypes), NULL);
        C2_UT_ASSERT(rc != 0);
	server_fini(NULL, 0, stypes, ARRAY_SIZE(stypes));
}

static void cs_ut_args_bad(void)
{
        int                          rc;
        struct c2_reqh_service_type *stypes[] = {
                        &ds1_service_type
        };

        rc = server_init(cs_ut_args_bad_cmd, ARRAY_SIZE(cs_ut_args_bad_cmd),
                        stypes, ARRAY_SIZE(stypes), NULL);
        C2_UT_ASSERT(rc != 0);
	server_fini(NULL, 0, stypes, ARRAY_SIZE(stypes));
}

void test_failures(void)
{
	cs_ut_reqh_none();
	cs_ut_stype_bad();
	cs_ut_xprt_bad();
	cs_ut_ep_bad();
	cs_ut_service_bad();
	cs_ut_args_bad();
}

const struct c2_test_suite colibri_setup_ut = {
        .ts_name = "colbri_setup_ut... this takes some time",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "colibri_setup_success_tests", test_success },
                { "colibri_setup_failure_tests", test_failures },
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
