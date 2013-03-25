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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 22-Mar-2013
 */

#include "fop/fop.h"
#include "lib/finject.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/ut.h"
#include "mgmt/mgmt.h"
#include "mgmt/mgmt_fops.h"
#include "reqh/reqh.h"

extern struct m0_addb_ctx m0_mgmt_addb_ctx;
static struct m0_reqh reqh;

static struct m0_fop *msut_ss_fop_alloc()
{
	struct m0_fop *fop;

	fop = m0_fop_alloc(&m0_fop_mgmt_service_state_req_fopt, NULL);
	if (fop != NULL) {
		int                                       rc;
		struct m0_fop_mgmt_service_terminate_req *ssfop;

		ssfop = m0_fop_data(fop);
		rc = m0_addb_ctx_export(&m0_mgmt_addb_ctx,
					&ssfop->mstrq_addb_ctx_id);
		if (rc != 0) {
			m0_addb_ctx_id_free(&ssfop->mstrq_addb_ctx_id);
			m0_fop_put(fop);
			fop = NULL;
		}
	}
	return fop;
}

/* intercept for the rso_fop_accept method */
static struct m0_reqh_service *mgmt_svc;
static const struct m0_reqh_service_ops *mgmt_svc_ops;
static struct m0_reqh_service_ops mgmt_svc_ut_ops;
static int mgmt_svc_rso_fop_accept_rc;
static int mgmt_svc_rso_fop_accept_called;
static int mgmt_svc_ut_rso_fop_accept(struct m0_reqh_service *service,
				      struct m0_fop *fop)
{
	M0_UT_ASSERT(mgmt_svc_ops != NULL);
	++mgmt_svc_rso_fop_accept_called;
	mgmt_svc_rso_fop_accept_rc =
		(*mgmt_svc_ops->rso_fop_accept)(service, fop);
	return 	mgmt_svc_rso_fop_accept_rc;
}

static void mgmt_svc_ut_rso_fop_accept_intercept(struct m0_reqh *reqh)
{
	M0_UT_ASSERT(reqh != NULL);
	M0_UT_ASSERT(reqh->rh_mgmt_svc != NULL);

	M0_UT_ASSERT(mgmt_svc == NULL);
	M0_UT_ASSERT(mgmt_svc_ops == NULL);
	mgmt_svc = reqh->rh_mgmt_svc;
	mgmt_svc_ops = mgmt_svc->rs_ops; /* save */

	mgmt_svc_ut_ops = *mgmt_svc->rs_ops; /* copy */
	mgmt_svc_ut_ops.rso_fop_accept = mgmt_svc_ut_rso_fop_accept;  /* repl */
	reqh->rh_mgmt_svc->rs_ops = &mgmt_svc_ut_ops; /* intercept */
	mgmt_svc_rso_fop_accept_called = 0;
}

static void mgmt_svc_ut_rso_fop_restore()
{
	M0_UT_ASSERT(mgmt_svc_ops != NULL);
	M0_UT_ASSERT(mgmt_svc != NULL);
	mgmt_svc->rs_ops = mgmt_svc_ops; /* restore */
	mgmt_svc_ops = NULL;
	mgmt_svc = NULL;
}

static void test_mgmt_svc_fail(void)
{
	int rc;
	struct m0_reqh *rh;

	/* Force failure during allocate */
	M0_ALLOC_PTR(rh);
	M0_UT_ASSERT(rh != NULL);
	rc = M0_REQH_INIT(rh,
			  .rhia_db      = (void *)1,
			  .rhia_mdstore = (void *)1,
			  .rhia_fol     = (void *)1);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_INIT);
	m0_fi_enable_once("m0_mgmt_service_allocate", "-EFAULT");
	M0_UT_ASSERT(m0_reqh_mgmt_service_start(rh) == -EFAULT);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_STOPPED);
	m0_reqh_fini(rh);
	m0_free(rh);

	/* force failure during start */
	M0_ALLOC_PTR(rh);
	M0_UT_ASSERT(rh != NULL);
	rc = M0_REQH_INIT(rh,
			  .rhia_db      = (void *)1,
			  .rhia_mdstore = (void *)1,
			  .rhia_fol     = (void *)1);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_INIT);
	m0_fi_enable_once("mgmt_svc_rso_start", "-ECANCELED");
	M0_UT_ASSERT(m0_reqh_mgmt_service_start(rh) == -ECANCELED);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_STOPPED);
	m0_reqh_fini(rh);
	m0_free(rh);
}

static void test_reqh_fop_allow(void)
{
	int rc;
	struct m0_fop *ss_fop;
	int rfp_cnt;
	struct m0_reqh *rh;

	M0_ALLOC_PTR(rh);
	M0_UT_ASSERT(rh != NULL);
	rc = M0_REQH_INIT(rh,
			  .rhia_db      = (void *)1,
			  .rhia_mdstore = (void *)1,
			  .rhia_fol     = (void *)1);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_INIT);

	ss_fop = msut_ss_fop_alloc();
	M0_UT_ASSERT(ss_fop != NULL);

	M0_UT_ASSERT(m0_reqh_fop_allow(rh, ss_fop) == -EAGAIN);

	M0_UT_ASSERT(m0_reqh_mgmt_service_start(rh) == 0);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_MGMT_STARTED);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, ss_fop) == 0);

	mgmt_svc_ut_rso_fop_accept_intercept(rh);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_called == 0);
	rfp_cnt = 0;
	M0_UT_ASSERT(mgmt_svc != NULL);
	M0_UT_ASSERT(mgmt_svc->rs_state == M0_RST_STARTED);

	M0_UT_ASSERT(m0_reqh_fop_allow(rh, ss_fop) == 0);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_called == ++rfp_cnt);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_rc == 0);

	m0_reqh_start(rh);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_NORMAL);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, ss_fop) == 0);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_called == rfp_cnt); /* no call */

	m0_reqh_shutdown_wait(rh);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_DRAIN);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, ss_fop) == 0);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_called == ++rfp_cnt);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_rc == 0);

	/* Temporarily fake the reqh state to M0_REQH_ST_SVCS_STOP to test that
	   the management service's rso_fop_accept method works in this state.
	 */
	reqh.rh_sm.sm_state = M0_REQH_ST_SVCS_STOP; /* HACK */
	M0_UT_ASSERT(mgmt_svc->rs_state == M0_RST_STARTED);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, ss_fop) == 0);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_called == ++rfp_cnt);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_rc == 0);
	reqh.rh_sm.sm_state = M0_REQH_ST_DRAIN; /* HACK to undo prev HACK */

	/* Real M0_REQH_ST_SVCS_STOP traversed below */
	m0_reqh_services_terminate(rh);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_MGMT_STOP);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, ss_fop) == -ESHUTDOWN);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_called == rfp_cnt); /* no call */

	mgmt_svc_ut_rso_fop_restore();

	m0_reqh_mgmt_service_stop(rh);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_STOPPED);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, ss_fop) == -ESHUTDOWN);

	m0_fop_put(ss_fop);
	m0_reqh_fini(rh);
	m0_free(rh);
}

const struct m0_test_suite m0_mgmt_svc_ut = {
	.ts_name = "mgmt-svc-ut",
	.ts_init = NULL,
	.ts_fini = NULL,
	.ts_tests = {
		{ "reqh-fop-allow",            test_reqh_fop_allow },
		{ "mgmt-svc-startup-failure",  test_mgmt_svc_fail },
		{ NULL, NULL }
	}
};
M0_EXPORTED(m0_mgmt_svc_ut);

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
