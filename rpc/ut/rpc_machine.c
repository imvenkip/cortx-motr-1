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
#include "rpc/rpc2.h"
#include "net/buffer_pool.h"
#include "net/lnet/lnet.h"

struct c2_rpc_machine            machine;
static uint32_t                  max_rpc_msg_size = C2_RPC_DEF_MAX_RPC_MSG_SIZE;
static const char               *ep_addr = "0@lo:12345:34:2";
static struct c2_cob_domain      cdom;
static struct c2_net_domain      ndom;
static struct c2_cob_domain_id   cdom_id = {
	.id = 10000
};
static struct c2_dbenv           dbenv;
static const char               *dbname = "db";
static struct c2_net_buffer_pool buf_pool;
static struct c2_net_xprt        *net_xprt = &c2_net_lnet_xprt;

static uint32_t tm_recv_queue_min_len = C2_NET_TM_RECV_QUEUE_DEF_LEN;

static void cob_domain_init(void)
{
	int rc;

	rc = c2_dbenv_init(&dbenv, dbname, 0);
	C2_ASSERT(rc == 0);

	rc = c2_cob_domain_init(&cdom, &dbenv, &cdom_id);
	C2_ASSERT(rc == 0);
}

static void cob_domain_fini(void)
{
	c2_cob_domain_fini(&cdom);
	c2_dbenv_fini(&dbenv);
}

static int rpc_mc_ut_init(void)
{
	int      rc;
	uint32_t bufs_nr;
	uint32_t tms_nr;

	tms_nr = 1;
	rc = c2_net_domain_init(&ndom, net_xprt);
	C2_ASSERT(rc == 0);

	cob_domain_init();

	bufs_nr = c2_rpc_bufs_nr(tm_recv_queue_min_len, tms_nr);
	rc = c2_rpc_net_buffer_pool_setup(&ndom, &buf_pool, bufs_nr, tms_nr);
	C2_ASSERT(rc == 0);

	return 0;
}

static int rpc_mc_ut_fini(void)
{
	c2_rpc_net_buffer_pool_cleanup(&buf_pool);
	cob_domain_fini();
	c2_net_domain_fini(&ndom);

	return 0;
}

static void rpc_mc_init_fini_test(void)
{
	int rc;

	/*
	 * Test - rpc_machine_init & rpc_machine_fini for success case
	 */

	rc = c2_rpc_machine_init(&machine, &cdom, &ndom, ep_addr, NULL,
			         &buf_pool, C2_BUFFER_ANY_COLOUR,
				 max_rpc_msg_size,
				 tm_recv_queue_min_len);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(machine.rm_reqh == NULL);
	C2_UT_ASSERT(machine.rm_dom == &cdom);
	C2_UT_ASSERT(machine.rm_stopping == false);
	c2_rpc_machine_fini(&machine);
}

static void rpc_mc_init_fail_test(void)
{
	int rc;

	/*
	 * Test - rpc_machine_init for failure cases
	 *	Case 1 - c2_net_tm_init failed, should return -EINVAL
	 *	Case 2 - c2_net_tm_start failed, should return -ENETUNREACH
	 *	Case 3 - root_session_cob_create failed, should return -EINVAL
	 *	Case 4 - c2_root_session_cob_create failed, should ret -EINVAL
	 *		checks for db_tx_abort code path execution
	 */

	c2_fi_enable_once("c2_net_tm_init", "fake_error");
	rc = c2_rpc_machine_init(&machine, &cdom, &ndom, ep_addr, NULL,
				 &buf_pool, C2_BUFFER_ANY_COLOUR,
				 max_rpc_msg_size,
				 tm_recv_queue_min_len);
	C2_UT_ASSERT(rc == -EINVAL);

	c2_fi_enable_once("c2_net_tm_start", "fake_error");
	rc = c2_rpc_machine_init(&machine, &cdom, &ndom, ep_addr, NULL,
				 &buf_pool, C2_BUFFER_ANY_COLOUR,
				 max_rpc_msg_size,
				 tm_recv_queue_min_len);
	C2_UT_ASSERT(rc == -ENETUNREACH);
#ifndef __KERNEL__
	c2_fi_enable_once("root_session_cob_create", "fake_error");
	rc = c2_rpc_machine_init(&machine, &cdom, &ndom, ep_addr, NULL,
				 &buf_pool, C2_BUFFER_ANY_COLOUR,
				 max_rpc_msg_size,
				 tm_recv_queue_min_len);
	C2_UT_ASSERT(rc == -EINVAL);

	c2_fi_enable_once("c2_rpc_root_session_cob_create", "fake_error");
	rc = c2_rpc_machine_init(&machine, &cdom, &ndom, ep_addr, NULL,
				 &buf_pool, C2_BUFFER_ANY_COLOUR,
				 max_rpc_msg_size,
				 tm_recv_queue_min_len);
	C2_UT_ASSERT(rc == -EINVAL);
#endif
}

const struct c2_test_suite rpc_mc_ut = {
	.ts_name = "rpc_mc_ut",
	.ts_init = rpc_mc_ut_init,
	.ts_fini = rpc_mc_ut_fini,
	.ts_tests = {
		{ "rpc_mc_init_fini", rpc_mc_init_fini_test},
		{ "rpc_mc_init_fail", rpc_mc_init_fail_test},
		{ NULL, NULL}
	}
};
C2_EXPORTED(rpc_mc_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
