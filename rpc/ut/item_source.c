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
 * Original author: Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 11-Feb-2013
 */


/**
 * @addtogroup rpc
 *
 * @{
 */

#include "lib/ut.h"
#include "rpc/ut/clnt_srv_ctx.c"
#include <stdio.h>

static int item_source_test_suite_init(void)
{
	start_rpc_client_and_server();
	printf("rpc started...\n");
	return 0;
}

static int item_source_test_suite_fini(void)
{
	stop_rpc_client_and_server();
	printf("rpc stopped...\n");
	return 0;
}

const struct m0_test_suite item_source_ut = {
	.ts_name = "item-source-ut",
	.ts_init = item_source_test_suite_init,
	.ts_fini = item_source_test_suite_fini,
	.ts_tests = {
		{NULL, NULL},
	}
};

/** @} end of rpc group */


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
