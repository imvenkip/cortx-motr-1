/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Amit Jambure<amit_jambure@xyratex.com>
 * Original creation date: 10/19/2012
 */

#include "lib/ut.h"
#include "lib/memory.h"
#include "lib/processor.h"
#include "lib/trace.h"
#include "lib/finject.h"
#include "addb/addb.h"
#include "fop/fop.h"
#include "reqh/reqh.h"

#include "rpc/session.h"
#include "rpc/it/ping_fop.h"
#include "rpc/it/ping_fop_ff.h"
#include "rpc/rpclib.h"
#include "net/lnet/lnet.h"

#include "ut/rpc.h"
#include "ut/cs_service.h"
#include "ut/cs_fop_foms.h"
#include "ut/cs_test_fops_ff.h"

#include "rpc/ut/clnt_srv_ctx.c"   /* sctx, cctx */

static int send_fop(struct c2_rpc_session *session)
{
	int                   rc;
	struct c2_fop         *fop;
	struct cs_ds2_req_fop *cs_ds2_fop;

	fop = c2_fop_alloc(&cs_ds2_req_fop_fopt, NULL);
	C2_UT_ASSERT(fop != NULL);

	cs_ds2_fop = c2_fop_data(fop);
	cs_ds2_fop->csr_value = 0xaaf5;

	rc = c2_rpc_client_call(fop, session, &cs_ds_req_fop_rpc_item_ops,
				CONNECT_TIMEOUT);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(fop->f_item.ri_error == 0);
	C2_UT_ASSERT(fop->f_item.ri_reply != 0);

	return rc;
}

static int ts_item_init(void)
{
	int rc;

	rc = c2_net_xprt_init(xprt);
	C2_ASSERT(rc == 0);

	rc = c2_net_domain_init(&client_net_dom, xprt);
	C2_ASSERT(rc == 0);

	rc = c2_rpc_server_start(&sctx);
	C2_ASSERT(rc == 0);

	rc = c2_rpc_client_init(&cctx);
	C2_ASSERT(rc == 0);

	return rc;
}

static int ts_item_fini(void)
{
	int rc;

	rc = c2_rpc_client_fini(&cctx);
	C2_ASSERT(rc == 0);
	c2_rpc_server_stop(&sctx);
	c2_net_domain_fini(&client_net_dom);
	c2_net_xprt_fini(xprt);
	return rc;
}

static void test_transitions(void)
{
	send_fop(&cctx.rcx_session);
}

const struct c2_test_suite item_ut= {
	.ts_name = "item-ut",
	.ts_init = ts_item_init,
	.ts_fini = ts_item_fini,
	.ts_tests = {
		{ "dummy", dummy },
		{ NULL, NULL },
	}
};

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
