/* -*- C -*- */
/*
 * COPYRIGHT 2013 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 18-Mar-2013
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

static struct m0_rpc_machine *machine;
static const char            *remote_addr;
enum {
	TIMEOUT  = 2 /* second */,
	NR_SLOTS = 5,
};

static int ts_rcv_session_init(void)   /* ts_ for "test suite" */
{
	start_rpc_client_and_server();
	machine     = &cctx.rcx_rpc_machine;
	remote_addr =  cctx.rcx_remote_addr;
	return 0;
}

static int ts_rcv_session_fini(void)
{
	stop_rpc_client_and_server();
	return 0;
}

#define FATAL(...) M0_LOG(M0_FATAL, __VA_ARGS__)

struct fp {
	const char *fn;
	const char *pt;
	int         erc; /* expected rc */
};

static bool enable_for_all_but_first_call(void *data)
{
	int *pcount = data;

	return ++*pcount > 1;
}

static void test_conn_establish(void)
{
	struct m0_net_end_point *ep;
	struct m0_rpc_conn       conn;
	int                      count;
	int                      rc;
	int                      i;
	struct fp fps1[] = {
		{"session_gen_fom_create",         "reply_fop_alloc_failed"},
		{"m0_rpc_fom_conn_establish_tick", "conn-alloc-failed"     },
		{"m0_db_tx_init",                  "failed"                },
		{"m0_rpc_slot_cob_create",         "failed"                },
	};
	struct fp fps2[] = {
		{"rpc_chan_get",        "fake_error"   },
		{"session_zero_attach", "out-of-memory"},
	};

	rc = m0_net_end_point_create(&ep, &machine->rm_tm, remote_addr);
	M0_UT_ASSERT(rc == 0);

	/* TEST: Connection established successfully */
	rc = m0_rpc_conn_create(&conn, ep, machine, MAX_RPCS_IN_FLIGHT,
				m0_time_from_now(TIMEOUT, 0));
	M0_UT_ASSERT(rc == 0);
	rc = m0_rpc_conn_destroy(&conn, m0_time_from_now(TIMEOUT, 0));
	M0_UT_ASSERT(rc == 0);

	/* TEST: Duplicate conn-establish requests are accepted but only
	         one of them gets executed and rest of them are ignored.
	 */
	m0_fi_enable_once("m0_rpc_fom_conn_establish_tick", "sleep_for_2sec");
	rc = m0_rpc_conn_create(&conn, ep, machine, MAX_RPCS_IN_FLIGHT,
				m0_time_from_now(2 * TIMEOUT, 0));
	M0_UT_ASSERT(rc == 0);
	rc = m0_rpc_conn_destroy(&conn, m0_time_from_now(TIMEOUT, 0));
	M0_UT_ASSERT(rc == 0);

	for (i = 0; i < ARRAY_SIZE(fps1); ++i) {
		FATAL("<%s, %s>", fps1[i].fn, fps1[i].pt);
		m0_fi_enable(fps1[i].fn, fps1[i].pt);
		rc = m0_rpc_conn_create(&conn, ep, machine, MAX_RPCS_IN_FLIGHT,
					m0_time_from_now(TIMEOUT, 0));
		M0_UT_ASSERT(rc == -ETIMEDOUT);
		m0_fi_disable(fps1[i].fn, fps1[i].pt);
	}
	for (i = 0; i < ARRAY_SIZE(fps2); ++i) {
		count = 0;
		m0_fi_enable_func(fps2[i].fn, fps2[i].pt,
				  enable_for_all_but_first_call, &count);
		rc = m0_rpc_conn_create(&conn, ep, machine, MAX_RPCS_IN_FLIGHT,
					m0_time_from_now(TIMEOUT, 0));
		M0_UT_ASSERT(rc == -ETIMEDOUT);
		m0_fi_disable(fps2[i].fn, fps2[i].pt);
	}
	m0_net_end_point_put(ep);
}

static void test_session_establish(void)
{
	struct m0_net_end_point *ep;
	struct m0_rpc_session    session;
	struct m0_rpc_conn       conn;
	int                      count;
	int                      rc;
	int                      i;
	struct fp fps[] = {
		//{"session_gen_fom_create", "reply_fop_alloc_failed"},
		{"m0_rpc_fom_session_establish_tick",
		                           "session-alloc-failed", -ENOMEM},
		{"m0_db_tx_init",          "failed",               -EINVAL},
		{"m0_rpc_slot_cob_create", "failed",               -EINVAL},
	};
	rc = m0_net_end_point_create(&ep, &machine->rm_tm, remote_addr);
	M0_UT_ASSERT(rc == 0);

	/* TEST1: Connection established successfully */
	rc = m0_rpc_conn_create(&conn, ep, machine, MAX_RPCS_IN_FLIGHT,
				m0_time_from_now(TIMEOUT, 0));
	M0_UT_ASSERT(rc == 0);
	rc = m0_rpc_session_create(&session, &conn, NR_SLOTS,
				   m0_time_from_now(TIMEOUT, 0));
	M0_UT_ASSERT(rc == 0);
	rc = m0_rpc_session_destroy(&session, m0_time_from_now(TIMEOUT, 0));
	M0_UT_ASSERT(rc == 0);

	for (i = 0; i < ARRAY_SIZE(fps); ++i) {
		m0_fi_enable(fps[i].fn, fps[i].pt);
		rc = m0_rpc_session_create(&session, &conn, NR_SLOTS,
					   m0_time_from_now(TIMEOUT, 0));
		M0_UT_ASSERT(rc == fps[i].erc);
		m0_fi_disable(fps[i].fn, fps[i].pt);
	}

	count = 0;
	m0_fi_enable_func("slot_table_alloc_and_init", "failed",
			  enable_for_all_but_first_call, &count);
	rc = m0_rpc_session_create(&session, &conn, NR_SLOTS,
				   m0_time_from_now(TIMEOUT, 0));
	M0_UT_ASSERT(rc == -ENOMEM);
	m0_fi_disable("slot_table_alloc_and_init", "failed");

	rc = m0_rpc_conn_destroy(&conn, m0_time_from_now(TIMEOUT, 0));
	M0_UT_ASSERT(rc == 0);

	m0_net_end_point_put(ep);
}

static void test_session_terminate(void)
{
	struct m0_net_end_point *ep;
	struct m0_rpc_session    session;
	struct m0_rpc_conn       conn;
	int                      rc;
	int                      i;
	struct fp fps[] = {
		//{"session_gen_fom_create", "reply_fop_alloc_failed"},
		{"m0_db_tx_init", "failed", -EINVAL},
	};
	rc = m0_net_end_point_create(&ep, &machine->rm_tm, remote_addr);
	M0_UT_ASSERT(rc == 0);

	/* TEST1: Connection established successfully */
	rc = m0_rpc_conn_create(&conn, ep, machine, MAX_RPCS_IN_FLIGHT,
				m0_time_from_now(TIMEOUT, 0));
	for (i = 0; i < ARRAY_SIZE(fps); ++i) {
		rc = m0_rpc_session_create(&session, &conn, NR_SLOTS,
					   m0_time_from_now(TIMEOUT, 0));
		M0_UT_ASSERT(rc == 0);

		m0_fi_enable(fps[i].fn, fps[i].pt);
		rc = m0_rpc_session_destroy(&session,
					    m0_time_from_now(TIMEOUT, 0));
		FATAL("rc: %d", rc);
		M0_UT_ASSERT(rc == fps[i].erc);
		m0_fi_disable(fps[i].fn, fps[i].pt);
	}

	rc = m0_rpc_conn_destroy(&conn, m0_time_from_now(TIMEOUT, 0));
	M0_UT_ASSERT(rc == 0);

	m0_net_end_point_put(ep);

}

const struct m0_test_suite rpc_rcv_session_ut = {
	.ts_name = "rpc-rcv-session-ut",
	.ts_init = ts_rcv_session_init,
	.ts_fini = ts_rcv_session_fini,
	.ts_tests = {
		{ "conn-establish",    test_conn_establish},
		{ "session-establish", test_session_establish},
		{ "session-terminate", test_session_terminate},
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
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
