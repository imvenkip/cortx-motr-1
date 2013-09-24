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
 * Original author: Rajesh Bhalerao <rajesh_bhalerao@xyratex.com>
 * Original creation date: 08/21/2012
 */

#include "rm/rm.h"
#include "rm/rm_internal.h"
#include "rm/rm_fops.h"
#include "rm/ut/rmut.h"
#include "rm/ut/rings.h"

/* Maximum test servers for this testcase */
static enum rm_server  test_servers_nr;
static struct m0_clink tests_clink[TEST_NR];

static void server1_in_complete(struct m0_rm_incoming *in, int32_t rc)
{
	M0_UT_ASSERT(in != NULL);
	m0_chan_broadcast_lock(&rm_ctx[SERVER_1].rc_chan);
}

static void server1_in_conflict(struct m0_rm_incoming *in)
{
}

static const struct m0_rm_incoming_ops server1_incoming_ops = {
	.rio_complete = server1_in_complete,
	.rio_conflict = server1_in_conflict
};

static void server2_in_complete(struct m0_rm_incoming *in, int32_t rc)
{
	M0_UT_ASSERT(in != NULL);
	m0_chan_broadcast_lock(&rm_ctx[SERVER_2].rc_chan);
}

static void server2_in_conflict(struct m0_rm_incoming *in)
{
}

static const struct m0_rm_incoming_ops server2_incoming_ops = {
	.rio_complete = server2_in_complete,
	.rio_conflict = server2_in_conflict
};

static void server3_in_complete(struct m0_rm_incoming *in, int32_t rc)
{
	M0_UT_ASSERT(in != NULL);
	m0_chan_broadcast_lock(&rm_ctx[SERVER_3].rc_chan);
}

static void server3_in_conflict(struct m0_rm_incoming *in)
{
}

static const struct m0_rm_incoming_ops server3_incoming_ops = {
	.rio_complete = server3_in_complete,
	.rio_conflict = server3_in_conflict
};

static void credit_setup(enum rm_server srv_id,
			enum m0_rm_incoming_flags flag,
			int value)
{
	struct m0_rm_incoming *in = &rm_ctx[srv_id].rc_test_data.rd_in;
	struct m0_rm_owner    *owner = rm_ctx[srv_id].rc_test_data.rd_owner;

	m0_rm_incoming_init(in, owner, M0_RIT_LOCAL, RIP_NONE, flag);
	in->rin_want.cr_datum = value;
	switch (srv_id) {
	case SERVER_1:
		in->rin_ops = &server1_incoming_ops;
		break;
	case SERVER_2:
		in->rin_ops = &server2_incoming_ops;
		break;
	case SERVER_3:
		in->rin_ops = &server3_incoming_ops;
		break;
	default:
		break;
	}
}

static void test2_verify(void)
{
	struct m0_rm_owner *so2 = rm_ctx[SERVER_2].rc_test_data.rd_owner;
	struct m0_rm_owner *so1 = rm_ctx[SERVER_1].rc_test_data.rd_owner;

	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so2->ro_sublet));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so2->ro_borrowed));
	M0_UT_ASSERT(m0_rm_ur_tlist_is_empty(&so2->ro_owned[OWOS_CACHED]));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so1->ro_borrowed));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so1->ro_owned[OWOS_CACHED]));
}

static void test2_run(void)
{
	struct m0_rm_incoming *in = &rm_ctx[SERVER_1].rc_test_data.rd_in;

	/* Server-2 is upward creditor for Server-1 */
	creditor_cookie_setup(SERVER_1, SERVER_2);
	/*
	 * This request will get NENYA from Server-2 and DURIN from
	 * Server-3 via Server-2.
	 */
	credit_setup(SERVER_1, RIF_MAY_BORROW, NENYA | DURIN);
	m0_rm_credit_get(in);
	if (incoming_state(in) == RI_WAIT)
		m0_chan_wait(&rm_ctx[SERVER_1].rc_clink);
	M0_UT_ASSERT(incoming_state(in) == RI_SUCCESS);
	M0_UT_ASSERT(in->rin_rc == 0);
	m0_rm_credit_put(in);
	m0_rm_incoming_fini(in);
}

static void server1_tests(void)
{
	m0_chan_wait(&tests_clink[TEST2]);
	m0_clink_add_lock(&rm_ctx[SERVER_1].rc_chan,
			  &rm_ctx[SERVER_1].rc_clink);
	test2_run();
	test2_verify();
	m0_clink_del_lock(&rm_ctx[SERVER_1].rc_clink);

	m0_chan_signal_lock(&rm_ut_tests_chan);
}

static void test3_verify(void)
{
	struct m0_rm_owner *so3 = rm_ctx[SERVER_3].rc_test_data.rd_owner;
	struct m0_rm_owner *so2 = rm_ctx[SERVER_2].rc_test_data.rd_owner;
	struct m0_rm_owner *so1 = rm_ctx[SERVER_1].rc_test_data.rd_owner;

	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so3->ro_sublet));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so2->ro_borrowed));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so2->ro_owned[OWOS_CACHED]));
	M0_UT_ASSERT(m0_rm_ur_tlist_is_empty(&so1->ro_borrowed));
	M0_UT_ASSERT(m0_rm_ur_tlist_is_empty(&so1->ro_owned[OWOS_CACHED]));
}

static void test3_run(void)
{
	struct m0_rm_incoming *in = &rm_ctx[SERVER_2].rc_test_data.rd_in;

	/*
	 * 1. Test-case - Set LOCAL_WAIT flags. We should get the error
	 *                -EREMOTE as NENYA is now on SERVER_1.
	 */
	credit_setup(SERVER_2, RIF_LOCAL_WAIT, NENYA);
	m0_rm_credit_get(in);
	M0_UT_ASSERT(incoming_state(in) == RI_FAILURE);
	M0_UT_ASSERT(in->rin_rc == -EREMOTE);
	m0_rm_incoming_fini(in);
	m0_chan_wait(&rm_ctx[SERVER_2].rc_clink);

	/*
	 * 2. Test-case - NENYA is on SERVER_1. VILYA is on SERVER_3.
	 *                Make sure both borrow and revoke succeed in a
	 *                single request.
	 */
	loan_session_set(SERVER_2, SERVER_1);
	credit_setup(SERVER_2, RIF_MAY_REVOKE | RIF_MAY_BORROW,
		    NENYA | VILYA);
	m0_rm_credit_get(in);
	if (incoming_state(in) == RI_WAIT)
		m0_chan_wait(&rm_ctx[SERVER_2].rc_clink);
	M0_UT_ASSERT(incoming_state(in) == RI_SUCCESS);
	M0_UT_ASSERT(in->rin_rc == 0);
	m0_rm_credit_put(in);
	m0_rm_incoming_fini(in);
}

static void test1_verify(void)
{
	struct m0_rm_owner *so3 = rm_ctx[SERVER_3].rc_test_data.rd_owner;
	struct m0_rm_owner *so2 = rm_ctx[SERVER_2].rc_test_data.rd_owner;

	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so3->ro_sublet));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so2->ro_borrowed));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so2->ro_owned[OWOS_CACHED]));
}

/*
 * Test borrow
 */
static void test1_run(void)
{
	struct m0_rm_incoming *in = &rm_ctx[SERVER_2].rc_test_data.rd_in;

	/*
	 * 1. Test-case - Set LOCAL_WAIT flags. We should get the error
	 *                -EREMOTE as NENYA is on SERVER_3.
	 */
	credit_setup(SERVER_2, RIF_LOCAL_WAIT, NENYA);
	m0_rm_credit_get(in);
	M0_UT_ASSERT(incoming_state(in) == RI_FAILURE);
	M0_UT_ASSERT(in->rin_rc == -EREMOTE);
	m0_rm_incoming_fini(in);
	m0_chan_wait(&rm_ctx[SERVER_2].rc_clink);

	/*
	 * 2. Test-case - Setup creditor cookie. Credit request should
	 *                succeed.
	 */
	/* Server-3 is upward creditor for Server-2 */
	creditor_cookie_setup(SERVER_2, SERVER_3);
	credit_setup(SERVER_2, RIF_MAY_BORROW, NENYA);
	m0_rm_credit_get(in);
	if (incoming_state(in) == RI_WAIT)
		m0_chan_wait(&rm_ctx[SERVER_2].rc_clink);
	M0_UT_ASSERT(incoming_state(in) == RI_SUCCESS);
	M0_UT_ASSERT(in->rin_rc == 0);
	m0_rm_credit_put(in);
	m0_rm_incoming_fini(in);
}

static void server2_tests(void)
{
	m0_chan_wait(&tests_clink[TEST1]);
	m0_clink_add_lock(&rm_ctx[SERVER_2].rc_chan,
			  &rm_ctx[SERVER_2].rc_clink);
	test1_run();
	test1_verify();

	/* Begin next test */
	m0_chan_signal_lock(&rm_ut_tests_chan);

	m0_chan_wait(&tests_clink[TEST3]);
	test3_run();
	test3_verify();
	m0_clink_del_lock(&rm_ctx[SERVER_2].rc_clink);

	/* Begin next test */
	m0_chan_signal_lock(&rm_ut_tests_chan);
}

static void test4_run(void)
{
	struct m0_rm_owner *so3 = rm_ctx[SERVER_3].rc_test_data.rd_owner;
	int		    rc;

	/*
	 * Tests m0_rm_owner_windup(). This tests automatic revokes.
	 */
	loan_session_set(SERVER_3, SERVER_2);
	m0_rm_owner_windup(so3);
	rc = m0_rm_owner_timedwait(so3, M0_BITS(ROS_FINAL), M0_TIME_NEVER);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(owner_state(so3) == ROS_FINAL);
	m0_rm_owner_fini(so3);
	M0_SET0(rm_ctx[SERVER_3].rc_test_data.rd_owner);
	m0_rm_owner_init(rm_ctx[SERVER_3].rc_test_data.rd_owner,
			 &m0_rm_no_group,
			 rm_ctx[SERVER_3].rc_test_data.rd_res,
			 NULL);
}

static void server3_tests(void)
{
	m0_chan_wait(&tests_clink[TEST4]);
	m0_clink_add_lock(&rm_ctx[SERVER_3].rc_chan,
			  &rm_ctx[SERVER_3].rc_clink);
	test4_run();
	m0_clink_del_lock(&rm_ctx[SERVER_3].rc_clink);
}

static void rm_server_start(const int tid)
{
	if (tid < test_servers_nr) {
		rings_utdata_ops_set(&rm_ctx[tid].rc_test_data);
		rm_ctx_server_start(tid);
	}

	switch(tid) {
	case SERVER_1:
		server1_tests();
		break;
	case SERVER_2:
		server2_tests();
		break;
	case SERVER_3:
		server3_tests();
		break;
	default:
		break;
	}
}

/*
 * Hierarchy description:
 * SERVER_1 is downward debtor for SERVER_2.
 * SERVER_2 is upward creditor for SERVER_1 and downward debtor for SERVER_3.
 * SERVER_3 is upward creditor for SERVER_2.
 */
static void server_hier_config(void)
{
	rm_ctx[SERVER_1].creditor_id = SERVER_2;
	rm_ctx[SERVER_1].debtor_id[0] = SERVER_INVALID;
	rm_ctx[SERVER_1].rc_debtors_nr = 1;

	rm_ctx[SERVER_2].creditor_id = SERVER_3;
	rm_ctx[SERVER_2].debtor_id[0] = SERVER_1;
	rm_ctx[SERVER_2].rc_debtors_nr = 1;

	rm_ctx[SERVER_3].creditor_id = SERVER_INVALID;
	rm_ctx[SERVER_3].debtor_id[0] = SERVER_2;
	rm_ctx[SERVER_3].rc_debtors_nr = 1;
}

static void remote_credits_utinit(void)
{
	uint32_t i;

	test_servers_nr = SERVER_NR;
	for (i = 0; i < test_servers_nr; ++i)
		rm_ctx_config(i);

	server_hier_config();
	m0_mutex_init(&rm_ut_tests_chan_mutex);
	m0_chan_init(&rm_ut_tests_chan, &rm_ut_tests_chan_mutex);
	/* Set up test sync points */
	for (i = 0; i < TEST_NR; ++i) {
		m0_clink_init(&tests_clink[i], NULL);
		m0_clink_add_lock(&rm_ut_tests_chan, &tests_clink[i]);
	}
}

static void remote_credits_utfini(void)
{
	uint32_t i;

	/*
	 * Following loops cannot be combined.
	 * The ops within the loops need sync points. Hence they are separate.
	 */
	/* De-construct RM objects hierarchy */
	for (i = 0; i < test_servers_nr; ++i) {
		rm_ctx_server_windup(i);
	}
	/* Disconnect the servers */
	for (i = 0; i < test_servers_nr; ++i) {
		rm_ctx_server_stop(i);
	}
	/* Finalise the servers */
	for (i = 0; i < test_servers_nr; ++i) {
		rm_ctx_fini(&rm_ctx[i]);
	}
	for (i = 0; i < TEST_NR; ++i) {
		m0_clink_del_lock(&tests_clink[i]);
		m0_clink_fini(&tests_clink[i]);
	}
	m0_chan_fini_lock(&rm_ut_tests_chan);
	m0_mutex_fini(&rm_ut_tests_chan_mutex);
}

void remote_credits_test(void)
{
	int rc;
	int i;

	remote_credits_utinit();
	/* Start RM servers */
	for (i = 0; i < test_servers_nr; ++i) {
		rc = M0_THREAD_INIT(&rm_ctx[i].rc_thr, int, NULL,
				    &rm_server_start, i, "rm_server_%d", i);
		M0_UT_ASSERT(rc == 0);
	}

	/* Now start the tests - wait till all the servers are ready */
	m0_chan_signal_lock(&rm_ut_tests_chan);
	for (i = 0; i < test_servers_nr; ++i) {
		m0_thread_join(&rm_ctx[i].rc_thr);
		m0_thread_fini(&rm_ctx[i].rc_thr);
	}
	remote_credits_utfini();
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
