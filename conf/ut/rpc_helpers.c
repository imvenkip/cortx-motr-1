/* -*- c -*- */
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 01-Dec-2012
 */

#include "db/db.h"    /* c2_dbenv */
#include "cob/cob.h"  /* c2_cob_domain */
#include "rpc/rpc.h"

static struct c2_net_domain      g_net_dom;
static struct c2_net_buffer_pool g_buf_pool;
static struct c2_dbenv           g_dbenv;
static struct c2_cob_domain      g_cob_dom;

static int
cob_init(struct c2_cob_domain *dom, uint32_t dom_id, const char *dbname)
{
	int rc;

	rc = c2_dbenv_init(&g_dbenv, dbname, 0);
	if (rc != 0)
		return rc;

	rc = c2_cob_domain_init(dom, &g_dbenv,
				&(const struct c2_cob_domain_id){
					.id = dom_id });
	if (rc != 0)
		c2_dbenv_fini(&g_dbenv);
	return rc;
}

static void cob_fini(struct c2_cob_domain *dom)
{
	c2_cob_domain_fini(dom);
	c2_dbenv_fini(&g_dbenv);
}

static int net_init(struct c2_net_xprt *xprt, struct c2_net_domain *dom)
{
	int rc;

	rc = c2_net_xprt_init(xprt);
	if (rc != 0)
		return rc;

	rc = c2_net_domain_init(dom, xprt);
	if (rc != 0)
		c2_net_xprt_fini(xprt);
	return rc;
}

static void net_fini(struct c2_net_xprt *xprt, struct c2_net_domain *dom)
{
	c2_net_domain_fini(dom);
	c2_net_xprt_fini(xprt);
}

static struct c2_net_xprt *net_xprt(const struct c2_rpc_machine *mach)
{
	return mach->rm_tm.ntm_dom->nd_xprt;
}

C2_INTERNAL int c2_ut_rpc_machine_start(struct c2_rpc_machine *mach,
					struct c2_net_xprt *xprt,
					const char *ep_addr, const char *dbname)
{
	enum {
		NR_TMS = 1,
		COB_DOM_ID = 221 /* just a random value */
	};
	int rc;

	rc = net_init(xprt, &g_net_dom);
	if (rc != 0)
		return rc;

	rc = c2_rpc_net_buffer_pool_setup(&g_net_dom, &g_buf_pool,
					  c2_rpc_bufs_nr(
						  C2_NET_TM_RECV_QUEUE_DEF_LEN,
						  NR_TMS),
					  NR_TMS);
	if (rc != 0)
		goto net;

	rc = cob_init(&g_cob_dom, COB_DOM_ID, dbname);
	if (rc != 0)
		goto buf_pool;

	rc = c2_rpc_machine_init(mach, &g_cob_dom, &g_net_dom, ep_addr, NULL,
				 &g_buf_pool, C2_BUFFER_ANY_COLOUR,
				 C2_RPC_DEF_MAX_RPC_MSG_SIZE,
				 C2_NET_TM_RECV_QUEUE_DEF_LEN);
	if (rc == 0) {
		C2_POST(net_xprt(mach) == xprt);
		return 0;
	}

	cob_fini(&g_cob_dom);
buf_pool:
	c2_rpc_net_buffer_pool_cleanup(&g_buf_pool);
net:
	net_fini(xprt, &g_net_dom);
	return rc;
}

C2_INTERNAL void c2_ut_rpc_machine_stop(struct c2_rpc_machine *mach)
{
	struct c2_net_xprt *xprt = net_xprt(mach);

	c2_rpc_machine_fini(mach);
	cob_fini(&g_cob_dom);
	c2_rpc_net_buffer_pool_cleanup(&g_buf_pool);
	net_fini(xprt, &g_net_dom);
}
