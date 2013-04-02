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

static int op_qa_run(struct m0_mgmt_ctl_ctx *ctx)
{
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

	rc = mgmt_ctl_client_init(ctx);
	if (rc != 0)
		return rc;

	while (repeat-- >= 0) {
		rc = op_qa_run(ctx);
	}

	mgmt_ctl_client_fini(ctx);
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
