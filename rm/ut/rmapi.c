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
#include "lib/finject.h"

#include "rm/rm.h"

#include "rm/ut/rings.h"
#include "rm/ut/rmut.h"

extern bool res_tlist_is_empty(const struct m0_tl *list);
extern bool res_tlist_contains(const struct m0_tl *list,
			       const struct m0_rm_resource *res);
extern bool m0_rm_ur_tlist_contains(const struct m0_tl *list,
			      const struct m0_rm_credit *credit);
extern bool m0_rm_ur_tlist_is_empty(const struct m0_tl *list);

/*
 * Please note that this is basic API testing.
 * Detailed scenario testing is in another file.
 */
static void credits_api_test (void)
{
	int rc;

	rm_utdata_init(&test_data, OBJ_OWNER);

	/* 1. Test m0_rm_incoming_init() */
	m0_rm_incoming_init(&test_data.rd_in, &test_data.rd_owner,
			    M0_RIT_LOCAL, RIP_NONE, RIF_LOCAL_WAIT);
	M0_UT_ASSERT(test_data.rd_in.rin_sm.sm_state == RI_INITIALISED);
	M0_UT_ASSERT(test_data.rd_in.rin_type == M0_RIT_LOCAL);
	M0_UT_ASSERT(test_data.rd_in.rin_policy == RIP_NONE);
	M0_UT_ASSERT(test_data.rd_in.rin_flags == RIF_LOCAL_WAIT);
	M0_UT_ASSERT(test_data.rd_in.rin_want.cr_datum == 0);
	M0_UT_ASSERT(test_data.rd_in.rin_rc == 0);

	/* 2. Test m0_rm_credit_init */
	m0_rm_credit_init(&test_data.rd_credit, &test_data.rd_owner);
	M0_UT_ASSERT(test_data.rd_credit.cr_datum == 0);
	M0_UT_ASSERT(test_data.rd_credit.cr_owner == &test_data.rd_owner);

	/* 3. Test m0_rm_owner_selfadd. Test memory failure */
	test_data.rd_credit.cr_datum = ALLRINGS;
	m0_fi_enable_once("m0_alloc", "fail_allocation");
	rc = m0_rm_owner_selfadd(&test_data.rd_owner, &test_data.rd_credit);
	M0_UT_ASSERT(rc == -ENOMEM);

	/* 4. Test m0_rm_owner_selfadd. Indirectly tests m0_rm_loan_init */
	rc = m0_rm_owner_selfadd(&test_data.rd_owner, &test_data.rd_credit);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_borrowed));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&test_data.rd_owner.ro_owned[OWOS_CACHED]));

	/*
	 * 5. Test m0_rm_credit_get for memory failure.
	 */
	m0_rm_credit_init(&test_data.rd_in.rin_want, &test_data.rd_owner);
	test_data.rd_in.rin_want.cr_datum = test_data.rd_credit.cr_datum;
	test_data.rd_in.rin_ops = &rings_incoming_ops;
	m0_fi_enable_once("m0_alloc", "fail_allocation");
	m0_rm_credit_get(&test_data.rd_in);
	M0_UT_ASSERT(test_data.rd_in.rin_rc == -ENOMEM);
	M0_UT_ASSERT(test_data.rd_in.rin_sm.sm_state == RI_FAILURE);
	/* Test m0_rm_incoming_fini */
	m0_rm_incoming_fini(&test_data.rd_in);

	/*
	 * 6. Test m0_rm_credit_get - Success case.
	 * Indirectly tests owner_balance, incoming_check, incoming_check_with,
	 * incoming_complete, pin_add.
	 */
	m0_rm_incoming_init(&test_data.rd_in, &test_data.rd_owner,
			    M0_RIT_LOCAL, RIP_NONE, RIF_LOCAL_WAIT);
	m0_rm_credit_init(&test_data.rd_in.rin_want, &test_data.rd_owner);
	test_data.rd_in.rin_want.cr_datum = test_data.rd_credit.cr_datum;
	test_data.rd_in.rin_ops = &rings_incoming_ops;
	m0_rm_credit_get(&test_data.rd_in);
	M0_UT_ASSERT(test_data.rd_in.rin_rc == 0);
	M0_UT_ASSERT(test_data.rd_in.rin_sm.sm_state == RI_SUCCESS);

	/* Test m0_rm_credit_put. Indirectly tests incoming_release, pin_del */
	m0_rm_credit_put(&test_data.rd_in);

	/* Test m0_rm_incoming_fini */
	m0_rm_incoming_fini(&test_data.rd_in);

	rm_utdata_fini(&test_data, OBJ_OWNER);
}

static void owner_api_test (void)
{
	rm_utdata_init(&test_data, OBJ_RES);

	/*
	 * 1. Test m0_rm_owner_init
	 * Indirectly tests resource_get(), owner_internal_init(),
	 * owner_invariant(), owner_invariant_state().
	 */
	m0_rm_owner_init(&test_data.rd_owner, &test_data.rd_res.rs_resource,
			 NULL);
	M0_UT_ASSERT(test_data.rd_owner.ro_sm.sm_state == ROS_ACTIVE);
	M0_UT_ASSERT(test_data.rd_owner.ro_creditor == NULL);
	M0_UT_ASSERT(test_data.rd_owner.ro_resource ==
			&test_data.rd_res.rs_resource);

	/* 2. Test m0_rm_owner_windup - on newly initialised owner */
	m0_rm_owner_windup(&test_data.rd_owner);
	M0_UT_ASSERT(test_data.rd_owner.ro_sm.sm_state == ROS_FINAL);
	M0_UT_ASSERT(test_data.rd_owner.ro_resource ==
			&test_data.rd_res.rs_resource);
	M0_UT_ASSERT(test_data.rd_res.rs_resource.r_ref == 1);

	/* 3. Test m0_rm_owner_fini. Indirectly tests resource_put(). */
	m0_rm_owner_fini(&test_data.rd_owner);
	M0_UT_ASSERT(test_data.rd_owner.ro_sm.sm_state == ROS_FINAL);
	M0_UT_ASSERT(test_data.rd_owner.ro_creditor == NULL);
	M0_UT_ASSERT(test_data.rd_owner.ro_resource == NULL);
	M0_UT_ASSERT(test_data.rd_res.rs_resource.r_ref == 0);

	rm_utdata_fini(&test_data, OBJ_RES);
}

static void res_api_test(void)
{
	rm_utdata_init(&test_data, OBJ_RES_TYPE);

	M0_SET0(&test_data.rd_res);
	/* 1. Test m0_rm_resource_add */
	m0_rm_resource_add(&test_data.rd_rt, &test_data.rd_res.rs_resource);

	m0_mutex_lock(&test_data.rd_rt.rt_lock);
	M0_UT_ASSERT(test_data.rd_rt.rt_nr_resources == 1);
	M0_UT_ASSERT(res_tlist_contains(&test_data.rd_rt.rt_resources,
				        &test_data.rd_res.rs_resource));
	m0_mutex_unlock(&test_data.rd_rt.rt_lock);

	M0_UT_ASSERT(test_data.rd_res.rs_resource.r_type == &test_data.rd_rt);

	/* 2. Test m0_rm_resource_del */
	m0_rm_resource_del(&test_data.rd_res.rs_resource);

	m0_mutex_lock(&test_data.rd_rt.rt_lock);
	M0_UT_ASSERT(test_data.rd_rt.rt_nr_resources == 0);
	M0_UT_ASSERT(res_tlist_is_empty(&test_data.rd_rt.rt_resources));
	m0_mutex_unlock(&test_data.rd_rt.rt_lock);

	rm_utdata_fini(&test_data, OBJ_RES_TYPE);
}

static void rt_api_test(void)
{
	rm_utdata_init(&test_data, OBJ_RES_TYPE);

	M0_UT_ASSERT(test_data.rd_rt.rt_dom == &test_data.rd_dom);
	M0_UT_ASSERT(test_data.rd_dom.rd_types[0] == &test_data.rd_rt);

	/* Test m0_rm_type_deregister */
	m0_rm_type_deregister(&test_data.rd_rt);

	M0_UT_ASSERT(test_data.rd_dom.rd_types[1] == NULL);
	M0_UT_ASSERT(test_data.rd_rt.rt_dom == NULL);

	m0_rm_domain_fini(&test_data.rd_dom);
}

static void dom_api_test(void)
{
	/* Initialise test_data.rd_domain */
	m0_rm_domain_init(&test_data.rd_dom);

	/* Make sure that all resource entries are NULL */
	M0_UT_ASSERT(m0_forall(i, ARRAY_SIZE(test_data.rd_dom.rd_types),
                         test_data.rd_dom.rd_types[i] == NULL));
	M0_UT_ASSERT(test_data.rd_dom.rd_lock.m_owner == 0);

	/* Finalise domain - Nothing to test - make sure it does not crash */
	m0_rm_domain_fini(&test_data.rd_dom);
}

void rm_api_test(void)
{
	/* Test domain APIs */
	dom_api_test();

	/* Test resource type APIs */
	rt_api_test();

	/* Test resource APIs */
	res_api_test();

	/* Test owner API s*/
	owner_api_test();

	/* Test credits, incoming APIs */
	credits_api_test();

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
