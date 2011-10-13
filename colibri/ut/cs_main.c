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
#include <signal.h>

#include "lib/ut.h"
#include "lib/errno.h"
#include "lib/memory.h"

#include "net/bulk_sunrpc.h"
#include "net/bulk_mem.h"
#include "reqh/reqh_service.h"
#include "colibri/colibri_setup.h"

/**
   @addtogroup colibri_setup
   @{
 */

/* Client side structures */

static struct c2_net_domain          cl_ndom;
static struct c2_cob_domain          cl_cob_domain;
static struct c2_cob_domain_id       cl_cob_dom_id;
static struct c2_rpcmachine          cl_rpc_mach;
static struct c2_net_end_point      *cl_rep;
static struct c2_rpc_conn            cl_conn;
static struct c2_dbenv               cl_db;
static struct c2_rpc_session         cl_rpc_session;

static char *cs_cmd[] = { "colibri_setup", "-r", "-T", "AD",
			 "-D", "cs_db", "-S", "cs_stob",
			 "-e", "bulk-sunrpc:127.0.0.1:1024:1",
			 "-s", "dummy"};


static int dummy_service_start(struct c2_reqh_service *service);
static int dummy_service_stop(struct c2_reqh_service *service);
static int dummy_service_alloc_init(struct c2_reqh_service_type *stype,
		struct c2_reqh *reqh, struct c2_reqh_service **service);
static void dummy_service_fini(struct c2_reqh_service *service);

static const struct c2_reqh_service_type_ops dummy_service_type_ops = {
        .rsto_service_alloc_and_init = dummy_service_alloc_init
};

const struct c2_reqh_service_ops dummy_service_ops = {
        .rso_start = dummy_service_start,
        .rso_stop = dummy_service_stop,
        .rso_fini = dummy_service_fini
};

C2_REQH_SERVICE_TYPE_DECLARE(dummy_service_type, &dummy_service_type_ops, "dummy");

static void rpc_item_reply_cb(struct c2_rpc_item *item, int rc)
{
        C2_PRE(item != NULL);
        C2_PRE(c2_chan_has_waiters(&item->ri_chan));

        c2_chan_signal(&item->ri_chan);
}

/**
   RPC item operations structures.
 */
static const struct c2_rpc_item_type_ops cs_req_fop_rpc_item_type_ops = {
        .rito_sent = NULL,
        .rito_added = NULL,
        .rito_replied = rpc_item_reply_cb,
        .rito_item_size = c2_rpc_item_default_size,
        .rito_items_equal = NULL,
        .rito_get_io_fragment_count = NULL,
        .rito_io_coalesce = NULL,
        .rito_encode = c2_rpc_fop_default_encode,
        .rito_decode = c2_rpc_fop_default_decode,
};

/**
   Reply rpc item type operations.
 */
static const struct c2_rpc_item_type_ops cs_rep_fop_rpc_item_type_ops = {
        .rito_sent = NULL,
        .rito_added = NULL,
        .rito_replied = NULL,
        .rito_item_size = c2_rpc_item_default_size,
        .rito_items_equal = NULL,
        .rito_get_io_fragment_count = NULL,
        .rito_io_coalesce = NULL,
        .rito_encode = c2_rpc_fop_default_encode,
        .rito_decode = c2_rpc_fop_default_decode,
};

/**
   Fop type operations.
 */
static const struct c2_fop_type_ops cs_req_fop_ops = {
        .fto_fom_init = cs_req_fop_fom_init,
        .fto_fop_replied = NULL,
        .fto_size_get = c2_xcode_fop_size_get,
        .fto_op_equal = NULL,
        .fto_get_nfragments = NULL,
        .fto_io_coalesce = NULL,
};

static const struct c2_fop_type_ops cs_rep_fop_ops = {
        .fto_fom_init = NULL,
        .fto_fop_replied = NULL,
        .fto_size_get = c2_xcode_fop_size_get,
        .fto_op_equal = NULL,
        .fto_get_nfragments = NULL,
        .fto_io_coalesce = NULL,
};

enum {
	CS_REQ = 91,
	CS_REP,
};

/**
   Item type declartaions
 */

static const struct c2_rpc_item_type cs_req_fop_rpc_item_type = {
        .rit_opcode = CS_REQ,
        .rit_ops = &cs_req_fop_rpc_item_type_ops,
        .rit_flags = C2_RPC_ITEM_TYPE_REQUEST | C2_RPC_ITEM_TYPE_MUTABO
};

/**
   Reply rpc item type
 */
static const struct c2_rpc_item_type cs_rep_fop_rpc_item_type = {
        .rit_opcode = CS_REP,
        .rit_ops = &cs_rep_fop_rpc_item_type_ops,
        .rit_flags = C2_RPC_ITEM_TYPE_REPLY
};

C2_FOP_TYPE_DECLARE_NEW(cs_req_fop, "cs request", CS_REQ,
                        &cs_req_fop_ops, &cs_req_fop_rpc_item_type);
C2_FOP_TYPE_DECLARE_NEW(cs_rep_fop, "cs reply", CS_REP,
                        &cs_rep_fop_ops, &cs_rep_fop_rpc_item_type);

/**
 * Fop type structures required for initialising corresponding fops.
 */
static struct c2_fop_type *cs_fops[] = {
        &cs_req_fopt,
	&cs_rep_fopt
};

static void cs_fop_fini(void)
{
        c2_fop_type_fini_nr(cs_fops, ARRAY_SIZE(cs_fops));
}

static int cs_fop_init(void)
{
        int result;
        result = c2_fop_type_build_nr(cs_fops, ARRAY_SIZE(cs_fops));
        if (result != 0)
                c2_reqh_fop_fini();
        return result;
}

static int cs_req_fop_fom_state(struct c2_fom *fom);
static int cs_rep_fop_fom_state(struct c2_fom *fom);
static void cs_ut_fom_fini(struct c2_fom *fom);
static size_t cs_ut_find_fom_home_locality(const struct c2_fom *fom);

/**
 * Operation structures for respective foms
 */
static struct c2_fom_ops reqh_ut_create_fom_ops = {
        .fo_fini = reqh_ut_io_fom_fini,
        .fo_state = reqh_ut_create_fom_state,
        .fo_home_locality = reqh_ut_find_fom_home_locality,
};

static int dummy_service_alloc_init(struct c2_reqh_service_type *stype,
		struct c2_reqh *reqh, struct c2_reqh_service **service)
{
	struct c2_reqh_service      *serv;

        C2_PRE(service != NULL && stype != NULL);

        printf("\n Initialising mds service \n");

        C2_ALLOC_PTR(serv);
        if (serv == NULL)
                return -ENOMEM;

        serv->rs_type = stype;
        serv->rs_ops = &dummy_service_ops;

	c2_reqh_service_init(serv, reqh);
	*service = serv;

        return 0;
}

static int dummy_service_start(struct c2_reqh_service *service)
{
        C2_PRE(service != NULL);

	printf("service started\n");
        /*
           Can perform service specific initialisation of
           objects like fops and invoke a generic service start
	   functions.
         */
	c2_reqh_service_start(service);

	return 0;
}

static int dummy_service_stop(struct c2_reqh_service *service)
{

        C2_PRE(service != NULL);

	printf("service stopped\n");
        /*
           Can finalise service specific objects like
           fops.
         */
	c2_reqh_service_stop(service);

	return 0;
}

static void dummy_service_fini(struct c2_reqh_service *service)
{
	c2_reqh_service_fini(service);
	c2_free(service);
}

/**
   Represents various network transports supported
   by a particular node in a cluster.
 */
static struct c2_net_xprt *cs_xprts[] = {
	&c2_net_bulk_sunrpc_xprt,
	&c2_net_bulk_mem_xprt
};

/**
   Global colibri context
 */
static struct c2_colibri srv_colibri_ctx;


static void send_fops(void)
{
	
}

static int client_init(char *dbname)
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
        rc = c2_dbenv_init(&cl_db, dbname, 0);
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
        rc = c2_rpc_session_init(&cl_rpc_session, &cl_conn,
                        5);
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

	send_fops();

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

static void client_fini()
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

int server_init()
{
        int     rc;
        FILE   *outfile;

        outfile = fopen("cs.errlog", "w+");
        if (outfile == NULL) {
                C2_UT_FAIL("Failed to open output file\n");
                goto out;
        }

        errno = 0;
        rc = c2_cs_init(&colibri_ctx, cs_xprts, ARRAY_SIZE(cs_xprts), outfile);
        if (rc != 0) {
                fprintf(outfile, "\n Failed to initialise Colibri \n");
                goto out;
        }

        /* Register the service type. */
        c2_reqh_service_type_register(&dummy_service_type);

        rc = c2_cs_setup_env(&colibri_ctx, ARRAY_SIZE(cs_cmd), cs_cmd);
        if (rc != 0)
                goto out;

        rc = c2_cs_start(&colibri_ctx);

out:
	return rc;
}

void server_fini(void)
{
        /* Unregister service type */
        c2_reqh_service_type_unregister(&dummy_service_type);

        c2_cs_fini(&colibri_ctx);
}

void test_colibri_setup(void)
{
	int     rc;

	rc = server_init();

	rc = client_init();
}

const struct c2_test_suite colibri_setup_ut = {
        .ts_name = "colbri_setup_ut",
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
