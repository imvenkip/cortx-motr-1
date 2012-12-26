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
 * Original creation date: 07/20/2012
 */
#pragma once
#ifndef __MERO_RM_UT_RMUT_H__
#define __MERO_RM_UT_RMUT_H__

#include "rings.h"

enum obj_type {
	OBJ_DOMAIN = 1,
	OBJ_RES_TYPE,
	OBJ_RES,
	OBJ_OWNER
};

/*
 * Resource manager class-collection.
 */
struct rm_ut_data {
	struct m0_rm_domain	   rd_dom;
	struct m0_rm_resource_type rd_rt;
	struct m0_rings		   rd_res;
	struct m0_rm_owner	   rd_owner;
	struct m0_rm_incoming	   rd_in;
	struct m0_rm_credit	   rd_credit;
};

/*
 * Test variable(s)
 */
struct rm_ut_data	   test_data;

void rm_utdata_init(struct rm_ut_data *data, enum obj_type type);
void rm_utdata_fini(struct rm_ut_data *data, enum obj_type type);
void rm_test_owner_capital_raise(struct m0_rm_owner *owner,
				 struct m0_rm_credit *credit);

/* __MERO_RM_UT_RMUT_H__ */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
