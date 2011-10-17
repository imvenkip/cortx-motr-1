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
#include "reqh/reqh_service.h"
#include "colibri/colibri_setup.h"

/**
   @addtogroup colibri_setup
   @{
 */

extern void send_fops(struct c2_rpc_session *cl_rpc_session);
extern struct c2_reqh_service_type dummy_service_type;
extern const struct c2_tl_descr ndoms_descr;

/* Client side structures */
static struct c2_net_domain          cl_ndom;
static struct c2_cob_domain          cl_cob_domain;
static struct c2_cob_domain_id       cl_cob_dom_id;
static struct c2_rpcmachine          cl_rpc_mach;
static struct c2_net_end_point      *cl_rep;
static struct c2_rpc_conn            cl_conn;
static struct c2_dbenv               cl_db;
static struct c2_rpc_session         cl_rpc_session;
static struct c2_net_transfer_mc    *srv_tm;
static struct c2_net_end_point      *srv_nep;

enum {
	MAX_RPCS_IN_FLIGHT = 10,
};

/* Configures colibri environment with given parameters. */
static char *cs_cmd[] = { "colibri_setup", "-r", "-T", "AD",
		   "-D", "cs_sdb", "-S", "cs_stob",
		   "-e", "bulk-sunrpc:127.0.0.1:1024:2",
		   "-s", "dummy"};

/**
   Transports used in colibri a context.
 */
static struct c2_net_xprt *cs_xprts[] = {
	&c2_net_bulk_sunrpc_xprt,
};

static struct c2_colibri srv_colibri_ctx;

/**
   Initialises client.
 */
static int client_init(void)
{
        int                                rc = 0;
        bool                               rcb;
        c2_time_t                          timeout;
        struct c2_net_transfer_mc         *cl_tm;

        /* Init client side network domain */
        rc = c2_net_domain_init(&cl_ndom, &c2_net_bulk_sunrpc_xprt);
        if(rc != 0)
                goto out;

        cl_cob_dom_id.id =  101 ;

        /* Init the db */
        rc = c2_dbenv_init(&cl_db, "cs_cdb", 0);
        if(rc != 0)
                goto out;

        /* Init the cob domain */
        rc = c2_cob_domain_init(&cl_cob_domain, &cl_db,
                        &cl_cob_dom_id);
        if(rc != 0)
                goto out;

        /* Init the rpcmachine */
        rc = c2_rpcmachine_init(&cl_rpc_mach, &cl_cob_domain,
                        &cl_ndom, "127.0.0.1:1024:1", NULL);
        if(rc != 0)
                goto out;

        cl_tm = &cl_rpc_mach.cr_tm;
        /* Create destination endpoint for client i.e server endpoint */
        rc = c2_net_end_point_create(&cl_rep, cl_tm, "127.0.0.1:1024:2");
        if(rc != 0)
                goto out;
        /* Init the connection structure */
        rc = c2_rpc_conn_init(&cl_conn, cl_rep, &cl_rpc_mach,
                        MAX_RPCS_IN_FLIGHT);
        if(rc != 0)
                goto out;

        /* Create RPC connection */
        rc = c2_rpc_conn_establish(&cl_conn);
        if(rc != 0)
                goto out;


        timeout = c2_time_now();
        c2_time_set(&timeout, c2_time_seconds(timeout) + 3000,
                                c2_time_nanoseconds(timeout));

        rcb = c2_rpc_conn_timedwait(&cl_conn, C2_RPC_CONN_ACTIVE |
                                   C2_RPC_CONN_FAILED, timeout);
        /* Init session */
        rc = c2_rpc_session_init(&cl_rpc_session, &cl_conn, 2);
        if(rc != 0)
                goto out;

        /* Create RPC session */
        rc = c2_rpc_session_establish(&cl_rpc_session);
        if(rc != 0)
                goto conn_term;
        timeout = c2_time_now();
        c2_time_set(&timeout, c2_time_seconds(timeout) + 3000,
                                c2_time_nanoseconds(timeout));
        /* Wait for session to become active */
        rcb = c2_rpc_session_timedwait(&cl_rpc_session,
                        C2_RPC_SESSION_IDLE, timeout);

        /* send dummy fops */
	send_fops(&cl_rpc_session);

        rc = c2_rpc_session_terminate(&cl_rpc_session);
        if(rc != 0)
                goto out;

        timeout = c2_time_now();
        c2_time_set(&timeout, c2_time_seconds(timeout) + 3000,
                                c2_time_nanoseconds(timeout));
        /* Wait for session to terminate */
        rcb = c2_rpc_session_timedwait(&cl_rpc_session,
                        C2_RPC_SESSION_TERMINATED | C2_RPC_SESSION_FAILED,
                        timeout);

conn_term:
        /* Terminate RPC connection */
        rc = c2_rpc_conn_terminate(&cl_conn);
        if(rc != 0)
                goto out;


        timeout = c2_time_now();
        c2_time_set(&timeout, c2_time_seconds(timeout) + 3000,
                                c2_time_nanoseconds(timeout));

        rcb = c2_rpc_conn_timedwait(&cl_conn, C2_RPC_CONN_TERMINATED |
                                   C2_RPC_CONN_FAILED, timeout);
        c2_rpc_session_fini(&cl_rpc_session);
        c2_rpc_conn_fini(&cl_conn);
out:
        return rc;
}

/**
   Finalises client.
 */
static void client_fini(void)
{
        /* Fini the remote net endpoint. */
        c2_net_end_point_put(cl_rep);

        /* Fini the rpcmachine */
        c2_rpcmachine_fini(&cl_rpc_mach);

        /* Fini the net domain */
        c2_net_domain_fini(&cl_ndom);

        /* Fini the cob domain */
        c2_cob_domain_fini(&cl_cob_domain);

        /* Fini the db */
        c2_dbenv_fini(&cl_db);
}

/**
   Initialises server side colibri environment using colibri_setup.
 */
int server_init(void)
{
        int    rc;
        FILE  *outfile;

        outfile = fopen("cs.errlog", "w+");
        if (outfile == NULL) {
                C2_UT_FAIL("Failed to open output file\n");
                goto out;
        }

        errno = 0;
        rc = c2_cs_init(&srv_colibri_ctx, cs_xprts, ARRAY_SIZE(cs_xprts), outfile);
        if (rc != 0) {
                fprintf(outfile, "\n Failed to initialise Colibri \n");
                goto out;
        }

        /* Register the service type. */
        c2_reqh_service_type_register(&dummy_service_type);

        rc = c2_cs_setup_env(&srv_colibri_ctx, ARRAY_SIZE(cs_cmd), cs_cmd);
        if (rc != 0)
                goto out;

        rc = c2_cs_start(&srv_colibri_ctx);

	srv_tm = c2_cs_tm_get(&srv_colibri_ctx, "dummy");
	C2_UT_ASSERT(srv_tm != NULL);

	/* Create destination endpoint for server i.e client endpoint */
        rc = c2_net_end_point_create(&srv_nep, srv_tm, "127.0.0.1:1024:1");

out:
	return rc;
}

/**
   Finalises server side colibri environent.
 */
void server_fini(void)
{
	/* Fini the net endpoint. */
	c2_net_end_point_put(srv_nep);

        /* Unregister service type */
        c2_reqh_service_type_unregister(&dummy_service_type);

        c2_cs_fini(&srv_colibri_ctx);
}

void test_colibri_setup(void)
{
	int     rc;

	rc = server_init();
	C2_UT_ASSERT(rc == 0);

	rc = client_init();
	C2_UT_ASSERT(rc == 0);

	client_fini();
	server_fini();
}

const struct c2_test_suite colibri_setup_ut = {
        .ts_name = "colbri_setup_ut... this takes some time",
        .ts_init = NULL,
        .ts_fini = NULL,
        .ts_tests = {
                { "colibri_setup", test_colibri_setup },
                { NULL, NULL }
        }
};

/** @} endgroup colibri_setup */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
