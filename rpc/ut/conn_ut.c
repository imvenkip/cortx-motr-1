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
 * Original creation date: 09/20/2012
 */

#include "lib/ut.h"
#include "lib/mutex.h"
#include "lib/finject.h"
#include "rpc/session.h"
#include "rpc/rpc2.h"
#include "rpc/session_ff.h"
#include "fop/fop.h"

enum {
	SENDER_ID = 1001,
};

static struct c2_rpc_machine machine;
static int		     rc;
static struct c2_rpc_conn    conn;
static struct c2_fop	     est_fop;
static struct c2_fop	     term_fop;
static struct c2_fop	     est_fop_rep;
static struct c2_fop	     term_fop_rep;

static struct c2_rpc_fop_conn_establish_rep est_reply;
static struct c2_rpc_fop_conn_terminate_rep term_reply;

static struct c2_net_end_point ep;

static int conn_ut_init(void)
{
	ep.nep_addr = "dummy ep";

	est_fop.f_item.ri_reply   = &est_fop_rep.f_item;
	est_fop.f_item.ri_error   = 0;

	est_reply.rcer_sender_id   = SENDER_ID; /* sender_id_allocate() */
	est_reply.rcer_rc          = 0;
	est_fop_rep.f_data.fd_data = &est_reply;

	term_fop.f_item.ri_reply   = &term_fop_rep.f_item;
	term_fop.f_item.ri_error   = 0;

	term_reply.ctr_sender_id    = est_reply.rcer_sender_id;
	term_reply.ctr_rc           = 0;
	term_fop_rep.f_data.fd_data = &term_reply;

	c2_list_init(&machine.rm_incoming_conns);
	c2_list_init(&machine.rm_outgoing_conns);
	c2_sm_group_init(&machine.rm_sm_grp);

	c2_fi_enable("rpc_chan_get", "do_nothing");
	c2_fi_enable("rpc_chan_put", "do_nothing");
	c2_fi_enable("c2_rpc_frm_run_formation", "do_nothing");
	c2_fi_enable("c2_rpc__fop_post", "do_nothing");
	return 0;
}

static int conn_ut_fini(void)
{
	c2_list_fini(&machine.rm_incoming_conns);
	c2_list_fini(&machine.rm_outgoing_conns);
	c2_sm_group_fini(&machine.rm_sm_grp);
	c2_fi_disable("rpc_chan_get", "do_nothing");
	c2_fi_disable("rpc_chan_put", "do_nothing");
	c2_fi_disable("c2_rpc_frm_run_formation", "do_nothing");
	c2_fi_disable("c2_rpc__fop_post", "do_nothing");

	c2_fi_disable("rpc_chan_get", "fake_error");
	c2_fi_disable("c2_alloc", "fail_allocation");
	c2_fi_disable("c2_rpc__fop_post", "fake_error");
	return 0;
}

static void conn_init(void)
{
	rc = c2_rpc_conn_init(&conn, &ep, &machine, 1);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(conn_state(&conn) == C2_RPC_CONN_INITIALISED);
}

static void fop_set_session(struct c2_fop *fop)
{
	fop->f_item.ri_session = c2_rpc_conn_session0(&conn);
}

static void conn_init_fini_test(void)
{
	struct c2_rpc_sender_uuid uuid;

	/* Checks for RPC connection initialisation and finalisation. */
	conn_init();
	C2_UT_ASSERT(conn.c_rpc_machine == &machine);

	uuid = conn.c_uuid;

	c2_rpc_conn_fini(&conn);

	/* Check for Receive side conn init and fini */

	c2_rpc_machine_lock(&machine);
	rc = c2_rpc_rcv_conn_init(&conn, &ep, &machine, &uuid);
	c2_rpc_machine_unlock(&machine);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(conn_state(&conn) == C2_RPC_CONN_INITIALISED);

	c2_rpc_conn_fini(&conn);
}

static void conn_init_fail_test(void)
{
	/* Checks for c2_rpc_conn_init() failure due to allocation failure */
	c2_fi_enable_once("c2_alloc", "fail_allocation");
	rc = c2_rpc_conn_init(&conn, &ep, &machine, 1);
	C2_UT_ASSERT(rc == -ENOMEM);

	c2_fi_enable_off_n_on_m("c2_alloc", "fail_allocation", 1, 1);
	rc = c2_rpc_conn_init(&conn, &ep, &machine, 1);
	C2_UT_ASSERT(rc == -ENOMEM);
	c2_fi_disable("c2_alloc", "fail_allocation");

	/* Checks for failure due to error in rpc_chan_get() */
	c2_fi_enable_once("rpc_chan_get", "fake_error");
	rc = c2_rpc_conn_init(&conn, &ep, &machine, 1);
	C2_UT_ASSERT(rc == -ENOMEM);
}

static void conn_init_and_establish(void)
{
	/* Checks for Conn C2_RPC_CONN_INITIALISED => C2_RPC_CONN_CONNECTING */

	conn_init();

	rc = c2_rpc_conn_establish(&conn);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(conn_state(&conn) == C2_RPC_CONN_CONNECTING);

	fop_set_session(&est_fop);
	fop_set_session(&est_fop_rep);
}

static void conn_establish_reply(void)
{
	/* Checks for Conn C2_RPC_CONN_CONNECTING => C2_RPC_CONN_ACTIVE */


	c2_rpc_machine_lock(&machine);
	c2_rpc_conn_establish_reply_received(&est_fop.f_item);
	c2_rpc_machine_unlock(&machine);
	C2_UT_ASSERT(conn_state(&conn) == C2_RPC_CONN_ACTIVE);
}

static void conn_terminate(void)
{
	/* Checks for Conn C2_RPC_CONN_ACTIVE => C2_RPC_CONN_TERMINATING */

	rc = c2_rpc_conn_terminate(&conn);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(conn_state(&conn) == C2_RPC_CONN_TERMINATING);

	fop_set_session(&term_fop);
	fop_set_session(&term_fop_rep);
}

static void conn_terminate_reply_and_fini(void)
{
	/* Checks for Conn C2_RPC_CONN_TERMINATING => C2_RPC_CONN_TERMINATED */

	c2_rpc_machine_lock(&machine);
	c2_rpc_conn_terminate_reply_received(&term_fop.f_item);
	c2_rpc_machine_unlock(&machine);
	C2_UT_ASSERT(conn_state(&conn) == C2_RPC_CONN_TERMINATED);

	/* Checks for Conn C2_RPC_CONN_TERMINATED => C2_RPC_CONN_FINALISED */
	c2_rpc_conn_fini(&conn);
}

static void conn_check(void)
{
	conn_init_and_establish();
	conn_establish_reply();
	conn_terminate();
	conn_terminate_reply_and_fini();
}

static void conn_establish_fail_test(void)
{
	/* Checks for Conn C2_RPC_CONN_INITIALISED => C2_RPC_CONN_FAILED */

	conn_init();

	c2_fi_enable_once("c2_rpc__fop_post", "fake_error");
	rc = c2_rpc_conn_establish(&conn);
	C2_UT_ASSERT(rc == -ETIMEDOUT);
	C2_UT_ASSERT(conn_state(&conn) == C2_RPC_CONN_FAILED);

	c2_rpc_conn_fini(&conn);

	/* Allocation failure */
	conn_init();

	c2_fi_enable_once("c2_alloc", "fail_allocation");
	rc = c2_rpc_conn_establish(&conn);
	C2_UT_ASSERT(rc == -ENOMEM);
	C2_UT_ASSERT(conn_state(&conn) == C2_RPC_CONN_FAILED);

	c2_rpc_conn_fini(&conn);
}

static void conn_establish_reply_fail_test(void)
{
	/* Checks for Conn C2_RPC_CONN_CONNECTING => C2_RPC_CONN_FAILED */

	conn_init_and_establish();

	est_fop.f_item.ri_error = -EINVAL;
	c2_rpc_machine_lock(&machine);
	c2_rpc_conn_establish_reply_received(&est_fop.f_item);
	C2_UT_ASSERT(conn.c_sm.sm_rc == -EINVAL);
	c2_rpc_machine_unlock(&machine);
	C2_UT_ASSERT(conn_state(&conn) == C2_RPC_CONN_FAILED);

	c2_rpc_conn_fini(&conn);
	est_fop.f_item.ri_error = 0;

	/* Due to invalid sender id. */
	conn_init_and_establish();

	est_reply.rcer_sender_id = SENDER_ID_INVALID;
	c2_rpc_machine_lock(&machine);
	c2_rpc_conn_establish_reply_received(&est_fop.f_item);
	C2_UT_ASSERT(conn.c_sm.sm_rc == -EPROTO);
	c2_rpc_machine_unlock(&machine);
	C2_UT_ASSERT(conn_state(&conn) == C2_RPC_CONN_FAILED);

	c2_rpc_conn_fini(&conn);
	est_reply.rcer_sender_id = SENDER_ID; /* restore */

}

static void conn_terminate_fail_test(void)
{
	/* Checks for Conn C2_RPC_CONN_ACTIVE => C2_RPC_CONN_FAILED */

	conn_init_and_establish();
	conn_establish_reply();

	c2_fi_enable_once("c2_alloc", "fail_allocation");
	rc = c2_rpc_conn_terminate(&conn);
	C2_UT_ASSERT(rc == -ENOMEM);
	C2_UT_ASSERT(conn_state(&conn) == C2_RPC_CONN_FAILED);

	c2_rpc_conn_fini(&conn);

	/* Due to c2_rpc__fop_post() failure. */

	conn_init_and_establish();
	conn_establish_reply();

	c2_fi_enable_once("c2_rpc__fop_post", "fake_error");
	rc = c2_rpc_conn_terminate(&conn);
	C2_UT_ASSERT(rc == -ETIMEDOUT);
	C2_UT_ASSERT(conn_state(&conn) == C2_RPC_CONN_FAILED);

	c2_rpc_conn_fini(&conn);
}

static void conn_terminate_reply_fail_test(void)
{
	/* Checks for Conn C2_RPC_CONN_TERMINATING => C2_RPC_CONN_FAILED */

	conn_init_and_establish();
	conn_establish_reply();
	conn_terminate();

	term_fop.f_item.ri_error = -EINVAL;
	c2_rpc_machine_lock(&machine);
	c2_rpc_conn_terminate_reply_received(&term_fop.f_item);
	c2_rpc_machine_unlock(&machine);
	C2_UT_ASSERT(conn.c_sm.sm_rc == -EINVAL);
	C2_UT_ASSERT(conn_state(&conn) == C2_RPC_CONN_FAILED);

	c2_rpc_conn_fini(&conn);
	term_fop.f_item.ri_error = 0;

	/* Due to non-matching sender id. */

	conn_init_and_establish();
	conn_establish_reply();
	conn_terminate();

	term_reply.ctr_sender_id = SENDER_ID + 1;
	c2_rpc_machine_lock(&machine);
	c2_rpc_conn_terminate_reply_received(&term_fop.f_item);
	c2_rpc_machine_unlock(&machine);
	C2_UT_ASSERT(conn.c_sm.sm_rc == -EPROTO);
	C2_UT_ASSERT(conn_state(&conn) == C2_RPC_CONN_FAILED);

	c2_rpc_conn_fini(&conn);
}

const struct c2_test_suite conn_ut = {
	.ts_name = "connection-ut",
	.ts_init = conn_ut_init,
	.ts_fini = conn_ut_fini,
	.ts_tests = {
		{ "conn-init-fini", conn_init_fini_test},
		{ "conn-check", conn_check},
		{ "conn-init-fail", conn_init_fail_test},
		{ "conn-establish-fail", conn_establish_fail_test},
		{ "conn-terminate-fail", conn_terminate_fail_test},
		{ "conn-establish-reply-fail", conn_establish_reply_fail_test},
		{ "conn-terminate_reply-fail", conn_terminate_reply_fail_test},
		{ NULL, NULL}
	}
};
C2_EXPORTED(conn_ut);
