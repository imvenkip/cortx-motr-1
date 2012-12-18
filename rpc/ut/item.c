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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/trace.h"
#include "lib/ut.h"
#include "lib/finject.h"
#include "lib/misc.h"              /* M0_BITS */
#include "lib/semaphore.h"
#include "fop/fop.h"               /* m0_fop_alloc */
#include "rpc/rpclib.h"
#include "net/lnet/lnet.h"         /* m0_net_lnet_xprt */
#include "ut/rpc.h"                /* m0_rpc_client_[init|fini] */
#include "ut/cs_fop_foms.h"        /* cs_ds2_req_fop_fopt */
#include "ut/cs_fop_foms_xc.h"     /* cs_ds2_req_fop */
#include "rpc/ut/clnt_srv_ctx.c"   /* sctx, cctx. NOTE: This is .c file */
#include "rpc/ut/rpc_test_fops.h"

static int __test(void);

static struct m0_fop *fop_alloc(void)
{
	struct cs_ds2_req_fop *cs_ds2_fop;
	struct m0_fop         *fop;

	fop = m0_fop_alloc(&cs_ds2_req_fop_fopt, NULL);
	M0_UT_ASSERT(fop != NULL);

	cs_ds2_fop = m0_fop_data(fop);
	cs_ds2_fop->csr_value = 0xaaf5;

	return fop;
}

static struct m0_rpc_machine *machine;
static struct m0_rpc_stats    saved;
static struct m0_rpc_stats    stats;
static struct m0_rpc_item    *item;
static struct m0_fop         *fop;

#define IS_INCR_BY_1(p) (saved.rs_ ## p + 1 == stats.rs_ ## p)

static int ts_item_init(void)   /* ts_ for "test suite" */
{
	int rc;

	m0_rpc_test_fops_init();

	rc = m0_net_xprt_init(xprt);
	M0_ASSERT(rc == 0);

	rc = m0_net_domain_init(&client_net_dom, xprt);
	M0_ASSERT(rc == 0);

	rc = m0_rpc_server_start(&sctx);
	M0_ASSERT(rc == 0);

	rc = m0_rpc_client_init(&cctx);
	M0_ASSERT(rc == 0);

	machine = cctx.rcx_session.s_conn->c_rpc_machine;

	return rc;
}

static int ts_item_fini(void)
{
	int rc;

	rc = m0_rpc_client_fini(&cctx);
	M0_ASSERT(rc == 0);
	m0_rpc_server_stop(&sctx);
	m0_net_domain_fini(&client_net_dom);
	m0_net_xprt_fini(xprt);
	m0_rpc_test_fops_fini();
	return rc;
}

static bool chk_state(const struct m0_rpc_item *item,
		      enum m0_rpc_item_state    state)
{
	return item->ri_sm.sm_state == state;
}

static void test_simple_transitions(void)
{
	int rc;

	/* TEST1: Simple request and reply sequence */
	M0_LOG(M0_DEBUG, "TEST:1:START");
	m0_rpc_machine_get_stats(machine, &saved, false /* clear stats? */);
	fop = fop_alloc();
	item = &fop->f_item;
	rc = m0_rpc_client_call(fop, &cctx.rcx_session,
				&cs_ds_req_fop_rpc_item_ops,
				0 /* deadline */,
				CONNECT_TIMEOUT);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(item->ri_error == 0);
	M0_UT_ASSERT(item->ri_reply != NULL);
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_REPLIED) &&
		     chk_state(item->ri_reply, M0_RPC_ITEM_ACCEPTED));
	m0_rpc_machine_get_stats(machine, &stats, true);
	M0_UT_ASSERT(IS_INCR_BY_1(nr_sent_items) &&
		     IS_INCR_BY_1(nr_rcvd_items));
	m0_fop_put(fop);
	M0_LOG(M0_DEBUG, "TEST:1:END");
}

static void test_timeout(void)
{
	int rc;

	/* Test2: Request item times out before reply reaches to sender.
		  Delayed reply is then dropped.
	 */
	M0_LOG(M0_DEBUG, "TEST:2:START");
	fop = fop_alloc();
	item = &fop->f_item;
	m0_rpc_machine_get_stats(machine, &saved, false);
	m0_fi_enable_once("cs_req_fop_fom_tick", "inject_delay");
	rc = m0_rpc_client_call(fop, &cctx.rcx_session,
				&cs_ds_req_fop_rpc_item_ops,
				0 /* deadline */,
				1 /* timeout in seconds */);
	M0_UT_ASSERT(rc == -ETIMEDOUT);
	M0_UT_ASSERT(item->ri_error == -ETIMEDOUT);
	M0_UT_ASSERT(item->ri_reply == NULL);
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_FAILED));
	m0_nanosleep(m0_time(2, 0), NULL);
	m0_rpc_machine_get_stats(machine, &stats, true);
	M0_UT_ASSERT(IS_INCR_BY_1(nr_dropped_items) &&
		     IS_INCR_BY_1(nr_timedout_items) &&
		     IS_INCR_BY_1(nr_failed_items));
	m0_fop_put(fop);
	M0_LOG(M0_DEBUG, "TEST:2:END");
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
		{"m0_bufvec_alloc_aligned", "oom",        -ENOMEM},
		{"m0_net_buffer_register",  "fake_error", -EINVAL},
		{"m0_rpc_packet_encode",    "fake_error", -EFAULT},
		{"m0_net_buffer_add",       "fake_error", -EMSGSIZE},
	};

	/* TEST3: packet_ready() routine failed.
		  The item should move to FAILED state.
	 */
	for (i = 0; i < ARRAY_SIZE(fp); ++i) {
		M0_LOG(M0_DEBUG, "TEST:3.%d:START", i + 1);
		m0_fi_enable_once(fp[i].func, fp[i].tag);
		rc = __test();
		M0_UT_ASSERT(rc == fp[i].rc);
		M0_UT_ASSERT(item->ri_error == fp[i].rc);
		M0_LOG(M0_DEBUG, "TEST:3.%d:END", i + 1);
	}
	/* TEST4: Network layer reported buffer send failure.
		  The item should move to FAILED state.
		  NOTE: Buffer sending is successful, hence reply will be
		  received but reply will be dropped.
	 */
	M0_LOG(M0_DEBUG, "TEST:4:START");
	m0_fi_enable("outgoing_buf_event_handler", "fake_err");
	rc = __test();
	M0_UT_ASSERT(rc == -EINVAL);
	M0_UT_ASSERT(item->ri_error == -EINVAL);
	/* Wait for reply */
	m0_nanosleep(m0_time(0, 10000000), 0); /* sleep 10 milli seconds */
	m0_rpc_machine_get_stats(machine, &stats, false);
	M0_UT_ASSERT(IS_INCR_BY_1(nr_dropped_items));
	m0_fi_disable("outgoing_buf_event_handler", "fake_err");
	M0_LOG(M0_DEBUG, "TEST:4:END");
}

static int __test(void)
{
	int rc;

	/* Check SENDING -> FAILED transition */
	m0_rpc_machine_get_stats(machine, &saved, false);
	fop  = fop_alloc();
	item = &fop->f_item;
	rc = m0_rpc_client_call(fop, &cctx.rcx_session,
				&cs_ds_req_fop_rpc_item_ops,
				0 /* deadline */,
				CONNECT_TIMEOUT);
	M0_UT_ASSERT(item->ri_reply == NULL);
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_FAILED));
	m0_rpc_machine_get_stats(machine, &stats, false);
	M0_UT_ASSERT(IS_INCR_BY_1(nr_failed_items));
	m0_fop_put(fop);
	return rc;
}

static bool arrow_sent_cb_called = false;
static void arrow_sent_cb(struct m0_rpc_item *item)
{
	arrow_sent_cb_called = true;
}
static struct m0_rpc_item_ops arrow_item_ops = {
	.rio_sent = arrow_sent_cb,
};

static void test_oneway_item(void)
{
	struct m0_rpc_item *item;
	struct m0_fop      *fop;
	bool                ok;
	int                 rc;

	fop = m0_fop_alloc(&m0_rpc_arrow_fopt, NULL);
	M0_UT_ASSERT(fop != NULL);

	item              = &fop->f_item;
	item->ri_prio     = M0_RPC_ITEM_PRIO_MID;
	item->ri_deadline = 0;
	item->ri_ops      = &arrow_item_ops;
	M0_UT_ASSERT(!arrow_sent_cb_called);
	rc = m0_rpc_oneway_item_post(&cctx.rcx_connection, item);
	M0_UT_ASSERT(rc == 0);

	rc = m0_rpc_item_timedwait(&fop->f_item,
				   M0_BITS(M0_RPC_ITEM_SENT,
					   M0_RPC_ITEM_FAILED),
				   M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_ASSERT(item->ri_sm.sm_state == M0_RPC_ITEM_SENT);
	M0_UT_ASSERT(arrow_sent_cb_called);

	ok = m0_semaphore_timeddown(&arrow_hit, m0_time_from_now(5, 0));
	M0_UT_ASSERT(ok);

	ok = m0_semaphore_timeddown(&arrow_destroyed, m0_time_from_now(5, 0));
	M0_UT_ASSERT(ok);

	m0_fop_put(fop);
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

const struct m0_test_suite item_ut = {
	.ts_name = "item-ut",
	.ts_init = ts_item_init,
	.ts_fini = ts_item_fini,
	.ts_tests = {
		{ "simple-transitions",     test_simple_transitions     },
		{ "timeout-transitions",    test_timeout                },
		{ "failure-before-sending", test_failure_before_sending },
		{ "oneway-item",            test_oneway_item            },
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
