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

static struct c2_chan lrights_chan;

extern bool res_tlist_contains(const struct c2_tl *list,
			       const struct c2_rm_resource *res);

static void lrights_in_complete(struct c2_rm_incoming *in, int32_t rc)
{
        C2_UT_ASSERT(in != NULL);
        c2_chan_broadcast(&lrights_chan);
}

static void lrights_in_conflict(struct c2_rm_incoming *in)
{
}

const struct c2_rm_incoming_ops lrights_incoming_ops = {
        .rio_complete = lrights_in_complete,
        .rio_conflict = lrights_in_conflict
};

static void local_rights_init()
{
	rm_utdata_init(&test_data, OBJ_OWNER);
	rm_test_owner_capital_raise(&test_data.rd_owner, &test_data.rd_right);
	C2_SET0(&test_data.rd_in);
	c2_chan_init(&lrights_chan);
}

static void local_rights_fini()
{
	c2_chan_fini(&lrights_chan);
	rm_utdata_fini(&test_data, OBJ_OWNER);
}

static void cached_rights_test(enum c2_rm_incoming_flags flags)
{
	struct c2_rm_incoming next_in;

	c2_rm_incoming_init(&test_data.rd_in, &test_data.rd_owner,
			    C2_RIT_LOCAL, RIP_NONE, flags);

	c2_rm_right_init(&test_data.rd_in.rin_want, &test_data.rd_owner);
	test_data.rd_in.rin_want.ri_datum = NENYA;
	test_data.rd_in.rin_ops = &rings_incoming_ops;
	/*
	 * 1. Test obtaining cached right.
	 */
	c2_rm_right_get(&test_data.rd_in);
	C2_UT_ASSERT(test_data.rd_in.rin_sm.sm_rc == 0);
	C2_UT_ASSERT(test_data.rd_in.rin_sm.sm_state == RI_SUCCESS);

	C2_SET0(&next_in);
	c2_rm_incoming_init(&next_in, &test_data.rd_owner,
			    C2_RIT_LOCAL, RIP_NONE, flags);
	next_in.rin_want.ri_datum = VILYA;
	next_in.rin_ops = &rings_incoming_ops;

	/*
	 * 2. Test obtaining another cached right.
	 */
	c2_rm_right_get(&next_in);
	C2_UT_ASSERT(next_in.rin_sm.sm_rc == 0);
	C2_UT_ASSERT(next_in.rin_sm.sm_state == RI_SUCCESS);

	c2_rm_right_put(&test_data.rd_in);
	c2_rm_right_put(&next_in);

	c2_rm_incoming_fini(&test_data.rd_in);
	c2_rm_incoming_fini(&next_in);
}

static void held_rights_test(enum c2_rm_incoming_flags flags)
{
	struct c2_rm_incoming next_in;
	struct c2_clink	      clink;

	C2_SET0(&test_data.rd_in);
	c2_rm_incoming_init(&test_data.rd_in, &test_data.rd_owner,
			    C2_RIT_LOCAL, RIP_NONE, flags);

	c2_rm_right_init(&test_data.rd_in.rin_want, &test_data.rd_owner);
	test_data.rd_in.rin_want.ri_datum = NENYA;
	test_data.rd_in.rin_ops = &lrights_incoming_ops;

	c2_rm_right_get(&test_data.rd_in);
	C2_UT_ASSERT(test_data.rd_in.rin_sm.sm_rc == 0);
	C2_UT_ASSERT(test_data.rd_in.rin_sm.sm_state == RI_SUCCESS);

	C2_SET0(&next_in);
	c2_rm_incoming_init(&next_in, &test_data.rd_owner,
			    C2_RIT_LOCAL, RIP_NONE, flags);
	next_in.rin_want.ri_datum = NENYA;
	next_in.rin_ops = &lrights_incoming_ops;

	/*
	 * 1. Try to obtain conflicting held right.
	 */
	c2_rm_right_get(&next_in);
	C2_UT_ASSERT(ergo(flags == RIF_LOCAL_WAIT,
			  next_in.rin_sm.sm_state == RI_WAIT));
	C2_UT_ASSERT(ergo(flags == RIF_LOCAL_TRY,
			  next_in.rin_sm.sm_state == RI_FAILURE));

	if (flags == RIF_LOCAL_WAIT) {
		c2_clink_init(&clink, NULL);
		c2_clink_add(&lrights_chan, &clink);
	}

	/* First caller releases the right */
	c2_rm_right_put(&test_data.rd_in);

	/*
	 * 2. If the flag is RIF_LOCAL_WAIT, check if we get the right
	 *    after the first caller releases it.
	 */
	if (flags == RIF_LOCAL_WAIT) {
		C2_UT_ASSERT(c2_chan_timedwait(&clink, !0));
		C2_UT_ASSERT(next_in.rin_sm.sm_rc == 0);
		C2_UT_ASSERT(next_in.rin_sm.sm_state == RI_SUCCESS);
		c2_rm_right_put(&next_in);
		c2_clink_del(&clink);
		c2_clink_fini(&clink);
	}

	c2_rm_incoming_fini(&test_data.rd_in);
	c2_rm_incoming_fini(&next_in);
}

static void failures_test()
{
	c2_rm_incoming_init(&test_data.rd_in, &test_data.rd_owner,
			    C2_RIT_LOCAL, RIP_NONE, RIF_LOCAL_WAIT);

	c2_rm_right_init(&test_data.rd_in.rin_want, &test_data.rd_owner);
	test_data.rd_in.rin_ops = &rings_incoming_ops;
	test_data.rd_in.rin_want.ri_datum = INVALID_RING;

	/*
	 * 1. Test - c2_rm_right_get() with invalid right (value) fails.
	 */
	c2_rm_right_get(&test_data.rd_in);
	C2_UT_ASSERT(test_data.rd_in.rin_sm.sm_state == RI_FAILURE);
	C2_UT_ASSERT(test_data.rd_in.rin_sm.sm_rc == -ESRCH);

	/*
	 * 2. Test - right_get fails when owner in not in ROS_ACTIVE state.
	 */
	c2_rm_incoming_init(&test_data.rd_in, &test_data.rd_owner,
			    C2_RIT_LOCAL, RIP_NONE, RIF_LOCAL_WAIT);
	c2_rm_right_init(&test_data.rd_in.rin_want, &test_data.rd_owner);
	test_data.rd_in.rin_ops = &rings_incoming_ops;
	test_data.rd_in.rin_want.ri_datum = INVALID_RING;
	test_data.rd_owner.ro_sm.sm_state = ROS_FINALISING;
	c2_rm_right_get(&test_data.rd_in);
	C2_UT_ASSERT(test_data.rd_in.rin_sm.sm_rc == -ENODEV);
	C2_UT_ASSERT(test_data.rd_in.rin_sm.sm_state == RI_FAILURE);
	test_data.rd_owner.ro_sm.sm_state = ROS_ACTIVE;
}

void local_rights_test()
{
	/*
	 * 1. Get cached right - successful - WAIT, TRY
	 * 2. Get two non-overlapping cached rights - successful - WAIT, TRY
	 * 3. Get held non-conflicting right - wait - WAIT, TRY
	 * 4. Get held conflicting rights - WAIT, TRY
	 * 5. Get invalid right - failure
	 * 6. Owner in non-active state - failure
	 */
	local_rights_init();
	cached_rights_test(RIF_LOCAL_WAIT);
	cached_rights_test(RIF_LOCAL_TRY);
	held_rights_test(RIF_LOCAL_WAIT);
	held_rights_test(RIF_LOCAL_TRY);
	failures_test();
	local_rights_fini();
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
