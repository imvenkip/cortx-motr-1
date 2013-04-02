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

static int mgmt_ctl_client_init(struct m0_mgmt_ctl_ctx *ctx)
{
	int rc;
	struct m0_rpc_client_ctx *c;
	struct m0_cob_domain_id   cob_dom_id;

	rc = m0_init();
	if (rc != 0) {
		fprintf(stderr, "Failed to initialize library\n");
		return rc;
	}

	c = &ctx->mcc_client;
	M0_PRE(c->rcx_net_dom == NULL);

	sprintf(ctx->mcc_dbname, "/tmp/m0ctlXXXXXX.db");
	mktemp(ctx->mcc_dbname); /** @todo mkdtemp? */
	M0_ASSERT(strlen(ctx->mcc_dbname) < ARRAY_SIZE(ctx->mcc_dbname));

	/*
	  Following based on ut/rpc.c and mgmt/svc/ut/mgmt_svc_setup.c
	  among others.
	 */
	c->rcx_net_dom               = &ctx->mcc_net_dom;
	c->rcx_local_addr            = 0; /* from genders */
	c->rcx_remote_addr           = 0; /* from genders */
	c->rcx_db_name               = ctx->mcc_dbname;
	c->rcx_dbenv                 = &ctx->mcc_dbenv;
	c->rcx_cob_dom_id            = 999; /* arbitrary */
	c->rcx_cob_dom               = &ctx->mcc_cob_dom;
	c->rcx_nr_slots              = 1;
	c->rcx_timeout_s             = m0_time_seconds(ctx->mcc_timeout);
	c->rcx_max_rpcs_in_flight    = 1;
	c->rcx_recv_queue_min_length = M0_NET_TM_RECV_QUEUE_DEF_LEN;

	rc = m0_net_xprt_init(&m0_net_lnet_xprt);
	if (rc != 0)
		return rc;
	rc = m0_net_domain_init(&ctx->mcc_net_dom, &m0_net_lnet_xprt,
				&m0_addb_proc_ctx);
	if (rc != 0)
		goto net_domain_fail;
	rc = m0_dbenv_init(c->rcx_dbenv, c->rcx_db_name, 0);
	if (rc != 0)
		goto dbenv_fail;
	cob_dom_id.id = c->rcx_cob_dom_id;
	rc = m0_cob_domain_init(c->rcx_cob_dom, c->rcx_dbenv, &cob_dom_id);
	if (rc != 0)
		goto cob_domain_fail;
	rc = m0_rpc_client_start(c);
	if (rc != 0)
		goto client_fail;
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

static int mgmt_ctl_client_fini(struct m0_mgmt_ctl_ctx *ctx)
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
	m0_fini();
	return rc;
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
