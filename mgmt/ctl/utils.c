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
		printf("---\n");
	} else
		fprintf(stderr, "Error: %s (%d)\n", msg, rc);
}

static int mgmt_ctl_client_init(struct m0_mgmt_ctl_ctx *ctx)
{
	int rc;
	struct m0_rpc_client_ctx *c;
	struct m0_cob_domain_id   cob_dom_id;

	rc = m0_init();
	if (rc != 0) {
		emit_error(ctx, "Failed to initialize library", rc);
		return rc;
	}

	M0_ADDB_CTX_INIT(&m0_addb_gmc, &ctx->mcc_addb_ctx, &m0_addb_ct_mgmt_ctl,
			 &m0_addb_proc_ctx);

	c = &ctx->mcc_client;
	M0_PRE(c->rcx_net_dom == NULL);

	/* Create a private database. */
	/** @todo Q: is this a file or a directory? */
	sprintf(ctx->mcc_dbname, "/tmp/m0ctlXXXXXX.db");
	mktemp(ctx->mcc_dbname);
	M0_ASSERT(strlen(ctx->mcc_dbname) < ARRAY_SIZE(ctx->mcc_dbname));

	/*
	  Following based on ut/rpc.c and mgmt/svc/ut/mgmt_svc_setup.c
	  among others.
	 */
	c->rcx_net_dom               = &ctx->mcc_net_dom;
	c->rcx_local_addr            = 0; /** @todo from genders */
	c->rcx_remote_addr           = 0; /** @todo from genders */
	c->rcx_db_name               = ctx->mcc_dbname;
	c->rcx_dbenv                 = &ctx->mcc_dbenv;
	c->rcx_cob_dom_id            = 999; /* arbitrary */
	c->rcx_cob_dom               = &ctx->mcc_cob_dom;
	c->rcx_nr_slots              = 1;
	c->rcx_timeout_s             = m0_time_seconds(ctx->mcc_timeout);
	c->rcx_max_rpcs_in_flight    = 1;
	c->rcx_recv_queue_min_length = M0_NET_TM_RECV_QUEUE_DEF_LEN;

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
	rc = m0_dbenv_init(c->rcx_dbenv, c->rcx_db_name, 0);
	if (rc != 0) {
		emit_error(ctx, "Failed to initialize dbenv", rc);
		goto dbenv_fail;
	}
	cob_dom_id.id = c->rcx_cob_dom_id;
	rc = m0_cob_domain_init(c->rcx_cob_dom, c->rcx_dbenv, &cob_dom_id);
	if (rc != 0) {
		emit_error(ctx, "Failed to initialize cob domain", rc);
		goto cob_domain_fail;
	}
	rc = m0_rpc_client_start(c);
	if (rc != 0) {
		emit_error(ctx, "Failed to start RPC client", rc);
		goto client_fail;
	}

	/*
	   Note that it is not strictly necessary to register the ADDB
	   item source as the context is created mainly for conveyance
	   in requests.
	 */

	return 0;

 client_fail:
	m0_cob_domain_fini(c->rcx_cob_dom);
 cob_domain_fail:
	m0_dbenv_fini(c->rcx_dbenv);
	unlink(c->rcx_db_name); /** @todo rmdir? */
 dbenv_fail:
	m0_net_domain_fini(c->rcx_net_dom);
 net_domain_fail:
	m0_net_xprt_fini(&m0_net_lnet_xprt);
	c->rcx_net_dom = NULL;
	M0_ASSERT(rc != 0);
	m0_fini();
	fprintf(stderr, "Failed to initialize client\n");
	return rc;
}

static void mgmt_ctl_client_fini(struct m0_mgmt_ctl_ctx *ctx)
{
	int rc;
	struct m0_rpc_client_ctx *c = &ctx->mcc_client;

	M0_PRE(c->rcx_net_dom != NULL);
	rc = m0_rpc_client_stop(c);
	m0_cob_domain_fini(c->rcx_cob_dom);
	m0_dbenv_fini(c->rcx_dbenv);
	unlink(c->rcx_db_name); /** @todo rmdir? */
	m0_net_domain_fini(c->rcx_net_dom);
	m0_net_xprt_fini(&m0_net_lnet_xprt);
	c->rcx_net_dom = NULL;
	m0_addb_ctx_fini(&ctx->mcc_addb_ctx);
	m0_fini();
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
	/** @todo need genders data */
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
