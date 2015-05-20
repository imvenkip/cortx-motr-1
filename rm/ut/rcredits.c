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
 * Original author: Egor Nikulenkov <egor.nikulenkov@seagate.com>
 * Original creation date: 08/21/2012
 */

#include "lib/chan.h"
#include "lib/semaphore.h"
#include "rm/rm.h"
#include "rm/rm_internal.h"
#include "ut/ut.h"
#include "rm/ut/rmut.h"
#include "rm/ut/rings.h"

/* Maximum test servers for all test cases */
static enum rm_server      test_servers_nr;
static struct m0_semaphore conflict1_sem;
static struct m0_semaphore conflict2_sem;
static struct m0_semaphore conflict3_sem;

enum rcredits_hier_type {
	RCREDITS_HIER_CHAIN,
	RCREDITS_HIER_STAR,
};

static void server1_in_complete(struct m0_rm_incoming *in, int32_t rc)
{
	M0_UT_ASSERT(in != NULL);
	m0_chan_broadcast_lock(&rm_ctxs[SERVER_1].rc_chan);
}

static void server1_in_conflict(struct m0_rm_incoming *in)
{
	m0_semaphore_up(&conflict1_sem);
}

static const struct m0_rm_incoming_ops server1_incoming_ops = {
	.rio_complete = server1_in_complete,
	.rio_conflict = server1_in_conflict
};

static void server2_in_complete(struct m0_rm_incoming *in, int32_t rc)
{
	M0_UT_ASSERT(in != NULL);
	m0_chan_broadcast_lock(&rm_ctxs[SERVER_2].rc_chan);
}

static void server2_in_conflict(struct m0_rm_incoming *in)
{
	m0_semaphore_up(&conflict2_sem);
}

static const struct m0_rm_incoming_ops server2_incoming_ops = {
	.rio_complete = server2_in_complete,
	.rio_conflict = server2_in_conflict
};

static void server3_in_complete(struct m0_rm_incoming *in, int32_t rc)
{
	M0_UT_ASSERT(in != NULL);
	m0_chan_broadcast_lock(&rm_ctxs[SERVER_3].rc_chan);
}

static void server3_in_conflict(struct m0_rm_incoming *in)
{
	m0_semaphore_up(&conflict3_sem);
}

static const struct m0_rm_incoming_ops server3_incoming_ops = {
	.rio_complete = server3_in_complete,
	.rio_conflict = server3_in_conflict
};

static void trick_in_complete(struct m0_rm_incoming *in, int32_t rc)
{
	M0_UT_ASSERT(in != NULL);
	m0_chan_broadcast_lock(&rm_ctxs[SERVER_2].rc_chan);
}

static void trick1_in_conflict(struct m0_rm_incoming *in)
{
	struct m0_rm_incoming *in1 = &rm_ctxs[SERVER_1].rc_test_data.rd_in;
	struct m0_rm_incoming *in2 = &rm_ctxs[SERVER_2].rc_test_data.rd_in;

	in2->rin_reserve.rrp_time = in2->rin_req_time = in1->rin_req_time - 1;
	m0_semaphore_up(&conflict2_sem);
}

static const struct m0_rm_incoming_ops trick1_incoming_ops = {
	.rio_complete = trick_in_complete,
	.rio_conflict = trick1_in_conflict
};

static void trick2_in_conflict(struct m0_rm_incoming *in)
{
	struct m0_rm_incoming *in1 = &rm_ctxs[SERVER_1].rc_test_data.rd_in;
	struct m0_rm_incoming *in2 = &rm_ctxs[SERVER_2].rc_test_data.rd_in;

	in2->rin_reserve.rrp_time = in2->rin_req_time = in1->rin_req_time;
	M0_ASSERT(m0_fid_cmp(&in1->rin_reserve.rrp_owner,
			     &in2->rin_reserve.rrp_owner) < 0);
	m0_semaphore_up(&conflict2_sem);
}

static const struct m0_rm_incoming_ops trick2_incoming_ops = {
	.rio_complete = trick_in_complete,
	.rio_conflict = trick2_in_conflict
};

/*
 * Hierarchy description:
 * SERVER_1 is downward debtor for SERVER_2.
 * SERVER_2 is upward creditor for SERVER_1 and downward debtor for SERVER_3.
 * SERVER_3 is upward creditor for SERVER_2.
 */
static void chain_hier_config(void)
{
	rm_ctxs[SERVER_1].creditor_id = SERVER_2;
	rm_ctxs[SERVER_1].debtor_id[0] = SERVER_INVALID;
	rm_ctxs[SERVER_1].rc_debtors_nr = 1;

	rm_ctxs[SERVER_2].creditor_id = SERVER_3;
	rm_ctxs[SERVER_2].debtor_id[0] = SERVER_1;
	rm_ctxs[SERVER_2].rc_debtors_nr = 1;

	rm_ctxs[SERVER_3].creditor_id = SERVER_INVALID;
	rm_ctxs[SERVER_3].debtor_id[0] = SERVER_2;
	rm_ctxs[SERVER_3].rc_debtors_nr = 1;
}

static void star_hier_config(void)
{
	rm_ctxs[SERVER_1].creditor_id = SERVER_3;
	rm_ctxs[SERVER_1].debtor_id[0] = SERVER_INVALID;
	rm_ctxs[SERVER_1].rc_debtors_nr = 1;

	rm_ctxs[SERVER_2].creditor_id = SERVER_3;
	rm_ctxs[SERVER_2].debtor_id[0] = SERVER_INVALID;
	rm_ctxs[SERVER_2].rc_debtors_nr = 1;

	rm_ctxs[SERVER_3].creditor_id = SERVER_INVALID;
	rm_ctxs[SERVER_3].debtor_id[0] = SERVER_1;
	rm_ctxs[SERVER_3].debtor_id[1] = SERVER_2;
	rm_ctxs[SERVER_3].rc_debtors_nr = 2;
}

static void remote_credits_utinit(enum rcredits_hier_type hier_type)
{
	uint32_t i;

	M0_PRE(M0_IN(hier_type, (RCREDITS_HIER_CHAIN, RCREDITS_HIER_STAR)));
	test_servers_nr = 3;
	for (i = 0; i < test_servers_nr; ++i)
		rm_ctx_init(&rm_ctxs[i]);

	if (hier_type == RCREDITS_HIER_CHAIN)
		chain_hier_config();
	else if (hier_type == RCREDITS_HIER_STAR)
		star_hier_config();

	/* Start RM servers */
	for (i = 0; i < test_servers_nr; ++i) {
		rings_utdata_ops_set(&rm_ctxs[i].rc_test_data);
		rm_ctx_server_start(i);
	}

	/* Set creditors cookie, so incoming RM service requests
	 * will be handled by owners stored in rm_ctxs[] context */
	if (hier_type == RCREDITS_HIER_CHAIN) {
		creditor_cookie_setup(SERVER_1, SERVER_2);
	} else if (hier_type == RCREDITS_HIER_STAR) {
		creditor_cookie_setup(SERVER_1, SERVER_3);
	}
	creditor_cookie_setup(SERVER_2, SERVER_3);
}

static void remote_credits_utfini(void)
{
	int32_t i;

	/*
	 * Following loops cannot be combined.
	 * The ops within the loops need sync points. Hence they are separate.
	 */
	/* De-construct RM objects hierarchy */
	for (i = test_servers_nr - 1; i >= 0; --i)
		rm_ctx_server_windup(i);

	/* Disconnect the servers */
	for (i = test_servers_nr - 1; i >= 0; --i)
		rm_ctx_server_stop(i);

	/*
	 * Finalise the servers. Must be done in the reverse order, so that the
	 * first initialised reqh is finalised last.
	 */
	for (i = test_servers_nr - 1; i >= 0; --i)
		rm_ctx_fini(&rm_ctxs[i]);
}

static void credit_setup(enum rm_server            srv_id,
			 enum m0_rm_incoming_flags flag,
			 uint64_t                  value)
{
	struct m0_rm_incoming *in    = &rm_ctxs[srv_id].rc_test_data.rd_in;
	struct m0_rm_owner    *owner = rm_ctxs[srv_id].rc_test_data.rd_owner;

	m0_rm_incoming_init(in, owner, M0_RIT_LOCAL, RINGS_RIP, flag);
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
		M0_IMPOSSIBLE("Invalid server id");
		break;
	}
}

static void credit_get_and_hold_nowait(enum rm_server            debtor_id,
				       enum m0_rm_incoming_flags flags,
				       uint64_t                  credit_value)
{
	struct m0_rm_incoming *in = &rm_ctxs[debtor_id].rc_test_data.rd_in;

	credit_setup(debtor_id, flags, credit_value);
	m0_rm_credit_get(in);
}

static void wait_for_credit(enum rm_server srv_id)
{
	struct m0_rm_incoming *in = &rm_ctxs[srv_id].rc_test_data.rd_in;

	m0_chan_wait(&rm_ctxs[srv_id].rc_clink);
	M0_UT_ASSERT(incoming_state(in) == RI_SUCCESS);
	M0_UT_ASSERT(in->rin_rc == 0);
}

static void credit_get_and_hold(enum rm_server            debtor_id,
				enum m0_rm_incoming_flags flags,
				uint64_t                  credit_value)
{
	credit_get_and_hold_nowait(debtor_id, flags, credit_value);
	wait_for_credit(debtor_id);
}

static void held_credit_cache(enum rm_server srv_id)
{
	struct m0_rm_incoming *in = &rm_ctxs[srv_id].rc_test_data.rd_in;

	M0_ASSERT(!m0_rm_ur_tlist_is_empty(
		&rm_ctxs[srv_id].rc_test_data.rd_owner->ro_owned[OWOS_HELD]));
	m0_rm_credit_put(in);
	m0_rm_incoming_fini(in);
}

static void credit_get_and_cache(enum rm_server            debtor_id,
				 enum m0_rm_incoming_flags flags,
				 uint64_t                  credit_value)
{
	credit_get_and_hold(debtor_id, flags, credit_value);
	held_credit_cache(debtor_id);
}

static void borrow_and_cache(enum rm_server debtor_id,
			     uint64_t       credit_value)
{
	return credit_get_and_cache(debtor_id, RIF_MAY_BORROW, credit_value);
}

static void borrow_and_hold(enum rm_server debtor_id,
			    uint64_t       credit_value)
{
	return credit_get_and_hold(debtor_id, RIF_MAY_BORROW, credit_value);
}

static void test_two_borrows_single_req(void)
{
	remote_credits_utinit(RCREDITS_HIER_CHAIN);

	/* Get NENYA from Server-3 to Server-2 */
	borrow_and_cache(SERVER_2, NENYA);

	/* Get NENYA from Server-2 and DURIN from Server-3 via Server-2 */
	borrow_and_cache(SERVER_1, NENYA | DURIN);

	credits_are_equal(SERVER_2, RCL_SUBLET,   NENYA | DURIN);
	credits_are_equal(SERVER_2, RCL_BORROWED, NENYA | DURIN);
	credits_are_equal(SERVER_2, RCL_CACHED,   0);
	credits_are_equal(SERVER_1, RCL_BORROWED, NENYA | DURIN);
	credits_are_equal(SERVER_1, RCL_CACHED,   NENYA | DURIN);
	credits_are_equal(SERVER_3, RCL_CACHED,   ALLRINGS & ~NENYA & ~DURIN);

	remote_credits_utfini();
}

static void test_borrow_revoke_single_req(void)
{
	remote_credits_utinit(RCREDITS_HIER_CHAIN);

	/* Get NENYA from Server-3 to Server-1 */
	borrow_and_cache(SERVER_1, NENYA);

	/*
	 * NENYA is on SERVER_1. VILYA is on SERVER_3.
	 * Make sure both borrow and revoke succeed in a single request.
	 */
	credit_get_and_cache(SERVER_2, RIF_MAY_REVOKE | RIF_MAY_BORROW,
		    NENYA | VILYA);

	credits_are_equal(SERVER_3, RCL_SUBLET,   NENYA | VILYA);
	credits_are_equal(SERVER_2, RCL_BORROWED, NENYA | VILYA);
	credits_are_equal(SERVER_2, RCL_CACHED,   NENYA | VILYA);
	credits_are_equal(SERVER_1, RCL_BORROWED, 0);
	credits_are_equal(SERVER_1, RCL_CACHED,   0);

	remote_credits_utfini();
}

static void test_revoke_with_hop(void)
{
	/*
	 * Test case checks credit revoking through intermediate owner.
	 */
	remote_credits_utinit(RCREDITS_HIER_CHAIN);

	/* ANGMAR is on SERVER_3, SERVER_1 borrows it through SERVER_2 */
	borrow_and_cache(SERVER_1, ANGMAR);

	/* ANGMAR is revoked from SERVER_1 through SERVER_2 */
	credit_get_and_cache(SERVER_3, RIF_MAY_REVOKE, ANGMAR);

	credits_are_equal(SERVER_1, RCL_BORROWED, 0);
	credits_are_equal(SERVER_1, RCL_CACHED,   0);
	credits_are_equal(SERVER_3, RCL_SUBLET,   0);
	credits_are_equal(SERVER_3, RCL_CACHED,   ALLRINGS);

	remote_credits_utfini();
}

static void test_no_borrow_flag(void)
{
	struct m0_rm_incoming *in = &rm_ctxs[SERVER_2].rc_test_data.rd_in;

	remote_credits_utinit(RCREDITS_HIER_CHAIN);

	/*
	 * Don't specify RIF_MAY_BORROW flag.
	 * We should get the error -EREMOTE as NENYA is on SERVER_3.
	 */
	credit_setup(SERVER_2, RIF_LOCAL_WAIT, NENYA);
	m0_rm_credit_get(in);
	m0_chan_wait(&rm_ctxs[SERVER_2].rc_clink);
	M0_UT_ASSERT(incoming_state(in) == RI_FAILURE);
	M0_UT_ASSERT(in->rin_rc == -EREMOTE);
	m0_rm_incoming_fini(in);

	remote_credits_utfini();
}

static void test_simple_borrow(void)
{
	remote_credits_utinit(RCREDITS_HIER_CHAIN);

	/* Get NENYA from SERVER_3 to SERVER_2 */
	borrow_and_cache(SERVER_2, NENYA);

	credits_are_equal(SERVER_3, RCL_SUBLET,   NENYA);
	credits_are_equal(SERVER_3, RCL_CACHED,   ALLRINGS & ~NENYA);
	credits_are_equal(SERVER_2, RCL_BORROWED, NENYA);
	credits_are_equal(SERVER_2, RCL_CACHED,   NENYA);

	remote_credits_utfini();
}

static void test_borrow_non_conflicting(void)
{
	remote_credits_utinit(RCREDITS_HIER_CHAIN);

	borrow_and_cache(SERVER_2, SHARED_RING);
	credits_are_equal(SERVER_2, RCL_CACHED,   SHARED_RING);
	credits_are_equal(SERVER_2, RCL_BORROWED, SHARED_RING);
	credits_are_equal(SERVER_3, RCL_SUBLET,   SHARED_RING);

	remote_credits_utfini();
}

static void test_revoke_conflicting_wait(void)
{
	remote_credits_utinit(RCREDITS_HIER_CHAIN);
	m0_semaphore_init(&conflict2_sem, 0);

	borrow_and_hold(SERVER_2, NENYA);
	credits_are_equal(SERVER_2, RCL_HELD, NENYA);

	credit_get_and_hold_nowait(SERVER_3, RIF_MAY_REVOKE | RIF_LOCAL_WAIT,
				   NENYA);
	m0_semaphore_timeddown(&conflict2_sem, m0_time_from_now(60, 0));
	held_credit_cache(SERVER_2);
	wait_for_credit(SERVER_3);
	held_credit_cache(SERVER_3);

	credits_are_equal(SERVER_2, RCL_HELD,     0);
	credits_are_equal(SERVER_2, RCL_BORROWED, 0);
	credits_are_equal(SERVER_3, RCL_CACHED,   ALLRINGS);

	m0_semaphore_fini(&conflict2_sem);
	remote_credits_utfini();
}

static void test_revoke_conflicting_try(void)
{
	struct m0_rm_incoming *in3 = &rm_ctxs[SERVER_3].rc_test_data.rd_in;

	remote_credits_utinit(RCREDITS_HIER_CHAIN);

	borrow_and_hold(SERVER_2, NENYA);
	credits_are_equal(SERVER_2, RCL_HELD, NENYA);

	credit_get_and_hold_nowait(SERVER_3, RIF_MAY_REVOKE | RIF_LOCAL_TRY,
				   NENYA);
	m0_chan_wait(&rm_ctxs[SERVER_3].rc_clink);
	M0_UT_ASSERT(incoming_state(in3) == RI_FAILURE);
	M0_UT_ASSERT(in3->rin_rc == -EBUSY);

	credits_are_equal(SERVER_2, RCL_HELD,     NENYA);
	credits_are_equal(SERVER_3, RCL_CACHED,   ALLRINGS & ~NENYA);

	held_credit_cache(SERVER_2);
	remote_credits_utfini();
}

static void test_revoke_no_conflict_wait(void)
{
	remote_credits_utinit(RCREDITS_HIER_CHAIN);
	m0_semaphore_init(&conflict2_sem, 0);

	borrow_and_hold(SERVER_2, SHARED_RING);
	credits_are_equal(SERVER_2, RCL_HELD, SHARED_RING);

	credit_get_and_hold_nowait(SERVER_3, RIF_MAY_REVOKE, SHARED_RING);
	m0_semaphore_timeddown(&conflict2_sem, m0_time_from_now(60, 0));
	held_credit_cache(SERVER_2);
	wait_for_credit(SERVER_3);

	credits_are_equal(SERVER_2, RCL_CACHED,   0);
	credits_are_equal(SERVER_2, RCL_BORROWED, 0);
	credits_are_equal(SERVER_3, RCL_HELD,     SHARED_RING);
	credits_are_equal(SERVER_3, RCL_CACHED,   ALLRINGS & ~SHARED_RING);

	held_credit_cache(SERVER_3);
	m0_semaphore_fini(&conflict2_sem);
	remote_credits_utfini();
}

static void test_revoke_no_conflict_try(void)
{
	struct m0_rm_incoming *in3 = &rm_ctxs[SERVER_3].rc_test_data.rd_in;

	remote_credits_utinit(RCREDITS_HIER_CHAIN);

	borrow_and_hold(SERVER_2, SHARED_RING);
	credits_are_equal(SERVER_2, RCL_HELD, SHARED_RING);

	credit_get_and_hold_nowait(SERVER_3, RIF_MAY_REVOKE | RIF_LOCAL_TRY,
				   SHARED_RING);
	m0_chan_wait(&rm_ctxs[SERVER_3].rc_clink);
	M0_UT_ASSERT(incoming_state(in3) == RI_FAILURE);
	M0_UT_ASSERT(in3->rin_rc == -EBUSY);

	credits_are_equal(SERVER_2, RCL_HELD,     SHARED_RING);
	credits_are_equal(SERVER_3, RCL_CACHED,   ALLRINGS & ~SHARED_RING);

	held_credit_cache(SERVER_2);
	remote_credits_utfini();
}

static void test_borrow_held_no_conflict(void)
{
	remote_credits_utinit(RCREDITS_HIER_CHAIN);

	borrow_and_hold(SERVER_2, NENYA);
	credits_are_equal(SERVER_2, RCL_HELD, NENYA);

	/*
	 * Held non-conflicting credits shouldn't be borrowed.
	 */
	borrow_and_cache(SERVER_1, ANY_RING);

	credits_are_equal(SERVER_2, RCL_HELD,     NENYA);
	credits_are_equal(SERVER_2, RCL_BORROWED, NENYA | NARYA);
	credits_are_equal(SERVER_1, RCL_BORROWED, NARYA);
	credits_are_equal(SERVER_1, RCL_CACHED,   NARYA);
	credits_are_equal(SERVER_3, RCL_CACHED,   ALLRINGS & ~NENYA & ~NARYA);

	held_credit_cache(SERVER_2);
	remote_credits_utfini();
}

static void test_borrow_held_conflicting(void)
{
	remote_credits_utinit(RCREDITS_HIER_CHAIN);

	m0_semaphore_init(&conflict2_sem, 0);
	borrow_and_hold(SERVER_2, NENYA);
	credits_are_equal(SERVER_2, RCL_HELD, NENYA);

	credit_get_and_hold_nowait(SERVER_1, RIF_MAY_BORROW, NENYA);
	m0_semaphore_timeddown(&conflict2_sem, m0_time_from_now(60, 0));
	held_credit_cache(SERVER_2);
	wait_for_credit(SERVER_1);

	credits_are_equal(SERVER_2, RCL_HELD,     0);
	credits_are_equal(SERVER_2, RCL_BORROWED, NENYA);
	credits_are_equal(SERVER_1, RCL_HELD,     NENYA);
	credits_are_equal(SERVER_1, RCL_BORROWED, NENYA);
	credits_are_equal(SERVER_3, RCL_CACHED,   ALLRINGS & ~NENYA);

	held_credit_cache(SERVER_1);
	m0_semaphore_fini(&conflict2_sem);
	remote_credits_utfini();
}

static void test_starvation(void)
{
	enum {
		CREDIT_GET_LOOPS = 10,
	};
	int i;

	/*
	 * Test idea is to simulate starvation for incoming request, when
	 * two credits satisfying this request are constantly borrowed/revoked,
	 * but not owned together at any given time.
	 */
	remote_credits_utinit(RCREDITS_HIER_CHAIN);
	m0_semaphore_init(&conflict1_sem, 0);
	m0_semaphore_init(&conflict2_sem, 0);

	borrow_and_hold(SERVER_1, NARYA);
	credits_are_equal(SERVER_1, RCL_HELD, NARYA);
	borrow_and_hold(SERVER_2, NENYA);
	credits_are_equal(SERVER_2, RCL_HELD, NENYA);

	credit_get_and_hold_nowait(SERVER_3, RIF_MAY_REVOKE, ALLRINGS);

	for (i = 0; i < CREDIT_GET_LOOPS; i++) {
		/*
		 * Do cache/hold cycle for NARYA and NENYA, so SERVER_3 will
		 * check incoming request for ALLRINGS constantly after each
		 * successful revoke. But SERVER_3 owns either NARYA or NENYA at
		 * a given time, not both.
		 */
		m0_semaphore_timeddown(&conflict1_sem, m0_time_from_now(10, 0));
		held_credit_cache(SERVER_1);
		credits_are_equal(SERVER_1, RCL_HELD,   0);
		credits_are_equal(SERVER_1, RCL_CACHED, 0);
		credit_get_and_hold(SERVER_1, RIF_MAY_BORROW | RIF_MAY_REVOKE,
				    NARYA);
		credits_are_equal(SERVER_1, RCL_HELD, NARYA);

		m0_semaphore_timeddown(&conflict2_sem, m0_time_from_now(10, 0));
		held_credit_cache(SERVER_2);
		credits_are_equal(SERVER_2, RCL_HELD,   0);
		credits_are_equal(SERVER_2, RCL_CACHED, 0);
		credit_get_and_hold(SERVER_2, RIF_MAY_BORROW | RIF_MAY_REVOKE,
				    NENYA);
		credits_are_equal(SERVER_2, RCL_HELD, NENYA);
	}
	/* Request for all rings is not satisfied yet */
	M0_UT_ASSERT(!m0_chan_trywait(&rm_ctxs[SERVER_3].rc_clink));
	held_credit_cache(SERVER_1);
	held_credit_cache(SERVER_2);
	wait_for_credit(SERVER_3);
	credits_are_equal(SERVER_3, RCL_HELD, ALLRINGS);
	held_credit_cache(SERVER_3);

	m0_semaphore_fini(&conflict1_sem);
	m0_semaphore_fini(&conflict2_sem);
	remote_credits_utfini();
}

static void test_barrier(void)
{
	remote_credits_utinit(RCREDITS_HIER_CHAIN);
	m0_semaphore_init(&conflict1_sem, 0);
	m0_semaphore_init(&conflict2_sem, 0);

	/*
	 * Test checks that credits suitable for request with RIF_RESERVE
	 * flag are reserved, so they are not granted to other requests without
	 * RIF_RESERVE flag.
	 */
	borrow_and_hold(SERVER_1, NARYA);
	credits_are_equal(SERVER_1, RCL_HELD, NARYA);
	borrow_and_hold(SERVER_2, NENYA);
	credits_are_equal(SERVER_2, RCL_HELD, NENYA);

	credit_get_and_hold_nowait(SERVER_3, RIF_MAY_REVOKE | RIF_RESERVE,
				   ALLRINGS);

	m0_semaphore_timeddown(&conflict1_sem, m0_time_from_now(10, 0));
	held_credit_cache(SERVER_1);
	credits_are_equal(SERVER_1, RCL_HELD,   0);
	credits_are_equal(SERVER_1, RCL_CACHED, 0);
	/*
	 * Try to get NARYA back, while NENYA is on SERVER_2. Normally
	 * request would succeed, but SERVER_3 use RIF_RESERVE
	 * flag forcing NARYA revocation and holding on SERVER_3 until
	 * request for all rings is granted.
	 */
	credit_get_and_hold_nowait(SERVER_1,
			RIF_MAY_BORROW | RIF_MAY_REVOKE, NARYA);
	/* Request is not satisfied within 1 second */
	M0_UT_ASSERT(!m0_chan_timedwait(&rm_ctxs[SERVER_1].rc_clink,
					m0_time_from_now(1, 0)));
	credits_are_equal(SERVER_3, RCL_CACHED, ALLRINGS & ~NENYA);

	m0_semaphore_timeddown(&conflict2_sem, m0_time_from_now(10, 0));
	held_credit_cache(SERVER_2);
	credits_are_equal(SERVER_2, RCL_HELD,   0);
	credits_are_equal(SERVER_2, RCL_CACHED, 0);

	/* Credits for all rings are granted */
	wait_for_credit(SERVER_3);
	credits_are_equal(SERVER_3, RCL_HELD, ALLRINGS);

	/* Allow request for NARYA to complete */
	held_credit_cache(SERVER_3);
	wait_for_credit(SERVER_1);
	credits_are_equal(SERVER_1, RCL_HELD, NARYA);
	held_credit_cache(SERVER_1);

	m0_semaphore_fini(&conflict1_sem);
	m0_semaphore_fini(&conflict2_sem);
	remote_credits_utfini();
}

static void test_barrier_overcome(enum rcredits_hier_type hier)
{
	struct m0_rm_incoming  trick_in;
	struct m0_rm_owner    *owner2;

	remote_credits_utinit(hier);
	m0_semaphore_init(&conflict2_sem, 0);
	m0_semaphore_init(&conflict3_sem, 0);

	/*
	 * Test checks that barrier (M0_RPF_BARRIER) set for credit can be
	 * overcome by request with smaller origination timestamp.
	 *
	 * Credit flow diagram to check in case of star topology:
	 * SERVER_1                    SERVER_3                    SERVER_2
	 *    |                        ---|                           |
	 *    |             hold NENYA |  |                           |
	 *    |                        -->|                           |
	 *    |                           |                        ---|
	 *    |                           |                  NENYA |  |
	 *    |                           |                        -->|
	 *    |---                        |                           |
	 *    |  | NENYA                  |                           |
	 *    |<--                        |                           |
	 *    |        borrow NENYA       |                           |
	 *    |-------------------------->|                           |
	 *    |                           |        borrow NENYA       |
	 *    |                           |<--------------------------|
	 *    |                        ---|                           |
	 *    |          release NENYA |  |                           |
	 *    |                        -->|                           |
	 *    |                           |        grant NENYA        |
	 *    |                           |-------------------------->|
	 *
	 * SERVER_3 grants NENYA to SERVER_2 instead of SERVER_1 because
	 * local request for NENYA was earlier on SERVER_2.
	 *
	 * Special trick is implemented to simulate such events ordering.
	 * Actually local request for NENYA on SERVER_2 is issued later than on
	 * SERVER_1, but timestamp is overridden in trick1_in_conflict()
	 * callback. In order to invoke this callback SERVER_2 gets NARYA and
	 * after that NARYA | NENYA, therefore provoking conflict.
	 *
	 * Chain topology case:
	 * SERVER_1                    SERVER_2                    SERVER_3
	 *    |                           |                        ---|
	 *    |                           |             hold NENYA |  |
	 *    |                           |                        -->|
	 *    |                           |---                        |
	 *    |                           |  | NENYA (t2)             |
	 *    |                           |<--                        |
	 *    |        borrow NENYA (t1)  |                           |
	 *    |-------------------------->|                           |
	 *    |                           |     borrow NENYA (t1)     |
	 *    |                           |-------------------------->|
	 *    |                           |                        ---|
	 *    |                           |          release NENYA |  |
	 *    |                           |                        -->|
	 *    |                           |     grant NENYA           |
	 *    |                           |<--------------------------|
	 *    |                           |---                        |
	 *    |                           |  | grant NENYA            |
	 *    |                           |<--                        |
	 *
	 * On diagram above t1, t2 denotes timestamps of original local request.
	 * Because of t2<t1 SERVER_2 local request is granted before borrow
	 * request from SERVER_1.
	 */

	/* Hold NARYA to make trick with timestamp overwrite */
	owner2 = rm_ctxs[SERVER_2].rc_test_data.rd_owner;
	m0_rm_incoming_init(&trick_in, owner2, M0_RIT_LOCAL, RINGS_RIP,
			    RIF_MAY_BORROW);
	trick_in.rin_want.cr_datum = NARYA;
	trick_in.rin_ops = &trick1_incoming_ops;
	m0_rm_credit_get(&trick_in);
	m0_chan_wait(&rm_ctxs[SERVER_2].rc_clink);
	credits_are_equal(SERVER_2, RCL_HELD, NARYA);

	credit_get_and_hold(SERVER_3, 0, NENYA);
	credit_get_and_hold_nowait(SERVER_1,
			RIF_MAY_BORROW | RIF_MAY_REVOKE | RIF_RESERVE, NENYA);
	m0_semaphore_timeddown(&conflict3_sem, m0_time_from_now(10, 0));
	credit_get_and_hold_nowait(SERVER_2,
			RIF_MAY_BORROW | RIF_LOCAL_WAIT | RIF_RESERVE,
			NARYA | NENYA);

	m0_semaphore_timeddown(&conflict2_sem, m0_time_from_now(10, 0));
	m0_rm_credit_put(&trick_in);
	m0_rm_incoming_fini(&trick_in);
	credits_are_equal(SERVER_2, RCL_HELD,   0);
	credits_are_equal(SERVER_2, RCL_CACHED, NARYA);

	/*
	 * In case of chain topology only one borrow request is sent, so
	 * conflict callback called only once on SERVER_3.
	 */
	if (hier == RCREDITS_HIER_STAR)
		m0_semaphore_timeddown(&conflict3_sem, m0_time_from_now(10, 0));
	held_credit_cache(SERVER_3);
	wait_for_credit(SERVER_2);
	credits_are_equal(SERVER_2, RCL_HELD, NARYA | NENYA);
	M0_UT_ASSERT(!m0_chan_trywait(&rm_ctxs[SERVER_1].rc_clink));
	held_credit_cache(SERVER_2);
	wait_for_credit(SERVER_1);
	held_credit_cache(SERVER_1);
	credits_are_equal(SERVER_1, RCL_CACHED, NENYA);
	credits_are_equal(SERVER_2, RCL_CACHED, NARYA);

	m0_semaphore_fini(&conflict2_sem);
	m0_semaphore_fini(&conflict3_sem);
	remote_credits_utfini();
}

static void test_barrier_overcome_star(void)
{
	test_barrier_overcome(RCREDITS_HIER_STAR);
}

static void test_barrier_overcome_chain(void)
{
	test_barrier_overcome(RCREDITS_HIER_CHAIN);
}

static void test_barrier_same_time(void)
{
	struct m0_rm_incoming  trick_in;
	struct m0_rm_owner    *owner2;

	remote_credits_utinit(RCREDITS_HIER_STAR);
	m0_semaphore_init(&conflict2_sem, 0);
	m0_semaphore_init(&conflict3_sem, 0);

	/*
	 * Test checks that if timestamps of two requests with RIF_RESERVE flag
	 * are equal, then credit is reserved for request originated by	owner
	 * with smaller fid.
	 *
	 * Credits flow is similar to test_barrier_overcome_star(), but in this
	 * test SERVER_1 is granted NENYA first. Also similar trick is used to
	 * set equal timestamps for two requests.
	 */
	owner2 = rm_ctxs[SERVER_2].rc_test_data.rd_owner;
	m0_rm_incoming_init(&trick_in, owner2, M0_RIT_LOCAL, RINGS_RIP,
			    RIF_MAY_BORROW);
	trick_in.rin_want.cr_datum = NARYA;
	trick_in.rin_ops = &trick2_incoming_ops;
	m0_rm_credit_get(&trick_in);
	m0_chan_wait(&rm_ctxs[SERVER_2].rc_clink);
	credits_are_equal(SERVER_2, RCL_HELD, NARYA);

	credit_get_and_hold(SERVER_3, 0, NENYA);
	credit_get_and_hold_nowait(SERVER_1,
			RIF_MAY_BORROW | RIF_MAY_REVOKE | RIF_RESERVE, NENYA);
	m0_semaphore_timeddown(&conflict3_sem, m0_time_from_now(10, 0));
	credit_get_and_hold_nowait(SERVER_2,
		RIF_MAY_BORROW | RIF_MAY_REVOKE | RIF_LOCAL_WAIT | RIF_RESERVE,
		NARYA | NENYA);

	m0_semaphore_timeddown(&conflict2_sem, m0_time_from_now(10, 0));
	m0_rm_credit_put(&trick_in);
	m0_rm_incoming_fini(&trick_in);
	credits_are_equal(SERVER_2, RCL_HELD,   0);
	credits_are_equal(SERVER_2, RCL_CACHED, NARYA);

	m0_semaphore_timeddown(&conflict3_sem, m0_time_from_now(10, 0));
	held_credit_cache(SERVER_3);
	wait_for_credit(SERVER_1);
	credits_are_equal(SERVER_1, RCL_HELD,   NENYA);
	M0_UT_ASSERT(!m0_chan_trywait(&rm_ctxs[SERVER_2].rc_clink));
	held_credit_cache(SERVER_1);
	wait_for_credit(SERVER_2);
	held_credit_cache(SERVER_2);
	credits_are_equal(SERVER_1, RCL_CACHED, 0);
	credits_are_equal(SERVER_2, RCL_CACHED, NARYA | NENYA);

	m0_semaphore_fini(&conflict2_sem);
	m0_semaphore_fini(&conflict3_sem);
	remote_credits_utfini();
}

struct m0_ut_suite rm_rcredits_ut = {
	.ts_name = "rm-rcredits-ut",
	.ts_tests = {
		{ "no-borrow-flag"           , test_no_borrow_flag },
		{ "simple-borrow"            , test_simple_borrow },
		{ "two-borrows-single-req"   , test_two_borrows_single_req },
		{ "borrow-revoke-single-req" , test_borrow_revoke_single_req },
		{ "revoke-with-hop"          , test_revoke_with_hop },
		{ "revoke-conflicting-wait"  , test_revoke_conflicting_wait },
		{ "revoke-conflicting-try"   , test_revoke_conflicting_try },
		{ "borrow-non-conflicting"   , test_borrow_non_conflicting },
		{ "revoke-no-conflict-wait"  , test_revoke_no_conflict_wait },
		{ "revoke-no-conflict-try"   , test_revoke_no_conflict_try },
		{ "borrow-held-no-conflict"  , test_borrow_held_no_conflict },
		{ "borrow-held-conflicting"  , test_borrow_held_conflicting },
		{ "starvation"               , test_starvation },
		{ "barrier"                  , test_barrier },
		{ "barrier-overcome-star"    , test_barrier_overcome_star },
		{ "barrier-overcome-chain"   , test_barrier_overcome_chain },
		{ "barrier-same-time"        , test_barrier_same_time },
		{ NULL, NULL }
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
