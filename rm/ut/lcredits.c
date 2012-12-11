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
 * Original author: Rajesh Bhalerao <rajesh_bhalerao@xyratex.com>
 * Original creation date: 07/19/2012
 */
#include "lib/types.h"            /* uint64_t */
#include "lib/chan.h"
#include "lib/misc.h"
#include "lib/ut.h"
#include "lib/ub.h"

#include "rm/rm.h"
#include "rm/ut/rmut.h"
#include "rm/ut/rings.h"

static struct m0_chan lcredits_chan;

extern bool res_tlist_contains(const struct m0_tl *list,
			       const struct m0_rm_resource *res);

static void lcredits_in_complete(struct m0_rm_incoming *in, int32_t rc)
{
        M0_UT_ASSERT(in != NULL);
        m0_chan_broadcast(&lcredits_chan);
}

static void lcredits_in_conflict(struct m0_rm_incoming *in)
{
}

const struct m0_rm_incoming_ops lcredits_incoming_ops = {
        .rio_complete = lcredits_in_complete,
        .rio_conflict = lcredits_in_conflict
};

static void local_credits_init()
{
	rm_utdata_init(&test_data, OBJ_OWNER);
	rm_test_owner_capital_raise(&test_data.rd_owner, &test_data.rd_credit);
	M0_SET0(&test_data.rd_in);
	m0_chan_init(&lcredits_chan);
}

static void local_credits_fini()
{
	m0_chan_fini(&lcredits_chan);
	rm_utdata_fini(&test_data, OBJ_OWNER);
}

static void cached_credits_test(enum m0_rm_incoming_flags flags)
{
	struct m0_rm_incoming next_in;

	m0_rm_incoming_init(&test_data.rd_in, &test_data.rd_owner,
			    M0_RIT_LOCAL, RIP_NONE, flags);

	m0_rm_credit_init(&test_data.rd_in.rin_want, &test_data.rd_owner);
	test_data.rd_in.rin_want.cr_datum = NENYA;
	test_data.rd_in.rin_ops = &rings_incoming_ops;
	/*
	 * 1. Test obtaining cached credit.
	 */
	m0_rm_credit_get(&test_data.rd_in);
	M0_UT_ASSERT(test_data.rd_in.rin_rc == 0);
	M0_UT_ASSERT(test_data.rd_in.rin_sm.sm_state == RI_SUCCESS);

	M0_SET0(&next_in);
	m0_rm_incoming_init(&next_in, &test_data.rd_owner,
			    M0_RIT_LOCAL, RIP_NONE, flags);
	next_in.rin_want.cr_datum = VILYA;
	next_in.rin_ops = &rings_incoming_ops;

	/*
	 * 2. Test obtaining another cached credit.
	 */
	m0_rm_credit_get(&next_in);
	M0_UT_ASSERT(next_in.rin_rc == 0);
	M0_UT_ASSERT(next_in.rin_sm.sm_state == RI_SUCCESS);

	m0_rm_credit_put(&test_data.rd_in);
	m0_rm_credit_put(&next_in);

	m0_rm_incoming_fini(&test_data.rd_in);
	m0_rm_incoming_fini(&next_in);
}

static void held_credits_test(enum m0_rm_incoming_flags flags)
{
	struct m0_rm_incoming next_in;
	struct m0_clink	      clink;

	M0_SET0(&test_data.rd_in);
	m0_rm_incoming_init(&test_data.rd_in, &test_data.rd_owner,
			    M0_RIT_LOCAL, RIP_NONE, flags);

	m0_rm_credit_init(&test_data.rd_in.rin_want, &test_data.rd_owner);
	test_data.rd_in.rin_want.cr_datum = NENYA;
	test_data.rd_in.rin_ops = &lcredits_incoming_ops;

	m0_rm_credit_get(&test_data.rd_in);
	M0_UT_ASSERT(test_data.rd_in.rin_rc == 0);
	M0_UT_ASSERT(test_data.rd_in.rin_sm.sm_state == RI_SUCCESS);

	M0_SET0(&next_in);
	m0_rm_incoming_init(&next_in, &test_data.rd_owner,
			    M0_RIT_LOCAL, RIP_NONE, flags);
	next_in.rin_want.cr_datum = NENYA;
	next_in.rin_ops = &lcredits_incoming_ops;

	/*
	 * 1. Try to obtain conflicting held credit.
	 */
	m0_rm_credit_get(&next_in);
	M0_UT_ASSERT(ergo(flags == RIF_LOCAL_WAIT,
			  next_in.rin_sm.sm_state == RI_WAIT));
	M0_UT_ASSERT(ergo(flags == RIF_LOCAL_TRY,
			  next_in.rin_sm.sm_state == RI_FAILURE));

	if (flags == RIF_LOCAL_WAIT) {
		m0_clink_init(&clink, NULL);
		m0_clink_add(&lcredits_chan, &clink);
	}

	/* First caller releases the credit */
	m0_rm_credit_put(&test_data.rd_in);

	/*
	 * 2. If the flag is RIF_LOCAL_WAIT, check if we get the credit
	 *    after the first caller releases it.
	 */
	if (flags == RIF_LOCAL_WAIT) {
		M0_UT_ASSERT(m0_chan_timedwait(&clink, !0));
		M0_UT_ASSERT(next_in.rin_rc == 0);
		M0_UT_ASSERT(next_in.rin_sm.sm_state == RI_SUCCESS);
		m0_rm_credit_put(&next_in);
		m0_clink_del(&clink);
		m0_clink_fini(&clink);
	}

	m0_rm_incoming_fini(&test_data.rd_in);
	m0_rm_incoming_fini(&next_in);
}

static void failures_test()
{
	m0_rm_incoming_init(&test_data.rd_in, &test_data.rd_owner,
			    M0_RIT_LOCAL, RIP_NONE, RIF_LOCAL_WAIT);

	m0_rm_credit_init(&test_data.rd_in.rin_want, &test_data.rd_owner);
	test_data.rd_in.rin_ops = &rings_incoming_ops;
	test_data.rd_in.rin_want.cr_datum = INVALID_RING;

	/*
	 * 1. Test - m0_rm_credit_get() with invalid credit (value) fails.
	 */
	m0_rm_credit_get(&test_data.rd_in);
	M0_UT_ASSERT(test_data.rd_in.rin_sm.sm_state == RI_FAILURE);
	M0_UT_ASSERT(test_data.rd_in.rin_rc == -ESRCH);

	/*
	 * 2. Test - credit_get fails when owner is not in ROS_ACTIVE state.
	 */
	m0_rm_incoming_init(&test_data.rd_in, &test_data.rd_owner,
			    M0_RIT_LOCAL, RIP_NONE, RIF_LOCAL_WAIT);
	m0_rm_credit_init(&test_data.rd_in.rin_want, &test_data.rd_owner);
	test_data.rd_in.rin_ops = &rings_incoming_ops;
	test_data.rd_in.rin_want.cr_datum = INVALID_RING;
	test_data.rd_owner.ro_sm.sm_state = ROS_FINALISING;
	m0_rm_credit_get(&test_data.rd_in);
	M0_UT_ASSERT(test_data.rd_in.rin_rc == -ENODEV);
	M0_UT_ASSERT(test_data.rd_in.rin_sm.sm_state == RI_FAILURE);
	test_data.rd_owner.ro_sm.sm_state = ROS_ACTIVE;
}

void local_credits_test()
{
	/*
	 * 1. Get cached credit - successful - WAIT, TRY
	 * 2. Get two non-overlapping cached credits - successful - WAIT, TRY
	 * 3. Get held non-conflicting credit - wait - WAIT, TRY
	 * 4. Get held conflicting credits - WAIT, TRY
	 * 5. Get invalid credit - failure
	 * 6. Owner in non-active state - failure
	 */
	local_credits_init();
	cached_credits_test(RIF_LOCAL_WAIT);
	cached_credits_test(RIF_LOCAL_TRY);
	held_credits_test(RIF_LOCAL_WAIT);
	held_credits_test(RIF_LOCAL_TRY);
	failures_test();
	local_credits_fini();
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
