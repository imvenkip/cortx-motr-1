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
 * Original author: Mikhail Antropov <mikhail.v.antropov@seagate.com>
 * Original creation date: 14-Apr-2015
 */

#include "ut/ut.h"
#include "lib/memory.h"
#include "lib/trace.h"
#include "reqh/reqh.h"
#include "rpc/session.h"
#include "net/lnet/lnet.h"
#include "rpc/link.h"

static void rlut_init(struct m0_net_domain      *net_dom,
		      struct m0_net_buffer_pool *buf_pool,
		      struct m0_reqh            *reqh,
		      struct m0_rpc_machine     *rmachine)
{
	const char               *client_ep = "0@lo:12345:34:1";
	/* unreacheable remote EP */
	struct m0_net_xprt       *xprt = &m0_net_lnet_xprt;
	int                       rc;

	enum {
		NR_TMS = 1,
	};

	M0_SET0(net_dom);
	M0_SET0(buf_pool);
	M0_SET0(reqh);
	M0_SET0(rmachine);

	rc = m0_net_domain_init(net_dom, xprt);
	M0_UT_ASSERT(rc == 0);

	rc = m0_rpc_net_buffer_pool_setup(net_dom,
	                                  buf_pool,
	                                  m0_rpc_bufs_nr(
	                                     M0_NET_TM_RECV_QUEUE_DEF_LEN,
	                                     NR_TMS),
	                                  NR_TMS);
	M0_UT_ASSERT(rc == 0);

	rc = M0_REQH_INIT(reqh,
			  .rhia_dtm     = (void *)1,
			  .rhia_mdstore = (void *)1,
			  .rhia_fid     = &g_process_fid);
	M0_UT_ASSERT(rc == 0);
	m0_reqh_start(reqh);

	rc = m0_rpc_machine_init(rmachine, net_dom, client_ep, reqh, buf_pool,
	                         M0_BUFFER_ANY_COLOUR,
	                         M0_RPC_DEF_MAX_RPC_MSG_SIZE,
	                         M0_NET_TM_RECV_QUEUE_DEF_LEN);
	M0_UT_ASSERT(rc == 0);
}

static void rlut_fini(struct m0_net_domain      *net_dom,
		      struct m0_net_buffer_pool *buf_pool,
		      struct m0_reqh            *reqh,
		      struct m0_rpc_machine     *rmachine)
{
	m0_reqh_services_terminate(reqh);
	m0_rpc_machine_fini(rmachine);
	m0_reqh_fini(reqh);
	m0_rpc_net_buffer_pool_cleanup(buf_pool);
	m0_net_domain_fini(net_dom);
}

static void rlut_remote_unreachable(void)
{
	struct m0_net_domain      net_dom;
	struct m0_net_buffer_pool buf_pool;
	struct m0_reqh            reqh;
	struct m0_rpc_machine     rmachine;
	struct m0_rpc_link       *rlink;
	/* unreacheable remote EP */
	const char               *remote_ep = "0@lo:12345:35:1";
	int                       rc;

	enum {
		MAX_RPCS_IN_FLIGHT = 1,
		CONN_TIMEOUT       = 2, /* seconds */
	};

	rlut_init(&net_dom, &buf_pool, &reqh, &rmachine);

	/* RPC link structure is too big to be allocated on stack */
	M0_ALLOC_PTR(rlink);
	M0_UT_ASSERT(rlink != NULL);

	rc = m0_rpc_link_init(rlink, &rmachine, remote_ep,
			      MAX_RPCS_IN_FLIGHT);
	M0_UT_ASSERT(rc == 0);

	rc = m0_rpc_link_connect_sync(rlink, m0_time_from_now(CONN_TIMEOUT, 0));
	M0_UT_ASSERT(rc != 0 && !rlink->rlk_connected);
	m0_rpc_link_fini(rlink);
	m0_free(rlink);

	rlut_fini(&net_dom, &buf_pool, &reqh, &rmachine);
}

struct m0_ut_suite link_lib_ut = {
	.ts_name = "rpc-link-ut",
	.ts_tests = {
		{ "remote-unreacheable", rlut_remote_unreachable },
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
