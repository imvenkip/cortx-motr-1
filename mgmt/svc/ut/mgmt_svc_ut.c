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
#include "fop/fom.h"
#include "lib/finject.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/time.h"
#include "lib/timer.h"
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_MGMT
#include "lib/trace.h"  /* M0_LOG() */
#include "lib/uuid.h"
#include "mgmt/mgmt.h"
#include "mgmt/mgmt_addb.h"
#include "mgmt/mgmt_fops.h"
#include "net/lnet/lnet.h"
#include "reqh/reqh.h"
#include "rpc/rpc_opcodes.h"
#include "rpc/rpclib.h"
#include "ut/ut.h"
#include "ut/ut_rpc_machine.h"

#include <stdio.h>

extern struct m0_addb_ctx m0_mgmt_addb_ctx;

#include "mgmt/svc/ut/mgmt_svc_ut.h"
#include "mgmt/svc/ut/mgmt_svc_ut_xc.h"
#include "mgmt/svc/ut/fake_svc.c"
#include "mgmt/svc/ut/fake_fom.c"
#include "mgmt/svc/ut/mgmt_svc_setup.c"

/*
 ******************************************************************************
 * Mgmt service tweaks
 ******************************************************************************
 */

/* intercept for the rso_fop_accept method */
static struct m0_reqh_service *mgmt_svc;
static const struct m0_reqh_service_ops *mgmt_svc_ops;
static struct m0_reqh_service_ops mgmt_svc_ut_ops;
static int mgmt_svc_rso_fop_accept_rc;
static int mgmt_svc_rso_fop_accept_called;
static struct m0_dbenv mgmt_svc_dbenv;

static int mgmt_svc_ut_rso_fop_accept(struct m0_reqh_service *service,
				      struct m0_fop *fop)
{
	M0_UT_ASSERT(mgmt_svc_ops != NULL);
	++mgmt_svc_rso_fop_accept_called;
	mgmt_svc_rso_fop_accept_rc =
		(*mgmt_svc_ops->rso_fop_accept)(service, fop);
	return mgmt_svc_rso_fop_accept_rc;
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

static struct m0_fop *mgmt_svc_ut_ss_fop_alloc(void)
{
	struct m0_fop *fop;

	fop = m0_fop_alloc(&m0_fop_mgmt_service_state_req_fopt, NULL);
	if (fop != NULL) {
		int                                   rc;
		struct m0_fop_mgmt_service_state_req *ssfop;

		ssfop = m0_fop_data(fop);
		rc = m0_addb_ctx_export(&m0_mgmt_addb_ctx,
					&ssfop->mssrq_addb_ctx_id);
		if (rc != 0) {
			m0_fop_put(fop);
			fop = NULL;
		}
	}
	return fop;
}

/*
 ******************************************************************************
 * Tests
 ******************************************************************************
 */
#ifdef ENABLE_FAULT_INJECTION
static void test_mgmt_svc_fail(void)
{
	int                        rc;
	struct m0_reqh            *rh;
	struct m0_ut_rpc_mach_ctx  rmach_ctx;

	/* Force failure during allocate */
	M0_ALLOC_PTR(rh);
	M0_UT_ASSERT(rh != NULL);
	rc = M0_REQH_INIT(rh,
			  .rhia_db      = &mgmt_svc_dbenv,
			  .rhia_mdstore = (void *)1,
			  .rhia_fol     = (void *)1);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_INIT);
	m0_fi_enable_once("m0_mgmt_service_allocate", "-EFAULT");

	/* Hack */
	M0_SET0(&rmach_ctx);
	rmach_ctx.rmc_cob_id.id = DUMMY_COB_ID;
	rmach_ctx.rmc_dbname    = DUMMY_DBNAME;
	rmach_ctx.rmc_ep_addr   = DUMMY_SERVER_ADDR;
	m0_ut_rpc_mach_init_and_add(&rmach_ctx);
	m0_reqh_rpc_mach_tlink_del_fini(&rmach_ctx.rmc_rpc);
	m0_reqh_rpc_mach_tlink_init_at_tail(&rmach_ctx.rmc_rpc,
					    &rh->rh_rpc_machines);

	M0_UT_ASSERT(m0_reqh_mgmt_service_start(rh) == -EFAULT);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_STOPPED);
	m0_ut_rpc_mach_fini(&rmach_ctx);
	m0_reqh_fini(rh);
	m0_free(rh);

	/* force failure during start */
	M0_ALLOC_PTR(rh);
	M0_UT_ASSERT(rh != NULL);
	rc = M0_REQH_INIT(rh,
			  .rhia_db      = &mgmt_svc_dbenv,
			  .rhia_mdstore = (void *)1,
			  .rhia_fol     = (void *)1);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_INIT);
	m0_fi_enable_once("mgmt_svc_rso_start", "-ECANCELED");

	/* Hack */
	M0_SET0(&rmach_ctx);
	rmach_ctx.rmc_cob_id.id = DUMMY_COB_ID;
	rmach_ctx.rmc_dbname    = DUMMY_DBNAME;
	rmach_ctx.rmc_ep_addr   = DUMMY_SERVER_ADDR;
	m0_ut_rpc_mach_init_and_add(&rmach_ctx);
	m0_reqh_rpc_mach_tlink_del_fini(&rmach_ctx.rmc_rpc);
	m0_reqh_rpc_mach_tlink_init_at_tail(&rmach_ctx.rmc_rpc,
					    &rh->rh_rpc_machines);
	M0_UT_ASSERT(m0_reqh_mgmt_service_start(rh) == -ECANCELED);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_STOPPED);
	m0_ut_rpc_mach_fini(&rmach_ctx);
	m0_reqh_fini(rh);
	m0_free(rh);
}
#endif /* ENABLE_FAULT_INJECTION */

static void test_reqh_fop_allow(void)
{
	int                        rc;
	struct m0_fop             *ss_fop;
	struct m0_fop             *f_fop;
	int                        rfp_cnt;
	struct m0_reqh            *rh;
	struct m0_ut_rpc_mach_ctx  rmach_ctx;

	M0_ALLOC_PTR(rh);
	M0_UT_ASSERT(rh != NULL);
	rc = M0_REQH_INIT(rh,
			  .rhia_db      = &mgmt_svc_dbenv,
			  .rhia_mdstore = (void *)1,
			  .rhia_fol     = (void *)1);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_INIT);

	/* Hack */
	M0_SET0(&rmach_ctx);
	rmach_ctx.rmc_cob_id.id = DUMMY_COB_ID;
	rmach_ctx.rmc_dbname    = DUMMY_DBNAME;
	rmach_ctx.rmc_ep_addr   = DUMMY_SERVER_ADDR;
	m0_ut_rpc_mach_init_and_add(&rmach_ctx);
	m0_reqh_rpc_mach_tlink_del_fini(&rmach_ctx.rmc_rpc);
	m0_reqh_rpc_mach_tlink_init_at_tail(&rmach_ctx.rmc_rpc,
					    &rh->rh_rpc_machines);
	ss_fop = mgmt_svc_ut_ss_fop_alloc();
	M0_UT_ASSERT(ss_fop != NULL);
	f_fop = mgmt_svc_ut_fake_fop_alloc();
	M0_UT_ASSERT(f_fop != NULL);

	M0_UT_ASSERT(m0_reqh_fop_allow(rh, ss_fop) == -EAGAIN);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, f_fop) == -EAGAIN);

	M0_UT_ASSERT(m0_reqh_mgmt_service_start(rh) == 0);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_MGMT_STARTED);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, ss_fop) == 0);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, f_fop) == -EAGAIN);

	mgmt_svc_ut_rso_fop_accept_intercept(rh);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_called == 0);
	rfp_cnt = 0;
	M0_UT_ASSERT(mgmt_svc != NULL);
	M0_UT_ASSERT(m0_reqh_service_state_get(mgmt_svc) == M0_RST_STARTED);

	M0_UT_ASSERT(m0_reqh_fop_allow(rh, ss_fop) == 0);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_called == ++rfp_cnt);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_rc == 0);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_called == rfp_cnt);

	/* start our fake service */
	M0_UT_ASSERT(mgmt_svc_ut_svc_start(rh, false) == 0);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, f_fop) == -EAGAIN); /* no delivery */

	/* normal mode */
	m0_reqh_start(rh);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_NORMAL);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, ss_fop) == 0);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_called == rfp_cnt); /* no call */
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, f_fop) == 0); /* normal delivery */

	/* stop our fake service */
	m0_reqh_service_stop(&mgmt_svc_ut_fake_svc->msus_reqhs);
	m0_reqh_service_fini(&mgmt_svc_ut_fake_svc->msus_reqhs);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, f_fop) == -ECONNREFUSED);

	/* restart our fake service */
	M0_UT_ASSERT(mgmt_svc_ut_svc_start(rh, false) == 0);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, f_fop) == 0); /* normal delivery */

	m0_reqh_shutdown_wait(rh);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_DRAIN);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, ss_fop) == 0); /* mgmt fop passes */
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_called == ++rfp_cnt);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_rc == 0);

	M0_UT_ASSERT(m0_reqh_service_state_get(&mgmt_svc_ut_fake_svc->msus_reqhs
	                                      ) == M0_RST_STOPPING);
	M0_UT_ASSERT(mgmt_svc_ut_svc_ops.rso_fop_accept != NULL); /* has mthd */
	mgmt_svc_ut_svc_rso_fop_accept_rc = -ESHUTDOWN;
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, f_fop) == -ESHUTDOWN); /* blocked */
	mgmt_svc_ut_svc_rso_fop_accept_rc = 0;
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, f_fop) == 0);        /* permitted */
	mgmt_svc_ut_svc_ops.rso_fop_accept = NULL;                /* no mthd */
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, f_fop) == -ESHUTDOWN); /* default */
	mgmt_svc_ut_svc_restore_defaults();

	/* Temporarily fake the reqh state to M0_REQH_ST_SVCS_STOP to test that
	   the management service's rso_fop_accept method works in this state.
	 */
	rh->rh_sm.sm_state = M0_REQH_ST_SVCS_STOP; /* HACK */
	M0_UT_ASSERT(m0_reqh_service_state_get(mgmt_svc) == M0_RST_STARTED);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, ss_fop) == 0);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_called == ++rfp_cnt);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_rc == 0);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, f_fop) == -ESHUTDOWN); /* blocked */
	rh->rh_sm.sm_state = M0_REQH_ST_DRAIN; /* HACK to undo prev HACK */

	/* Real M0_REQH_ST_SVCS_STOP traversed below */
	m0_reqh_services_terminate(rh);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_MGMT_STOP);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, ss_fop) == -ESHUTDOWN);
	M0_UT_ASSERT(mgmt_svc_rso_fop_accept_called == rfp_cnt); /* no call */
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, f_fop) == -ESHUTDOWN);

	mgmt_svc_ut_rso_fop_restore();

	m0_reqh_mgmt_service_stop(rh);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_STOPPED);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, ss_fop) == -ESHUTDOWN);
	M0_UT_ASSERT(m0_reqh_fop_allow(rh, f_fop) == -ESHUTDOWN);

	m0_fop_put(ss_fop);
	m0_fop_put(f_fop);
	m0_ut_rpc_mach_fini(&rmach_ctx);
	m0_reqh_fini(rh);
	m0_free(rh);
}

static void test_run_fom_rso_start_async(void)
{
	int                        rc;
	struct m0_reqh            *rh;
	struct m0_ut_rpc_mach_ctx  rmach_ctx;

	mgmt_svc_ut_svc_restore_defaults_async();

	M0_ALLOC_PTR(rh);
	M0_UT_ASSERT(rh != NULL);
	rc = M0_REQH_INIT(rh,
			  .rhia_db      = &mgmt_svc_dbenv,
			  .rhia_mdstore = (void *)1,
			  .rhia_fol     = (void *)1);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_INIT);

	/* Hack */
	M0_SET0(&rmach_ctx);
	rmach_ctx.rmc_cob_id.id = DUMMY_COB_ID;
	rmach_ctx.rmc_dbname    = DUMMY_DBNAME;
	rmach_ctx.rmc_ep_addr   = DUMMY_SERVER_ADDR;
	m0_ut_rpc_mach_init_and_add(&rmach_ctx);
	m0_reqh_rpc_mach_tlink_del_fini(&rmach_ctx.rmc_rpc);
	m0_reqh_rpc_mach_tlink_init_at_tail(&rmach_ctx.rmc_rpc,
					    &rh->rh_rpc_machines);

	M0_UT_ASSERT(m0_reqh_mgmt_service_start(rh) == 0);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_MGMT_STARTED);
	mgmt_svc = rh->rh_mgmt_svc;
	M0_UT_ASSERT(mgmt_svc != NULL);
	M0_UT_ASSERT(m0_reqh_service_state_get(mgmt_svc) == M0_RST_STARTED);

	/* reqh in normal mode */
	m0_reqh_start(rh);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_NORMAL);

	/*
         ***************************************************************
	 * Start our fake service asynchronously with rso_start_async().
         ***************************************************************
	 */
	M0_LOG(M0_DEBUG, "Test: rso_start_async()/success");
	M0_UT_ASSERT(mgmt_svc_ut_svc_ops.rso_start_async != NULL);
	M0_UT_ASSERT(mgmt_svc_ut_svc_start(rh, true) == 0);

	/* wait until the reqh services start */
	M0_LOG(M0_DEBUG, "Waiting for startup with rso_start_async()");
	m0_mgmt_reqh_services_start_wait(rh);
	M0_LOG(M0_DEBUG, "Startup complete");
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_NORMAL);
	M0_UT_ASSERT(mgmt_svc_ut_rso_start_async_called == 1);
	M0_UT_ASSERT(mgmt_svc_ut_start_timer_called == 1);
	M0_UT_ASSERT(mgmt_svc_ut_rso_start_called == 0);
	M0_UT_ASSERT(mgmt_svc_ut_rso_fini_called == 0);
	M0_UT_ASSERT(m0_reqh_service_state_get(&mgmt_svc_ut_fake_svc->msus_reqhs
					       ) == M0_RST_STARTED);

	/* terminate the service */
	m0_reqh_service_stop(&mgmt_svc_ut_fake_svc->msus_reqhs);
	M0_UT_ASSERT(m0_reqh_service_state_get(&mgmt_svc_ut_fake_svc->msus_reqhs
					       ) == M0_RST_STOPPED);
	m0_reqh_service_fini(&mgmt_svc_ut_fake_svc->msus_reqhs);
	M0_UT_ASSERT(mgmt_svc_ut_rso_fini_called == 1);
	M0_UT_ASSERT(mgmt_svc_ut_fake_svc == NULL);
	mgmt_svc_ut_svc_restore_defaults_async();

	/*
         ***************************************************************
	 * Repeat with rso_start_async() but force background failure
         ***************************************************************
	 */
	M0_LOG(M0_DEBUG, "Test: rso_start_async()/init fail");
	mgmt_svc_ut_start_async_result = -ENOSYS;
	M0_UT_ASSERT(mgmt_svc_ut_svc_start(rh, true) == 0);

	/* wait until the reqh services start */
	M0_LOG(M0_DEBUG, "Waiting for startup with rso_start_async()");
	m0_mgmt_reqh_services_start_wait(rh);
	M0_LOG(M0_DEBUG, "Startup complete");
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_NORMAL);
	M0_UT_ASSERT(mgmt_svc_ut_rso_start_async_called == 1);
	M0_UT_ASSERT(mgmt_svc_ut_start_timer_called == 1);
	M0_UT_ASSERT(mgmt_svc_ut_rso_start_called == 0);
	M0_UT_ASSERT(mgmt_svc_ut_rso_fini_called == 0);
	M0_UT_ASSERT(m0_reqh_service_state_get(&mgmt_svc_ut_fake_svc->msus_reqhs
					       ) == M0_RST_FAILED);
	m0_reqh_service_fini(&mgmt_svc_ut_fake_svc->msus_reqhs);
	M0_UT_ASSERT(mgmt_svc_ut_rso_fini_called == 1);
	M0_UT_ASSERT(mgmt_svc_ut_fake_svc == NULL);
	mgmt_svc_ut_svc_restore_defaults_async();

	/*
         ***************************************************************
	 * Repeat with rso_start_async() but force it to fail
         ***************************************************************
	 */
	M0_LOG(M0_DEBUG, "Test: rso_start_async()/op fail");
	mgmt_svc_ut_start_async_rc = -ENOSYS;
	M0_UT_ASSERT(mgmt_svc_ut_svc_start(rh, true) == 0);

	/* wait until the reqh services start */
	M0_LOG(M0_DEBUG, "Waiting for startup with rso_start_async()");
	m0_mgmt_reqh_services_start_wait(rh);
	M0_LOG(M0_DEBUG, "Startup complete");
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_NORMAL);
	M0_UT_ASSERT(mgmt_svc_ut_rso_start_async_called == 1);
	M0_UT_ASSERT(mgmt_svc_ut_start_timer_called == 0);
	M0_UT_ASSERT(mgmt_svc_ut_rso_start_called == 0);
	M0_UT_ASSERT(mgmt_svc_ut_rso_fini_called == 0);
	M0_UT_ASSERT(m0_reqh_service_state_get(&mgmt_svc_ut_fake_svc->msus_reqhs
					       ) == M0_RST_FAILED);
	m0_reqh_service_fini(&mgmt_svc_ut_fake_svc->msus_reqhs);
	M0_UT_ASSERT(mgmt_svc_ut_rso_fini_called == 1);
	M0_UT_ASSERT(mgmt_svc_ut_fake_svc == NULL);
	mgmt_svc_ut_svc_restore_defaults_async();

#ifdef ENABLE_FAULT_INJECTION
	/*
         ***************************************************************
	 * Repeat with rso_start_async() but force FOP creation failure.
	 * This exercises the INITIALIZED -> FAILED state transition.
         ***************************************************************
	 */
	M0_LOG(M0_DEBUG, "Test: rso_start_async()/fop fail");
	m0_fi_enable_once("mgmt_run_fom_create", "-ECANCELED");
	M0_UT_ASSERT(mgmt_svc_ut_svc_start(rh, true) == -ECANCELED);

	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_NORMAL);
	M0_UT_ASSERT(mgmt_svc_ut_rso_start_async_called == 0);
	M0_UT_ASSERT(mgmt_svc_ut_start_timer_called == 0);
	M0_UT_ASSERT(mgmt_svc_ut_rso_start_called == 0);
	M0_UT_ASSERT(mgmt_svc_ut_rso_fini_called == 0);
	M0_UT_ASSERT(mgmt_svc_ut_fake_svc != NULL);
	M0_UT_ASSERT(m0_reqh_service_state_get(&mgmt_svc_ut_fake_svc->msus_reqhs
					       ) == M0_RST_FAILED);
	m0_reqh_service_fini(&mgmt_svc_ut_fake_svc->msus_reqhs);
	M0_UT_ASSERT(mgmt_svc_ut_rso_fini_called == 1);
	M0_UT_ASSERT(mgmt_svc_ut_fake_svc == NULL);
	mgmt_svc_ut_svc_restore_defaults_async();

#endif /* ENABLE_FAULT_INJECTION */

	/* finalize test */
	m0_reqh_services_terminate(rh);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_MGMT_STOP);

	m0_reqh_mgmt_service_stop(rh);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_STOPPED);

	m0_ut_rpc_mach_fini(&rmach_ctx);
	m0_reqh_fini(rh);
	m0_free(rh);
}

static void test_run_fom_rso_start(void)
{
	int                        rc;
	struct m0_reqh            *rh;
	struct m0_ut_rpc_mach_ctx  rmach_ctx;

	mgmt_svc_ut_svc_restore_defaults();

	M0_ALLOC_PTR(rh);
	M0_UT_ASSERT(rh != NULL);
	rc = M0_REQH_INIT(rh,
			  .rhia_db      = &mgmt_svc_dbenv,
			  .rhia_mdstore = (void *)1,
			  .rhia_fol     = (void *)1);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_INIT);

	/* Hack */
	M0_SET0(&rmach_ctx);
	rmach_ctx.rmc_cob_id.id = DUMMY_COB_ID;
	rmach_ctx.rmc_dbname    = DUMMY_DBNAME;
	rmach_ctx.rmc_ep_addr   = DUMMY_SERVER_ADDR;
	m0_ut_rpc_mach_init_and_add(&rmach_ctx);
	m0_reqh_rpc_mach_tlink_del_fini(&rmach_ctx.rmc_rpc);
	m0_reqh_rpc_mach_tlink_init_at_tail(&rmach_ctx.rmc_rpc,
					    &rh->rh_rpc_machines);

	M0_UT_ASSERT(m0_reqh_mgmt_service_start(rh) == 0);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_MGMT_STARTED);
	mgmt_svc = rh->rh_mgmt_svc;
	M0_UT_ASSERT(mgmt_svc != NULL);
	M0_UT_ASSERT(m0_reqh_service_state_get(mgmt_svc) == M0_RST_STARTED);

	/* reqh in normal mode */
	m0_reqh_start(rh);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_NORMAL);

	/*
         ***************************************************************
	 * Start our fake service asynchronously with rso_start().
         ***************************************************************
	 */
	M0_LOG(M0_DEBUG, "Test: rso_start()/success");
	mgmt_svc_ut_svc_ops.rso_start_async = NULL;
	M0_UT_ASSERT(mgmt_svc_ut_svc_start(rh, true) == 0);

	/* wait until the reqh services start */
	M0_LOG(M0_DEBUG, "Waiting for startup with rso_start()");
	m0_mgmt_reqh_services_start_wait(rh);
	M0_LOG(M0_DEBUG, "Startup complete");
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_NORMAL);
	M0_UT_ASSERT(mgmt_svc_ut_rso_start_async_called == 0);
	M0_UT_ASSERT(mgmt_svc_ut_start_timer_called == 0);
	M0_UT_ASSERT(mgmt_svc_ut_rso_start_called == 1);
	M0_UT_ASSERT(mgmt_svc_ut_rso_fini_called == 0);
	M0_UT_ASSERT(m0_reqh_service_state_get(&mgmt_svc_ut_fake_svc->msus_reqhs
					       ) == M0_RST_STARTED);

	/* terminate the service */
	m0_reqh_service_stop(&mgmt_svc_ut_fake_svc->msus_reqhs);
	M0_UT_ASSERT(m0_reqh_service_state_get(&mgmt_svc_ut_fake_svc->msus_reqhs
					       ) == M0_RST_STOPPED);
	m0_reqh_service_fini(&mgmt_svc_ut_fake_svc->msus_reqhs);
	M0_UT_ASSERT(mgmt_svc_ut_rso_fini_called == 1);
	M0_UT_ASSERT(mgmt_svc_ut_fake_svc == NULL);
	mgmt_svc_ut_svc_restore_defaults_async();

	/*
         ***************************************************************
	 * Start asynchronously with rso_start() but force failure.
         ***************************************************************
	 */
	M0_LOG(M0_DEBUG, "Test: rso_start()/op fail");
	mgmt_svc_ut_svc_ops.rso_start_async = NULL;
	mgmt_svc_ut_start_rc = -ENOSYS;
	M0_UT_ASSERT(mgmt_svc_ut_svc_start(rh, true) == 0);

	/* wait until the reqh services start */
	M0_LOG(M0_DEBUG, "Waiting for startup with rso_start()");
	m0_mgmt_reqh_services_start_wait(rh);
	M0_LOG(M0_DEBUG, "Startup complete");
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_NORMAL);
	M0_UT_ASSERT(mgmt_svc_ut_rso_start_async_called == 0);
	M0_UT_ASSERT(mgmt_svc_ut_start_timer_called == 0);
	M0_UT_ASSERT(mgmt_svc_ut_rso_start_called == 1);
	M0_UT_ASSERT(mgmt_svc_ut_rso_fini_called == 0);
	M0_UT_ASSERT(m0_reqh_service_state_get(&mgmt_svc_ut_fake_svc->msus_reqhs
					       ) == M0_RST_FAILED);
	m0_reqh_service_fini(&mgmt_svc_ut_fake_svc->msus_reqhs);
	M0_UT_ASSERT(mgmt_svc_ut_rso_fini_called == 1);
	M0_UT_ASSERT(mgmt_svc_ut_fake_svc == NULL);
	mgmt_svc_ut_svc_restore_defaults_async();

	/* finalize test */
	m0_reqh_services_terminate(rh);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_MGMT_STOP);

	m0_reqh_mgmt_service_stop(rh);
	M0_UT_ASSERT(m0_reqh_state_get(rh) == M0_REQH_ST_STOPPED);

	m0_ut_rpc_mach_fini(&rmach_ctx);
	m0_reqh_fini(rh);
	m0_free(rh);
}

static void test_status_query(void)
{
	int rc;
	int i;
	struct m0_fop *ss_fop;
	struct m0_fop *ssr_fop;
	struct m0_rpc_item *item;
	struct m0_fop_mgmt_service_state_res *ssr;
	char uuid[M0_UUID_STRLEN+1];
	bool foundFakeService = false;

	M0_UT_ASSERT(mgmt_svc_ut_setup_start() == 0);

	ss_fop = mgmt_svc_ut_ss_fop_alloc();
	M0_UT_ASSERT(ss_fop != NULL);
	/** @todo figure out how to ensure that the server is ready.
	    Retry handles this for now.
	 */
	M0_LOG(M0_DEBUG, "Sending SS FOP");
	rc = m0_rpc_client_call(ss_fop, &mgmt_svc_ut_cctx.rcx_session, NULL, 0);
	M0_LOG(M0_DEBUG, "rpc_client_call: %d", rc);
	M0_UT_ASSERT(rc == 0);

	item = &ss_fop->f_item;
	M0_UT_ASSERT(item->ri_error == 0);
	ssr_fop = m0_rpc_item_to_fop(item->ri_reply);
	M0_UT_ASSERT(ssr_fop != NULL);
	ssr = m0_fop_data(ssr_fop);
	M0_LOG(M0_DEBUG, "msr_rc: %d", ssr->msr_rc);
	M0_LOG(M0_DEBUG, "msr_reqh_state: %d", ssr->msr_reqh_state);
	M0_UT_ASSERT(ssr->msr_rc == 0);
	M0_UT_ASSERT(ssr->msr_reqh_state == M0_REQH_ST_NORMAL);
	for (i = 0; i < ssr->msr_ss.msss_nr; ++i) {
		struct m0_mgmt_service_state *ss = &ssr->msr_ss.msss_state[i];

		m0_uuid_format(&ss->mss_uuid, uuid, ARRAY_SIZE(uuid));
		M0_LOG(M0_DEBUG, "\t%s %d", &uuid[0], ss->mss_state);
		M0_UT_ASSERT(ss->mss_state == M0_RST_STARTED);
		if (strcasecmp(uuid, M0_MGMT_SVC_UT_SVC_UUID) == 0)
			foundFakeService = true;
	}
	M0_UT_ASSERT(foundFakeService);

	m0_fop_put(ss_fop);
	mgmt_svc_ut_setup_stop();
}

static int test_init(void)
{
	int rc;

	rc = m0_dbenv_init(&mgmt_svc_dbenv, "something", 0);
	M0_ASSERT(rc == 0);
	m0_xc_mgmt_svc_ut_init();
	return m0_reqh_service_type_register(&m0_mgmt_svc_ut_svc_type) ?:
		mgmt_svc_ut_fake_fop_init();
}

static int test_fini(void)
{
	mgmt_svc_ut_fake_fop_fini();
        m0_reqh_service_type_unregister(&m0_mgmt_svc_ut_svc_type);
	m0_xc_mgmt_svc_ut_fini();
	m0_dbenv_fini(&mgmt_svc_dbenv);
	return 0;
}

const struct m0_test_suite m0_mgmt_svc_ut = {
	.ts_name = "mgmt-svc-ut",
	.ts_init = test_init,
	.ts_fini = test_fini,
	.ts_tests = {
		{ "reqh-fop-allow",           test_reqh_fop_allow },
		{ "run-fom-rso-start-async",  test_run_fom_rso_start_async },
		{ "run-fom-rso-start",        test_run_fom_rso_start },
#ifdef ENABLE_FAULT_INJECTION
		{ "mgmt-svc-startup-failure", test_mgmt_svc_fail },
#endif
		{ "status-query",             test_status_query },
		{ NULL, NULL }
	}
};
M0_EXPORTED(m0_mgmt_svc_ut);

#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
