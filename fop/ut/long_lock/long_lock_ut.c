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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 08/06/2012
 */

#include "lib/ut.h"
#include "fop/fop.h"
#include "reqh/reqh.h"
#include "rpc/rpclib.h"
#include "net/lnet/lnet.h"
#include "ut/rpc.h"
#include "fop/fom_generic.h"        /* c2_generic_conf */

enum {
	RDWR_REQUEST_MAX = 48,
	REQH_IN_UT_MAX   = 2
};

#include "fop/ut/long_lock/rdwr_fom.c"
#include "fop/ut/long_lock/rdwr_test_bench.c"

extern struct c2_fom_type rdwr_fom_type;
extern const struct c2_fom_type_ops fom_rdwr_type_ops;
static struct c2_reqh reqh[REQH_IN_UT_MAX];

static void test_long_lock_n(void)
{
	static struct c2_reqh *r[REQH_IN_UT_MAX] = { &reqh[0], &reqh[1] };

	rdwr_send_fop(r, REQH_IN_UT_MAX);
}

static void test_long_lock_1(void)
{
	static struct c2_reqh *r[1] = { &reqh[0] };

	rdwr_send_fop(r, 1);
}

static int test_long_lock_init(void)
{
	int rc;
	int i;

	/*
	 * Instead of using colibri_setup and dealing with network, database and
	 * other subsystems, request handler is initialised in a 'special way'.
	 * This allows it to operate in a 'limited mode' which is enough for
	 * this test.
	 */
	for (i = 0; i < REQH_IN_UT_MAX; ++i) {
		rc = c2_reqh_init(&reqh[i], (void *)1, (void *)1,
				  (void *)1, (void *)1, (void *)1);
		C2_ASSERT(rc == 0);
	}
	c2_fom_type_init(&rdwr_fom_type, &fom_rdwr_type_ops, NULL,
			 &c2_generic_conf);
	return rc;
}

static int test_long_lock_fini(void)
{
	int i;

	for (i = 0; i < REQH_IN_UT_MAX; ++i)
		c2_reqh_fini(&reqh[i]);

	return 0;
}

const struct c2_test_suite c2_fop_lock_ut = {
	.ts_name = "fop-lock-ut",
	.ts_init = test_long_lock_init,
	.ts_fini = test_long_lock_fini,
	.ts_tests = {
		{ "fop-lock: 1reqh", test_long_lock_1 },
		{ "fop-lock: 2reqh", test_long_lock_n },
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
