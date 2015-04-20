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

#include "ut/ut.h"
#include "lib/finject.h"
#include "lib/misc.h"              /* M0_BITS */
#include "lib/semaphore.h"
#include "lib/memory.h"
#include "rpc/rpclib.h"
#include "rpc/ut/clnt_srv_ctx.c"   /* sctx, cctx. NOTE: This is .c file */
#include "rpc/ut/rpc_test_fops.h"
#include "rpc/rpc_internal.h"

enum { MILLISEC = 1000 * 1000 };

static int __test(void);
static void __test_timeout(m0_time_t deadline, m0_time_t timeout);
static void __test_resend(struct m0_fop *fop);
static void __test_timer_start_failure(void);

static struct m0_rpc_machine *machine;
static struct m0_rpc_session *session;
static struct m0_rpc_stats    saved;
static struct m0_rpc_stats    stats;
static struct m0_rpc_item    *item;
static struct m0_fop         *fop;
static int                    item_rc;
static struct m0_rpc_item_type test_item_cache_itype;
extern const struct m0_sm_conf outgoing_item_sm_conf;
extern const struct m0_sm_conf incoming_item_sm_conf;

#define IS_INCR_BY_1(p) _0C(saved.rs_ ## p + 1 == stats.rs_ ## p)

static int ts_item_init(void)   /* ts_ for "test suite" */
{
	test_item_cache_itype.rit_incoming_conf = incoming_item_sm_conf;
	test_item_cache_itype.rit_outgoing_conf = outgoing_item_sm_conf;
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
	fop = fop_alloc(machine);
	item = &fop->f_item;
	rc = m0_rpc_post_sync(fop, session, &cs_ds_req_fop_rpc_item_ops,
			      0 /* deadline */);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(item->ri_error == 0);
	M0_UT_ASSERT(item->ri_reply != NULL);
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_REPLIED) &&
		     chk_state(item->ri_reply, M0_RPC_ITEM_ACCEPTED));
	m0_rpc_machine_get_stats(machine, &stats, true);
	M0_UT_ASSERT(IS_INCR_BY_1(nr_sent_items) &&
		     IS_INCR_BY_1(nr_rcvd_items));
	M0_UT_ASSERT(m0_ref_read(&fop->f_ref) == 1);
	m0_fop_put_lock(fop);
	M0_LOG(M0_DEBUG, "TEST:1:END");
}

void disable_packet_ready_set_reply_error(int arg)
{
	m0_nanosleep(m0_time(M0_RPC_ITEM_RESEND_INTERVAL * 2 + 1, 0), NULL);
	m0_fi_disable("packet_ready", "set_reply_error");
}

static void test_reply_item_error(void)
{
	int rc;
	struct m0_thread thread = {0};

	M0_LOG(M0_DEBUG, "TEST:1:START");
	m0_rpc_machine_get_stats(machine, &saved, false /* clear stats? */);
	fop = fop_alloc(machine);
	item = &fop->f_item;
	m0_fi_enable("packet_ready", "set_reply_error");
	rc = M0_THREAD_INIT(&thread, int, NULL,
			    &disable_packet_ready_set_reply_error,
			    0, "disable_fi");
	M0_UT_ASSERT(rc == 0);

	rc = m0_rpc_post_sync(fop, session, NULL, 0 /* deadline */);

	/* Error happens on server side, and client will try to resend fop.
	 * (M0_RPC_ITEM_RESEND_INTERVAL * 2 + 1) seconds later, server sends
	 * back reply successfully. */
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(item->ri_error == 0);
	M0_UT_ASSERT(item->ri_reply != NULL);
	m0_fop_put_lock(fop);
	M0_LOG(M0_DEBUG, "TEST:1:END");
	m0_thread_join(&thread);
}

extern void (*m0_rpc__item_dropped)(struct m0_rpc_item *item);

static struct m0_semaphore wait;
static void test_dropped(struct m0_rpc_item *item)
{
	m0_semaphore_up(&wait);
}

static void test_timeout(void)
{
	int rc;

	/* Test2.1: Request item times out before reply reaches to sender.
		    Delayed reply is then dropped.
	 */
	M0_LOG(M0_DEBUG, "TEST:2.1:START");
	fop = fop_alloc(machine);
	item = &fop->f_item;
	item->ri_nr_sent_max = 1;
	m0_rpc_machine_get_stats(machine, &saved, false);
	m0_fi_enable_once("cs_req_fop_fom_tick", "inject_delay");
	m0_fi_enable_once("item_received", "drop_signal");
	m0_semaphore_init(&wait, 0);
	m0_rpc__item_dropped = &test_dropped;
	rc = m0_rpc_post_sync(fop, session, &cs_ds_req_fop_rpc_item_ops,
			      0 /* deadline */);
	M0_UT_ASSERT(rc == -ETIMEDOUT);
	M0_UT_ASSERT(item->ri_error == -ETIMEDOUT);
	M0_UT_ASSERT(item->ri_reply == NULL);
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_FAILED));
	m0_semaphore_down(&wait);
	m0_semaphore_fini(&wait);
	m0_rpc_machine_get_stats(machine, &stats, true);
	M0_UT_ASSERT(IS_INCR_BY_1(nr_dropped_items) &&
		     IS_INCR_BY_1(nr_timedout_items) &&
		     IS_INCR_BY_1(nr_failed_items));
	M0_UT_ASSERT(m0_ref_read(&fop->f_ref) == 1);
	m0_fop_put_lock(fop);
	m0_rpc__item_dropped = NULL;
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
	fop = fop_alloc(machine);
	item = &fop->f_item;
	m0_rpc_machine_get_stats(machine, &saved, false);
	item->ri_nr_sent_max = 1;
	item->ri_resend_interval = timeout;
	m0_rpc_post_sync(fop, session, NULL, deadline);
	M0_UT_ASSERT(item->ri_error == -ETIMEDOUT);
	M0_UT_ASSERT(item->ri_reply == NULL);
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_FAILED));
	m0_rpc_machine_get_stats(machine, &stats, true);
	M0_UT_ASSERT(IS_INCR_BY_1(nr_timedout_items) &&
		     IS_INCR_BY_1(nr_failed_items));
	M0_UT_ASSERT(m0_ref_read(&fop->f_ref) == 1);
	m0_fop_put_lock(fop);
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
	fop = fop_alloc(machine);
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
	M0_UT_ASSERT(m0_ref_read(&fop->f_ref) == 1);
	m0_fop_put_lock(fop);
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

static void misordered_item_replied_cb(struct m0_rpc_item *item)
{
	M0_UT_ASSERT(false); /* it should never be called */
}

static const struct m0_rpc_item_ops misordered_item_ops = {
	.rio_replied = misordered_item_replied_cb
};

static void __test_resend(struct m0_fop *fop)
{
	bool fop_put_flag = false;
	int rc;

	if (fop == NULL) {
		fop = fop_alloc(machine);
		fop_put_flag = true;
	}
	item = &fop->f_item;
	rc = m0_rpc_post_sync(fop, session, NULL, 0 /* urgent */);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(item->ri_error == 0);
	M0_UT_ASSERT(item->ri_nr_sent >= 1);
	M0_UT_ASSERT(item->ri_reply != NULL);
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_REPLIED));
	if (fop_put_flag) {
		M0_UT_ASSERT(m0_ref_read(&fop->f_ref) == 1);
		m0_fop_put_lock(fop);
	}
}

static void __test_timer_start_failure(void)
{
	int rc;

	fop = fop_alloc(machine);
	item = &fop->f_item;
	rc = m0_rpc_post_sync(fop, session, NULL, 0 /* urgent */);
	M0_UT_ASSERT(rc == -EINVAL);
	M0_UT_ASSERT(item->ri_error == -EINVAL);
	M0_UT_ASSERT(item->ri_reply == NULL);
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_FAILED));
	/* sleep until request reaches at server and is dropped */
	m0_nanosleep(m0_time(0, 5 * 1000 * 1000), NULL);
	M0_UT_ASSERT(m0_ref_read(&fop->f_ref) == 1);
	m0_fop_put_lock(fop);
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
		M0_UT_ASSERT(item_rc == fp[i].rc);
		M0_LOG(M0_DEBUG, "TEST:3.%d:END", i + 1);
		m0_fop_put_lock(fop);
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
	M0_UT_ASSERT(item_rc == -EINVAL);
	m0_rpc_machine_get_stats(machine, &stats, false);
	m0_fi_disable("buf_send_cb", "fake_err");
	m0_fi_disable("item_received", "drop_item");
	M0_LOG(M0_DEBUG, "TEST:4:END");
	m0_fop_put_lock(fop);
}

static int __test(void)
{
	int rc;

	/* Check SENDING -> FAILED transition */
	m0_rpc_machine_get_stats(machine, &saved, false);
	fop  = fop_alloc(machine);
	item = &fop->f_item;
	rc = m0_rpc_post_sync(fop, session, &cs_ds_req_fop_rpc_item_ops,
			      0 /* deadline */);
	M0_UT_ASSERT(item->ri_reply == NULL);
	item_rc = item->ri_error;
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_FAILED));
	m0_rpc_machine_get_stats(machine, &stats, false);
	M0_UT_ASSERT(IS_INCR_BY_1(nr_failed_items));
	M0_UT_ASSERT(m0_ref_read(&fop->f_ref) == 1);
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
	fop = m0_fop_alloc(&m0_rpc_arrow_fopt, NULL, machine);
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

	M0_UT_ASSERT(m0_ref_read(&fop->f_ref) == 1);
	m0_fop_put_lock(fop);

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
	m0_fop_put_lock(fop);
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

static void check_cancel(void)
{
	int rc;

	m0_rpc_item_cancel(item);
	M0_UT_ASSERT(item->ri_reply == NULL);
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_UNINITIALISED));
	rc = m0_rpc_post_sync(fop, session, &cs_ds_req_fop_rpc_item_ops,
			      0 /* deadline */);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(item->ri_error == 0);
	M0_UT_ASSERT(item->ri_reply != NULL);
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_REPLIED) &&
		     chk_state(item->ri_reply, M0_RPC_ITEM_ACCEPTED));
}

static void test_cancel(void)
{
	int rc;

	/* TEST5: Send item, cancel it and send again. */
	M0_LOG(M0_DEBUG, "TEST:5:1:START");
	fop = fop_alloc(machine);
	item = &fop->f_item;
	rc = m0_rpc_post_sync(fop, session, &cs_ds_req_fop_rpc_item_ops,
			      0 /* deadline */);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(item->ri_error == 0);
	M0_UT_ASSERT(item->ri_reply != NULL);
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_REPLIED) &&
		     chk_state(item->ri_reply, M0_RPC_ITEM_ACCEPTED));
	check_cancel();
	M0_UT_ASSERT(m0_ref_read(&fop->f_ref) == 1);
	m0_fop_put_lock(fop);
	M0_LOG(M0_DEBUG, "TEST:5:1:END");

	/* Cancel item while in formation. */
	M0_LOG(M0_DEBUG, "TEST:5:2:START");
	fop = fop_alloc(machine);
	item              = &fop->f_item;
	item->ri_session  = session;
	item->ri_prio     = M0_RPC_ITEM_PRIO_MID;
	item->ri_deadline = m0_time_from_now(50, 0);
	rc = m0_rpc_post(item);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(item->ri_reply == NULL);
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_ENQUEUED));
	check_cancel();
	M0_UT_ASSERT(m0_ref_read(&fop->f_ref) == 1);
	m0_fop_put_lock(fop);
	M0_LOG(M0_DEBUG, "TEST:5:2:END");

	/* Cancel while waiting for reply. */
	M0_LOG(M0_DEBUG, "TEST:5:3:START");
	m0_fi_enable_once("cs_req_fop_fom_tick", "inject_delay");
	fop = fop_alloc(machine);
	item              = &fop->f_item;
	item->ri_session  = session;
	item->ri_prio     = M0_RPC_ITEM_PRIO_MID;
	item->ri_deadline = m0_time_from_now(0, 0);
	rc = m0_rpc_post(item);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(item->ri_reply == NULL);
	m0_nanosleep(m0_time(0, 100000000), NULL);
	M0_UT_ASSERT(chk_state(item, M0_RPC_ITEM_WAITING_FOR_REPLY));
	check_cancel();
	M0_UT_ASSERT(m0_ref_read(&fop->f_ref) == 1);
	m0_fop_put_lock(fop);
	M0_LOG(M0_DEBUG, "TEST:5:3:END");
}

enum {
	M0_RPC_ITEM_CACHE_ITEMS_NR_MAX = 0x40,
};

static uint64_t test_item_cache_item_get_xid = UINT64_MAX - 1;
static uint64_t test_item_cache_item_put_xid = UINT64_MAX - 1;

static void test_item_cache_item_get(struct m0_rpc_item *item)
{
	M0_UT_ASSERT(test_item_cache_item_get_xid == UINT64_MAX ||
		     item->ri_header.osr_xid ==
		     test_item_cache_item_get_xid);
	test_item_cache_item_get_xid = UINT64_MAX - 1;
}

static void test_item_cache_item_put(struct m0_rpc_item *item)
{
	M0_UT_ASSERT(test_item_cache_item_put_xid == UINT64_MAX ||
		     item->ri_header.osr_xid ==
		     test_item_cache_item_put_xid);
	test_item_cache_item_put_xid = UINT64_MAX - 1;
}

extern const struct m0_sm_conf outgoing_item_sm_conf;
extern const struct m0_sm_conf incoming_item_sm_conf;

static struct m0_rpc_item_type_ops test_item_cache_type_ops = {
	.rito_item_get = test_item_cache_item_get,
	.rito_item_put = test_item_cache_item_put,
};
static struct m0_rpc_item_type test_item_cache_itype = {
	.rit_ops           = &test_item_cache_type_ops,
};

/*
 * Add each nth item to the cache.
 * Lookup each item in cache.
 * Then remove each item from the cache.
 */
static void test_item_cache_add_nth(struct m0_rpc_item_cache *ic,
				    struct m0_mutex	     *lock,
				    struct m0_rpc_item	     *items,
				    int			      items_nr,
				    int			      n)
{
	struct m0_rpc_item *item;
	int		    added_nr;
	int		    test_nr;
	int		    i;

	M0_SET0(ic);
	m0_rpc_item_cache_init(ic, lock);
	added_nr = 0;
	for (i = 0; i < items_nr; ++i) {
		if ((i % n) == 0) {
			test_item_cache_item_get_xid = i;
			m0_rpc_item_cache_add(ic, &items[i], M0_TIME_NEVER);
			++added_nr;
		}
	}
	/* no-op */
	m0_rpc_item_cache_purge(ic);
	for (i = 0; i < items_nr; ++i) {
		/* do nothing */
		if ((i % n) == 0)
			m0_rpc_item_cache_add(ic, &items[i], M0_TIME_NEVER);
	}
	test_nr = 0;
	for (i = 0; i < items_nr; ++i) {
		item = m0_rpc_item_cache_lookup(ic, i);
		/* m0_rpc_item_cache_lookup() returns either NULL or item */
		M0_UT_ASSERT(item == NULL || item == &items[i]);
		M0_UT_ASSERT(equi(item != NULL, (i % n) == 0));
		test_nr += item != NULL;
	}
	M0_UT_ASSERT(test_nr == added_nr);
	for (i = 0; i < items_nr; ++i) {
		if ((i % n) == 0)
			test_item_cache_item_put_xid = i;
		m0_rpc_item_cache_del(ic, i);
	}
	/* cache is empty now */
	/* do nothing */
	m0_rpc_item_cache_clear(ic);
	for (i = 0; i < items_nr; ++i) {
		item = m0_rpc_item_cache_lookup(ic, i);
		M0_UT_ASSERT(item == NULL);
	}
	m0_rpc_item_cache_fini(ic);
}

static void test_item_cache(void)
{
	struct m0_rpc_item_cache  ic;
	struct m0_rpc_machine	  rmach = {};
	struct m0_rpc_item	 *items;
	struct m0_mutex		  lock;
	int			  items_nr;
	int			  n;
	int			  i;

	M0_ALLOC_ARR(items, M0_RPC_ITEM_CACHE_ITEMS_NR_MAX);
	M0_UT_ASSERT(items != NULL);
	for (i = 0; i < M0_RPC_ITEM_CACHE_ITEMS_NR_MAX; ++i) {
		m0_rpc_item_init(&items[i], &test_item_cache_itype);
		items[i].ri_header.osr_xid = i;
		items[i].ri_rmachine = &rmach;
	}
	m0_mutex_init(&lock);
	m0_mutex_lock(&lock);
	/*
	 * This is needed because m0_rpc_item_put() checks rpc machine lock.
	 */
	m0_mutex_init(&rmach.rm_sm_grp.s_lock);
	m0_mutex_lock(&rmach.rm_sm_grp.s_lock);
	for (items_nr = 1;
	     items_nr < M0_RPC_ITEM_CACHE_ITEMS_NR_MAX; ++items_nr) {
		for (n = 1; n <= items_nr; ++n)
			test_item_cache_add_nth(&ic, &lock, items, items_nr, n);
	}
	m0_mutex_unlock(&rmach.rm_sm_grp.s_lock);
	m0_mutex_fini(&rmach.rm_sm_grp.s_lock);
	m0_mutex_unlock(&lock);
	m0_mutex_fini(&lock);
	for (i = 0; i < M0_RPC_ITEM_CACHE_ITEMS_NR_MAX; ++i)
		m0_rpc_item_fini(&items[i]);
	m0_free(items);
}

struct m0_ut_suite item_ut = {
	.ts_name = "rpc-item-ut",
	.ts_init = ts_item_init,
	.ts_fini = ts_item_fini,
	.ts_tests = {
		{ "cache",		    test_item_cache		},
		{ "simple-transitions",     test_simple_transitions     },
		{ "reply-item-error",       test_reply_item_error       },
		{ "item-timeout",           test_timeout                },
		{ "item-resend",            test_resend                 },
		{ "failure-before-sending", test_failure_before_sending },
		{ "oneway-item",            test_oneway_item            },
		{ "cancel",                 test_cancel                 },
		{ NULL, NULL },
	}
};

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
