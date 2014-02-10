/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 21-Jan-2014
 */

#include "net/module.h"
#include "module/instance.h"  /* m0_get */
#include "ut/ut.h"

static void test_net_modules(void)
{
	struct m0_net    *net = &m0_get()->i_net;
	struct m0_module *xprt_m = \
		&net->n_xprts[M0_NET_XPRT_BULK_MEM].nx_module;
	int               rc;

	m0_net_modules_setup(net);

	M0_UT_ASSERT(xprt_m->m_cur == 0);
	M0_UT_ASSERT(net->n_module.m_cur == 0);

#define CURIOUS 0
	if (CURIOUS) {
		struct m0 another_instance;

		m0_set(&another_instance);
		m0_net_modules_setup(&another_instance.i_net);
		/*
		 * This statement fails, since `xprt_m' does not belong
		 * `another_instance'.
		 */
		rc = m0_module_init(xprt_m, M0_LEVEL_NET_DOMAIN);
		M0_UT_FAIL("The program should have failed earlier");
	}
#undef CURIOUS

	rc = m0_module_init(xprt_m, M0_LEVEL_NET_DOMAIN);
	M0_UT_ASSERT(rc == 0);

	M0_UT_ASSERT(xprt_m->m_cur == M0_LEVEL_NET_DOMAIN);
	M0_UT_ASSERT(net->n_module.m_cur == M0_LEVEL_NET);

	m0_module_fini(xprt_m, 0);

	M0_UT_ASSERT(xprt_m->m_cur == 0);
	M0_UT_ASSERT(net->n_module.m_cur == 0);
}

const struct m0_test_suite m0_net_module_ut = {
	.ts_name  = "net-module",
	.ts_tests = {
		{ "test", test_net_modules },
		{ NULL, NULL }
	}
};
