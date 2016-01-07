/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Subhash Arya <subhash_arya@xyratex.com>
 * Original creation date: 09/25/2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CM
#include "lib/trace.h"
#include "lib/finject.h"
#include "lib/memory.h"
#include "ut/ut.h"
#include "lib/misc.h"
#include "lib/thread.h"

#include "rpc/rpclib.h"

#include "reqh/reqh.h"
#include "reqh/reqh_service.h"
#include "ioservice/io_device.h"
#include "pool/pool.h"

#include "sns/cm/cm.h"

#include "cm/cm.h"
#include "cm/cp.h"
#include "cm/ag.h"
#include "cm/ut/common_service.h"

#include "conf/ut/common.h"
#include "conf/preload.h"
#include "ut/file_helpers.h"
#include <unistd.h>			/* usleep */
#include "lib/locality.h"

struct m0_rpc_server_ctx cm_ut_sctx;
struct m0_net_xprt *xprt = &m0_net_lnet_xprt;
static const char *SERVER_LOGFILE = "cm_ut.log";
char  *cm_ut_server_args[] = { "m0d", "-T", "LINUX",
				"-D", "sr_db", "-S", "sr_stob",
				"-A", "linuxstob:sr_addb_stob",
				"-f", "<0x7200000000000001:1>",
				"-w", "10",
				"-G", "lnet:0@lo:12345:34:1",
				"-e", "lnet:0@lo:12345:34:1",
				"-P", M0_UT_CONF_PROFILE,
				"-c", M0_UT_PATH("conf-str.txt")};

static void cm_ut_server_start(void)
{
	int rc;

	cm_ut_sctx.rsx_xprts            = &xprt;
	cm_ut_sctx.rsx_xprts_nr         = 1;
	cm_ut_sctx.rsx_argv             = cm_ut_server_args;
	cm_ut_sctx.rsx_argc             = ARRAY_SIZE(cm_ut_server_args);
	cm_ut_sctx.rsx_log_file_name    = SERVER_LOGFILE;

	rc = m0_rpc_server_start(&cm_ut_sctx);
	M0_UT_ASSERT(rc == 0);
}

static void cm_ut_server_stop(void)
{
	m0_rpc_server_stop(&cm_ut_sctx);
}

static struct m0_cm *cm_ut_sctx2cm(void)
{
	struct m0_reqh         *reqh;
	struct m0_reqh_service *svc;

	reqh = m0_cs_reqh_get(&cm_ut_sctx.rsx_mero_ctx);
	svc = m0_reqh_service_find(m0_reqh_service_type_find("sns_repair"),
				   reqh);
	return container_of(svc, struct m0_cm, cm_service);
}

static int cm_ut_init(void)
{
	int rc;

	M0_SET0(&cmut_rmach_ctx);
	cmut_rmach_ctx.rmc_cob_id.id = DUMMY_COB_ID;
	cmut_rmach_ctx.rmc_dbname    = DUMMY_DBNAME;
	cmut_rmach_ctx.rmc_ep_addr   = DUMMY_SERVER_ADDR;
	m0_ut_rpc_mach_init_and_add(&cmut_rmach_ctx);

	rc = m0_cm_type_register(&cm_ut_cmt);
	M0_ASSERT(rc == 0);

	cm_ut_server_start();

	return 0;
}

static int cm_ut_fini(void)
{
	cm_ut_server_stop();
	m0_cm_type_deregister(&cm_ut_cmt);
	m0_ut_rpc_mach_fini(&cmut_rmach_ctx);

	return 0;
}

static void cm_setup_ut(void)
{
	struct m0_locality *locality;
	struct m0_cm       *cm = &cm_ut[0].ut_cm;
	struct m0_confc    *confc;
	char                local_conf[M0_CONF_STR_MAXLEN];
	int                 rc;

	cm_ut_service_alloc_init();
	confc = &cm_ut_service->rs_reqh->rh_confc;
	rc = m0_ut_file_read(M0_UT_PATH("diter_xc.txt"), local_conf,
			     sizeof local_conf);
	M0_UT_ASSERT(rc == 0);
	locality = m0_locality0_get();

	rc = m0_confc_init(confc, locality->lo_grp, NULL, NULL, local_conf);
	M0_UT_ASSERT(rc == 0);
	/* Internally calls m0_cm_setup(). */
	rc = m0_reqh_service_start(cm_ut_service);
	M0_UT_ASSERT(rc == 0);

	cm_ut_service->rs_reqh_ctx->rc_mero->cc_profile = M0_UT_CONF_PROFILE;
	rc = m0_fid_sscanf(cm_ut_service->rs_reqh_ctx->rc_mero->cc_profile,
			   &cm_ut_service->rs_reqh->rh_profile);
	M0_UT_ASSERT(rc == 0);

	m0_cm_lock(cm);
	rc = cm->cm_ops->cmo_prepare(cm);
	M0_UT_ASSERT(rc == 0);
	m0_cm_state_set(cm, M0_CMS_PREPARE);
	m0_cm_sw_update_start(cm);
	m0_cm_cp_pump_prepare(cm);
	/*
	 * Start sliding window update FOM to avoid failure during
	 * m0_cm_stop().
	 */
	m0_cm_state_set(cm, M0_CMS_READY);
	m0_cm_unlock(cm);
	/* Checks if the restructuring process is started successfully. */
	rc = m0_cm_start(cm);
	M0_UT_ASSERT(rc == 0);
	cm->cm_sw_update.swu_is_complete = true;
	while (m0_fom_domain_is_idle_for(&cm->cm_service) ||
	       !m0_cm_cp_pump_is_complete(&cm->cm_cp_pump))
		usleep(200);

	m0_reqh_shutdown_wait(&cmut_rmach_ctx.rmc_reqh);
	m0_confc_fini(confc);
	cm_ut_service_cleanup();
}

static void cm_init_failure_ut(void)
{
	int rc;

	m0_fi_enable_once("m0_cm_init", "init_failure");
	rc = m0_reqh_service_allocate(&cm_ut_service, &cm_ut_cmt.ct_stype,
				      NULL);
	/* Set the global cm_ut_service pointer to NULL */
	cm_ut_service = NULL;
	ut_cm_id = 0;
	M0_SET0(&cm_ut[0].ut_cm);
	M0_UT_ASSERT(rc != 0);
}

static void cm_setup_failure_ut(void)
{
	int rc;

	cm_ut_service_alloc_init();
	m0_fi_enable_once("m0_cm_setup", "setup_failure_2");
	rc = m0_reqh_service_start(cm_ut_service);
	M0_UT_ASSERT(rc != 0);
	m0_reqh_service_fini(cm_ut_service);
}

static void cm_prepare_failure_ut(void)
{
	struct m0_cm *cm = cm_ut_sctx2cm();
	int           rc;

	m0_fi_enable_once("m0_cm_prepare", "prepare_failure");
	rc = m0_cm_prepare(cm);
	M0_UT_ASSERT(rc != 0);
}

static void cm_wait_pre(struct m0_cm *cm, struct m0_clink *clink)
{
	m0_clink_init(clink, NULL);
	clink->cl_is_oneshot = true;
	m0_mutex_lock(&cm->cm_wait_mutex);
	m0_clink_add(&cm->cm_wait, clink);
	m0_mutex_unlock(&cm->cm_wait_mutex);
}

static void cm_wait_post(struct m0_cm *cm, struct m0_clink *clink)
{
	m0_chan_wait(clink);
	m0_clink_fini(clink);
}

static void cm_ready_failure_ut(void)
{
	struct m0_cm     *cm = cm_ut_sctx2cm();
	struct m0_sns_cm *scm = cm2sns(cm);
	struct m0_clink   cm_wait_link;
	int               rc;

	m0_fi_enable_once("m0_cm_ready", "ready_failure");
	cm_wait_pre(cm, &cm_wait_link);
	scm->sc_op = SNS_REPAIR;
	rc = m0_cm_prepare(cm);
	M0_UT_ASSERT(rc == 0);
	cm_wait_post(cm, &cm_wait_link);
	rc = m0_cm_ready(cm);
	M0_UT_ASSERT(rc != 0);
	m0_reqh_idle_wait_for(cm->cm_service.rs_reqh, &cm->cm_service);
}

static void cm_start_failure_ut(void)
{
	struct m0_cm     *cm = cm_ut_sctx2cm();
	struct m0_sns_cm *scm = cm2sns(cm);
	struct m0_clink   cm_wait_link;
	int               rc;

	m0_fi_enable_once("m0_cm_start", "start_failure");
	cm_wait_pre(cm, &cm_wait_link);
	scm->sc_op = SNS_REPAIR;
	rc = m0_cm_prepare(cm);
	M0_UT_ASSERT(rc == 0);
	cm_wait_post(cm, &cm_wait_link);
	//cm_wait_pre(cm, &cm_wait_link);
	rc = m0_cm_ready(cm);
	M0_UT_ASSERT(rc == 0);
	//cm_wait_post(cm, &cm_wait_link);
	rc = m0_cm_start(cm);
	M0_UT_ASSERT(rc != 0);
	m0_reqh_idle_wait_for(cm->cm_service.rs_reqh, &cm->cm_service);
}

static void ag_id_assign(struct m0_cm_ag_id *id, uint64_t hi_hi, uint64_t hi_lo,
			 uint64_t lo_hi, uint64_t lo_lo)
{
	id->ai_hi.u_hi = hi_hi;
	id->ai_hi.u_lo = hi_lo;
	id->ai_lo.u_hi = lo_hi;
	id->ai_lo.u_lo = lo_lo;
}

static void ag_id_test_cmp()
{
	struct m0_cm_ag_id id0;
	struct m0_cm_ag_id id1;
	int    rc;

	/* Assign random test values to aggregation group ids. */
	ag_id_assign(&id0, 2, 3, 4, 5);
	ag_id_assign(&id1, 4, 4, 4, 4);
	rc = m0_cm_ag_id_cmp(&id0, &id1);
	M0_UT_ASSERT(rc < 0);
	rc = m0_cm_ag_id_cmp(&id1, &id0);
	M0_UT_ASSERT(rc > 0);
	rc = m0_cm_ag_id_cmp(&id0, &id0);
	M0_UT_ASSERT(rc == 0);
}

static void ag_id_test_find()
{
	struct m0_cm_ag_id	 id;
	int			 i;
	int			 rc;
	struct m0_cm_aggr_group *ag;
	struct m0_cm            *cm = &cm_ut[0].ut_cm;

	for (i = AG_ID_NR - 1; i >= 0; --i) {
		ag_id_assign(&id, i, i, i, i);
		ag = m0_cm_aggr_group_locate(cm, &id, false);
		M0_UT_ASSERT(ag != NULL);
		rc = m0_cm_ag_id_cmp(&id, &ag->cag_id);
		M0_UT_ASSERT(rc == 0);
	}
	ag_id_assign(&id, 10, 35, 2, 3);
	ag = m0_cm_aggr_group_locate(cm, &id, false);
	M0_UT_ASSERT(ag == NULL);
}

static void ag_list_test_sort()
{
	struct m0_cm_aggr_group *found;
	struct m0_cm_aggr_group *prev_ag;
	struct m0_cm            *cm = &cm_ut[0].ut_cm;

	prev_ag = aggr_grps_out_tlist_head(&cm->cm_aggr_grps_out);
	m0_tl_for(aggr_grps_out, &cm->cm_aggr_grps_out, found) {
		M0_UT_ASSERT(m0_cm_ag_id_cmp(&prev_ag->cag_id,
					     &found->cag_id) <= 0);
		prev_ag = found;
	} m0_tl_endfor;

}

static void cm_ag_ut(void)
{
	int                      i;
	int                      j;
	int                      rc;
	struct m0_cm_ag_id       ag_ids[AG_ID_NR];
	struct m0_cm_aggr_group  ags[AG_ID_NR];
	struct m0_cm            *cm;

	test_ready_fop = false;
	M0_UT_ASSERT(ut_cm_id == 0);
	cm = &cm_ut[ut_cm_id].ut_cm;
	cm_ut_service_alloc_init();
	rc = m0_reqh_service_start(cm_ut_service);
	M0_UT_ASSERT(rc == 0);

	m0_cm_lock(cm);
	/* Populate ag & ag ids with test values. */
	for(i = AG_ID_NR - 1, j = 0; i >= 0 ; --i, ++j) {
		ag_id_assign(&ag_ids[j], i, i, i, i);
		m0_cm_aggr_group_init(&ags[j], cm, &ag_ids[j],
				      false, &cm_ag_ut_ops);
		m0_cm_aggr_group_add(cm, &ags[j], false);
	}

	/* Test 3-way comparision. */
	ag_id_test_cmp();

	/* Test aggregation group id search. */
	ag_id_test_find();

	/* Test to check if the aggregation group list is sorted. */
	ag_list_test_sort();

	/* Cleanup. */
	for(i = 0; i < AG_ID_NR; i++)
		m0_cm_aggr_group_fini_and_progress(&ags[i]);
	m0_cm_unlock(cm);

	cm_ut_service_cleanup();
}

struct m0_ut_suite cm_generic_ut = {
        .ts_name = "cm-ut",
        .ts_init = &cm_ut_init,
        .ts_fini = &cm_ut_fini,
        .ts_tests = {
		{ "cm_setup_ut",           cm_setup_ut           },
		{ "cm_setup_failure_ut",   cm_setup_failure_ut   },
		{ "cm_init_failure_ut",    cm_init_failure_ut    },
		{ "cm_prepare_failure_ut", cm_prepare_failure_ut },
		{ "cm_ready_failure_ut",   cm_ready_failure_ut   },
		{ "cm_start_failure_ut",   cm_start_failure_ut   },
		{ "cm_ag_ut",              cm_ag_ut               },
		{ NULL, NULL }
        }
};

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
