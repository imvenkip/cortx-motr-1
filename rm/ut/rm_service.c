/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Manish Honap <manish_honap@xyratex.com>
 * Original creation date: 20-Mar-2013
 */

#include "net/lnet/lnet.h"
#include "rm/rm.h"
#include "rm/rm_internal.h"
#include "rm/rm_service.h"
#include "rm/ut/rmut.h"
#include "rpc/rpclib.h"
#include "ut/cs_service.h"
#include "lib/ut.h"

#define SERVER_ENDPOINT_ADDR "0@lo:12345:34:1"

#define BORROWER_ENDPOINT    "0@lo:12345:34:2"
#define SERVER_ENDPOINT      "lnet:" SERVER_ENDPOINT_ADDR
#define SERVER_DB            "server_db"
#define SERVER_STOB          "server_stob"
#define SERVER_ADDB_STOB     "server_addb_stob"
#define SERVER_LOG           "rmserver.log"

extern struct rm_context rm_ctx[SERVER_NR];
extern void rm_ctx_init(struct rm_context *rmctx);
extern void rm_ctx_fini(struct rm_context *rmctx);
extern const struct m0_rm_incoming_ops server1_incoming_ops;

static bool                server_flag = true;
static struct m0_net_xprt *xprt        = &m0_net_lnet_xprt;
static struct rm_context  *rm_svc_ctx  = &rm_ctx[0];

static char *server_argv[] = {
	"rm-ut", "-r", "-p", "-T", "linux", "-D", SERVER_DB,
	"-S", SERVER_STOB, "-A", SERVER_ADDB_STOB, "-e", SERVER_ENDPOINT,
	"-s", "ds1", "-s", "ds2", "-s", "rmservice"
};

static struct m0_rpc_server_ctx sctx = {
	.rsx_xprts            = &xprt,
	.rsx_xprts_nr         = 1,
	.rsx_argv             = server_argv,
	.rsx_argc             = ARRAY_SIZE(server_argv),
	.rsx_service_types    = m0_cs_default_stypes,
	.rsx_service_types_nr = 3,
	.rsx_log_file_name    = SERVER_LOG,
};

static void rm_service_start(struct m0_rpc_server_ctx *sctx)
{
	int result;

	result = m0_rpc_server_start(sctx);
	M0_UT_ASSERT(result == 0);
}

static void rm_service_stop(struct m0_rpc_server_ctx *sctx)
{
	m0_rpc_server_stop(sctx);
}

static void rm_svc_server(const int tid)
{
	rm_service_start(&sctx);

	while (server_flag)
		m0_nanosleep(m0_time(0, 100 * 1000 * 1000), NULL); /* 100 ms */

	rm_service_stop(&sctx);
}

void dummy_session_create()
{
	struct m0_net_end_point *ep;
	int                      rc;

	rc = m0_net_end_point_create(&ep, &rm_svc_ctx->rc_rpc.rm_tm,
				     BORROWER_ENDPOINT);
	M0_UT_ASSERT(rc == 0);

	rm_svc_ctx->rc_ep[0] = ep;

	rc = m0_rpc_conn_create(&rm_svc_ctx->rc_conn[0], ep,
				&rm_svc_ctx->rc_rpc, 15,
				m0_time_from_now(10, 0));
	M0_UT_ASSERT(rc == 0);

	rc = m0_rpc_session_create(&rm_svc_ctx->rc_sess[0],
				   &rm_svc_ctx->rc_conn[0], 1,
				   m0_time_from_now(30, 0));
	M0_UT_ASSERT(rc == 0);
}

void dummy_session_destroy()
{
	int rc;

	rc = m0_rpc_session_destroy(&rm_svc_ctx->rc_sess[0],
				    m0_time_from_now(30, 0));
	M0_UT_ASSERT(rc == 0);
	rc = m0_rpc_conn_destroy(&rm_svc_ctx->rc_conn[0],
				 m0_time_from_now(30, 0));
	M0_UT_ASSERT(rc == 0);
	m0_net_end_point_put(rm_svc_ctx->rc_ep[0]);
}

void rmsvc(void)
{
	int                    rc;
	struct m0_rm_incoming  in;
	struct m0_rm_remote   *creditor;
	struct m0_rm_resource *resource;
	/*
	 * resource manager service is identified from confd
	 * initialise rm_ut_data (domain and resource type);
	 * initialise owner with a null cookie;
	 */
	rm_utdata_init(&test_data, OBJ_OWNER);

	M0_SET0(rm_svc_ctx);
	rm_svc_ctx->rc_ep_addr   = BORROWER_ENDPOINT;
	rm_svc_ctx->rc_cob_id.id = 10;
	rm_ctx_init(rm_svc_ctx);

	/* Start the server */
	rc = M0_THREAD_INIT(&rm_svc_ctx->rc_thr, int, NULL, &rm_svc_server, 0,
			    "rm_svc_%d", 0);
	M0_UT_ASSERT(rc == 0);

	/* Create a dummy session */
	dummy_session_create();

	M0_ALLOC_PTR(creditor);
	M0_UT_ASSERT(creditor != NULL);
	M0_ALLOC_PTR(resource);
	M0_UT_ASSERT(resource != NULL);

	resource->r_type = &rings_resource_type;
	resource->r_ops  = &rings_ops;

	m0_rm_remote_init(creditor, resource);
	creditor->rem_session          = &rm_svc_ctx->rc_sess[0];
	creditor->rem_cookie           = M0_COOKIE_NULL;
	test_data.rd_owner.ro_creditor = creditor;

	m0_rm_incoming_init(&in, &test_data.rd_owner, M0_RIT_BORROW, RIP_NONE,
			    RIF_MAY_BORROW);
	in.rin_want.cr_datum = NENYA | DURIN;
	in.rin_ops = &server1_incoming_ops;
	//in.rin_want.cr_owner->ro_creditor->rem_session = &rm_svc_ctx->rc_sess[0];

	m0_clink_add(&rm_svc_ctx->rc_chan, &rm_svc_ctx->rc_clink);
	m0_rm_credit_get(&in);
	if (incoming_state(&in) == RI_WAIT)
		m0_chan_wait(&rm_svc_ctx->rc_clink);
	M0_UT_ASSERT(incoming_state(&in) == RI_SUCCESS);
	M0_UT_ASSERT(in.rin_rc == 0);
	m0_clink_del(&rm_svc_ctx->rc_clink);
	m0_rm_credit_put(&in);
	m0_rm_incoming_fini(&in);

	m0_rm_remote_fini(creditor);
	m0_free(resource);
	m0_free(creditor);
	dummy_session_destroy();
	rm_ctx_fini(rm_svc_ctx);

	server_flag = false;
	m0_thread_join(&rm_svc_ctx->rc_thr);
	m0_thread_fini(&rm_svc_ctx->rc_thr);

	rm_utdata_fini(&test_data, OBJ_RES);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
