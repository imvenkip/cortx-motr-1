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

static void op_qa_output(struct m0_mgmt_ctl_ctx *ctx,
			 struct m0_fop_mgmt_service_state_res *ssr)
{
	int i;
	struct m0_mgmt_service_state *ss;
	char uuid[M0_UUID_STRLEN+1];

	if (ssr->msr_rc != 0) {
		emit_error(ctx, "Request failed", ssr->msr_rc);
		return;
	}

	if (ctx->mcc_yaml) {
		printf("---\n");
		printf("msr_reqh_state: %s\n",
		       rs_to_string(ssr->msr_reqh_state));
		printf("msr_ss:\n");
		for (i = 0; i < ssr->msr_ss.msss_nr; ++i) {
			ss = &ssr->msr_ss.msss_state[i];
			m0_uuid_format(&ss->mss_uuid, uuid, ARRAY_SIZE(uuid));
			printf("   - mss_uuid: %s\n", uuid);
			printf("     mss_state: %s\n",
			       rst_to_string(ss->mss_state));
			printf("     stype: %s\n", uuid_to_stype(ctx, uuid));
		}
	} else {
		/*
		  ugly:
		  printf("REQH %s\n", rs_to_string(ssr->msr_reqh_state));
		 */
		for (i = 0; i < ssr->msr_ss.msss_nr; ++i) {
			ss = &ssr->msr_ss.msss_state[i];
			m0_uuid_format(&ss->mss_uuid, uuid, ARRAY_SIZE(uuid));
			printf("%s (%s) %s\n", uuid, uuid_to_stype(ctx, uuid),
			       rst_to_string(ss->mss_state));
		}
	}
}

static int op_qa_run(struct m0_mgmt_ctl_ctx *ctx)
{
	int rc;
	struct m0_fop *fop;
	struct m0_fop_mgmt_service_state_req *ssfop;
	struct m0_rpc_item *item;
	struct m0_fop *rfop;
	struct m0_fop_mgmt_service_state_res *ssr;

	/* allocate and initialize */
	fop = m0_fop_alloc(&m0_fop_mgmt_service_state_req_fopt, NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		emit_error(ctx, "Unable to allocate FOP", rc);
		return rc;
	}
	ssfop = m0_fop_data(fop);
	rc = m0_addb_ctx_export(&ctx->mcc_addb_ctx,
				&ssfop->mssrq_addb_ctx_id);
	if (rc != 0) {
		emit_error(ctx, "Unable to export ADDB context", rc);
		m0_fop_put(fop);
		return rc;
	}

	/* send */
	item = &fop->f_item;
	item->ri_nr_sent_max = 10;
	rc = m0_rpc_client_call(fop, &ctx->mcc_client.rcx_session, NULL, 0);
	if (rc != 0) {
		emit_error(ctx, "Client call failed", rc);
		m0_fop_put(fop);
		return rc;
	}
	if (item->ri_error != 0) {
		rc = item->ri_error;
		emit_error(ctx, "RPC error", rc);
		m0_fop_put(fop);
		return rc;
	}

	/* process response */
	rfop = m0_rpc_item_to_fop(item->ri_reply);
	ssr = m0_fop_data(rfop);
	op_qa_output(ctx, ssr);

	m0_fop_put(fop);
	return 0;
}

static int op_qa_main(int argc, char *argv[],
		      struct m0_mgmt_ctl_ctx *ctx)
{
	int64_t repeat = 0;
	int rc;

	rc = M0_GETOPTS("query-all", argc, argv,
			M0_NUMBERARG('r', "Repeat rate in seconds",
				     LAMBDA(void, (int64_t secs)
					    {
						    repeat = secs;
					    })),
			);
	if (rc != 0)
		return rc;
	if (repeat < 0) {
		fprintf(stderr, "Invalid repeat rate '%ld'\n", repeat);
		return -EINVAL;
	}

	while (1) {  /* infinite if repeating */
		rc = client_init(ctx);
		if (rc != 0 && repeat > 0)
			goto retry;

		rc = op_qa_run(ctx);
		if (rc != 0 && repeat > 0)
			goto retry;

		client_fini(ctx);
		if (repeat == 0)
			break;
	retry:
		sleep(repeat);
	}

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
