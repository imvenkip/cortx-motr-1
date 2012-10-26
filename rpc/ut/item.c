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
 * Original author: Amit Jambure <amit_jambure@xyratex.com>
 * Original creation date: 10/19/2012
 */

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/ut.h"
#include "lib/finject.h"
#include "fop/fop.h"               /* c2_fop_alloc */
#include "rpc/rpclib.h"
#include "net/lnet/lnet.h"         /* c2_net_lnet_xprt */
#include "ut/rpc.h"                /* c2_rpc_client_[init|fini] */
#include "ut/cs_fop_foms.h"        /* cs_ds2_req_fop_fopt */
#include "ut/cs_test_fops_ff.h"    /* cs_ds2_req_fop */
#include "rpc/ut/clnt_srv_ctx.c"   /* sctx, cctx. NOTE: This is .c file */

static int __test(void);

static struct c2_fop *fop_alloc(void)
{
	struct cs_ds2_req_fop *cs_ds2_fop;
	struct c2_fop         *fop;

	fop = c2_fop_alloc(&cs_ds2_req_fop_fopt, NULL);
	C2_UT_ASSERT(fop != NULL);

	cs_ds2_fop = c2_fop_data(fop);
	cs_ds2_fop->csr_value = 0xaaf5;

	return fop;
}

static struct c2_rpc_machine *machine;
static struct c2_rpc_stats    saved;
static struct c2_rpc_stats    stats;
static struct c2_rpc_item    *item;
static struct c2_fop         *fop;

#define IS_INCR_BY_1(p) (saved.rs_ ## p + 1 == stats.rs_ ## p)

static int ts_item_init(void)   /* ts_ for "test suite" */
{
	int rc;

	rc = c2_net_xprt_init(xprt);
	C2_ASSERT(rc == 0);

	rc = c2_net_domain_init(&client_net_dom, xprt);
	C2_ASSERT(rc == 0);

	rc = c2_rpc_server_start(&sctx);
	C2_ASSERT(rc == 0);

	rc = c2_rpc_client_init(&cctx);
	C2_ASSERT(rc == 0);

	machine = cctx.rcx_session.s_conn->c_rpc_machine;

	return rc;
}

static int ts_item_fini(void)
{
	int rc;

	rc = c2_rpc_client_fini(&cctx);
	C2_ASSERT(rc == 0);
	c2_rpc_server_stop(&sctx);
	c2_net_domain_fini(&client_net_dom);
	c2_net_xprt_fini(xprt);
	return rc;
}

static bool chk_state(const struct c2_rpc_item *item,
		      enum c2_rpc_item_state    state)
{
	return item->ri_sm.sm_state == state;
}

static void test_simple_transitions(void)
{
	int rc;

	/* TEST1: Simple request and reply sequence */
	C2_LOG(C2_DEBUG, "TEST:1:START");
	c2_rpc_machine_get_stats(machine, &saved, false /* clear stats? */);
	fop = fop_alloc();
	item = &fop->f_item;
	rc = c2_rpc_client_call(fop, &cctx.rcx_session,
				&cs_ds_req_fop_rpc_item_ops,
				CONNECT_TIMEOUT);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(item->ri_error == 0);
	C2_UT_ASSERT(item->ri_reply != NULL);
	C2_UT_ASSERT(chk_state(item, C2_RPC_ITEM_REPLIED) &&
		     chk_state(item->ri_reply, C2_RPC_ITEM_ACCEPTED));
	c2_rpc_machine_get_stats(machine, &stats, true);
	C2_UT_ASSERT(IS_INCR_BY_1(nr_sent_items) &&
		     IS_INCR_BY_1(nr_rcvd_items));
	C2_LOG(C2_DEBUG, "TEST:1:END");
}

static void test_timeout(void)
{
	int rc;

	/* Test2: Request item times out before reply reaches to sender.
		  Delayed reply is then dropped.
	 */
	C2_LOG(C2_DEBUG, "TEST:2:START");
	fop = fop_alloc();
	item = &fop->f_item;
	c2_rpc_machine_get_stats(machine, &saved, false);
	c2_fi_enable_once("cs_req_fop_fom_tick", "inject_delay");
	rc = c2_rpc_client_call(fop, &cctx.rcx_session,
				&cs_ds_req_fop_rpc_item_ops,
				1 /* timeout in seconds */);
	C2_UT_ASSERT(rc == -ETIMEDOUT);
	C2_UT_ASSERT(item->ri_error == -ETIMEDOUT);
	C2_UT_ASSERT(item->ri_reply == NULL);
	C2_UT_ASSERT(chk_state(item, C2_RPC_ITEM_FAILED));
	c2_nanosleep(c2_time(2, 0), NULL);
	c2_rpc_machine_get_stats(machine, &stats, true);
	C2_UT_ASSERT(IS_INCR_BY_1(nr_dropped_items) &&
		     IS_INCR_BY_1(nr_timedout_items) &&
		     IS_INCR_BY_1(nr_failed_items));
	C2_LOG(C2_DEBUG, "TEST:2:END");
}

static void test_failure_before_sending(void)
{
	int rc;
	int i;
	struct /* anonymous */ {
		const char *func;
		const char *tag;
		int         rc;
	} fp[] = {
		{"c2_bufvec_alloc_aligned", "oom",        -ENOMEM},
		{"c2_net_buffer_register",  "fake_error", -EINVAL},
		{"c2_rpc_packet_encode",    "fake_error", -EFAULT},
		{"c2_net_buffer_add",       "fake_error", -EMSGSIZE},
	};

	/* TEST3: packet_ready() routine failed.
		  The item should move to FAILED state.
	 */
	for (i = 0; i < ARRAY_SIZE(fp); ++i) {
		C2_LOG(C2_DEBUG, "TEST:3.%d:START", i + 1);
		c2_fi_enable_once(fp[i].func, fp[i].tag);
		rc = __test();
		C2_UT_ASSERT(rc == fp[i].rc);
		C2_UT_ASSERT(item->ri_error == fp[i].rc);
		C2_LOG(C2_DEBUG, "TEST:3.%d:END", i + 1);
	}
	/* TEST4: Network layer reported buffer send failure.
		  The item should move to FAILED state.
		  NOTE: Buffer sending is successful, hence reply will be
		  received but reply will be dropped.
	 */
	C2_LOG(C2_DEBUG, "TEST:4:START");
	c2_fi_enable("outgoing_buf_event_handler", "fake_err");
	rc = __test();
	C2_UT_ASSERT(rc == -EINVAL);
	C2_UT_ASSERT(item->ri_error == -EINVAL);
	/* Wait for reply */
	c2_nanosleep(c2_time(0, 10000000), 0); /* sleep 10 milli seconds */
	c2_rpc_machine_get_stats(machine, &stats, false);
	C2_UT_ASSERT(IS_INCR_BY_1(nr_dropped_items));
	c2_fi_disable("outgoing_buf_event_handler", "fake_err");
	C2_LOG(C2_DEBUG, "TEST:4:END");
}

static int __test(void)
{
	int rc;

	/* Check SENDING -> FAILED transition */
	c2_rpc_machine_get_stats(machine, &saved, false);
	fop  = fop_alloc();
	item = &fop->f_item;
	rc = c2_rpc_client_call(fop, &cctx.rcx_session,
				&cs_ds_req_fop_rpc_item_ops,
				CONNECT_TIMEOUT);
	C2_UT_ASSERT(item->ri_reply == NULL);
	C2_UT_ASSERT(chk_state(item, C2_RPC_ITEM_FAILED));
	c2_rpc_machine_get_stats(machine, &stats, false);
	C2_UT_ASSERT(IS_INCR_BY_1(nr_failed_items));
	return rc;
}

/*
static void rply_before_sentcb(void)
{
	@todo Simulate a case where:
		- Request item A is serialised in network buffer NB_A;
		- NB_A is submitted to net layer;
		- A is in SENDING state;
		- NB_A.sent() callback is not yet received;
		- And reply to A is received.
	     In this case reply processing of A should be postponed until
	     NB_A.sent() callback is invoked.

	     Tried to simulate this case, by introducing artificial delay in
	     outgoing_buf_event_handler(). But because there is only one thread
	     from lnet transport that delivers buffer events, it also blocks
	     delivery of net_buf_receieved(A.reply) event.
}
*/

const struct c2_test_suite item_ut = {
	.ts_name = "item-ut",
	.ts_init = ts_item_init,
	.ts_fini = ts_item_fini,
	.ts_tests = {
		{ "simple-transitions",     test_simple_transitions     },
		{ "timeout-transitions",    test_timeout                },
		{ "failure-before-sending", test_failure_before_sending },
		{ NULL, NULL },
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
