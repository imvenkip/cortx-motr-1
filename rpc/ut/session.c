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
#include "rpc/session.h"
#include "rpc/session_internal.h"
#include "rpc/rpc2.h"
#include "fop/fop.h"
#include "rpc/session_ff.h"

enum {
	SENDER_ID  = 1001,
	SESSION_ID = 101,
};

static struct c2_rpc_machine machine;
static struct c2_rpc_conn    conn;
static struct c2_rpc_session session;
static struct c2_rpc_session session0;

struct fop_session_establish_ctx
{
	/** A fop instance of type c2_rpc_fop_conn_establish_fopt */
	struct c2_fop          sec_fop;
	/** sender side session object */
	struct c2_rpc_session *sec_session;
} est_ctx;

static struct c2_fop	     est_fop_rep;
static struct c2_fop	     term_fop;
static struct c2_fop	     term_fop_rep;

struct c2_rpc_fop_session_establish     est;
struct c2_rpc_fop_session_establish_rep est_reply;
struct c2_rpc_fop_session_terminate     term;
struct c2_rpc_fop_session_terminate_rep term_reply;

static int session_ut_init(void)
{	int rc;
	conn.c_rpc_machine = &machine;
	conn.c_sender_id  = SENDER_ID;
	rpc_session_tlist_init(&conn.c_sessions);
	c2_sm_group_init(&machine.rm_sm_grp);
	rc = c2_rpc_session_init(&session0, &conn, 1);
	C2_ASSERT(rc == 0);
	session0.s_session_id = SESSION_ID_0;

	est_ctx.sec_fop.f_item.ri_reply  = &est_fop_rep.f_item;
	term_fop.f_item.ri_reply         = &term_fop_rep.f_item;

	est_ctx.sec_fop.f_item.ri_session  = &session0;
	est_fop_rep.f_item.ri_session  = &session0;
	term_fop.f_item.ri_session     = &session0;
	term_fop_rep.f_item.ri_session = &session0;

	c2_fi_enable("c2_rpc__fop_post", "do_nothing");
	return 0;
}

static int session_ut_fini(void)
{
	c2_rpc_session_fini(&session0);
	rpc_session_tlist_fini(&conn.c_sessions);
	c2_sm_group_fini(&machine.rm_sm_grp);
	c2_fi_disable("c2_rpc__fop_post", "do_nothing");
	return 0;
}

static void session_init_fini_test(void)
{
	int rc;
	int nr_slots = 1;

	rc = c2_rpc_session_init(&session, &conn, nr_slots);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_INITIALISED);

	conn.c_sm.sm_state = C2_RPC_CONN_ACTIVE;
	rc = c2_rpc_session_establish(&session);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_ESTABLISHING);

	est_ctx.sec_session             = &session;
	est_ctx.sec_fop.f_data.fd_data  = &est;
	est_ctx.sec_fop.f_item.ri_error = 0;

	est_reply.rser_session_id   = SESSION_ID; /* session_id_allocate() */
	est_reply.rser_sender_id    = SENDER_ID; /* sender_id_allocate()  */
	est_reply.rser_rc           = 0;

	est_fop_rep.f_data.fd_data  = &est_reply;

	c2_rpc_machine_lock(&machine);
	c2_rpc_session_establish_reply_received(&est_ctx.sec_fop.f_item);
	c2_rpc_machine_unlock(&machine);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_IDLE);

	rc = c2_rpc_session_terminate(&session);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_TERMINATING);

	term_fop.f_item.ri_error = 0;
	term_fop.f_data.fd_data  = &term;
	term.rst_sender_id       = SENDER_ID;
	term.rst_session_id      = SESSION_ID;

	term_reply.rstr_session_id = SESSION_ID;
	term_reply.rstr_sender_id  = SENDER_ID;
	term_reply.rstr_rc         = 0;

	term_fop_rep.f_data.fd_data  = &term_reply;

	c2_rpc_machine_lock(&machine);
	c2_rpc_session_terminate_reply_received(&term_fop.f_item);
	c2_rpc_machine_unlock(&machine);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_TERMINATED);

	c2_rpc_session_fini(&session);
}

const struct c2_test_suite session_ut = {
	.ts_name = "session-ut",
	.ts_init = session_ut_init,
	.ts_fini = session_ut_fini,
	.ts_tests = {
		{ "session-init-fini", session_init_fini_test},
		{ NULL, NULL}
	}
};
C2_EXPORTED(session_ut);
