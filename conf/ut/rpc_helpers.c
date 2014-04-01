/* -*- c -*- */
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 01-Dec-2012
 */

#include "db/db.h"    /* m0_dbenv */
#include "reqh/reqh.h"
#include "rpc/rpc.h"

static struct m0_reqh            g_reqh;
static struct m0_net_domain      g_net_dom;
static struct m0_net_buffer_pool g_buf_pool;

static int net_init(struct m0_net_xprt *xprt, struct m0_net_domain *dom)
{
	int rc;

	rc = m0_net_xprt_init(xprt);
	if (rc != 0)
		return rc;

	rc = m0_net_domain_init(dom, xprt, &m0_addb_proc_ctx);
	if (rc != 0)
		m0_net_xprt_fini(xprt);
	return rc;
}

static void net_fini(struct m0_net_xprt *xprt, struct m0_net_domain *dom)
{
	m0_net_domain_fini(dom);
	m0_net_xprt_fini(xprt);
}

static struct m0_net_xprt *net_xprt(const struct m0_rpc_machine *mach)
{
	return mach->rm_tm.ntm_dom->nd_xprt;
}

M0_INTERNAL int m0_ut_rpc_machine_start(struct m0_rpc_machine *mach,
					struct m0_net_xprt *xprt,
					const char *ep_addr)
{
	enum {
		NR_TMS = 1,
	};
	int rc;

	rc = net_init(xprt, &g_net_dom);
	if (rc != 0)
		return rc;

	rc = m0_rpc_net_buffer_pool_setup(&g_net_dom, &g_buf_pool,
					  m0_rpc_bufs_nr(
						  M0_NET_TM_RECV_QUEUE_DEF_LEN,
						  NR_TMS),
					  NR_TMS);
	if (rc != 0)
		goto net;

	rc = M0_REQH_INIT(&g_reqh,
			  .rhia_dtm       = (void*)1,
			  .rhia_db        = NULL,
			  .rhia_mdstore   = (void*)1);
	if (rc != 0)
		goto buf_pool;
	m0_reqh_start(&g_reqh);

	rc = m0_rpc_machine_init(mach, &g_net_dom, ep_addr, &g_reqh,
				 &g_buf_pool, M0_BUFFER_ANY_COLOUR,
				 M0_RPC_DEF_MAX_RPC_MSG_SIZE,
				 M0_NET_TM_RECV_QUEUE_DEF_LEN);
	if (rc == 0) {
		M0_POST(net_xprt(mach) == xprt);
		return 0;
	}

buf_pool:
	m0_rpc_net_buffer_pool_cleanup(&g_buf_pool);
net:
	net_fini(xprt, &g_net_dom);
	return rc;
}

M0_INTERNAL void m0_ut_rpc_machine_stop(struct m0_rpc_machine *mach)
{
	struct m0_net_xprt *xprt = net_xprt(mach);

	m0_rpc_machine_fini(mach);
	m0_reqh_services_terminate(&g_reqh);
	m0_reqh_fini(&g_reqh);
	m0_rpc_net_buffer_pool_cleanup(&g_buf_pool);
	net_fini(xprt, &g_net_dom);
}
