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
 * Original author: Madhav Vemuri<madhav_vemuri@xyratex.com>
 * Original creation date: 10/09/2012
 */

#include "lib/ut.h"
#include "lib/mutex.h"
#include "lib/finject.h"
#include "fop/fop.h"

#include "rpc/rpc.h"
#include "rpc/rpc_internal.h"

enum {
	SENDER_ID  = 1001,
	SESSION_ID = 101,
	SLOTS_NR   = 1,
};

static struct c2_rpc_machine machine;
static struct c2_rpc_conn    conn;
static struct c2_rpc_session session;
static struct c2_rpc_session session0;

/* This structure defination is copied from rpc/session.c. */
static struct fop_session_establish_ctx {
	/** A fop instance of type c2_rpc_fop_session_establish_fopt */
	struct c2_fop          sec_fop;
	/** sender side session object */
	struct c2_rpc_session *sec_session;
} est_ctx;

static struct c2_fop est_fop_rep;
static struct c2_fop term_fop;
static struct c2_fop term_fop_rep;

struct c2_rpc_fop_session_establish     est;
struct c2_rpc_fop_session_establish_rep est_reply;
struct c2_rpc_fop_session_terminate     term;
struct c2_rpc_fop_session_terminate_rep term_reply;

static void fop_set_session0(struct c2_fop *fop)
{
	fop->f_item.ri_session = c2_rpc_conn_session0(&conn);
}

static int session_ut_init(void)
{
	int rc;

	conn.c_rpc_machine = &machine;
	conn.c_sender_id   = SENDER_ID;
	rpc_session_tlist_init(&conn.c_sessions);

	c2_sm_group_init(&machine.rm_sm_grp);
	rc = c2_rpc_session_init(&session0, &conn, SLOTS_NR);
	C2_ASSERT(rc == 0);
	session0.s_session_id = SESSION_ID_0;

	est_ctx.sec_fop.f_item.ri_reply = &est_fop_rep.f_item;
	term_fop.f_item.ri_reply        = &term_fop_rep.f_item;

	fop_set_session0(&est_ctx.sec_fop);
	fop_set_session0(&est_fop_rep);
	fop_set_session0(&term_fop);
	fop_set_session0(&term_fop_rep);

	c2_fi_enable("c2_rpc__fop_post", "do_nothing");
	return 0;
}

static int session_ut_fini(void)
{
	session0.s_session_id = SESSION_ID_INVALID;
	c2_rpc_session_fini(&session0);
	rpc_session_tlist_fini(&conn.c_sessions);
	c2_sm_group_fini(&machine.rm_sm_grp);
	c2_fi_disable("c2_rpc__fop_post", "do_nothing");
	c2_fi_disable("c2_rpc__fop_post", "fake_error");
	c2_fi_disable("c2_alloc", "fail_allocation");
	return 0;
}

static void session_init(void)
{
	int rc;

	rc = c2_rpc_session_init(&session, &conn, SLOTS_NR);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_INITIALISED);
}

static void session_init_fini_test(void)
{
	session_init();

	c2_rpc_session_fini(&session);
}

static void prepare_fake_est_reply(void)
{
	est_ctx.sec_session             = &session;
	est_ctx.sec_fop.f_data.fd_data  = &est;
	est_ctx.sec_fop.f_item.ri_error = 0;

	est_reply.rser_session_id = SESSION_ID; /* session_id_allocate() */
	est_reply.rser_sender_id  = SENDER_ID;  /* sender_id_allocate()  */
	est_reply.rser_rc         = 0;

	est_fop_rep.f_data.fd_data  = &est_reply;
}

static void session_init_and_establish(void)
{
	int rc;

	/* Session transition from INITIALISED => ESTABLISHING */
	session_init();

	conn.c_sm.sm_state = C2_RPC_CONN_ACTIVE;
	rc = c2_rpc_session_establish(&session);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_ESTABLISHING);

	prepare_fake_est_reply();
}

static void session_establish_reply(int err)
{
	/* Session transition from ESTABLISHING => IDLE | FAILED */
	est_ctx.sec_fop.f_item.ri_error = err;
	c2_rpc_machine_lock(&machine);
	c2_rpc_session_establish_reply_received(&est_ctx.sec_fop.f_item);
	c2_rpc_machine_unlock(&machine);
}

static void prepare_fake_term_reply(void)
{
	term_fop.f_item.ri_error = 0;
	term_fop.f_data.fd_data  = &term;
	term.rst_sender_id       = SENDER_ID;
	term.rst_session_id      = SESSION_ID;

	term_reply.rstr_session_id = SESSION_ID;
	term_reply.rstr_sender_id  = SENDER_ID;
	term_reply.rstr_rc         = 0;

	term_fop_rep.f_data.fd_data  = &term_reply;
}

static void session_terminate(void)
{
	int rc;

	/* Session transition from IDLE => TERMINATING */
	rc = c2_rpc_session_terminate(&session);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_TERMINATING);

	rc = c2_rpc_session_terminate(&session);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_TERMINATING);

	prepare_fake_term_reply();
}

static void session_terminate_reply_and_fini(int err)
{
	/* Session transition from TERMINATING => TERMINATED | FAILED */
	term_fop.f_item.ri_error = err;
	c2_rpc_machine_lock(&machine);
	c2_rpc_session_terminate_reply_received(&term_fop.f_item);
	c2_rpc_machine_unlock(&machine);
	C2_UT_ASSERT(err == 0 ?
		     session_state(&session) == C2_RPC_SESSION_TERMINATED :
		     session_state(&session) == C2_RPC_SESSION_FAILED);

	c2_rpc_session_fini(&session);
}

static void session_hold_release(void)
{
	/* Session transition from IDLE => BUSY => IDLE */
	c2_rpc_machine_lock(&machine);
	c2_rpc_session_hold_busy(&session);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_BUSY);
	c2_rpc_session_release(&session);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_IDLE);
	c2_rpc_machine_unlock(&machine);
}

static void session_check(void)
{
	/* Checks for session states transitions,
	   INITIALISED => ESTABLISHING => IDLE => BUSY => IDLE =>
	   TERMINATING => FINALISED.
	 */
	session_init_and_establish();
	session_establish_reply(0);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_IDLE);
	session_hold_release();
	session_terminate();
	session_terminate_reply_and_fini(0);
}

static void session_init_fail_test(void)
{
	int rc;
	/* Checks for c2_rpc_session_init() failure due to allocation failure */
	c2_fi_enable_once("c2_alloc", "fail_allocation");
	rc = c2_rpc_session_init(&session, &conn, SLOTS_NR);
	C2_UT_ASSERT(rc == -ENOMEM);

	c2_fi_enable_off_n_on_m("c2_alloc", "fail_allocation", 1, 1);
	rc = c2_rpc_session_init(&session, &conn, SLOTS_NR);
	C2_UT_ASSERT(rc == -ENOMEM);
	c2_fi_disable("c2_alloc", "fail_allocation");
}

static void session_establish_fail_test(void)
{
	int rc;

	conn.c_sm.sm_state = C2_RPC_CONN_ACTIVE;

	/* Checks for Session state transition,
	   C2_RPC_SESSION_INITIALISED => C2_RPC_SESSION_FAILED
	 */
	session_init();

	c2_fi_enable_once("c2_rpc__fop_post", "fake_error");
	rc = c2_rpc_session_establish(&session);
	C2_UT_ASSERT(rc == -ETIMEDOUT);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_FAILED);

	c2_rpc_session_fini(&session);

	/* Allocation failure */
	session_init();

	c2_fi_enable_once("c2_alloc", "fail_allocation");
	rc = c2_rpc_session_establish(&session);
	C2_UT_ASSERT(rc == -ENOMEM);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_FAILED);

	c2_rpc_session_fini(&session);
}

static void session_establish_reply_fail_test(void)
{
	/* Checks for Session state transition,
	   C2_RPC_SESSION_ESTABLISHING => C2_RPC_SESSION_FAILED
	 */
	session_init_and_establish();

	session_establish_reply(-EINVAL);
	C2_UT_ASSERT(session.s_sm.sm_rc == -EINVAL);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_FAILED);

	c2_rpc_session_fini(&session);

	/* Due to invalid sender id. */
	session_init_and_establish();

	est_reply.rser_sender_id = SENDER_ID_INVALID;
	session_establish_reply(0);
	C2_UT_ASSERT(session.s_sm.sm_rc == -EPROTO);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_FAILED);

	c2_rpc_session_fini(&session);

	/* Due to error in establish reply fop. */
	session_init_and_establish();

	est_reply.rser_rc = -EINVAL;
	session_establish_reply(0);
	C2_UT_ASSERT(session.s_sm.sm_rc == -EINVAL);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_FAILED);

	c2_rpc_session_fini(&session);
}

static void session_terminate_fail_test(void)
{
	int rc;

	/* Checks for session C2_RPC_SESSION_IDLE => C2_RPC_SESSION_FAILED */
	session_init_and_establish();
	session_establish_reply(0);

	c2_fi_enable_once("c2_alloc", "fail_allocation");
	rc = c2_rpc_session_terminate(&session);
	C2_UT_ASSERT(rc == -ENOMEM);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_FAILED);

	c2_rpc_session_fini(&session);

	/* Due to c2_rpc__fop_post() failure. */
	session_init_and_establish();
	session_establish_reply(0);

	c2_fi_enable_once("c2_rpc__fop_post", "fake_error");
	rc = c2_rpc_session_terminate(&session);
	C2_UT_ASSERT(rc == -ETIMEDOUT);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_FAILED);

	c2_rpc_session_fini(&session);
}

static void session_terminate_reply_fail_test(void)
{
	/* Checks for Conn C2_RPC_SESSION_TERMINATING => C2_RPC_SESSION_FAILED */
	session_init_and_establish();
	session_establish_reply(0);
	session_terminate();

	session_terminate_reply_and_fini(-EINVAL);
}

const struct c2_test_suite session_ut = {
	.ts_name = "session-ut",
	.ts_init = session_ut_init,
	.ts_fini = session_ut_fini,
	.ts_tests = {
		{ "session-init-fini", session_init_fini_test},
		{ "session-check", session_check},
		{ "session-init-fail", session_init_fail_test},
		{ "session-establish-fail", session_establish_fail_test},
		{ "session-terminate-fail", session_terminate_fail_test},
		{ "session-establish-reply-fail", session_establish_reply_fail_test},
		{ "session-terminate_reply-fail", session_terminate_reply_fail_test},
		{ NULL, NULL}
	}
};
C2_EXPORTED(session_ut);
