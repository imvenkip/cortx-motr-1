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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 25-Mar-2013
 */

/* This file is designed to be included by mgmt/svc/ut/mgmt_svc_ut.c */

#define MGMT_SVC_UT_SERVER_ENDPOINT_ADDR "0@lo:12345:34:7"
#define MGMT_SVC_UT_SERVER_ENDPOINT "lnet:" MGMT_SVC_UT_SERVER_ENDPOINT_ADDR
#define MGMT_SVC_UT_LOG_FILE_NAME "mgmt_svc_ut.errlog"

#define MGMT_SVC_UT_CLIENT_ENDPOINT_ADDR "0@lo:12345:35:*"
enum {
	MGMT_SVC_UT_CLIENT_COB_DOM_ID  = 311,
	MGMT_SVC_UT_SESSION_SLOTS      = 5,
	MGMT_SVC_UT_MAX_RPCS_IN_FLIGHT = 32,
	MGMT_SVC_UT_CONNECT_TIMEOUT    = 35,
};

static struct m0_net_xprt   *mgmt_svc_ut_xprt = &m0_net_lnet_xprt;
static struct m0_net_domain  mgmt_svc_ut_client_net_dom = { };
static struct m0_dbenv       mgmt_svc_ut_client_dbenv;
static struct m0_cob_domain  mgmt_svc_ut_client_cob_dom;

static struct m0_rpc_client_ctx mgmt_svc_ut_cctx = {
	.rcx_net_dom               = &mgmt_svc_ut_client_net_dom,
	.rcx_local_addr            = MGMT_SVC_UT_CLIENT_ENDPOINT_ADDR,
	.rcx_remote_addr           = MGMT_SVC_UT_SERVER_ENDPOINT_ADDR,
	.rcx_db_name               = "mc.db",
	.rcx_dbenv                 = &mgmt_svc_ut_client_dbenv,
	.rcx_cob_dom_id            = MGMT_SVC_UT_CLIENT_COB_DOM_ID,
	.rcx_cob_dom               = &mgmt_svc_ut_client_cob_dom,
	.rcx_nr_slots              = MGMT_SVC_UT_SESSION_SLOTS,
	.rcx_timeout_s             = MGMT_SVC_UT_CONNECT_TIMEOUT,
	.rcx_max_rpcs_in_flight    = MGMT_SVC_UT_MAX_RPCS_IN_FLIGHT,
	.rcx_recv_queue_min_length = M0_NET_TM_RECV_QUEUE_DEF_LEN,
};

#define MGMT_SVC_UT_SVC M0_MGMT_SVC_UT_SVC_TYPE_NAME ":" M0_MGMT_SVC_UT_SVC_UUID
static char *mgmt_svc_ut_setup_args[] = { "m0d", "-r", "-p", "-T", "linux",
					  "-D", "ms.db", "-S", "ms.stob",
					  "-A", "ms.addb_stob",
					  "-e", MGMT_SVC_UT_SERVER_ENDPOINT,
					  "-s", MGMT_SVC_UT_SVC
};
static struct m0_net_xprt *mgmt_svc_ut_setup_xprts[] = {
	&m0_net_lnet_xprt
};
static struct m0_mero mgmt_svc_ut_setup_sctx;
static FILE          *mgmt_svc_ut_setup_lfile;

static void mgmt_svc_ut_setup_stop(void)
{
	int rc;

	rc = m0_rpc_client_stop(&mgmt_svc_ut_cctx);
	M0_UT_ASSERT(rc == 0);
	m0_cs_fini(&mgmt_svc_ut_setup_sctx);
	m0_net_domain_fini(&mgmt_svc_ut_client_net_dom);
	fclose(mgmt_svc_ut_setup_lfile);
}

static int mgmt_svc_ut_setup_start(void)
{
	int rc;

	mgmt_svc_ut_setup_lfile = fopen(MGMT_SVC_UT_LOG_FILE_NAME, "w+");
	M0_UT_ASSERT(mgmt_svc_ut_setup_lfile != NULL);

	rc = m0_net_xprt_init(mgmt_svc_ut_xprt);
	M0_UT_ASSERT(rc == 0);

	rc = m0_net_domain_init(&mgmt_svc_ut_client_net_dom, mgmt_svc_ut_xprt,
				&m0_addb_proc_ctx);
	M0_UT_ASSERT(rc == 0);

	M0_SET0(&mgmt_svc_ut_setup_sctx);

	rc = m0_cs_init(&mgmt_svc_ut_setup_sctx, mgmt_svc_ut_setup_xprts,
			ARRAY_SIZE(mgmt_svc_ut_setup_xprts),
			mgmt_svc_ut_setup_lfile);
	if (rc != 0)
		goto done;
	rc = m0_cs_setup_env(&mgmt_svc_ut_setup_sctx,
			     ARRAY_SIZE(mgmt_svc_ut_setup_args),
			     mgmt_svc_ut_setup_args);
	if (rc == 0)
		rc = m0_cs_start(&mgmt_svc_ut_setup_sctx);
	if (rc != 0)
		goto done;

	rc = m0_rpc_client_start(&mgmt_svc_ut_cctx);
	if (rc != 0)
		goto stop_server;

	return rc;

 stop_server:
	m0_cs_fini(&mgmt_svc_ut_setup_sctx);
 done:
	M0_UT_ASSERT(rc != 0);
	m0_net_domain_fini(&mgmt_svc_ut_client_net_dom);
	fclose(mgmt_svc_ut_setup_lfile);
	return rc;
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
