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
 * Original author: Andriy Tkachuk <andriy_tkachuk@xyratex.com>
 * Original creation date: 04/01/2013
 */

#define M0_ADDB_RT_CREATE_DEFINITION

#include "ut/ut.h"
#include "fop/fop.h"
#include "reqh/reqh.h"

#include "fop/ut/stats/stats_fom.c"
#include "ut/ut_rpc_machine.h"

#define DUMMY_DBNAME      "dummy-db"
#define DUMMY_COB_ID      20
#define DUMMY_SERVER_ADDR "0@lo:12345:34:10"

extern struct m0_fom_type            stats_fom_type;
extern const struct m0_fom_type_ops  fom_stats_type_ops;
static struct m0_ut_rpc_mach_ctx     rmach_ctx;
static struct m0_reqh_service       *service;

static void test_stats(void)
{
	test_stats_req_handle(&rmach_ctx.rmc_reqh);
}

static int ut_stats_service_start(struct m0_reqh_service *service)
{
	M0_ASSERT(service != NULL);
	return 0;
}

static void ut_stats_service_stop(struct m0_reqh_service *service)
{
	M0_ASSERT(service != NULL);
}

static void ut_stats_service_fini(struct m0_reqh_service *service)
{
	M0_ASSERT(service != NULL);
	m0_free(service);
}

static const struct m0_reqh_service_ops ut_stats_service_ops = {
	.rso_start = ut_stats_service_start,
	.rso_stop = ut_stats_service_stop,
	.rso_fini = ut_stats_service_fini
};

static int ut_stats_service_allocate(struct m0_reqh_service **service,
				     struct m0_reqh_service_type *stype,
				     struct m0_reqh_context *rctx)
{
	struct m0_reqh_service *serv;

	M0_PRE(stype != NULL && service != NULL);

	M0_ALLOC_PTR(serv);
	M0_ASSERT(serv != NULL);

	serv->rs_type = stype;
	serv->rs_ops = &ut_stats_service_ops;
	*service = serv;
	return 0;
}

static const struct m0_reqh_service_type_ops ut_stats_service_type_ops = {
        .rsto_service_allocate = ut_stats_service_allocate
};

M0_ADDB_CT(m0_addb_ct_ut_service, M0_ADDB_CTXID_UT_SERVICE, "hi", "low");
M0_REQH_SERVICE_TYPE_DEFINE(ut_stats_service_type,
			    &ut_stats_service_type_ops,
			    "ut-stats-service",
                            &m0_addb_ct_ut_service, 2);

static int test_stats_init(void)
{
	int rc;

	m0_sm_conf_init(&fom_phases_conf);
	m0_addb_rec_type_register(&addb_rt_fom_phase_stats);
	rc = m0_reqh_service_type_register(&ut_stats_service_type);
	M0_ASSERT(rc == 0);
	m0_fom_type_init(&stats_fom_type, &fom_stats_type_ops,
			 &ut_stats_service_type,
			 &fom_phases_conf);

	M0_SET0(&rmach_ctx);
	rmach_ctx.rmc_cob_id.id = DUMMY_COB_ID;
	rmach_ctx.rmc_dbname    = DUMMY_DBNAME;
	rmach_ctx.rmc_ep_addr   = DUMMY_SERVER_ADDR;
	m0_ut_rpc_mach_init_and_add(&rmach_ctx);

	rc = m0_reqh_service_allocate(&service, &ut_stats_service_type, NULL);
	M0_ASSERT(rc == 0);
	m0_reqh_service_init(service, &rmach_ctx.rmc_reqh, NULL);
	rc = m0_reqh_service_start(service);
	M0_ASSERT(rc == 0);

	return rc;
}

static int test_stats_fini(void)
{
	m0_ut_rpc_mach_fini(&rmach_ctx);
	m0_reqh_service_type_unregister(&ut_stats_service_type);
	return 0;
}

const struct m0_test_suite m0_fom_stats_ut = {
	.ts_name = "fom-stats-ut",
	.ts_init = test_stats_init,
	.ts_fini = test_stats_fini,
	.ts_tests = {
		{ "stats", test_stats },
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
