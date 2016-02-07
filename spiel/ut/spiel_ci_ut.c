/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Egor Nikulenkov <egor.nikulenkov@seagate.com>
 * Original creation date: 06-Mar-2015
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"

#include "spiel/spiel.h"
#include "spiel/ut/spiel_ut_common.h"
#include "conf/obj_ops.h"     /* M0_CONF_DIRNEXT */
#include "module/instance.h"  /* m0_get */
#include "stob/domain.h"      /* m0_stob_domain */
#include "lib/finject.h"
#include "lib/fs.h"           /* m0_file_read */
#include "conf/ut/common.h"   /* conf_ut_ast_thread_fini */
#include "ut/misc.h"          /* M0_UT_PATH */
#include "ut/ut.h"

static struct m0_spiel spiel;

int spiel_ci_ut_init(void)
{
	int rc;

	rc = m0_spiel__ut_init(&spiel, M0_UT_PATH("conf.xc"), true);
	M0_UT_ASSERT(rc == 0);
	m0_fi_enable("ss_process_quiesce", "keep_confd_rmservice");
	return 0;
}

int spiel_ci_ut_fini(void)
{
	m0_fi_disable("ss_process_quiesce", "keep_confd_rmservice");
	m0_spiel__ut_fini(&spiel, true);
	return 0;
}


static void test_spiel_service_cmds(void)
{
	const struct m0_fid svc_fid = M0_FID_TINIT(
			M0_CONF_SERVICE_TYPE.cot_ftype.ft_id, 1, 10);
	const struct m0_fid svc_invalid_fid = M0_FID_TINIT(
			M0_CONF_SERVICE_TYPE.cot_ftype.ft_id, 1, 13);
	int                 rc;

	spiel_ci_ut_init();

	/* Doing `service stop` intialised during startup. */
	rc = m0_spiel_service_quiesce(&spiel, &svc_fid);
	M0_UT_ASSERT(rc == 0);
	rc = m0_spiel_service_stop(&spiel, &svc_fid);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_service_init(&spiel, &svc_fid);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_service_start(&spiel, &svc_fid);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_service_health(&spiel, &svc_fid);
	/* This is true while the service doesn't implement rso_health */
	M0_UT_ASSERT(rc == M0_HEALTH_UNKNOWN);

	rc = m0_spiel_service_quiesce(&spiel, &svc_fid);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_service_stop(&spiel, &svc_fid);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_service_health(&spiel, &svc_invalid_fid);
	M0_UT_ASSERT(rc == -ENOENT);
	spiel_ci_ut_fini();
}

static void test_spiel_process_services_list(void)
{
	struct m0_spiel_running_svc *svcs;
	int                          rc;
	int                          i;
	const struct m0_fid          proc_fid = M0_FID_TINIT(
				M0_CONF_PROCESS_TYPE.cot_ftype.ft_id, 1, 5);

	spiel_ci_ut_init();
	rc = m0_spiel_process_list_services(&spiel, &proc_fid, &svcs);
	M0_UT_ASSERT(rc > 0);
	M0_UT_ASSERT(svcs != NULL);
	for (i = 0; i < rc; ++i)
		m0_free(svcs[i].spls_name);
	m0_free(svcs);

	spiel_ci_ut_fini();
}

static void test_spiel_process_cmds(void)
{
	int                 rc;
	const struct m0_fid process_fid = M0_FID_TINIT(
				M0_CONF_PROCESS_TYPE.cot_ftype.ft_id, 1, 5);
	const struct m0_fid process_second_fid = M0_FID_TINIT(
				M0_CONF_PROCESS_TYPE.cot_ftype.ft_id, 1, 13);
	const struct m0_fid process_invalid_fid = M0_FID_TINIT(
				M0_CONF_SERVICE_TYPE.cot_ftype.ft_id, 4, 15);

	spiel_ci_ut_init();
	/* Reconfig */
	rc = m0_spiel_process_reconfig(&spiel, &process_invalid_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	m0_fi_enable_once("m0_alloc", "fail_allocation");
	rc = m0_spiel_process_reconfig(&spiel, &process_fid);
	M0_UT_ASSERT(rc == -ENOMEM);

	rc = m0_spiel_process_reconfig(&spiel, &process_second_fid);
	M0_UT_ASSERT(rc == -ENOENT);

	m0_fi_enable_once("ss_process_reconfig", "unit_test");
	rc = m0_spiel_process_reconfig(&spiel, &process_fid);
	M0_UT_ASSERT(rc == 0);

	/* Health */
	rc = m0_spiel_process_health(&spiel, &process_invalid_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_process_health(&spiel, &process_fid);
	M0_UT_ASSERT(rc == M0_HEALTH_GOOD);

	/* Stop */
	rc = m0_spiel_process_stop(&spiel, &process_invalid_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	m0_fi_enable_once("m0_ss_process_stop_fop_release", "no_kill");
	rc = m0_spiel_process_stop(&spiel, &process_fid);
	M0_UT_ASSERT(rc == 0);

	/* Quiesce */
	rc = m0_spiel_process_quiesce(&spiel, &process_invalid_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	/**
	 * Must be last test in tests set -
	 * because switch off services on server side
	 */
	rc = m0_spiel_process_quiesce(&spiel, &process_fid);
	M0_UT_ASSERT(rc == 0);
	spiel_ci_ut_fini();
}

static bool spiel_stob_exists(uint64_t cid)
{
	struct m0_storage_devs *devs = &m0_get()->i_storage_devs;
	struct m0_stob_id       id;
	struct m0_stob         *stob;
	int                     rc;

	m0_stob_id_make(0, cid, &devs->sds_back_domain->sd_id, &id);
	rc = m0_stob_lookup(&id, &stob);
	if (stob != NULL)
		m0_stob_put(stob);
	return rc == 0 && stob != NULL;
}

static void spiel_change_svc_type(struct m0_confc *confc,
				  const struct m0_fid *fid)
{
	struct m0_conf_obj     *obj;
	struct m0_conf_service *svc;
	int                     rc;

	rc = m0_confc_open_by_fid_sync(confc, fid, &obj);
	M0_UT_ASSERT(rc == 0);
	svc = M0_CONF_CAST(obj, m0_conf_service);
	svc->cs_type = M0_CST_IOS;
	m0_confc_close(obj);
}

static void test_spiel_device_cmds(void)
{
	/*
	 * According to ut/conf.xc:
	 * - disk-16 is associated with sdev-15;
	 * - sdev-15 belongs IO service-9;
	 * - disk-55 is associated with sdev-51;
	 * - sdev-51 belongs service-27, which is not an IO service;
	 * - disk-23 does not exist.
	 */
	uint64_t            io_sdev = 15;
	uint64_t            nonio_sdev = 51;
	const struct m0_fid io_disk = M0_FID_TINIT(
				M0_CONF_DISK_TYPE.cot_ftype.ft_id, 1, 16);
	const struct m0_fid nonio_disk = M0_FID_TINIT(
				M0_CONF_DISK_TYPE.cot_ftype.ft_id, 1, 55);
	const struct m0_fid nosuch_disk = M0_FID_TINIT(
				M0_CONF_DISK_TYPE.cot_ftype.ft_id, 1, 23);
	const struct m0_fid nonio_svc = M0_FID_TINIT(
				M0_CONF_SERVICE_TYPE.cot_ftype.ft_id, 1, 27);
	int                 rc;

	spiel_ci_ut_init();
	/*
	 * After mero startup devices are online by default,
	 * so detach them at first.
	 */
	rc = m0_spiel_device_detach(&spiel, &io_disk);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(!spiel_stob_exists(io_sdev));

	rc = m0_spiel_device_format(&spiel, &io_disk);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(!spiel_stob_exists(io_sdev));

	m0_fi_enable_once("m0_storage_dev_attach_by_conf", "no_real_dev");
	rc = m0_spiel_device_attach(&spiel, &io_disk);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(spiel_stob_exists(io_sdev));

	/*
	 * Change type nonio_svc (owner nonio_disk) to IO service for this test
	 * only.
	 * Now client part m0_spiel_device_xxx command process nonio_disk as
	 * IO disk, server part process disk as disk from another node.
	 */
	spiel_change_svc_type(&spiel.spl_rconfc.rc_confc, &nonio_svc);

	rc = m0_spiel_device_format(&spiel, &nonio_disk);
	M0_UT_ASSERT(rc == 0);

	m0_fi_enable_once("m0_storage_dev_attach_by_conf", "no_real_dev");
	rc = m0_spiel_device_attach(&spiel, &nonio_disk);
	M0_UT_ASSERT(rc == 0);
	/*
	 * Stob is not created for device that does not belong IO service
	 * current node.
	 */
	M0_UT_ASSERT(!spiel_stob_exists(nonio_sdev));

	rc = m0_spiel_device_detach(&spiel, &nonio_disk);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(!spiel_stob_exists(nonio_sdev));

	rc = m0_spiel_device_attach(&spiel, &nosuch_disk);
	M0_UT_ASSERT(rc == -ENOENT);
	spiel_ci_ut_fini();
}

static void test_spiel_service_order(void)
{
	int                          rc;
	int                          i;
	struct m0_spiel_running_svc *svcs;
	const struct m0_fid          process_fid = M0_FID_TINIT(
				M0_CONF_PROCESS_TYPE.cot_ftype.ft_id, 1, 5);
	const struct m0_fid          svc_fid = M0_FID_TINIT(
				M0_CONF_SERVICE_TYPE.cot_ftype.ft_id, 1, 10);
	bool                         found = false;

	spiel_ci_ut_init();
	/* Doing process quiesce */
	rc = m0_spiel_process_quiesce(&spiel, &process_fid);
	M0_UT_ASSERT(rc == 0);

	/* Doing `service stop` intialised during startup. */
	rc = m0_spiel_service_stop(&spiel, &svc_fid);
	M0_UT_ASSERT(rc == 0);

	/* Doing `service start` after process quiesce. */
	rc = m0_spiel_service_init(&spiel, &svc_fid);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_service_start(&spiel, &svc_fid);
	M0_UT_ASSERT(rc == 0);

	/* Read Service list - rmservice must be started */
	rc = m0_spiel_process_list_services(&spiel, &process_fid, &svcs);
	M0_UT_ASSERT(rc > 0);
	M0_UT_ASSERT(svcs != NULL);
	for (i = 0; i < rc; ++i) {
		found |= strcmp(svcs[i].spls_name, "rmservice") == 0;
		m0_free(svcs[i].spls_name);
	}
	m0_free(svcs);
	M0_UT_ASSERT(found);
	spiel_ci_ut_fini();
}

static uint64_t test_spiel_fs_stats_sdevs_total(struct m0_confc        *confc,
						struct m0_conf_service *ios)
{
	struct m0_conf_obj  *sdevs_dir = &ios->cs_sdevs->cd_obj;
	struct m0_conf_obj  *obj;
	struct m0_conf_sdev *sdev;
	uint64_t             total = 0;
	int                  rc;

	rc = m0_confc_open_sync(&sdevs_dir, sdevs_dir, M0_FID0);
	M0_UT_ASSERT(rc == 0);
	obj = NULL;
	while (m0_confc_readdir_sync(sdevs_dir, &obj) > 0) {
		sdev = M0_CONF_CAST(obj, m0_conf_sdev);
		M0_UT_ASSERT(!m0_addu64_will_overflow(total, sdev->sd_size));
		total += sdev->sd_size;
	}

	m0_confc_close(obj);
	m0_confc_close(sdevs_dir);
	return total;
}

static bool test_spiel_filter_service(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_SERVICE_TYPE;
}

static uint64_t test_spiel_fs_stats_ios_total(const struct m0_fid *fs_fid)
{
	struct m0_confc         confc;
	struct m0_conf_diter    it;
	struct m0_conf_obj     *fs_obj;
	struct m0_conf_service *svc;
	char                   *confstr = NULL;
	int                     rc;
	uint64_t                svc_total = 0;
	uint64_t                total = 0;

	rc = m0_file_read(M0_UT_PATH("conf.xc"), &confstr);
	M0_UT_ASSERT(rc == 0);
	rc = m0_confc_init(&confc, m0_locality0_get()->lo_grp, NULL, NULL,
			   confstr);
	M0_UT_ASSERT(rc == 0);
	m0_free0(&confstr);
	rc = m0_confc_open_by_fid_sync(&confc, fs_fid, &fs_obj);
	M0_UT_ASSERT(rc == 0);
	rc = m0_conf_diter_init(&it, &confc, fs_obj,
				M0_CONF_FILESYSTEM_NODES_FID,
				M0_CONF_NODE_PROCESSES_FID,
				M0_CONF_PROCESS_SERVICES_FID);
	M0_UT_ASSERT(rc == 0);
	while (m0_conf_diter_next_sync(&it, test_spiel_filter_service) ==
	       M0_CONF_DIRNEXT) {
		svc = M0_CONF_CAST(m0_conf_diter_result(&it), m0_conf_service);
		if(svc->cs_type == M0_CST_IOS) {
			svc_total = test_spiel_fs_stats_sdevs_total(&confc,
								    svc);
			M0_UT_ASSERT(!m0_addu64_will_overflow(total,
							      svc_total));
			total += svc_total;
		}
	}
	m0_conf_diter_fini(&it);
	m0_confc_close(fs_obj);
	m0_confc_fini(&confc);
	return total;
}

void test_spiel_fs_stats(void)
{
	int                 rc;
	const struct m0_fid fs_fid = M0_FID_TINIT('f', 1,  1);
	const struct m0_fid fs_bad = M0_FID_TINIT('f', 1,  200);
	struct m0_fs_stats  fs_stats = {0};
	uint64_t            ios_total = test_spiel_fs_stats_ios_total(&fs_fid);

	m0_fi_enable_once("cs_storage_devs_init", "init_via_conf");
	m0_fi_enable("m0_storage_dev_attach_by_conf", "no_real_dev");
	m0_fi_enable("m0_storage_dev_attach", "no_real_dev");
	spiel_ci_ut_init();
	m0_fi_disable("m0_storage_dev_attach_by_conf", "no_real_dev");
	m0_fi_disable("m0_storage_dev_attach", "no_real_dev");
	/* test non-existent fs */
	rc = m0_spiel_filesystem_stats_fetch(&spiel, &fs_bad, &fs_stats);
	M0_UT_ASSERT(rc == -ENOENT);

	/* test the existent one */
	rc = m0_spiel_filesystem_stats_fetch(&spiel, &fs_fid, &fs_stats);
	M0_UT_ASSERT(rc == 0);
	/*
	 * fs_stats.fs_total contains sum of ios total space and be total space
	 */
	M0_UT_ASSERT(fs_stats.fs_total > ios_total);
	M0_UT_ASSERT(fs_stats.fs_free > 0 && fs_stats.fs_free <=
		     fs_stats.fs_total);
	spiel_ci_ut_fini();
}

static void test_spiel_pool_repair(void)
{
	const struct m0_fid         pool_fid = M0_FID_TINIT(
				M0_CONF_POOL_TYPE.cot_ftype.ft_id, 1, 4);
	struct m0_fid               svc_fid = M0_FID_TINIT(
				M0_CONF_SERVICE_TYPE.cot_ftype.ft_id, 1, 9);
	struct m0_fid               pool_invalid_fid = M0_FID_TINIT(
				M0_CONF_POOL_TYPE.cot_ftype.ft_id, 1, 3);
	struct m0_fid               invalid_fid = M0_FID_TINIT(
				M0_CONF_SERVICE_TYPE.cot_ftype.ft_id, 1, 4);
	struct m0_spiel_sns_status *status;
	enum m0_sns_cm_status       state;
	int                         rc;

	spiel_ci_ut_init();
	m0_fi_enable("ready", "no_wait");
	rc = m0_spiel_pool_repair_start(&spiel, &invalid_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_pool_repair_start(&spiel, &pool_invalid_fid);
	M0_UT_ASSERT(rc == -ENOENT);

	rc = m0_spiel_pool_repair_quiesce(&spiel, &invalid_fid);
	M0_UT_ASSERT(rc == -EINVAL);
	rc = m0_spiel_pool_repair_continue(&spiel, &invalid_fid);
	M0_UT_ASSERT(rc == -EINVAL);
	rc = m0_spiel_pool_repair_status(&spiel, &invalid_fid, &status);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_pool_repair_status(&spiel, &pool_fid, &status);
	M0_LOG(M0_DEBUG, "rc = %d", rc);
	M0_UT_ASSERT(rc == 1);
	M0_UT_ASSERT(m0_fid_eq(&status[0].sss_fid, &svc_fid));
	M0_UT_ASSERT(status[0].sss_state == SNS_CM_STATUS_IDLE);
	M0_UT_ASSERT(status[0].sss_progress >= 0);
	m0_free(status);

	rc = m0_spiel_pool_repair_start(&spiel, &pool_fid);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_pool_repair_status(&spiel, &pool_fid, &status);
	M0_UT_ASSERT(rc == 1);
	state = status[0].sss_state;
	M0_UT_ASSERT(m0_fid_eq(&status[0].sss_fid, &svc_fid));
	M0_UT_ASSERT(M0_IN(state, (SNS_CM_STATUS_IDLE, SNS_CM_STATUS_STARTED,
				   SNS_CM_STATUS_FAILED)));
	M0_UT_ASSERT(status[0].sss_progress >= 0);
	m0_free(status);
	if (M0_IN(state, (SNS_CM_STATUS_IDLE, SNS_CM_STATUS_FAILED)))
		goto done;

	rc = m0_spiel_pool_repair_quiesce(&spiel, &pool_fid);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_pool_repair_status(&spiel, &pool_fid, &status);
	M0_UT_ASSERT(rc == 1);
	M0_UT_ASSERT(m0_fid_eq(&status[0].sss_fid, &svc_fid));
	M0_UT_ASSERT(status[0].sss_state = SNS_CM_STATUS_PAUSED);
	M0_UT_ASSERT(status[0].sss_progress >= 0);
	m0_free(status);

	rc = m0_spiel_pool_repair_continue(&spiel, &pool_fid);
	M0_UT_ASSERT(rc == 0);

	while(1) {
		rc = m0_spiel_pool_repair_status(&spiel, &pool_fid, &status);
		M0_UT_ASSERT(rc == 1);
		state = status[0].sss_state;
		M0_UT_ASSERT(m0_fid_eq(&status[0].sss_fid, &svc_fid));
		M0_UT_ASSERT(M0_IN(state, (SNS_CM_STATUS_IDLE,
					   SNS_CM_STATUS_STARTED,
					   SNS_CM_STATUS_FAILED)));
		M0_UT_ASSERT(status[0].sss_progress >= 0);
		m0_free(status);

		if (M0_IN(state, (SNS_CM_STATUS_IDLE, SNS_CM_STATUS_FAILED)))
			break;
		m0_nanosleep(m0_time(5, 0), NULL);
	}

done:
	m0_fi_disable("ready", "no_wait");
	spiel_ci_ut_fini();
}

static void test_spiel_pool_rebalance(void)
{
	const struct m0_fid         pool_fid = M0_FID_TINIT(
				M0_CONF_POOL_TYPE.cot_ftype.ft_id, 1, 4);
	struct m0_fid               svc_fid = M0_FID_TINIT(
				M0_CONF_SERVICE_TYPE.cot_ftype.ft_id, 1, 9);
	struct m0_fid               pool_invalid_fid = M0_FID_TINIT(
				M0_CONF_POOL_TYPE.cot_ftype.ft_id, 1, 3);
	struct m0_fid               invalid_fid = M0_FID_TINIT(
				M0_CONF_SERVICE_TYPE.cot_ftype.ft_id, 1, 4);
	struct m0_spiel_sns_status *status;
	enum m0_sns_cm_status       state;
	int                         rc;

	spiel_ci_ut_init();
	m0_fi_enable("ready", "no_wait");
	rc = m0_spiel_pool_rebalance_start(&spiel, &invalid_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_pool_rebalance_start(&spiel, &pool_invalid_fid);
	M0_UT_ASSERT(rc == -ENOENT);

	rc = m0_spiel_pool_rebalance_quiesce(&spiel, &invalid_fid);
	M0_UT_ASSERT(rc == -EINVAL);
	rc = m0_spiel_pool_rebalance_continue(&spiel, &invalid_fid);
	M0_UT_ASSERT(rc == -EINVAL);
	rc = m0_spiel_pool_rebalance_status(&spiel, &invalid_fid, &status);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_pool_rebalance_status(&spiel, &pool_fid, &status);
	M0_UT_ASSERT(rc == 1);
	M0_UT_ASSERT(m0_fid_eq(&status[0].sss_fid, &svc_fid));
	M0_UT_ASSERT(status[0].sss_state == SNS_CM_STATUS_IDLE);
	M0_UT_ASSERT(status[0].sss_progress >= 0);
	m0_free(status);

	rc = m0_spiel_pool_rebalance_start(&spiel, &pool_fid);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_pool_rebalance_status(&spiel, &pool_fid, &status);
	M0_UT_ASSERT(rc == 1);
	state = status[0].sss_state;
	M0_UT_ASSERT(m0_fid_eq(&status[0].sss_fid, &svc_fid));
	M0_UT_ASSERT(M0_IN(state, (SNS_CM_STATUS_IDLE, SNS_CM_STATUS_STARTED,
				   SNS_CM_STATUS_FAILED)));
	M0_UT_ASSERT(status[0].sss_progress >= 0);
	m0_free(status);
	if (M0_IN(state, (SNS_CM_STATUS_IDLE, SNS_CM_STATUS_FAILED)))
		goto done;

	rc = m0_spiel_pool_rebalance_quiesce(&spiel, &pool_fid);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_pool_rebalance_status(&spiel, &pool_fid, &status);
	M0_UT_ASSERT(rc == 1);
	M0_UT_ASSERT(m0_fid_eq(&status[0].sss_fid, &svc_fid));
	M0_UT_ASSERT(status[0].sss_state = SNS_CM_STATUS_PAUSED);
	M0_UT_ASSERT(status[0].sss_progress >= 0);
	m0_free(status);

	rc = m0_spiel_pool_rebalance_continue(&spiel, &pool_fid);
	M0_UT_ASSERT(rc == 0);

	while(1) {
		rc = m0_spiel_pool_rebalance_status(&spiel, &pool_fid, &status);
		M0_UT_ASSERT(rc == 1);
		state = status[0].sss_state;
		M0_UT_ASSERT(m0_fid_eq(&status[0].sss_fid, &svc_fid));
		M0_UT_ASSERT(M0_IN(state, (SNS_CM_STATUS_IDLE,
					   SNS_CM_STATUS_STARTED,
					   SNS_CM_STATUS_FAILED)));
		M0_UT_ASSERT(status[0].sss_progress >= 0);
		m0_free(status);

		if (M0_IN(state, (SNS_CM_STATUS_IDLE, SNS_CM_STATUS_FAILED)))
			break;
		m0_nanosleep(m0_time(5, 0), NULL);
	}

done:
	m0_fi_disable("ready", "no_wait");
	spiel_ci_ut_fini();
}

static int spiel_ci_tests_fini()
{
	return conf_ut_ast_thread_fini() ?: system("rm -rf ut_spiel.db/");
}

struct m0_ut_suite spiel_ci_ut = {
	.ts_name  = "spiel-ci-ut",
	.ts_init  = conf_ut_ast_thread_init,
	.ts_fini  = spiel_ci_tests_fini,
	.ts_tests = {
		{ "service-cmds", test_spiel_service_cmds },
		{ "process-cmds", test_spiel_process_cmds },
		{ "service-order", test_spiel_service_order },
		{ "process-services-list", test_spiel_process_services_list },
		{ "device-cmds", test_spiel_device_cmds },
		{ "stats", test_spiel_fs_stats },
		{ "pool-repair", test_spiel_pool_repair },
		{ "pool-rebalance", test_spiel_pool_rebalance },
		{ NULL, NULL }
	}
};

#undef M0_TRACE_SUBSYSTEM
