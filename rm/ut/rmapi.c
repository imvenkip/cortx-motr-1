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
 * Original creation date: 07/17/2012
 */

#include "lib/types.h"            /* uint64_t */
#include "lib/misc.h"
#include "lib/misc.h"
#include "lib/ut.h"
#include "lib/ub.h"

#include "rm/rm.h"

#include "rm/ut/rings.h"
#include "rm/ut/rmut.h"

extern bool res_tlist_is_empty(const struct c2_tl *list);
extern bool res_tlist_contains(const struct c2_tl *list,
			       const struct c2_rm_resource *res);
extern bool c2_rm_ur_tlist_contains(const struct c2_tl *list,
			      const struct c2_rm_right *right);
extern bool c2_rm_ur_tlist_is_empty(const struct c2_tl *list);

/*
 * Please note that this is basic API testing.
 * Detailed scenario testing is in another file.
 */
static void rights_api_test ()
{
	rm_utdata_init(&test_data, OBJ_OWNER);

	/* 1. Test c2_rm_incoming_init() */
	C2_SET0(&test_data.rd_in);
	c2_rm_incoming_init(&test_data.rd_in, &test_data.rd_owner,
			    C2_RIT_LOCAL, RIP_NONE, RIF_LOCAL_WAIT);
	C2_UT_ASSERT(test_data.rd_in.rin_sm.sm_state == RI_INITIALISED);
	C2_UT_ASSERT(test_data.rd_in.rin_type == C2_RIT_LOCAL);
	C2_UT_ASSERT(test_data.rd_in.rin_policy == RIP_NONE);
	C2_UT_ASSERT(test_data.rd_in.rin_flags == RIF_LOCAL_WAIT);
	C2_UT_ASSERT(test_data.rd_in.rin_want.ri_datum == 0);
	C2_UT_ASSERT(test_data.rd_in.rin_sm.sm_rc == 0);

	/* 2. Test c2_rm_right_init */
	c2_rm_right_init(&test_data.rd_right, &test_data.rd_owner);
	C2_UT_ASSERT(test_data.rd_right.ri_datum == 0);
	C2_UT_ASSERT(test_data.rd_right.ri_owner == &test_data.rd_owner);

	/* 3. Test c2_rm_owner_selfadd. Indirectly tests c2_rm_loan_init */
	test_data.rd_right.ri_datum = ALLRINGS;
	c2_rm_owner_selfadd(&test_data.rd_owner, &test_data.rd_right);
	C2_UT_ASSERT(!c2_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_borrowed));
	C2_UT_ASSERT(!c2_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_owned[OWOS_CACHED]));

	c2_rm_right_init(&test_data.rd_in.rin_want, &test_data.rd_owner);
	test_data.rd_in.rin_want.ri_datum = test_data.rd_right.ri_datum;
	test_data.rd_in.rin_ops = &rings_incoming_ops;
	/*
	 * 4. Test c2_rm_right_get
	 * Indirectly tests owner_balance, incoming_check, incoming_check_with,
	 * incoming_complete, pin_add.
	 */
	c2_rm_right_get(&test_data.rd_in);
	C2_UT_ASSERT(test_data.rd_in.rin_sm.sm_rc == 0);
	C2_UT_ASSERT(test_data.rd_in.rin_sm.sm_state == RI_SUCCESS);

	/* Test c2_rm_right_put. Indirectly tests incoming_release, pin_del */
	c2_rm_right_put(&test_data.rd_in);

	/* Test c2_rm_incoming_fini */
	c2_rm_incoming_fini(&test_data.rd_in);

	rm_utdata_fini(&test_data, OBJ_OWNER);
}

static void owner_api_test ()
{
	rm_utdata_init(&test_data, OBJ_RES);

	/*
	 * 1. Test c2_rm_owner_init
	 * Indirectly tests resource_get(), owner_internal_init(),
	 * owner_invariant(), owner_invariant_state().
	 */
	c2_rm_owner_init(&test_data.rd_owner, &test_data.rd_res.rs_resource, NULL);
	C2_UT_ASSERT(test_data.rd_owner.ro_sm.sm_state == ROS_ACTIVE);
	C2_UT_ASSERT(test_data.rd_owner.ro_creditor == NULL);
	C2_UT_ASSERT(test_data.rd_owner.ro_resource == &test_data.rd_res.rs_resource);

	/* 2. Test c2_rm_owner_retire - on newly initialised owner */
	c2_rm_owner_retire(&test_data.rd_owner);
	C2_UT_ASSERT(test_data.rd_owner.ro_sm.sm_state == ROS_FINAL);
	C2_UT_ASSERT(test_data.rd_owner.ro_resource == &test_data.rd_res.rs_resource);
	C2_UT_ASSERT(test_data.rd_res.rs_resource.r_ref == 1);

	/* 3. Test c2_rm_owner_fini. Indirectly tests resource_put(). */
	c2_rm_owner_fini(&test_data.rd_owner);
	C2_UT_ASSERT(test_data.rd_owner.ro_sm.sm_state == ROS_FINAL);
	C2_UT_ASSERT(test_data.rd_owner.ro_creditor == NULL);
	C2_UT_ASSERT(test_data.rd_owner.ro_resource == NULL);
	C2_UT_ASSERT(test_data.rd_res.rs_resource.r_ref == 0);

	rm_utdata_fini(&test_data, OBJ_RES);
}

static void res_api_test()
{
	rm_utdata_init(&test_data, OBJ_RES_TYPE);

	C2_SET0(&test_data.rd_res);
	/* 1. Test c2_rm_resource_add */
	c2_rm_resource_add(&test_data.rd_rt, &test_data.rd_res.rs_resource);

	c2_mutex_lock(&test_data.rd_rt.rt_lock);
	C2_UT_ASSERT(test_data.rd_rt.rt_nr_resources == 1);
	C2_UT_ASSERT(res_tlist_contains(&test_data.rd_rt.rt_resources,
				        &test_data.rd_res.rs_resource));
	c2_mutex_unlock(&test_data.rd_rt.rt_lock);

	C2_UT_ASSERT(test_data.rd_res.rs_resource.r_type == &test_data.rd_rt);

	/* 2. Test c2_rm_resource_del */
	c2_rm_resource_del(&test_data.rd_res.rs_resource);

	c2_mutex_lock(&test_data.rd_rt.rt_lock);
	C2_UT_ASSERT(test_data.rd_rt.rt_nr_resources == 0);
	C2_UT_ASSERT(res_tlist_is_empty(&test_data.rd_rt.rt_resources));
	c2_mutex_unlock(&test_data.rd_rt.rt_lock);

	rm_utdata_fini(&test_data, OBJ_RES_TYPE);
}

static void rt_api_test()
{
	rm_utdata_init(&test_data, OBJ_RES_TYPE);

	C2_UT_ASSERT(test_data.rd_rt.rt_dom == &test_data.rd_dom);
	C2_UT_ASSERT(test_data.rd_dom.rd_types[0] == &test_data.rd_rt);

	/* Test c2_rm_type_deregister */
	c2_rm_type_deregister(&test_data.rd_rt);

	C2_UT_ASSERT(test_data.rd_dom.rd_types[1] == NULL);
	C2_UT_ASSERT(test_data.rd_rt.rt_dom == NULL);

	c2_rm_domain_fini(&test_data.rd_dom);
}

static void dom_api_test()
{
	/* Initialise test_data.rd_domain */
	c2_rm_domain_init(&test_data.rd_dom);

	/* Make sure that all resource entries are NULL */
	C2_UT_ASSERT(c2_forall(i, ARRAY_SIZE(test_data.rd_dom.rd_types),
                         test_data.rd_dom.rd_types[i] == NULL));
	C2_UT_ASSERT(test_data.rd_dom.rd_lock.m_owner == 0);

	/* Finalise domain - Nothing to test - make sure it does not crash */
	c2_rm_domain_fini(&test_data.rd_dom);
}

void rm_api_test()
{
	/* Test domain APIs */
	dom_api_test();

	/* Test resource type APIs */
	rt_api_test();

	/* Test resource APIs */
	res_api_test();

	/* Test owner API s*/
	owner_api_test();

	/* Test rights, incoming APIs */
	rights_api_test();

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
