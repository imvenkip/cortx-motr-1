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
 * Original creation date: 08/22/2013
 */

#ifndef __KERNEL__
#include <stdio.h>
#endif
#include "rm/rm.h"
#include "rm/rm_internal.h"
#include "rm/rm_fops.h"
#include "rm/ut/rmut.h"
#include "rm/ut/rings.h"

/* Maximum test servers for this testcase */
static enum rm_server  test_servers_nr;
static struct m0_clink group_tests_clink[GROUP_TESTS_NR];
static uint64_t M0_RM_SNS_GROUP = 1;

static void rmg_in_complete(struct m0_rm_incoming *in, int32_t rc)
{
}

static void rmg_in_conflict(struct m0_rm_incoming *in)
{
}

static const struct m0_rm_incoming_ops rmg_incoming_ops = {
	.rio_complete = rmg_in_complete,
	.rio_conflict = rmg_in_conflict
};

static void ring_get(enum rm_server            srv_id,
		     uint64_t                  group,
		     enum m0_rm_incoming_flags in_flag,
		     int                       which_ring)
{
	struct m0_rm_incoming *in    = &rm_ctx[srv_id].rc_test_data.rd_in;
	struct m0_rm_owner    *owner = rm_ctx[srv_id].rc_test_data.rd_owner;
	int                    rc;

	/*
	 * Test-case - Setup creditor cookie. Group credit request should
	 *             succeed.
	 */
	/* Server-3 is upward creditor for Server with id 'srv_id'*/
	if (srv_id != SERVER_3)
		creditor_cookie_setup(srv_id, SERVER_3);

	m0_rm_incoming_init(in, owner, M0_RIT_LOCAL,
			    RIP_NONE, RIF_LOCAL_WAIT | in_flag);
	in->rin_want.cr_datum = which_ring;
	in->rin_ops = &rmg_incoming_ops;
	in->rin_want.cr_group_id = M0_UINT128(0, group);
	m0_rm_credit_get(in);
	m0_rm_owner_lock(owner);
	rc = m0_sm_timedwait(&in->rin_sm, M0_BITS(RI_SUCCESS), M0_TIME_NEVER);
	m0_rm_owner_unlock(owner);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(in->rin_rc == 0);
	m0_rm_credit_put(in);
	m0_rm_incoming_fini(in);
}

static void standalone_borrow_verify(void)
{
	struct m0_rm_owner *so2 = rm_ctx[SERVER_2].rc_test_data.rd_owner;
	struct m0_rm_owner *so1 = rm_ctx[SERVER_1].rc_test_data.rd_owner;

	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so2->ro_borrowed));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so2->ro_owned[OWOS_CACHED]));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so1->ro_borrowed));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&so1->ro_owned[OWOS_CACHED]));
}

static void standalone_borrow_run(void)
{
	ring_get(SERVER_3, 0, RIF_MAY_BORROW, DURIN);
}

static void group_revoke_verify(void)
{
	struct m0_rm_owner *so3 = rm_ctx[SERVER_3].rc_test_data.rd_owner;
	struct m0_rm_owner *so2 = rm_ctx[SERVER_2].rc_test_data.rd_owner;
	struct m0_rm_owner *so1 = rm_ctx[SERVER_1].rc_test_data.rd_owner;

	M0_UT_ASSERT(m0_rm_ur_tlist_is_empty(&so3->ro_sublet));
	M0_UT_ASSERT(m0_rm_ur_tlist_is_empty(&so2->ro_borrowed));
	M0_UT_ASSERT(m0_rm_ur_tlist_is_empty(&so2->ro_owned[OWOS_CACHED]));
	M0_UT_ASSERT(m0_rm_ur_tlist_is_empty(&so1->ro_borrowed));
	M0_UT_ASSERT(m0_rm_ur_tlist_is_empty(&so1->ro_owned[OWOS_CACHED]));
}

/*
 * Test group revoke
 */
static void group_revoke_run(void)
{
	loan_session_set(SERVER_3, SERVER_1);
	loan_session_set(SERVER_3, SERVER_2);
	ring_get(SERVER_3, 0, RIF_MAY_REVOKE, NENYA);
}

static void group_borrow_verify(enum rm_server srv_id)
{
	struct m0_rm_owner *cso = rm_ctx[SERVER_3].rc_test_data.rd_owner;
	struct m0_rm_owner *dso = rm_ctx[srv_id].rc_test_data.rd_owner;

	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&cso->ro_sublet));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&dso->ro_borrowed));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&dso->ro_owned[OWOS_CACHED]));
}

/*
 * Test group borrow
 */
static void group_borrow_run(enum rm_server srv_id)
{
	ring_get(srv_id, M0_RM_SNS_GROUP, RIF_MAY_BORROW, NENYA);
}

static void server1_tests(void)
{
	m0_chan_wait(&group_tests_clink[GROUP_BORROW_TEST1]);
	group_borrow_run(SERVER_1);
	group_borrow_verify(SERVER_1);

	/* Begin next test */
	m0_chan_signal_lock(&rm_ut_tests_chan);
}

static void server2_tests(void)
{
	m0_chan_wait(&group_tests_clink[GROUP_BORROW_TEST2]);
	group_borrow_run(SERVER_2);
	group_borrow_verify(SERVER_2);

	/* Begin next test */
	m0_chan_signal_lock(&rm_ut_tests_chan);

}

static void server3_tests(void)
{
	m0_chan_wait(&group_tests_clink[STAND_ALONE_BORROW_TEST]);
	standalone_borrow_run();
	standalone_borrow_verify();

	group_revoke_run();
	group_revoke_verify();
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
 * SERVER_1 is downward debtor for SERVER_3 and belongs to group
 *          M0_RM_SNS_GROUP.
 * SERVER_2 is downward debtor for SERVER_3 and also belongs to group
 *          M0_RM_SNS_GROUP.
 * SERVER_3 is upward creditor for SERVER_1 and SERVER_2.
 */
static void server_hier_config(void)
{
	rm_ctx[SERVER_1].creditor_id = SERVER_3;
	rm_ctx[SERVER_1].debtor_id[0] = SERVER_INVALID;
	rm_ctx[SERVER_1].rc_debtors_nr = 1;

	rm_ctx[SERVER_2].creditor_id = SERVER_3;
	rm_ctx[SERVER_2].debtor_id[0] = SERVER_INVALID;
	rm_ctx[SERVER_2].rc_debtors_nr = 1;

	rm_ctx[SERVER_3].creditor_id = SERVER_INVALID;
	rm_ctx[SERVER_3].debtor_id[0] = SERVER_1;
	rm_ctx[SERVER_3].debtor_id[1] = SERVER_2;
	rm_ctx[SERVER_3].rc_debtors_nr = 2;
}

static void rm_group_utinit(void)
{
	uint32_t i;

	test_servers_nr = SERVER_NR;
	for (i = 0; i < test_servers_nr; ++i)
		rm_ctx_config(i);

	server_hier_config();
	m0_mutex_init(&rm_ut_tests_chan_mutex);
	m0_chan_init(&rm_ut_tests_chan, &rm_ut_tests_chan_mutex);

	/* Set up test sync points */
	for (i = 0; i < GROUP_TESTS_NR; ++i) {
		m0_clink_init(&group_tests_clink[i], NULL);
		m0_clink_add_lock(&rm_ut_tests_chan, &group_tests_clink[i]);
	}
}

static void rm_group_utfini(void)
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
	for (i = 0; i < GROUP_TESTS_NR; ++i) {
		m0_clink_del_lock(&group_tests_clink[i]);
		m0_clink_fini(&group_tests_clink[i]);
	}
	m0_chan_fini_lock(&rm_ut_tests_chan);
	m0_mutex_fini(&rm_ut_tests_chan_mutex);
}

void rm_group_test(void)
{
	int rc;
	int i;

	rm_group_utinit();
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
	rm_group_utfini();
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
