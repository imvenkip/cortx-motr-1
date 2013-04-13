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
 * Original creation date: 07/20/2012
 */

#include "lib/memory.h"
#include "lib/misc.h"
#include "ut/ut.h"
#include "lib/ub.h"
#include "rm/rm.h"
#include "rm/rm_internal.h"
#include "rm/ut/rings.h"
#include "rm/ut/rmut.h"

extern const struct m0_tl_descr m0_remotes_tl;

/*
 * Test variable(s)
 */
M0_INTERNAL struct rm_ut_data	   test_data;

extern void rm_api_test(void);
extern void local_credits_test(void);
extern void remote_credits_test(void);
extern void rm_fom_funcs_test(void);
extern void rm_fop_funcs_test(void);
extern bool m0_rm_ur_tlist_is_empty(const struct m0_tl *list);
extern void m0_remotes_tlist_del(struct m0_rm_remote *other);
extern void rmsvc(void);

struct rm_ut_data test_data;

void rm_test_owner_capital_raise(struct m0_rm_owner *owner,
				 struct m0_rm_credit *credit)
{
	m0_rm_credit_init(credit, owner);
	credit->cr_datum = ALLRINGS;
	m0_rm_owner_selfadd(owner, credit);
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&owner->ro_borrowed));
	M0_UT_ASSERT(!m0_rm_ur_tlist_is_empty(&owner->ro_owned[OWOS_CACHED]));
}

/*
 * Recursive call to initialise object hierarchy
 */
void rm_utdata_init(struct rm_ut_data *data, enum obj_type type)
{
	int rc;

	M0_UT_ASSERT(data != NULL);

	switch (type) {
		case OBJ_DOMAIN:
			/* Initialise test_domain */
			m0_rm_domain_init(&data->rd_dom);
			break;
		case OBJ_RES_TYPE:
			rm_utdata_init(data, OBJ_DOMAIN);
			/* Register test resource type */
			rc = m0_rm_type_register(&data->rd_dom, &data->rd_rt);
			M0_UT_ASSERT(rc == 0);
			data->rd_rt.rt_ops = &rings_rtype_ops;
			break;
		case OBJ_RES:
			rm_utdata_init(data, OBJ_RES_TYPE);
			M0_SET0(&data->rd_res);
			data->rd_res.rs_resource.r_ops = &rings_ops;
			m0_rm_resource_add(&data->rd_rt,
					   &data->rd_res.rs_resource);
			break;
		case OBJ_OWNER:
			rm_utdata_init(data, OBJ_RES);
			m0_rm_owner_init(&data->rd_owner,
					 &data->rd_res.rs_resource, NULL);
			break;

		default:
			M0_IMPOSSIBLE("Invalid value of obj_type");
	}
}

/*
 * Recursive call to finalise object hierarchy
 */
void rm_utdata_fini(struct rm_ut_data *data, enum obj_type type)
{
	struct m0_rm_remote *other;
	struct m0_rm_credit *credit;

	M0_UT_ASSERT(data != NULL);

	switch (type) {
		case OBJ_DOMAIN:
			/* Finalise test_domain */
			m0_rm_domain_init(&data->rd_dom);
			break;
		case OBJ_RES_TYPE:
			/* De-register test resource type */
			m0_rm_type_deregister(&data->rd_rt);
			rm_utdata_fini(data, OBJ_DOMAIN);
			break;
		case OBJ_RES:
			m0_tl_for(m0_remotes,
				  &data->rd_res.rs_resource.r_remote, other) {
				m0_remotes_tlist_del(other);
				m0_rm_remote_fini(other);
				m0_free(other);
			} m0_tl_endfor;
			m0_rm_resource_del(&data->rd_res.rs_resource);
			rm_utdata_fini(data, OBJ_RES_TYPE);
			break;
		case OBJ_OWNER:
			m0_rm_owner_windup(&data->rd_owner);

			data->rd_owner.ro_creditor = NULL;
			m0_tl_for(m0_rm_ur, &data->rd_owner.ro_borrowed,
				  credit) {
				m0_rm_ur_tlist_del(credit);
			} m0_tl_endfor;
			m0_rm_owner_fini(&data->rd_owner);
			rm_utdata_fini(data, OBJ_RES);
			break;
		default:
			M0_IMPOSSIBLE("Invalid value of obj_type");
	}
}

const struct m0_test_suite rm_ut = {
	.ts_name = "rm-ut",
	.ts_tests = {
		{ "api", rm_api_test },
		{ "lcredits", local_credits_test },
		{ "fom-funcs", rm_fom_funcs_test },
		{ "fop-funcs", rm_fop_funcs_test },
		{ "rcredits", remote_credits_test },
		{ "rmsvc", rmsvc },
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
