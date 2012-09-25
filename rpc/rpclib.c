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

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#ifndef __KERNEL__
#include <errno.h> /* errno */
#include <stdio.h> /* fopen(), fclose() */
#endif

#include "lib/cdefs.h"
#include "lib/types.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "rpc/rpc2.h"
#include "net/net.h"
#include "net/lnet/lnet.h"
#include "fop/fop.h"
#include "rpc/rpclib.h"

#ifndef __KERNEL__
#include "reqh/reqh.h"
#include "reqh/reqh_service.h"
#include "colibri/colibri_setup.h"
#endif

#ifndef __KERNEL__
int c2_rpc_server_start(struct c2_rpc_server_ctx *sctx)
{
	int  i;
	int  rc;

	C2_ENTRY("server_ctx: %p", sctx);
	C2_PRE(sctx->rsx_argv != NULL && sctx->rsx_argc > 0);

	/* Open error log file */
	sctx->rsx_log_file = fopen(sctx->rsx_log_file_name, "w+");
	if (sctx->rsx_log_file == NULL)
		C2_RETERR(errno, "Open of error log file");

	/* Register service types */
	for (i = 0; i < sctx->rsx_service_types_nr; ++i) {
		rc = c2_reqh_service_type_register(sctx->rsx_service_types[i]);
		if (rc != 0)
			goto fclose;
	}

	/* Start rpc server */
	rc = c2_cs_init(&sctx->rsx_colibri_ctx, sctx->rsx_xprts,
			sctx->rsx_xprts_nr, sctx->rsx_log_file);
	if (rc != 0)
		goto service_unreg;

	rc = c2_cs_setup_env(&sctx->rsx_colibri_ctx, sctx->rsx_argc,
			     sctx->rsx_argv);
	if (rc != 0)
		goto cs_fini;

	rc = c2_cs_start(&sctx->rsx_colibri_ctx);

	C2_RETURN(rc);

cs_fini:
	c2_cs_fini(&sctx->rsx_colibri_ctx);
service_unreg:
	for (i = 0; i < sctx->rsx_service_types_nr; ++i)
		c2_reqh_service_type_unregister(sctx->rsx_service_types[i]);
fclose:
	fclose(sctx->rsx_log_file);
	C2_RETURN(rc);
}

void c2_rpc_server_stop(struct c2_rpc_server_ctx *sctx)
{
	int i;

	C2_ENTRY("server_ctx: %p", sctx);

	c2_cs_fini(&sctx->rsx_colibri_ctx);

	for (i = 0; i < sctx->rsx_service_types_nr; ++i)
		c2_reqh_service_type_unregister(sctx->rsx_service_types[i]);

	fclose(sctx->rsx_log_file);

	C2_LEAVE();
	return;
}
#endif

int c2_rpc_client_start(struct c2_rpc_client_ctx *cctx)
{
	int rc;
	struct c2_net_transfer_mc *tm;
	struct c2_net_domain      *ndom;
	struct c2_rpc_machine	  *rpc_mach;
	struct c2_net_buffer_pool *buffer_pool;
	uint32_t		   tms_nr;
	uint32_t		   bufs_nr;

	C2_ENTRY("client_ctx: %p", cctx);

	ndom	    = cctx->rcx_net_dom;
	rpc_mach    = &cctx->rcx_rpc_machine;
	buffer_pool = &cctx->rcx_buffer_pool;

	if (cctx->rcx_recv_queue_min_length == 0)
		cctx->rcx_recv_queue_min_length = C2_NET_TM_RECV_QUEUE_DEF_LEN;

	tms_nr   = 1;
	bufs_nr  = c2_rpc_bufs_nr(cctx->rcx_recv_queue_min_length, tms_nr);

	rc = c2_rpc_net_buffer_pool_setup(ndom, buffer_pool,
					  bufs_nr, tms_nr);
	if (rc != 0)
		goto pool_fini;

	rc = c2_rpc_machine_init(rpc_mach, cctx->rcx_cob_dom,
				 ndom, cctx->rcx_local_addr, NULL,
				 buffer_pool, C2_BUFFER_ANY_COLOUR,
				 cctx->rcx_max_rpc_msg_size,
				 cctx->rcx_recv_queue_min_length);
	if (rc != 0)
		goto pool_fini;

	tm = &cctx->rcx_rpc_machine.rm_tm;

	rc = c2_net_end_point_create(&cctx->rcx_remote_ep, tm,
				      cctx->rcx_remote_addr);
	if (rc != 0)
		goto rpcmach_fini;

	rc = c2_rpc_conn_create(&cctx->rcx_connection, cctx->rcx_remote_ep,
				rpc_mach, cctx->rcx_max_rpcs_in_flight,
				cctx->rcx_timeout_s);
	if (rc != 0)
		goto ep_put;

	rc = c2_rpc_session_create(&cctx->rcx_session, &cctx->rcx_connection,
				   cctx->rcx_nr_slots, cctx->rcx_timeout_s);
	if (rc != 0)
		goto conn_destroy;

	C2_RETURN(rc);

conn_destroy:
	c2_rpc_conn_destroy(&cctx->rcx_connection, cctx->rcx_timeout_s);
ep_put:
	c2_net_end_point_put(cctx->rcx_remote_ep);
rpcmach_fini:
	c2_rpc_machine_fini(rpc_mach);
pool_fini:
	c2_rpc_net_buffer_pool_cleanup(buffer_pool);
	C2_ASSERT(rc != 0);
	C2_RETURN(rc);
}

int c2_rpc_client_call(struct c2_fop *fop, struct c2_rpc_session *session,
		       const struct c2_rpc_item_ops *ri_ops, uint32_t timeout_s)
{
	int                 rc;
	c2_time_t           timeout;
	struct c2_clink     clink;
	struct c2_rpc_item *item;

	C2_ENTRY("fop: %p, session: %p", fop, session);
	C2_PRE(fop != NULL);
	C2_PRE(session != NULL);
	/*
	 * It is mandatory to specify item_ops, because rpc layer needs
	 * implementation of c2_rpc_item_ops::rio_free() in order to free the
	 * item. Consumer can use c2_fop_default_item_ops if, it is not
	 * interested in implementing other (excluding ->rio_free())
	 * interfaces of c2_rpc_item_ops. See also c2_fop_item_free().
	 */
	C2_PRE(ri_ops != NULL);

	item              = &fop->f_item;
	item->ri_ops      = ri_ops;
	item->ri_session  = session;
	item->ri_prio     = C2_RPC_ITEM_PRIO_MAX;
	item->ri_deadline = 0;

	c2_clink_init(&clink, NULL);
	c2_clink_add(&item->ri_chan, &clink);
	c2_time_set(&timeout, timeout_s, 0);
	timeout = c2_time_add(c2_time_now(), timeout);

	rc = c2_rpc_post(item);
	if (rc != 0 || timeout_s == 0)
		goto clean;

	rc = c2_rpc_reply_timedwait(&clink, timeout);
clean:
	c2_clink_del(&clink);
	c2_clink_fini(&clink);

	C2_RETURN(rc);
}
C2_EXPORTED(c2_rpc_client_call);

int c2_rpc_client_stop(struct c2_rpc_client_ctx *cctx)
{
	int rc;

	C2_ENTRY("client_ctx: %p", cctx);
	rc = c2_rpc_session_destroy(&cctx->rcx_session, cctx->rcx_timeout_s);
	if (rc != 0) {
		C2_RETURN(rc);
	}

	rc = c2_rpc_conn_destroy(&cctx->rcx_connection, cctx->rcx_timeout_s);
	if (rc != 0) {
		C2_RETURN(rc);
	}

	c2_net_end_point_put(cctx->rcx_remote_ep);
	c2_rpc_machine_fini(&cctx->rcx_rpc_machine);

	c2_rpc_net_buffer_pool_cleanup(&cctx->rcx_buffer_pool);

	C2_RETURN(rc);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
