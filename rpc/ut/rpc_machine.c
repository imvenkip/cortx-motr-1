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
 * Original author: Rohan Puri<rohan_puri@xyratex.com>
 * Original creation date: 10/04/2012
 */

#include "lib/ut.h"
#include "lib/finject.h"
#include "rpc/rpc_machine.h"
#include "net/net.h"
#include "db/db.h"
#include "cob/cob.h"
#include "rpc/rpc.h"
#include "net/buffer_pool.h"
#include "net/lnet/lnet.h"

struct m0_rpc_machine            machine;
static uint32_t                  max_rpc_msg_size = M0_RPC_DEF_MAX_RPC_MSG_SIZE;
static const char               *ep_addr = "0@lo:12345:34:2";
static struct m0_cob_domain      cdom;
static struct m0_net_domain      ndom;
static struct m0_cob_domain_id   cdom_id = {
	.id = 10000
};
static struct m0_dbenv           dbenv;
static const char               *dbname = "db";
static struct m0_net_buffer_pool buf_pool;
static struct m0_net_xprt        *net_xprt = &m0_net_lnet_xprt;

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

	tms_nr = 1;
	rc = m0_net_domain_init(&ndom, net_xprt);
	M0_ASSERT(rc == 0);

	cob_domain_init();

	bufs_nr = m0_rpc_bufs_nr(tm_recv_queue_min_len, tms_nr);
	rc = m0_rpc_net_buffer_pool_setup(&ndom, &buf_pool, bufs_nr, tms_nr);
	M0_ASSERT(rc == 0);

	return 0;
}

static int rpc_mc_ut_fini(void)
{
	m0_rpc_net_buffer_pool_cleanup(&buf_pool);
	cob_domain_fini();
	m0_net_domain_fini(&ndom);

	return 0;
}

static void rpc_mc_init_fini_test(void)
{
	int rc;

	/*
	 * Test - rpc_machine_init & rpc_machine_fini for success case
	 */

	rc = m0_rpc_machine_init(&machine, &cdom, &ndom, ep_addr, NULL,
			         &buf_pool, M0_BUFFER_ANY_COLOUR,
				 max_rpc_msg_size,
				 tm_recv_queue_min_len);
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
	rc = m0_rpc_machine_init(&machine, &cdom, &ndom, ep_addr, NULL,
				 &buf_pool, M0_BUFFER_ANY_COLOUR,
				 max_rpc_msg_size,
				 tm_recv_queue_min_len);
	M0_UT_ASSERT(rc == -EINVAL);

	m0_fi_enable_once("m0_net_tm_start", "fake_error");
	rc = m0_rpc_machine_init(&machine, &cdom, &ndom, ep_addr, NULL,
				 &buf_pool, M0_BUFFER_ANY_COLOUR,
				 max_rpc_msg_size,
				 tm_recv_queue_min_len);
	M0_UT_ASSERT(rc == -ENETUNREACH);
	/**
	  Root session cob as well as other mkfs related structres are now
	  created on behalf of serivice startup if -p option is specified.
	 */
/*#ifndef __KERNEL__
	m0_fi_enable_once("root_session_cob_create", "fake_error");
	rc = m0_rpc_machine_init(&machine, &cdom, &ndom, ep_addr, NULL,
				 &buf_pool, M0_BUFFER_ANY_COLOUR,
				 max_rpc_msg_size,
				 tm_recv_queue_min_len);
	M0_UT_ASSERT(rc == -EINVAL);

	m0_fi_enable_once("m0_rpc_root_session_cob_create", "fake_error");
	rc = m0_rpc_machine_init(&machine, &cdom, &ndom, ep_addr, NULL,
				 &buf_pool, M0_BUFFER_ANY_COLOUR,
				 max_rpc_msg_size,
				 tm_recv_queue_min_len);
	M0_UT_ASSERT(rc == -EINVAL);
#endif*/
}

const struct m0_test_suite rpc_mc_ut = {
	.ts_name = "rpc_mc_ut",
	.ts_init = rpc_mc_ut_init,
	.ts_fini = rpc_mc_ut_fini,
	.ts_tests = {
		{ "rpc_mc_init_fini", rpc_mc_init_fini_test},
		{ "rpc_mc_init_fail", rpc_mc_init_fail_test},
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
