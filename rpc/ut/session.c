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
#include "rpc/rpc2.h"

static struct c2_rpc_machine machine;
static struct c2_rpc_conn    conn;
static struct c2_rpc_session session;

static int session_ut_init(void)
{
	conn.c_rpc_machine = &machine;
	c2_sm_group_init(&machine.rm_sm_grp);
	return 0;
}

static int session_ut_fini(void)
{
	c2_sm_group_fini(&machine.rm_sm_grp);
	return 0;
}

static void session_init_fini_test(void)
{
	int rc;
	int nr_slots = 1;

	rc = c2_rpc_session_init(&session, &conn, nr_slots);
	C2_UT_ASSERT(rc == 0);
	C2_UT_ASSERT(session_state(&session) == C2_RPC_SESSION_INITIALISED);
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
