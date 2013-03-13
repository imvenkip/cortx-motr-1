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
#include "lib/memory.h"
#include "rpc/rpclib.h"
#include "rpc/ut/clnt_srv_ctx.c"   /* sctx, cctx. NOTE: This is .c file */
#include "rpc/ut/rpc_test_fops.h"
#include "rpc/rpc_internal.h"

static int __test(void);
static void __test_timeout(m0_time_t deadline,
			   m0_time_t timeout);
static void __test_resend(struct m0_fop *fop);
static void __test_timer_start_failure(void);

static struct m0_rpc_machine *machine;
static struct m0_rpc_session *session;
static struct m0_rpc_stats    saved;
static struct m0_rpc_stats    stats;
static struct m0_rpc_item    *item;
static struct m0_fop         *fop;

#define IS_INCR_BY_1(p) (saved.rs_ ## p + 1 == stats.rs_ ## p)

static int ts_item_init(void)   /* ts_ for "test suite" */
{
	m0_rpc_test_fops_init();
	start_rpc_client_and_server();
	session = &cctx.rcx_session;
	machine = cctx.rcx_session.s_conn->c_rpc_machine;

	return 0;
}

static int ts_item_fini(void)
{
	stop_rpc_client_and_server();
	m0_rpc_test_fops_fini();
	return 0;
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
	rc = m0_rpc_client_call(fop, session,
				&cs_ds_req_fop_rpc_item_ops,
				0 /* deadline */);
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
	enum { MILLISEC = 1000 * 1000 };

	/* Test2.1: Request item times out before reply reaches to sender.
		    Delayed reply is then dropped.
	 */
	M0_LOG(M0_DEBUG, "TEST:2.1:START");
	fop = fop_alloc();
	item = &fop->f_item;
	item->ri_nr_sent_max = 1;
	m0_rpc_machine_get_stats(machine, &saved, false);
	m0_fi_enable_once("cs_req_fop_fom_tick", "inject_delay");
	rc = m0_rpc_client_call(fop, session,
				&cs_ds_req_fop_rpc_item_ops,
				0 /* deadline */);
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
	M0_LOG(M0_DEBUG, "TEST:2.1:END");

	/* Test [ENQUEUED] ---timeout----> [FAILED] */
	M0_LOG(M0_DEBUG, "TEST:2.2:START");
	__test_timeout(m0_time_from_now(1, 0),
		       m0_time(0, 100 * MILLISEC));
	M0_LOG(M0_DEBUG, "TEST:2.2:END");

	/* Test [URGENT] ---timeout----> [FAILED] */
	m0_fi_enable("frm_balance", "do_nothing");
	M0_LOG(M0_DEBUG, "TEST:2.3:START");
	__test_timeout(m0_time_from_now(-1, 0),
		       m0_time(0, 100 * MILLISEC));
	m0_fi_disable("frm_balance", "do_nothing");
	M0_LOG(M0_DEBUG, "TEST:2.3:END");

	/* Test: [SENDING] ---timeout----> [FAILED] */

	M0_LOG(M0_DEBUG, "TEST:2.4:START");
	/* Delay "sent" callback for 300 msec. */
	m0_fi_enable("buf_send_cb", "delay_callback");
	/* ASSUMPTION: Sender will not get "new item received" event until
		       the thread that has called buf_send_cb()
		       comes out of sleep and returns to net layer.
	 */
	__test_timeout(m0_time_from_now(-1, 0),
		       m0_time(0, 100 * MILLISEC));
	/* wait until reply is processed */
	m0_nanosleep(m0_time(0, 500 * MILLISEC), NULL);
	M0_LOG(M0_DEBUG, "TEST:2.4:END");
	m0_fi_disable("buf_send_cb", "delay_callback");
}

static void __test_timeout(m0_time_t deadline,
			   m0_time_t timeout)
{
	int rc;

	fop = fop_alloc();
	item = &fop->f_item;
	m0_rpc_machine_get_stats(machine, &saved, false);
	item->ri_nr_sent_max = 1;
	item->ri_resend_interval = timeout;
	rc = m0_rpc_client_call(fop, session, NULL, deadline);
	M0_UT_ASSERT(item->ri_error == -ETIMEDOUT);
	M0_UT_ASSERT(item->ri_reply == NULL);
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_FAILED));
	m0_rpc_machine_get_stats(machine, &stats, true);
	M0_UT_ASSERT(IS_INCR_BY_1(nr_timedout_items) &&
		     IS_INCR_BY_1(nr_failed_items));
	m0_rpc_machine_lock(item->ri_rmachine);
	m0_fop_put(fop);
	m0_rpc_machine_unlock(item->ri_rmachine);
}

static bool only_second_time(void *data)
{
	int *ip = data;

	++*ip;
	return *ip == 2;
}

static void test_resend(void)
{
	struct m0_rpc_item *item;
	int                 rc;
	int                 cnt = 0;

	/* Test: Request is dropped. */
	M0_LOG(M0_DEBUG, "TEST:3.1:START");
	m0_fi_enable_once("item_received", "drop_item");
	__test_resend(NULL);
	M0_LOG(M0_DEBUG, "TEST:3.1:END");

	/* Test: Reply is dropped. */
	M0_LOG(M0_DEBUG, "TEST:3.2:START");
	m0_fi_enable_func("item_received", "drop_item",
			  only_second_time, &cnt);
	__test_resend(NULL);
	m0_fi_disable("item_received", "drop_item");
	M0_LOG(M0_DEBUG, "TEST:3.2:END");

	/* Test: ENQUEUED -> REPLIED transition.

	   Reply is delayed. On sender, request item is enqueued for
	   resending. But before formation could send the item,
	   reply is received.

	   nanosleep()s are inserted at specific points to create
	   this scenario:
	   - request is sent;
	   - the request is moved to WAITING_FOR_REPLY state;
	   - the item's timer is set to trigger after 1 sec;
	   - fault_point<"m0_rpc_reply_post", "delay_reply"> delays
	     sending reply by 1.2 sec;
	   - resend timer of request item triggers and calls
	     m0_rpc_item_send();
	   - fault_point<"m0_rpc_item_send", "advance_delay"> moves
	     deadline of request item 500ms in future, ergo the item
	     moves to ENQUEUED state when handed over to formation;
	   - receiver comes out of 1.2 sec sleep and sends reply.
	 */
	M0_LOG(M0_DEBUG, "TEST:3.3:START");
	cnt = 0;
	m0_fi_enable_func("m0_rpc_item_send", "advance_deadline",
			  only_second_time, &cnt);
	m0_fi_enable_once("m0_rpc_reply_post", "delay_reply");
	fop = fop_alloc();
	item = &fop->f_item;
	__test_resend(fop);
	m0_fi_disable("m0_rpc_item_send", "advance_deadline");
	M0_LOG(M0_DEBUG, "TEST:3.3:END");

	M0_LOG(M0_DEBUG, "TEST:3.4:START");
	/* CONTINUES TO USE fop/item FROM PREVIOUS TEST-CASE. */
	/* RPC call is complete i.e. item is in REPLIED state.
	   Explicitly resend the completed request; the way the item
	   will be resent during recovery.
	 */
	m0_rpc_machine_lock(item->ri_rmachine);
	M0_UT_ASSERT(item->ri_nr_sent == 2);
	item->ri_resend_interval = M0_TIME_NEVER;
	m0_rpc_item_send(item);
	m0_rpc_machine_unlock(item->ri_rmachine);
	rc = m0_rpc_item_wait_for_reply(item, m0_time_from_now(2, 0));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(item->ri_error == 0);
	M0_UT_ASSERT(item->ri_nr_sent == 3);
	M0_UT_ASSERT(item->ri_reply != NULL);
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_REPLIED));
	m0_fop_put(fop);
	M0_LOG(M0_DEBUG, "TEST:3.4:END");

	/* Test: INITIALISED -> FAILED transition when m0_rpc_post()
		 fails to start item timer.
	 */
	M0_LOG(M0_DEBUG, "TEST:3.5.1:START");
	m0_fi_enable_once("m0_rpc_item_start_timer", "failed");
	__test_timer_start_failure();
	M0_LOG(M0_DEBUG, "TEST:3.5.1:END");

	/* Test: Move item from WAITING_FOR_REOLY to FAILED state if
		 item_sent() fails to start resend timer.
	 */
	M0_LOG(M0_DEBUG, "TEST:3.5.2:START");
	cnt = 0;
	m0_fi_enable_func("m0_rpc_item_start_timer", "failed",
			  only_second_time, &cnt);
	m0_fi_enable("item_received", "drop_item");
	__test_timer_start_failure();
	m0_fi_disable("item_received", "drop_item");
	m0_fi_disable("m0_rpc_item_start_timer", "failed");
	M0_LOG(M0_DEBUG, "TEST:3.5.2:END");
}

static void __test_resend(struct m0_fop *fop)
{
	bool fop_put_flag = false;
	int rc;

	if (fop == NULL) {
		fop = fop_alloc();
		fop_put_flag = true;
	}
	item = &fop->f_item;
	rc = m0_rpc_client_call(fop, session, NULL, 0 /* urgent */);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(item->ri_error == 0);
	M0_UT_ASSERT(item->ri_nr_sent >= 1);
	M0_UT_ASSERT(item->ri_reply != NULL);
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_REPLIED));
	if (fop_put_flag)
		m0_fop_put(fop);
}

static void __test_timer_start_failure(void)
{
	int rc;

	fop = fop_alloc();
	item = &fop->f_item;
	rc = m0_rpc_client_call(fop, session, NULL, 0 /* urgent */);
	M0_UT_ASSERT(rc == -EINVAL);
	M0_UT_ASSERT(item->ri_error == -EINVAL);
	M0_UT_ASSERT(item->ri_reply == NULL);
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_FAILED));
	/* sleep until request reaches at server and is dropped */
	m0_nanosleep(m0_time(0, 5 * 1000 * 1000), NULL);
	m0_rpc_machine_lock(item->ri_rmachine);
	m0_fop_put(fop);
	m0_rpc_machine_unlock(item->ri_rmachine);
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
		  NOTE: Buffer sending is successful, hence we need
		  to explicitly drop the item on receiver using
		  fault_point<"item_received", "drop_item">.
	 */
	M0_LOG(M0_DEBUG, "TEST:4:START");
	m0_fi_enable("buf_send_cb", "fake_err");
	m0_fi_enable("item_received", "drop_item");
	rc = __test();
	M0_UT_ASSERT(rc == -EINVAL);
	M0_UT_ASSERT(item->ri_error == -EINVAL);
	m0_rpc_machine_get_stats(machine, &stats, false);
	m0_fi_disable("buf_send_cb", "fake_err");
	m0_fi_disable("item_received", "drop_item");
	M0_LOG(M0_DEBUG, "TEST:4:END");
}

static int __test(void)
{
	int rc;

	/* Check SENDING -> FAILED transition */
	m0_rpc_machine_get_stats(machine, &saved, false);
	fop  = fop_alloc();
	item = &fop->f_item;
	rc = m0_rpc_client_call(fop, session,
				&cs_ds_req_fop_rpc_item_ops,
				0 /* deadline */);
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
static const struct m0_rpc_item_ops arrow_item_ops = {
	.rio_sent = arrow_sent_cb,
};

static bool fop_release_called;
static void fop_release(struct m0_ref *ref)
{
	fop_release_called = true;
	m0_fop_release(ref);
}

static void test_oneway_item(void)
{
	struct m0_rpc_item *item;
	struct m0_fop      *fop;
	bool                ok;
	int                 rc;

	/* Test 1: Confirm one-way items reach receiver */
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

	m0_rpc_machine_lock(item->ri_rmachine);
	m0_fop_put(fop);
	m0_rpc_machine_unlock(item->ri_rmachine);

	/* Test 2: Remaining queued oneway items are dropped during
		   m0_rpc_frm_fini()
	 */
	M0_ALLOC_PTR(fop);
	M0_UT_ASSERT(fop != NULL);
	m0_fop_init(fop, &m0_rpc_arrow_fopt, NULL, fop_release);
	rc = m0_fop_data_alloc(fop);
	M0_UT_ASSERT(rc == 0);
	item              = &fop->f_item;
	item->ri_prio     = M0_RPC_ITEM_PRIO_MID;
	item->ri_deadline = m0_time_from_now(10, 0);
	item->ri_ops      = &arrow_item_ops;
	arrow_sent_cb_called = fop_release_called = false;
	rc = m0_rpc_oneway_item_post(&cctx.rcx_connection, item);
	M0_UT_ASSERT(rc == 0);
	m0_fop_put(fop);
	M0_UT_ASSERT(!arrow_sent_cb_called);
	M0_UT_ASSERT(!fop_release_called);
	m0_fi_enable("frm_fill_packet", "skip_oneway_items");
	/* stop client server to trigger m0_rpc_frm_fini() */
	stop_rpc_client_and_server();
	M0_UT_ASSERT(arrow_sent_cb_called); /* callback with FAILED items */
	M0_UT_ASSERT(fop_release_called);
	start_rpc_client_and_server();
	m0_fi_disable("frm_fill_packet", "skip_oneway_items");
}

static struct m0_semaphore done_sem;
enum {NR_ITEMS = 100};

static void bound_item_replied_cb(struct m0_rpc_item *item)
{
	struct cs_ds2_rep_fop *reply;
	static int count = 0;

	M0_UT_ASSERT(item->ri_error == 0 && item->ri_reply != NULL);
	reply = m0_fop_data(m0_rpc_item_to_fop(item->ri_reply));
	if (reply->csr_rc != count)
		m0_console_printf("ERROR: expected %d received %d\n", count,
				  reply->csr_rc);
	M0_UT_ASSERT(reply->csr_rc == count);
	++count;
	if (count == NR_ITEMS)
		m0_semaphore_up(&done_sem);
}

static const struct m0_rpc_item_ops bound_item_ops = {
	.rio_replied = bound_item_replied_cb,
};

static void test_bound_items(void)
{
	struct cs_ds2_req_fop *data;
	int                    rc;
	int                    i;

	m0_semaphore_init(&done_sem, 0);
	/* Test case confirms that items that are posted on a specific slot are
	   delivered in order.

	   The test posts 100 request items on slot0 of session. Each fop
	   carries its sequence number. Reciever simply copies the sequence
	   number in reply fop. RPC is instructed to invoke
	   bound_item_replied_cb() upon receiving reply to any of the request
	   items. The callback ensures that the sequence number in
	   received reply is one greater than sequence number received
	   in previous call.
	 */
	for (i = 0; i < 100; i++) {
		fop = fop_alloc();
		data = m0_fop_data(fop);
		M0_UT_ASSERT(data != NULL);
		data->csr_value   = i;
		item              = &fop->f_item;
		item->ri_session  = session;
		item->ri_deadline = 0;
		item->ri_ops      = &bound_item_ops;
		rc = m0_rpc_post(item, session->s_slot_table[0]);
		M0_UT_ASSERT(rc == 0);
		m0_fop_put(fop);
	}
	/* wait until reply to all items is received */
	m0_semaphore_down(&done_sem);
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
	     buf_send_cb(). But because there is only one thread from lnet
	     transport that delivers buffer events, it also blocks delivery of
	     net_buf_receieved(A.reply) event.
}
*/

const struct m0_test_suite item_ut = {
	.ts_name = "rpc-item-ut",
	.ts_init = ts_item_init,
	.ts_fini = ts_item_fini,
	.ts_tests = {
		{ "simple-transitions",     test_simple_transitions     },
		{ "item-timeout",           test_timeout                },
		{ "item-resend",            test_resend                 },
		{ "failure-before-sending", test_failure_before_sending },
		{ "oneway-item",            test_oneway_item            },
		{ "bound-item",             test_bound_items            },
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
