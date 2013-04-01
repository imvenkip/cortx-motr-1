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
 * Original author: Rohan Puri <rohan_puri@xyratex.com>
 * Original creation date: 04-Oct-2012
 */

#include "ut/ut.h"
#include "lib/finject.h"
#include "rpc/rpc.h"
#include "rpc/rpc_internal.h"
#include "net/net.h"
#include "db/db.h"
#include "cob/cob.h"
#include "rpc/rpc.h"
#include "net/buffer_pool.h"
#include "net/lnet/lnet.h"
#include "rpc/ut/clnt_srv_ctx.c"   /* sctx, cctx. NOTE: This is .c file */

static struct m0_rpc_machine     machine;
static uint32_t                  max_rpc_msg_size = M0_RPC_DEF_MAX_RPC_MSG_SIZE;
static const char               *ep_addr = "0@lo:12345:34:2";
static struct m0_cob_domain      cdom;
static struct m0_cob_domain_id   cdom_id = { .id = 10000 };
static struct m0_dbenv           dbenv;
static const char               *dbname = "db";
static struct m0_net_buffer_pool buf_pool;
static uint32_t tm_recv_queue_min_len = M0_NET_TM_RECV_QUEUE_DEF_LEN;

static void cob_domain_init(void)
{
	int rc;

	rc = m0_dbenv_init(&dbenv, dbname, 0);
	M0_ASSERT(rc == 0);

	rc = m0_cob_domain_init(&cdom, &dbenv, &cdom_id);
	M0_ASSERT(rc == 0);
}

static void cob_domain_fini(void)
{
	m0_cob_domain_fini(&cdom);
	m0_dbenv_fini(&dbenv);
}

static int rpc_mc_ut_init(void)
{
	int      rc;
	uint32_t bufs_nr;
	uint32_t tms_nr;

	rc = m0_net_xprt_init(xprt);
	M0_ASSERT(rc == 0);
	rc = m0_net_domain_init(&client_net_dom, xprt, &m0_addb_proc_ctx);
	M0_ASSERT(rc == 0);
	cob_domain_init();

	tms_nr  = 1;
	bufs_nr = m0_rpc_bufs_nr(tm_recv_queue_min_len, tms_nr);
	rc = m0_rpc_net_buffer_pool_setup(&client_net_dom, &buf_pool, bufs_nr,
					  tms_nr);
	M0_ASSERT(rc == 0);

	return 0;
}

static int rpc_mc_ut_fini(void)
{
	m0_rpc_net_buffer_pool_cleanup(&buf_pool);
	cob_domain_fini();
	m0_net_domain_fini(&client_net_dom);
	m0_net_xprt_fini(xprt);

	return 0;
}

static void rpc_mc_init_fini_test(void)
{
	int rc;

	/*
	 * Test - rpc_machine_init & rpc_machine_fini for success case
	 */

	rc = m0_rpc_machine_init(&machine, &cdom, &client_net_dom, ep_addr,
				 NULL, &buf_pool, M0_BUFFER_ANY_COLOUR,
				 max_rpc_msg_size, tm_recv_queue_min_len);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(machine.rm_reqh == NULL);
	M0_UT_ASSERT(machine.rm_dom == &cdom);
	M0_UT_ASSERT(machine.rm_stopping == false);
	m0_rpc_machine_fini(&machine);
}

static void rpc_mc_init_fail_test(void)
{
	int rc;

	/*
	 * Test - rpc_machine_init for failure cases
	 *	Case 1 - m0_net_tm_init failed, should return -EINVAL
	 *	Case 2 - m0_net_tm_start failed, should return -ENETUNREACH
	 *	Case 3 - root_session_cob_create failed, should return -EINVAL
	 *	Case 4 - m0_root_session_cob_create failed, should ret -EINVAL
	 *		checks for db_tx_abort code path execution
	 */

	m0_fi_enable_once("m0_net_tm_init", "fake_error");
	rc = m0_rpc_machine_init(&machine, &cdom, &client_net_dom, ep_addr,
				 NULL, &buf_pool, M0_BUFFER_ANY_COLOUR,
				 max_rpc_msg_size, tm_recv_queue_min_len);
	M0_UT_ASSERT(rc == -EINVAL);

	m0_fi_enable_once("m0_net_tm_start", "fake_error");
	rc = m0_rpc_machine_init(&machine, &cdom, &client_net_dom, ep_addr,
				 NULL, &buf_pool, M0_BUFFER_ANY_COLOUR,
				 max_rpc_msg_size, tm_recv_queue_min_len);
	M0_UT_ASSERT(rc == -ENETUNREACH);
	/**
	  Root session cob as well as other mkfs related structres are now
	  created on behalf of serivice startup if -p option is specified.
	 */
/*#ifndef __KERNEL__
	m0_fi_enable_once("root_session_cob_create", "fake_error");
	rc = m0_rpc_machine_init(&machine, &cdom, &client_net_dom, ep_addr,
				 NULL, &buf_pool, M0_BUFFER_ANY_COLOUR,
				 max_rpc_msg_size, tm_recv_queue_min_len);
	M0_UT_ASSERT(rc == -EINVAL);

	m0_fi_enable_once("m0_rpc_root_session_cob_create", "fake_error");
	rc = m0_rpc_machine_init(&machine, &cdom, &client_net_dom, ep_addr,
				 NULL, &buf_pool, M0_BUFFER_ANY_COLOUR,
				 max_rpc_msg_size, tm_recv_queue_min_len);
	M0_UT_ASSERT(rc == -EINVAL);
#endif*/
}

#ifndef __KERNEL__

static bool conn_added_called;
static bool session_added_called;
static bool mach_terminated_called;

static void conn_added(struct m0_rpc_machine_watch *watch,
		       struct m0_rpc_conn *conn)
{
	M0_UT_ASSERT(conn->c_sm.sm_state == M0_RPC_CONN_INITIALISED);
	M0_UT_ASSERT(rpc_conn_tlink_is_in(conn));
	M0_UT_ASSERT(m0_rpc_machine_is_locked(watch->mw_mach));
	conn_added_called = true;
}

static void session_added(struct m0_rpc_machine_watch *watch,
			  struct m0_rpc_session *session)
{
	M0_UT_ASSERT(session->s_sm.sm_state == M0_RPC_SESSION_INITIALISED);
	M0_UT_ASSERT(rpc_session_tlink_is_in(session));
	M0_UT_ASSERT(m0_rpc_machine_is_locked(watch->mw_mach));
	session_added_called = true;
}

static void mach_terminated(struct m0_rpc_machine_watch *watch)
{
	M0_UT_ASSERT(!rmach_watch_tlink_is_in(watch));
	mach_terminated_called = true;
}

static void rpc_machine_watch_test(void)
{
	struct m0_rpc_machine_watch  watch;
	struct m0_rpc_machine       *rmach;
	int                          rc;

	rc = m0_rpc_server_start(&sctx);
	M0_UT_ASSERT(rc == 0);

	rmach = m0_rpc_server_ctx_get_rmachine(&sctx);
	M0_UT_ASSERT(rmach != NULL);

	watch.mw_mach          = rmach;
	watch.mw_conn_added    = conn_added;
	watch.mw_session_added = session_added;

	m0_rpc_machine_watch_attach(&watch);

	rc = m0_rpc_client_start(&cctx);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(conn_added_called && session_added_called);

	m0_rpc_machine_watch_detach(&watch);
	/* It is safe to call detach if watch is already detached */
	m0_rpc_machine_watch_detach(&watch);

	/* If rpc machine is being terminated, while still having attached
	   watchers, then they are detached and mw_mach_terminated() callback
	   is called.
	 */
	watch.mw_mach_terminated = mach_terminated;
	m0_rpc_machine_watch_attach(&watch);
	rc = m0_rpc_client_stop(&cctx);
	M0_UT_ASSERT(rc == 0);
	m0_rpc_server_stop(&sctx);
	M0_UT_ASSERT(mach_terminated_called);
}
#endif /* __KERNEL__ */

const struct m0_test_suite rpc_mc_ut = {
	.ts_name = "rpc-machine-ut",
	.ts_init = rpc_mc_ut_init,
	.ts_fini = rpc_mc_ut_fini,
	.ts_tests = {
		{ "rpc_mc_init_fini", rpc_mc_init_fini_test },
		{ "rpc_mc_init_fail", rpc_mc_init_fail_test },
#ifndef __KERNEL__
		{ "rpc_mc_watch",     rpc_machine_watch_test},
#endif
		{ NULL, NULL}
	}
};
M0_EXPORTED(rpc_mc_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
