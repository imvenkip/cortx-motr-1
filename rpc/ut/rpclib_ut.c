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
 * Original author: Dmitriy Chumak <dmitriy_chumak@xyratex.com>
 * Original creation date: 09/28/2011
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

#ifdef ENABLE_FAULT_INJECTION
static void test_m0_rpc_server_start(void)
{
	m0_fi_enable_once("m0_cs_init", "fake_error");
	M0_UT_ASSERT(m0_rpc_server_start(&sctx) != 0);

	m0_fi_enable_once("m0_cs_setup_env", "fake_error");
	M0_UT_ASSERT(m0_rpc_server_start(&sctx) != 0);
}

static void test_m0_rpc_client_start(void)
{
	int rc;

	rc = m0_rpc_server_start(&sctx);
	M0_UT_ASSERT(rc == 0);
	if (rc != 0)
		return;

	m0_fi_enable_once("m0_rpc_machine_init", "fake_error");
	M0_UT_ASSERT(m0_rpc_client_init(&cctx) != 0);

	m0_fi_enable_once("m0_net_end_point_create", "fake_error");
	M0_UT_ASSERT(m0_rpc_client_init(&cctx) != 0);

	m0_fi_enable_once("m0_rpc_conn_create", "fake_error");
	M0_UT_ASSERT(m0_rpc_client_init(&cctx) != 0);

	m0_fi_enable_once("m0_rpc_conn_establish", "fake_error");
	M0_UT_ASSERT(m0_rpc_client_init(&cctx) != 0);

	m0_fi_enable_once("m0_rpc_session_establish", "fake_error");
	M0_UT_ASSERT(m0_rpc_client_init(&cctx) != 0);

	m0_rpc_server_stop(&sctx);
}

static void test_rpclib_error_paths(void)
{
	test_m0_rpc_server_start();
	test_m0_rpc_client_start();
}
#else
static void test_rpclib_error_paths(void)
{
}
#endif

static int send_fop(struct m0_rpc_session *session)
{
	int                   rc;
	struct m0_fop         *fop;
	struct cs_ds2_req_fop *cs_ds2_fop;

	fop = m0_fop_alloc(&cs_ds2_req_fop_fopt, NULL);
	M0_UT_ASSERT(fop != NULL);
	if (fop == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	cs_ds2_fop = m0_fop_data(fop);
	cs_ds2_fop->csr_value = 0xaaf5;

	rc = m0_rpc_client_call(fop, session, &cs_ds_req_fop_rpc_item_ops,
				0 /* deadline */, CONNECT_TIMEOUT);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(fop->f_item.ri_error == 0);
	M0_UT_ASSERT(fop->f_item.ri_reply != 0);

	m0_fop_put(fop);
out:
	return rc;
}

static void test_rpclib(void)
{
	int                    rc;

	/*
	 * There is no need to initialize xprt explicitly if client and server
	 * run within a single process, because in this case transport is
	 * initialized by m0_rpc_server_start().
	 */

	rc = m0_rpc_server_start(&sctx);
	M0_UT_ASSERT(rc == 0);
	if (rc != 0)
		return;

	rc = m0_rpc_client_init(&cctx);
	M0_UT_ASSERT(rc == 0);
	if (rc != 0)
		goto server_fini;

	rc = send_fop(&cctx.rcx_session);
	M0_UT_ASSERT(rc == 0);

	rc = m0_rpc_client_fini(&cctx);
	M0_UT_ASSERT(rc == 0);

server_fini:
	m0_rpc_server_stop(&sctx);
	return;
}

static int test_rpclib_init(void)
{
	int rc;

	/* set ADDB leve to AEL_WARN to see ADDB messages on STDOUT */
	/*m0_addb_choose_default_level(AEL_WARN);*/

	rc = m0_net_xprt_init(xprt);
	M0_ASSERT(rc == 0);

	rc = m0_net_domain_init(&client_net_dom, xprt);
	M0_ASSERT(rc == 0);

	return rc;
}

static int test_rpclib_fini(void)
{
	m0_net_domain_fini(&client_net_dom);
	m0_net_xprt_fini(xprt);
	return 0;
}

const struct m0_test_suite rpclib_ut = {
	.ts_name = "rpclib-ut",
	.ts_init = test_rpclib_init,
	.ts_fini = test_rpclib_fini,
	.ts_tests = {
		{ "rpclib", test_rpclib },
		{ "rpclib_error_paths", test_rpclib_error_paths },
		{ NULL, NULL }
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
