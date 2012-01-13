/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "lib/errno.h"

#include "lib/cdefs.h"
#include "lib/types.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "rpc/rpc2.h"
#include "net/net.h"
#include "fop/fop.h"
#include "reqh/reqh.h"

#include "rpc/rpclib.h"


int c2_rpc_server_start(struct c2_rpc_ctx *rctx)
{
	return c2_rpcmachine_init(&rctx->rx_rpc_machine, rctx->rx_cob_dom,
				  rctx->rx_net_dom, rctx->rx_local_addr,
				  rctx->rx_reqh);
}

void c2_rpc_server_stop(struct c2_rpc_ctx *rctx)
{
	c2_rpcmachine_fini(&rctx->rx_rpc_machine);
}

int c2_rpc_client_start(struct c2_rpc_ctx *rctx)
{
	int rc;
	struct c2_net_transfer_mc *tm;

	rc = c2_rpcmachine_init(&rctx->rx_rpc_machine, rctx->rx_cob_dom,
				rctx->rx_net_dom, rctx->rx_local_addr, NULL);
	if (rc != 0)
		return rc;

	tm = &rctx->rx_rpc_machine.cr_tm;

	rc = c2_net_end_point_create(&rctx->rx_remote_ep, tm, rctx->rx_remote_addr);
	if (rc != 0)
		goto rpcmach_fini;

	rc = c2_rpc_conn_create(&rctx->rx_connection, rctx->rx_remote_ep,
				&rctx->rx_rpc_machine,
				rctx->rx_max_rpcs_in_flight,
				rctx->rx_timeout_s);
	if (rc != 0)
		goto ep_put;

	rc = c2_rpc_session_create(&rctx->rx_session, &rctx->rx_connection,
				   rctx->rx_nr_slots, rctx->rx_timeout_s);
	if (rc != 0)
		goto conn_destroy;

	return rc;

conn_destroy:
	c2_rpc_conn_destroy(&rctx->rx_connection, rctx->rx_timeout_s);
ep_put:
	c2_net_end_point_put(rctx->rx_remote_ep);
rpcmach_fini:
	c2_rpcmachine_fini(&rctx->rx_rpc_machine);
	C2_ASSERT(rc != 0);
	return rc;
}
C2_EXPORTED(c2_rpc_client_start);

int c2_rpc_client_call(struct c2_fop *fop, struct c2_rpc_session *session,
		       uint32_t timeout_s)
{
	int                 rc;
	c2_time_t           timeout;
	struct c2_clink     clink;
	struct c2_rpc_item  *item;

	C2_PRE(fop != NULL);
	C2_PRE(session != NULL);

	item = &fop->f_item;
	c2_rpc_item_init(item);

	item->ri_session = session;
	item->ri_type = &fop->f_type->ft_rpc_item_type;
	item->ri_prio = C2_RPC_ITEM_PRIO_MAX;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&item->ri_chan, &clink);
	c2_time_set(&timeout, timeout_s, 0);
	timeout = c2_time_add(c2_time_now(), timeout);

	rc = c2_rpc_post(item);
	if (rc != 0)
		goto clean;

	rc = c2_rpc_reply_timedwait(&clink, timeout);
clean:
	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	return rc;
}
C2_EXPORTED(c2_rpc_client_call);

int c2_rpc_client_stop(struct c2_rpc_ctx *rctx)
{
	int rc;

	rc = c2_rpc_session_destroy(&rctx->rx_session, rctx->rx_timeout_s);
	if (rc != 0)
		return rc;

	rc = c2_rpc_conn_destroy(&rctx->rx_connection, rctx->rx_timeout_s);
	if (rc != 0)
		return rc;

	c2_net_end_point_put(rctx->rx_remote_ep);
	c2_rpcmachine_fini(&rctx->rx_rpc_machine);

	return rc;
}
C2_EXPORTED(c2_rpc_client_stop);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
