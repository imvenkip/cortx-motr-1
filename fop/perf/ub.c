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
 * Original author: Anatoliy Bilenko <Anatoliy_Bilenko@xyratex.com>
 * Original creation date: 15/01/2013
 */

#include "lib/ut.h"
#include "lib/ub.h"
#include "lib/cdefs.h"
#include "lib/mutex.h"
#include "fop/fop.h"
#include "fop/fom_generic.h"        /* m0_generic_conf */
#include "fop/fom_long_lock.h"
#include "reqh/reqh.h"
#include "net/lnet/lnet.h"
#include "addb/addb.h"

enum {
	REQH_MAX = 2
};

static struct m0_reqh_service *g_service[REQH_MAX];
static struct m0_reqh          g_reqh[REQH_MAX];
static struct m0_mutex         g_fom_mutex;
static struct m0_long_lock     g_fom_long_lock;
/* 8MB ~= 2*(L3 cache size) on most of VMs used by our team */
static char                    g_test_mem[8 * (1 << 20)];


#include "fop/perf/fom.c"
#include "fop/perf/tb.c"


M0_UNUSED static void ub_fom_test_n(int iter)
{
	int i;
	static struct m0_reqh *r[REQH_MAX];

	for (i = 0; i < REQH_MAX; ++i)
		r[i] = &g_reqh[i];

	reqh_test_run(r, REQH_MAX, UB_FOM_MEM_B);
}

static void ub_fom_test_mem_b(int iter)
{
	static struct m0_reqh *r[1] = { &g_reqh[0] };

	reqh_test_run(r, 1, UB_FOM_MEM_B);
}

static void ub_fom_test_mem_kb(int iter)
{
	static struct m0_reqh *r[1] = { &g_reqh[0] };

	reqh_test_run(r, 1, UB_FOM_MEM_KB);
}

static void ub_fom_test_mem_mb(int iter)
{
	static struct m0_reqh *r[1] = { &g_reqh[0] };

	reqh_test_run(r, 1, UB_FOM_MEM_MB);
}

static void ub_fom_test_mutex(int iter)
{
	static struct m0_reqh *r[1] = { &g_reqh[0] };

	reqh_test_run(r, 1, UB_FOM_MUTEX);
}

static void ub_fom_test_long_lock(int iter)
{
	static struct m0_reqh *r[1] = { &g_reqh[0] };

	reqh_test_run(r, 1, UB_FOM_LONG_LOCK);
}

M0_UNUSED static void ub_fom_test_block(int iter)
{
	static struct m0_reqh *r[1] = { &g_reqh[0] };

	reqh_test_run(r, 1, UB_FOM_BLOCK);
}

static int ub_perf_fom_service_start(struct m0_reqh_service *service)
{
	M0_ASSERT(service != NULL);
	return 0;
}

static void ub_perf_fom_service_stop(struct m0_reqh_service *service)
{
	M0_ASSERT(service != NULL);
}

static void ub_perf_fom_service_fini(struct m0_reqh_service *service)
{
	M0_ASSERT(service != NULL);
	m0_free(service);
}

static const struct m0_reqh_service_ops ub_perf_fom_service_ops = {
	.rso_start = ub_perf_fom_service_start,
	.rso_stop = ub_perf_fom_service_stop,
	.rso_fini = ub_perf_fom_service_fini
};

static int ub_perf_fom_service_allocate(struct m0_reqh_service **service,
					struct m0_reqh_service_type *stype,
					const char *arg)
{
	struct m0_reqh_service *serv;

	M0_PRE(stype != NULL && service != NULL);

	M0_ALLOC_PTR(serv);
	M0_ASSERT(serv != NULL);

	serv->rs_type = stype;
	serv->rs_ops = &ub_perf_fom_service_ops;
	*service = serv;
	return 0;
}

static const struct m0_reqh_service_type_ops ub_fom_service_type_ops = {
        .rsto_service_allocate = ub_perf_fom_service_allocate
};

M0_ADDB_CT(m0_addb_ct_ut_service, M0_ADDB_CTXID_UT_SERVICE, "hi", "low");
M0_REQH_SERVICE_TYPE_DEFINE(ub_fom_service_type, &ub_fom_service_type_ops,
			    "ub-fom-service", &m0_addb_ct_ut_service);

static int ub_fom_init(void)
{
	int rc;
	int i;

	rc = m0_reqh_service_type_register(&ub_fom_service_type);
	M0_ASSERT(rc == 0);
	m0_fom_type_init(&ub_fom_type, &ub_fom_type_ops,
			 &ub_fom_service_type,
			 &m0_generic_conf);
	/*
	 * Instead of using m0d and dealing with network, database and
	 * other subsystems, request handler is initialised in a 'special way'.
	 * This allows it to operate in a 'limited mode' which is enough for
	 * this test.
	 */
	for (i = 0; i < REQH_MAX; ++i) {
		rc = M0_REQH_INIT(&g_reqh[i],
				  .rhia_dtm       = (void *)1,
				  .rhia_db        = (void *)1,
				  .rhia_mdstore   = (void *)1,
				  .rhia_fol       = (void *)1,
				  .rhia_svc       = NULL,
				  .rhia_addb_stob = NULL);
		M0_ASSERT(rc == 0);
	}
	for (i = 0; i < REQH_MAX; ++i) {
		rc = m0_reqh_service_allocate(&g_service[i],
					      &ub_fom_service_type, NULL);
		M0_ASSERT(rc == 0);
		m0_reqh_service_init(g_service[i], &g_reqh[i]);
		rc = m0_reqh_service_start(g_service[i]);
		M0_ASSERT(rc == 0);
	}
	return rc;
}

static void ub_fini(void)
{
	int i;

	for (i = 0; i < REQH_MAX; ++i) {
		m0_reqh_shutdown_wait(&g_reqh[i]);
		m0_reqh_service_stop(g_service[i]);
		m0_reqh_service_fini(g_service[i]);
		m0_reqh_fini(&g_reqh[i]);
	}
	m0_reqh_service_type_unregister(&ub_fom_service_type);

	m0_long_lock_fini(&g_fom_long_lock);
	m0_mutex_fini(&g_fom_mutex);
}

static void ub_init(void)
{
	int rc;
	int i;

	/* Zeroed global structures are reused in several subsequent rounds.
	   Ensure there is no garbage left from the previous rounds: */
	M0_SET_ARR0(g_reqh);
	M0_SET_ARR0(g_service);
	M0_SET0(&g_fom_mutex);
	M0_SET0(&g_fom_long_lock);

	m0_mutex_init(&g_fom_mutex);
	m0_long_lock_init(&g_fom_long_lock);

	/* fill g_test_mem with dummy values */
	for (i = 0; i < ARRAY_SIZE(g_test_mem); ++i)
		g_test_mem[i] = i;

	rc = ub_fom_init();
	M0_UB_ASSERT(rc == 0);
}

/* See fop/perf/README for more details regarding the following definition: */
/* #define __MERO_DISABLE_POORLY_PROFILED_TESTS__ */
struct m0_ub_set m0_fom_ub = {
        .us_name = "fom-ub",
        .us_init = ub_init,
        .us_fini = ub_fini,
        .us_run  = {
                { .ub_name  = "mem-b",
                  .ub_iter  = 1,
                  .ub_round = ub_fom_test_mem_b },
                { .ub_name  = "mem-kb",
                  .ub_iter  = 1,
                  .ub_round = ub_fom_test_mem_kb },
                { .ub_name  = "mem-mb",
                  .ub_iter  = 1,
                  .ub_round = ub_fom_test_mem_mb },
                { .ub_name  = "mutex",
                  .ub_iter  = 1,
                  .ub_round = ub_fom_test_mutex },
                { .ub_name  = "llock",
                  .ub_iter  = 1,
                  .ub_round = ub_fom_test_long_lock },
#ifndef __MERO_DISABLE_POORLY_PRFILED_TESTS__
                { .ub_name  = "block",
                  .ub_iter  = 1,
                  .ub_round = ub_fom_test_block },
#endif /* __MERO_DISABLE_POORLY_PROFILED_TESTS__ */
		{ .ub_name = NULL}
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
