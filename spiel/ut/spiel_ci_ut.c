/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Egor Nikulenkov <egor.nikulenkov@seagate.com>
 * Original creation date: 06-Mar-2015
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"

#include "net/lnet/lnet.h"    /* m0_net_lnet_xprt */
#include "rpc/rpc_machine.h"  /* m0_rpc_machine */
#include "rpc/rpclib.h"       /* m0_rpc_server_ctx */
#include "ut/ut.h"
#include "spiel/spiel.h"
#include "spiel/ut/spiel_ut_common.h"

#define SERVER_ENDPOINT_ADDR "0@lo:12345:34:1"
#define SERVER_ENDPOINT      "lnet:" SERVER_ENDPOINT_ADDR
#define CLIENT_ENDPOINT_ADDR "0@lo:12345:34:*"

static struct m0_reqh            g_reqh;
static struct m0_net_domain      g_net_dom;
static struct m0_net_buffer_pool g_buf_pool;

static void test_spiel_service_cmds(void)
{
	struct m0_rpc_server_ctx  confd_srv;
	struct m0_spiel           spiel;
	int                       rc;
	const char               *confd_eps[] = { SERVER_ENDPOINT_ADDR, NULL };
	const char               *profile = "<0x7000000000000001:0>";
	const struct m0_fid       svc_fid = M0_FID_TINIT('s', 1,  10);
	const struct m0_fid       svc_invalid_fid = M0_FID_TINIT('s', 1,  13);
	struct m0_spiel_ut_reqh   ut_reqh = {
		.sur_net_dom  = g_net_dom,
		.sur_buf_pool = g_buf_pool,
		.sur_reqh     = g_reqh,
	};

	rc = m0_spiel__ut_confd_start(&confd_srv, confd_eps[0],
		       	M0_SPIEL_UT_PATH("conf-str-ci.txt"));
	M0_UT_ASSERT(rc == 0);

	m0_spiel__ut_reqh_init(&ut_reqh, CLIENT_ENDPOINT_ADDR);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_start(&spiel, &ut_reqh.sur_reqh, confd_eps, profile);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_service_init(&spiel, &svc_fid);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_service_start(&spiel, &svc_fid);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_service_health(&spiel, &svc_fid);
	/* This is true while the service doesn't implement rso_health */
	M0_UT_ASSERT(rc == M0_HEALTH_UNKNOWN);

	rc = m0_spiel_service_quiesce(&spiel, &svc_fid);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_service_stop(&spiel, &svc_fid);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_service_health(&spiel, &svc_invalid_fid);
	M0_UT_ASSERT(rc == -ENOENT);

	m0_spiel_stop(&spiel);

	m0_spiel__ut_reqh_fini(&ut_reqh);
	m0_spiel__ut_confd_stop(&confd_srv);
}

struct m0_ut_suite spiel_ci_ut = {
	.ts_name  = "spiel-ci-ut",
	.ts_init  = NULL,
	.ts_fini  = NULL,
	.ts_tests = {
		{ "service-cmds", test_spiel_service_cmds },
		{ NULL, NULL }
	}
};

#undef M0_TRACE_SUBSYSTEM
