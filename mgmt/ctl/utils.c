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
 * Original creation date: 1-April-2013
 */

/* This file is designed to be included in mgmt/ctl/mgmt_ctl.c */

/**
   @ingroup mgmt_ctl_pvt
   @{
 */

/**
   Emit an error in a yaml doc on stdout or as a message on stderr.
 */
static void emit_error(struct m0_mgmt_ctl_ctx *ctx, const char *msg, int rc)
{
	if (ctx->mcc_yaml) {
		printf("---\n");
		printf("error:\n");
		printf("  msg: %s\n", msg);
		printf("  rc: %d\n", rc);
		printf("timestamp: %lu\n", m0_time_now());

	} else {
		fprintf(stderr, "Error: %s (%d)\n", msg, rc);
	}
}

/**
  Create a private directory for the database and other
  internal garbage.
 */
static int make_tmpdir(struct m0_mgmt_ctl_ctx *ctx)
{
	sprintf(ctx->mcc_tmpdir, "/tmp/m0ctlXXXXXX");
	M0_ASSERT(strlen(ctx->mcc_tmpdir) < ARRAY_SIZE(ctx->mcc_tmpdir));
	if (mkdtemp(ctx->mcc_tmpdir) == NULL) {
		int rc = -errno;

		emit_error(ctx, "Failed to create temporary directory", rc);
		return rc;
	}
	return 0;
}

static void unlink_tmpdir(struct m0_mgmt_ctl_ctx *ctx)
{
	char cmd[64];
	int  rc;

	if (ctx->mcc_tmpdir[0] == '\0')
		return;
	M0_ASSERT(strcmp(ctx->mcc_tmpdir, "/") != 0);
	sprintf(cmd, "/bin/rm -rf %s", ctx->mcc_tmpdir);
	rc = system(cmd);
}

/**
   Initialize the RPC client interfaces.
   Must be callable again in case of failure.
 */
static int client_init(struct m0_mgmt_ctl_ctx *ctx)
{
	int                       rc;
	struct m0_rpc_client_ctx *c;

	c = &ctx->mcc_rpc_client;
	M0_PRE(c->rcx_net_dom == NULL);

	M0_ASSERT(ctx->mcc_tmpdir[0] != '\0');
	sprintf(ctx->mcc_dbname, "%s/db", ctx->mcc_tmpdir);
	M0_ASSERT(strlen(ctx->mcc_dbname) < ARRAY_SIZE(ctx->mcc_dbname));

	/*
	 * Following based on ut/rpc.c and mgmt/svc/ut/mgmt_svc_setup.c
	 * among others.
	 */
	c->rcx_net_dom               = &ctx->mcc_net_dom;
	c->rcx_local_addr            = ctx->mcc_client.mcc_mgmt_ep;
	c->rcx_remote_addr           = ctx->mcc_node.mnc_m0d_ep;
	c->rcx_db_name               = ctx->mcc_dbname;
	c->rcx_dbenv                 = &ctx->mcc_dbenv;
	c->rcx_cob_dom_id            = 999; /* arbitrary */
	c->rcx_cob_dom               = &ctx->mcc_cob_dom;
	c->rcx_nr_slots              = 1;
	c->rcx_max_rpcs_in_flight    = 1;
	if (ctx->mcc_client.mcc_recvq_min_len != 0)
		c->rcx_recv_queue_min_length =
			ctx->mcc_client.mcc_recvq_min_len;
	if (ctx->mcc_client.mcc_max_rpc_msg != 0)
		c->rcx_max_rpc_msg_size      = ctx->mcc_client.mcc_max_rpc_msg;

	rc = m0_net_xprt_init(&m0_net_lnet_xprt);
	if (rc != 0) {
		emit_error(ctx, "Failed to initialize transport", rc);
		return rc;
	}
	rc = m0_net_domain_init(&ctx->mcc_net_dom, &m0_net_lnet_xprt,
				&m0_addb_proc_ctx);
	if (rc != 0) {
		emit_error(ctx, "Failed to initialize network domain", rc);
		goto net_domain_fail;
	}
	rc = m0_rpc_client_start(c);
	if (rc != 0) {
		emit_error(ctx, "Failed to start RPC client", rc);
		goto client_fail;
	}

	/*
	 * Note that it is not strictly necessary to register the ADDB
	 * item source as the context is created mainly for conveyance
	 * in requests.
	 */

	return 0;

 client_fail:
	m0_net_domain_fini(c->rcx_net_dom);
 net_domain_fail:
	m0_net_xprt_fini(&m0_net_lnet_xprt);
	c->rcx_net_dom = NULL;
	M0_ASSERT(rc != 0);
	return rc;
}

/**
   Teardown RPC client interfaces.
   Must be initializable again.
 */
static void client_fini(struct m0_mgmt_ctl_ctx *ctx)
{
	int                       rc;
	struct m0_rpc_client_ctx *c = &ctx->mcc_rpc_client;

	M0_PRE(c->rcx_net_dom != NULL);
	rc = m0_rpc_client_stop(c);
	m0_net_domain_fini(c->rcx_net_dom);
	m0_net_xprt_fini(&m0_net_lnet_xprt);
	M0_SET0(c);
}

/**
   Request handler service state to string.
 */
static const char *rst_to_string(int rst)
{
	switch (rst) {
#define RST_TO_S(rst) case M0_RST_##rst: return #rst
		RST_TO_S(INITIALISING);
		RST_TO_S(INITIALISED);
		RST_TO_S(STARTING);
		RST_TO_S(STARTED);
		RST_TO_S(STOPPING);
		RST_TO_S(STOPPED);
		RST_TO_S(FAILED);
#undef RST_TO_S
	default:
		return "unknown";
	}
}

/**
   Request handler state to string.
 */
static const char *rs_to_string(int rs)
{
	switch (rs) {
#define RS_TO_S(rs) case M0_REQH_ST_##rs: return #rs
	RS_TO_S(INIT);
	RS_TO_S(MGMT_STARTED);
	RS_TO_S(NORMAL);
	RS_TO_S(DRAIN);
	RS_TO_S(SVCS_STOP);
	RS_TO_S(MGMT_STOP);
	RS_TO_S(STOPPED);
#undef RS_TO_S
	default:
		return "unknown";
	}
}

/**
   Determine service type from UUID
 */
static const char *uuid_to_stype(struct m0_mgmt_ctl_ctx *ctx,
				 const char *uuid)
{
	struct m0_mgmt_svc_conf *svc;

	m0_tl_for(m0_mgmt_conf, &ctx->mcc_node.mnc_svc, svc) {
		if (strcasecmp(svc->msc_uuid, uuid) == 0)
			return svc->msc_name;
	} m0_tl_endfor;
	return "unknown";
}

/** @} end mgmt_ctl_pvt group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
