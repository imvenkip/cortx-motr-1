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

#include "ut/ut.h"
#include "fop/fop.h"
#include "reqh/reqh.h"
#include "rpc/rpclib.h"
#include "net/lnet/lnet.h"
#include "fop/fom_generic.h"  /* m0_generic_conf */
#include "addb/addb.h"

enum {
	RDWR_REQUEST_MAX = 48,
	REQH_IN_UT_MAX   = 2
};

#include "fop/ut/long_lock/rdwr_fom.c"
#include "fop/ut/long_lock/rdwr_test_bench.c"

extern struct m0_fom_type rdwr_fom_type;
extern const struct m0_fom_type_ops fom_rdwr_type_ops;
static struct m0_reqh reqh[REQH_IN_UT_MAX];
static struct m0_reqh_service *service[REQH_IN_UT_MAX];

static void test_long_lock_n(void)
{
	static struct m0_reqh *r[REQH_IN_UT_MAX] = { &reqh[0], &reqh[1] };

	rdwr_send_fop(r, REQH_IN_UT_MAX);
}

static void test_long_lock_1(void)
{
	static struct m0_reqh *r[1] = { &reqh[0] };

	rdwr_send_fop(r, 1);
}

static int ut_long_lock_service_start(struct m0_reqh_service *service)
{
	M0_ASSERT(service != NULL);
	return 0;
}

static void ut_long_lock_service_stop(struct m0_reqh_service *service)
{
	M0_ASSERT(service != NULL);
}

static void ut_long_lock_service_fini(struct m0_reqh_service *service)
{
	M0_ASSERT(service != NULL);
	m0_free(service);
}

static const struct m0_reqh_service_ops ut_long_lock_service_ops = {
	.rso_start = ut_long_lock_service_start,
	.rso_stop = ut_long_lock_service_stop,
	.rso_fini = ut_long_lock_service_fini
};

static int ut_long_lock_service_allocate(struct m0_reqh_service **service,
					 struct m0_reqh_service_type *stype,
					 struct m0_reqh_context *rctx)
{
	struct m0_reqh_service *serv;

	M0_PRE(stype != NULL && service != NULL);

	M0_ALLOC_PTR(serv);
	M0_ASSERT(serv != NULL);

	serv->rs_type = stype;
	serv->rs_ops = &ut_long_lock_service_ops;
	*service = serv;
	return 0;
}

static const struct m0_reqh_service_type_ops ut_long_lock_service_type_ops = {
        .rsto_service_allocate = ut_long_lock_service_allocate
};

M0_ADDB_CT(m0_addb_ct_ut_service, M0_ADDB_CTXID_UT_SERVICE, "hi", "low");
M0_REQH_SERVICE_TYPE_DEFINE(ut_long_lock_service_type,
			    &ut_long_lock_service_type_ops,
			    "ut-long-lock-service",
                            &m0_addb_ct_ut_service);

static int test_long_lock_init(void)
{
	int rc;
	int i;

	rc = m0_reqh_service_type_register(&ut_long_lock_service_type);
	M0_ASSERT(rc == 0);
	m0_fom_type_init(&rdwr_fom_type, &fom_rdwr_type_ops,
			 &ut_long_lock_service_type,
			 &m0_generic_conf);
	/*
	 * Instead of using m0d and dealing with network, database and
	 * other subsystems, request handler is initialised in a 'special way'.
	 * This allows it to operate in a 'limited mode' which is enough for
	 * this test.
	 */
	for (i = 0; i < REQH_IN_UT_MAX; ++i) {
		rc = M0_REQH_INIT(&reqh[i],
				  .rhia_dtm       = (void *)1,
				  .rhia_db        = (void *)1,
				  .rhia_mdstore   = (void *)1,
				  .rhia_fol       = (void *)1,
				  .rhia_svc       = NULL,
				  .rhia_addb_stob = NULL);
		M0_ASSERT(rc == 0);
	}
	for (i = 0; i < REQH_IN_UT_MAX; ++i) {
		rc = m0_reqh_service_allocate(&service[i],
					      &ut_long_lock_service_type, NULL);
		M0_ASSERT(rc == 0);
		m0_reqh_service_init(service[i], &reqh[i]);
		rc = m0_reqh_service_start(service[i]);
		M0_ASSERT(rc == 0);
	}
	return rc;
}

static int test_long_lock_fini(void)
{
	int i;

	for (i = 0; i < REQH_IN_UT_MAX; ++i) {
		m0_reqh_service_stop(service[i]);
		m0_reqh_service_fini(service[i]);
		m0_reqh_fini(&reqh[i]);
	}
	m0_reqh_service_type_unregister(&ut_long_lock_service_type);

	return 0;
}

const struct m0_test_suite m0_fop_lock_ut = {
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
