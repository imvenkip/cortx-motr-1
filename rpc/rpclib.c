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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#ifndef __KERNEL__
#include <errno.h> /* errno */
#include <stdio.h> /* fopen(), fclose() */
#endif

#include "lib/cdefs.h"
#include "lib/types.h"
#include "lib/memory.h"
#include "lib/assert.h"
#include "rpc/rpc.h"
#include "net/net.h"
#include "net/lnet/lnet.h"
#include "fop/fop.h"
#include "rpc/rpclib.h"

#ifndef __KERNEL__
#include "reqh/reqh.h"
#include "reqh/reqh_service.h"
#include "mero/setup.h"
#endif

#ifndef __KERNEL__
int m0_rpc_server_start(struct m0_rpc_server_ctx *sctx)
{
	int  rc;

	M0_ENTRY("server_ctx: %p", sctx);
	M0_PRE(sctx->rsx_argv != NULL && sctx->rsx_argc > 0);

	/* Open error log file */
	sctx->rsx_log_file = fopen(sctx->rsx_log_file_name, "w+");
	if (sctx->rsx_log_file == NULL)
		M0_RETERR(errno, "Open of error log file");

	/*
	 * Start rpc server.
	 * Note: This only starts the services specified in the
	 * struct m0_rpc_service_ctx, (i.e. m0_rpc_server_ctx::rsx_service_types
	 * ) and does not register the given service types. The user of
	 * m0_rpc_server_start() must register the service stypes before.
	 */
	rc = m0_cs_init(&sctx->rsx_mero_ctx, sctx->rsx_xprts,
			sctx->rsx_xprts_nr, sctx->rsx_log_file);
	M0_LOG(M0_DEBUG, "cs_init: rc=%d", rc);
	if (rc != 0)
		goto fclose;

	rc = m0_cs_setup_env(&sctx->rsx_mero_ctx, sctx->rsx_argc,
			     sctx->rsx_argv);
	M0_LOG(M0_DEBUG, "cs_setup_env: rc=%d", rc);
	if (rc == 0)
		M0_RETURN(m0_cs_start(&sctx->rsx_mero_ctx));

	m0_cs_fini(&sctx->rsx_mero_ctx);
fclose:
	fclose(sctx->rsx_log_file);
	M0_RETURN(rc);
}

void m0_rpc_server_stop(struct m0_rpc_server_ctx *sctx)
{
	M0_ENTRY("server_ctx: %p", sctx);

	m0_cs_fini(&sctx->rsx_mero_ctx);
	fclose(sctx->rsx_log_file);

	M0_LEAVE();
	return;
}

M0_INTERNAL struct m0_rpc_machine *
m0_rpc_server_ctx_get_rmachine(struct m0_rpc_server_ctx *sctx)
{
	return m0_mero_to_rmach(&sctx->rsx_mero_ctx);
}
#endif

M0_INTERNAL int m0_rpc_client_connect(struct m0_rpc_conn    *conn,
				      struct m0_rpc_session *session,
				      struct m0_rpc_machine *rpc_mach,
				      const char            *remote_addr,
				      uint64_t               max_rpcs_in_flight,
				      uint32_t               nr_slots,
				      uint32_t               rpc_timeout_sec)
{
	struct m0_net_end_point *ep;
	int                      rc;

	M0_ENTRY();

	rc = m0_net_end_point_create(&ep, &rpc_mach->rm_tm, remote_addr);
	if (rc != 0)
		M0_RETURN(rc);

	rc = m0_rpc_conn_create(conn, ep, rpc_mach, max_rpcs_in_flight,
				m0_time_from_now(rpc_timeout_sec, 0));
	m0_net_end_point_put(ep);
	if (rc != 0)
		M0_RETURN(rc);

	rc = m0_rpc_session_create(session, conn, nr_slots,
				   m0_time_from_now(rpc_timeout_sec, 0));
	if (rc != 0)
		(void)m0_rpc_conn_destroy(conn,
					 m0_time_from_now(rpc_timeout_sec, 0));

	M0_RETURN(rc);
}

int m0_rpc_client_start(struct m0_rpc_client_ctx *cctx)
{
	enum { NR_TM = 1 }; /* Number of TMs. */
	int rc;

	M0_ENTRY("client_ctx: %p", cctx);

	if (cctx->rcx_recv_queue_min_length == 0)
		cctx->rcx_recv_queue_min_length = M0_NET_TM_RECV_QUEUE_DEF_LEN;

	rc = m0_rpc_net_buffer_pool_setup(
		cctx->rcx_net_dom, &cctx->rcx_buffer_pool,
		m0_rpc_bufs_nr(cctx->rcx_recv_queue_min_length, NR_TM),
		NR_TM);
	if (rc != 0)
		goto err;

	rc = m0_rpc_machine_init(&cctx->rcx_rpc_machine, cctx->rcx_cob_dom,
				 cctx->rcx_net_dom, cctx->rcx_local_addr, NULL,
				 &cctx->rcx_buffer_pool, M0_BUFFER_ANY_COLOUR,
				 cctx->rcx_max_rpc_msg_size,
				 cctx->rcx_recv_queue_min_length);
	if (rc != 0)
		goto err;

	rc = m0_rpc_client_connect(&cctx->rcx_connection, &cctx->rcx_session,
				   &cctx->rcx_rpc_machine,
				   cctx->rcx_remote_addr,
				   cctx->rcx_max_rpcs_in_flight,
				   cctx->rcx_nr_slots,
				   cctx->rcx_timeout_s);
	if (rc == 0)
		M0_RETURN(rc);

	m0_rpc_machine_fini(&cctx->rcx_rpc_machine);
err:
	m0_rpc_net_buffer_pool_cleanup(&cctx->rcx_buffer_pool);
	M0_RETURN(rc);
}

int m0_rpc_client_call(struct m0_fop *fop,
		       struct m0_rpc_session *session,
		       const struct m0_rpc_item_ops *ri_ops,
		       m0_time_t deadline)
{
	struct m0_rpc_item *item;
	int                 rc;

	M0_ENTRY("fop: %p, session: %p", fop, session);
	M0_PRE(fop != NULL);
	M0_PRE(session != NULL);

	item                = &fop->f_item;
	item->ri_ops        = ri_ops;
	item->ri_session    = session;
	item->ri_prio       = M0_RPC_ITEM_PRIO_MID;
	item->ri_deadline   = deadline;

	rc = m0_rpc_post(item);
	if (rc == 0)
		rc = m0_rpc_item_wait_for_reply(item, M0_TIME_NEVER);
	M0_RETURN(rc);
}
M0_EXPORTED(m0_rpc_client_call);

int m0_rpc_client_stop(struct m0_rpc_client_ctx *cctx)
{
	int rc0;
	int rc1;

	M0_ENTRY("client_ctx: %p", cctx);

	rc0 = m0_rpc_session_destroy(&cctx->rcx_session,
				     m0_time_from_now(cctx->rcx_timeout_s, 0));
	if (rc0 != 0)
		M0_LOG(M0_ERROR, "Failed to terminate session %d", rc0);

	rc1 = m0_rpc_conn_destroy(&cctx->rcx_connection,
				  m0_time_from_now(cctx->rcx_timeout_s, 0));
	if (rc1 != 0)
		M0_LOG(M0_ERROR, "Failed to terminate connection %d", rc1);

	m0_rpc_machine_fini(&cctx->rcx_rpc_machine);
	m0_rpc_net_buffer_pool_cleanup(&cctx->rcx_buffer_pool);

	M0_RETURN(rc0 ?: rc1);
}

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
