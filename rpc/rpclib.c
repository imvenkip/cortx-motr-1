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

#include <errno.h>

#include "lib/cdefs.h"
#include "lib/types.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "rpc/rpccore.h"
#include "net/net.h"
#include "fop/fop.h"
#include "reqh/reqh.h"

#include "rpc/rpclib.h"


static int rpc_init_common(struct c2_rpc_ctx *rctx)
{
	int rc;
	struct c2_cob_domain_id   cob_dom_id = { .id = rctx->rx_cob_dom_id };
	struct c2_reqh            *reqh;

	rc = c2_dbenv_init(&rctx->rx_dbenv, rctx->rx_db_name, 0);
	if (rc != 0)
		return rc;

	rc = c2_cob_domain_init(&rctx->rx_cob_dom, &rctx->rx_dbenv, &cob_dom_id);
	if (rc != 0)
		goto dbenv_fini;

	/* Assume that rctx->rx_reqh is already initialized if it's not NULL.
	 * Otherwise allocate and initialize a default reqh */
	if (rctx->rx_reqh != NULL) {
		reqh = rctx->rx_reqh;
	} else {
		C2_ALLOC_PTR(reqh);
		if (reqh == NULL) {
			rc = -ENOMEM;
			goto cob_dom_fini;
		}
		rc = c2_reqh_init(reqh, NULL, NULL, NULL, NULL, NULL);
		if (rc != 0)
			goto reqh_free;
	}

	/* Init the rpcmachine */
	rc = c2_rpcmachine_init(&rctx->rx_rpc_machine, &rctx->rx_cob_dom,
				rctx->rx_net_dom, rctx->rx_local_addr, reqh);
	if (rc != 0)
		goto reqh_fini;

	return rc;

reqh_fini:
	if (rctx->rx_reqh == NULL)
		c2_reqh_fini(reqh);
reqh_free:
	if (rctx->rx_reqh == NULL)
		c2_free(reqh);
cob_dom_fini:
	c2_cob_domain_fini(&rctx->rx_cob_dom);
dbenv_fini:
	c2_dbenv_fini(&rctx->rx_dbenv);
	C2_ASSERT(rc != 0);
	return rc;
}

static void rpc_fini_common(struct c2_rpc_ctx *rctx)
{
	c2_rpcmachine_fini(&rctx->rx_rpc_machine);

	/* If rctx->rx_reqh is NULL it means that a default reqh was allocated,
	 * initialized and stored in rctx->rx_rpc_machine.cr_reqh in
	 * rpc_init_common(), so it should be finalized and freed here */
	if (rctx->rx_reqh == NULL) {
		c2_reqh_fini(rctx->rx_rpc_machine.cr_reqh);
		c2_free(rctx->rx_rpc_machine.cr_reqh);
	}

	c2_cob_domain_fini(&rctx->rx_cob_dom);
	c2_dbenv_fini(&rctx->rx_dbenv);

	return;
}

int c2_rpc_server_init(struct c2_rpc_ctx *rctx)
{
	return rpc_init_common(rctx);
}
C2_EXPORTED(c2_rpc_server_init);

int c2_rpc_client_init(struct c2_rpc_ctx *rctx)
{
	int rc;
	struct c2_net_transfer_mc *tm;

	rc = rpc_init_common(rctx);
	if (rc != 0)
		return rc;

	tm = &rctx->rx_rpc_machine.cr_tm;

	/* Init destination endpoint */
	rc = c2_net_end_point_create(&rctx->rx_remote_ep, tm, rctx->rx_remote_addr);
	if (rc != 0)
		goto fini_common;

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
fini_common:
	rpc_fini_common(rctx);
	C2_ASSERT(rc != 0);
	return rc;
}
C2_EXPORTED(c2_rpc_client_init);

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
	item->ri_type = fop->f_type->ft_ri_type;
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

void c2_rpc_server_fini(struct c2_rpc_ctx *rctx)
{
	rpc_fini_common(rctx);
}
C2_EXPORTED(c2_rpc_server_fini);

int c2_rpc_client_fini(struct c2_rpc_ctx *rctx)
{
	int rc;

	rc = c2_rpc_session_destroy(&rctx->rx_session, rctx->rx_timeout_s);
	if (rc != 0)
		return rc;

	rc = c2_rpc_conn_destroy(&rctx->rx_connection, rctx->rx_timeout_s);
	if (rc != 0)
		return rc;

	c2_net_end_point_put(rctx->rx_remote_ep);
	rpc_fini_common(rctx);

	return rc;
}
C2_EXPORTED(c2_rpc_client_fini);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
