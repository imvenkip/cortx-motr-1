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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 30-May-2013
 */

#include "ut/ut.h"

static void test_list_api(void)
{
#if 0 /*XXX*/
	struct m0_be_list *a;
	struct m0_be_tx_credit cred;

	list = m0_be_alloc(sizeof *a);
	M0_UT_ASSERT(a != NULL);

	m0_be_list_create(a);

	m0_be_list_get(a, 0, op); /* gets the head of the list */

	m0_be_list_init(a, seg);
	/*
	 * Sequential init()s must be allowed.
	 *
	 * Imagine this scenario:
	 * init() --> crash, reboot, recovery --> init() again.
	 */
	m0_be_list_init(a, seg);

	m0_be_tx_credit_init(&cred);
	m0_be_list_credit(a, M0_BLO_INSERT, 3, &cred);
	M0_UT_ASSERT(cred.tc_reg_nr != 0);
	M0_UT_ASSERT(cred.tc_reg_size != 0);

	/* XXX perform 3 insertions */

	m0_be_list_capture(a, ...);

	...
#endif /*XXX*/
}

const struct m0_test_suite be_list_ut = {
	.ts_name = "be-list-ut",
	.ts_tests = {
		{ "api  # XXX noop", test_list_api },
		{ NULL, NULL }
	}
};
