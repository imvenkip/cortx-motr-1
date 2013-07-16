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

#include "rm/rm.h"
#include "rm/rm_internal.h"
#include "rm/rm_service.h"
#include "rm/file.h"
#include "rm/ut/rmut.h"
#include "rpc/rpclib.h"
#include "ut/cs_service.h"

#define SERVER_ENDPOINT_ADDR "0@lo:12345:34:1"
#define SERVER_ENDPOINT      "lnet:" SERVER_ENDPOINT_ADDR
#define SERVER_DB            "server_db"
#define SERVER_STOB          "server_stob"
#define SERVER_ADDB_STOB     "server_addb_stob"
#define SERVER_LOG           "rmserver.log"

static char *server_argv[] = {
	"rm-ut", "-r", "-p", "-T", "linux", "-D", SERVER_DB,
	"-S", SERVER_STOB, "-A", SERVER_ADDB_STOB, "-e", SERVER_ENDPOINT,
	"-w", "10", "-s", "rmservice"
};

extern struct m0_reqh_service_type      m0_rms_type;
extern struct rm_context                rm_ctx[SERVER_NR];
extern const char                      *serv_addr[];
extern const int                        cob_ids[];
extern const struct m0_rm_incoming_ops  server2_incoming_ops;
extern struct m0_chan                   rr_tests_chan;
extern struct m0_mutex                  rr_tests_chan_mutex;
extern struct m0_clink                  tests_clink[];

static struct m0_net_xprt *xprt        = &m0_net_lnet_xprt;
static struct rm_context  *server_ctx  = &rm_ctx[SERVER_1];
static struct rm_context  *client_ctx  = &rm_ctx[SERVER_2];

extern void rm_ctx_init(struct rm_context *rmctx);
extern void rm_ctx_fini(struct rm_context *rmctx);
extern void rm_connect(struct rm_context *src, const struct rm_context *dest);
extern void rm_disconnect(struct rm_context *src,
			  const struct rm_context *dest);
extern void flock_client_utdata_ops_set(struct rm_ut_data *data);

enum {
	RM_SERVICE_SVC_NR = 1,
};

struct m0_reqh_service_type **stype;

static struct m0_rpc_server_ctx sctx = {
	.rsx_xprts            = &xprt,
	.rsx_xprts_nr         = 1,
	.rsx_argv             = server_argv,
	.rsx_argc             = ARRAY_SIZE(server_argv),
	.rsx_service_types    = m0_cs_default_stypes,
	.rsx_service_types_nr = RM_SERVICE_SVC_NR,
	.rsx_log_file_name    = SERVER_LOG,
};

static void rm_service_start(struct m0_rpc_server_ctx *sctx)
{
	int result;

	M0_ALLOC_ARR(stype, RM_SERVICE_SVC_NR);
	M0_ASSERT(stype != NULL);
	stype[0] = &m0_rms_type;

	result = m0_rpc_server_start(sctx);
	M0_UT_ASSERT(result == 0);
}

static void rm_service_stop(struct m0_rpc_server_ctx *sctx)
{
	m0_rpc_server_stop(sctx);
	m0_free(stype);
}

static void rm_svc_server(const int tid)
{
	rm_service_start(&sctx);

	/* Signal client that server is now up and running */
	m0_chan_signal_lock(&rr_tests_chan);
	/* Stay alive till client runs its test cases */
	m0_chan_wait(&tests_clink[SERVER_2]);

	rm_service_stop(&sctx);
	/* Tell client that I am done */
	m0_chan_signal_lock(&rr_tests_chan);
}

static void rm_client(const int tid)
{
	int                    rc;
	struct m0_rm_incoming  in;
	struct m0_rm_remote   *creditor;
	struct m0_rm_resource *resource;
	struct m0_rm_owner     owner1;
	struct m0_file         file1;
	struct m0_file         file2;
	struct m0_fid          fids[] = {{0, 1}, {0, 2}};

	/* Wait till server starts */
	m0_chan_wait(&tests_clink[SERVER_1]);

	m0_ut_rpc_mach_init_and_add(&client_ctx->rc_rmach_ctx);

	m0_mutex_init(&client_ctx->rc_mutex);
	m0_chan_init(&client_ctx->rc_chan, &client_ctx->rc_mutex);
	m0_clink_init(&client_ctx->rc_clink, NULL);

	/* Connect to end point of SERVER_1 */
	rm_connect(client_ctx, server_ctx);

	M0_ALLOC_PTR(creditor);
	M0_UT_ASSERT(creditor != NULL);
	M0_ALLOC_PTR(resource);
	M0_UT_ASSERT(resource != NULL);

	flock_client_utdata_ops_set(&rm_test_data);
	rm_utdata_init(&rm_test_data, OBJ_RES);

	resource->r_type = rm_test_data.rd_rt;

	m0_rm_remote_init(creditor, resource);
	creditor->rem_session              = &client_ctx->rc_sess[SERVER_1];
	creditor->rem_cookie               = M0_COOKIE_NULL;

	/*
	 * Test for Cancel
	 * We perform resource owner_finalisation on debtor before rm-service
	 * shuts down. This results in sending cancel request to rm-service.
	 */
	m0_file_init(&file1, &fids[0], &rm_test_data.rd_dom);
	m0_file_owner_init(&owner1, &file1, NULL);
	owner1.ro_creditor = creditor;
	m0_file_lock(&owner1, &in);
	m0_rm_owner_lock(&owner1);
	rc = m0_sm_timedwait(&in.rin_sm,
			     M0_BITS(RI_SUCCESS, RI_FAILURE),
			     M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(in.rin_rc == 0);
	M0_UT_ASSERT(incoming_state(&in) == RI_SUCCESS);

	m0_rm_owner_unlock(&owner1);
	m0_file_unlock(&in);
	m0_rm_owner_windup(&owner1);
	rc = m0_rm_owner_timedwait(&owner1, M0_BITS(ROS_FINAL), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	m0_file_owner_fini(&owner1);
	m0_file_fini(&file1);

	/*
	 * Test for Revoke
	 * We disconnect from server and stop the rm-service before
	 * performing resource owner finalisation on debtor. In this case
	 * server sends revoke request for file resource.
	 */
	m0_file_init(&file2, &fids[1], &rm_test_data.rd_dom);
	m0_file_owner_init(&owner1, &file2, NULL);
	owner1.ro_creditor = creditor;
	m0_file_lock(&owner1, &in);
	m0_rm_owner_lock(&owner1);
	rc = m0_sm_timedwait(&in.rin_sm,
			     M0_BITS(RI_SUCCESS, RI_FAILURE),
			     M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(in.rin_rc == 0);
	M0_UT_ASSERT(incoming_state(&in) == RI_SUCCESS);

	m0_rm_owner_unlock(&owner1);
	m0_file_unlock(&in);

	rm_disconnect(client_ctx, server_ctx);

	/* Tell server to stop */
	m0_chan_signal_lock(&rr_tests_chan);
	/* Wait for server to stop */
	m0_chan_wait(&tests_clink[SERVER_1]);

	m0_rm_owner_windup(&owner1);
	rc = m0_rm_owner_timedwait(&owner1, M0_BITS(ROS_FINAL), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	m0_file_owner_fini(&owner1);
	m0_file_fini(&file2);
	rm_utdata_fini(&rm_test_data, OBJ_RES);

	m0_rm_remote_fini(creditor);
	m0_free(resource);
	m0_free(creditor);

	m0_clink_fini(&client_ctx->rc_clink);
	m0_chan_fini_lock(&client_ctx->rc_chan);
	m0_mutex_fini(&client_ctx->rc_mutex);
	m0_ut_rpc_mach_fini(&client_ctx->rc_rmach_ctx);
}

/*
 * Two threads are started; One server, which runs rm-service and one client
 * which requests resource credits to server.
 *
 * First server is started by using m0_rpc_server_start. rm-service option is
 * provided in rpc server context.
 *
 * Client asks for credit request of rings resource type. A dummy creditor is
 * created which points to session established with rm-service.
 *
 * When borrow request FOP reaches server, server checks that creditor cookie is
 * NULL; It now creates an owner for given resource type and grants this request
 * to client.
 */
void rmsvc(void)
{
	int rc;

	m0_mutex_init(&rr_tests_chan_mutex);
	m0_chan_init(&rr_tests_chan, &rr_tests_chan_mutex);

	for (rc = 0; rc <= 1; ++rc) {
		M0_SET0(&rm_ctx[rc]);
		rm_ctx[rc].rc_id = rc;
		rm_ctx[rc].rc_rmach_ctx.rmc_cob_id.id = cob_ids[rc];
		rm_ctx[rc].rc_rmach_ctx.rmc_dbname = db_name[rc];
		rm_ctx[rc].rc_rmach_ctx.rmc_ep_addr = serv_addr[rc];
		m0_clink_init(&tests_clink[rc], NULL);
		m0_clink_add_lock(&rr_tests_chan, &tests_clink[rc]);
	}

	/* Start the server */
	rc = M0_THREAD_INIT(&server_ctx->rc_thr, int, NULL, &rm_svc_server, 0,
			    "rm_svc_%d", 0);
	M0_UT_ASSERT(rc == 0);

	/* Start client */
	rc = M0_THREAD_INIT(&client_ctx->rc_thr, int, NULL, &rm_client, 0,
			    "rm_cli_%d", 0);
	M0_UT_ASSERT(rc == 0);

	m0_thread_join(&server_ctx->rc_thr);
	m0_thread_join(&client_ctx->rc_thr);
	m0_thread_fini(&server_ctx->rc_thr);
	m0_thread_fini(&client_ctx->rc_thr);

	for (rc = 0; rc <= 1; ++rc) {
		m0_clink_del_lock(&tests_clink[rc]);
		m0_clink_fini(&tests_clink[rc]);
	}

	m0_chan_fini_lock(&rr_tests_chan);
	m0_mutex_fini(&rr_tests_chan_mutex);
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
