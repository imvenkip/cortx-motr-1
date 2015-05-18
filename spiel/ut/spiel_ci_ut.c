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
#include "lib/finject.h"

#include "net/lnet/lnet.h"    /* m0_net_lnet_xprt */
#include "lib/finject.h"      /* m0_fi_enable_once */
#include "conf/obj.h"
#include "conf/obj_ops.h"
#include "rpc/rpc_machine.h"  /* m0_rpc_machine */
#include "rpc/rpclib.h"       /* m0_rpc_server_ctx */
#include "rm/rm_rwlock.h"     /* m0_rwlockable_domain_init,
				 m0_rwlockable_domain_fini */
#include "ut/ut.h"
#include "spiel/spiel.h"
#include "spiel/ut/spiel_ut_common.h"
#include "ut/file_helpers.h"  /* M0_UT_PATH */


static struct m0_spiel spiel;

int spiel_ci_ut_init(void)
{
	int rc;

	rc= m0_spiel__ut_init(&spiel, M0_UT_PATH("conf-str.txt"));
	M0_UT_ASSERT(rc == 0);
	return 0;
}

int spiel_ci_ut_fini(void)
{
	m0_spiel__ut_fini(&spiel);
	return 0;
}


static void test_spiel_service_cmds(void)
{
	const struct m0_fid svc_fid = M0_FID_TINIT(
			M0_CONF_SERVICE_TYPE.cot_ftype.ft_id, 1, 20);
	const struct m0_fid svc_invalid_fid = M0_FID_TINIT(
			M0_CONF_SERVICE_TYPE.cot_ftype.ft_id, 1, 13);
	int                 rc;

	spiel_ci_ut_init();
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

static void test_spiel_device_cmds(void)
{
	const struct m0_fid disk_fid = M0_FID_TINIT(
				M0_CONF_DISK_TYPE.cot_ftype.ft_id, 1, 16);
	const struct m0_fid disk_invalid_fid = M0_FID_TINIT(
				M0_CONF_DISK_TYPE.cot_ftype.ft_id, 1, 23);
	int                 rc;

	spiel_ci_ut_init();
	/*
	 * After mero startup devices are online by default,
	 * so detach them at first.
	 */
	rc = m0_spiel_device_detach(&spiel, &disk_fid);
	M0_UT_ASSERT(rc == 0);

	m0_fi_enable_once("sss_device_stob_attach", "no_real_dev");
	rc = m0_spiel_device_attach(&spiel, &disk_fid);
	M0_UT_ASSERT(rc == 0);

	/**
	 * @todo Format is not implemented yet.
	 */
	rc = m0_spiel_device_format(&spiel, &disk_fid);
	M0_UT_ASSERT(rc == -ENOSYS);
	rc = m0_spiel_device_detach(&spiel, &disk_fid);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_device_detach(&spiel, &disk_invalid_fid);
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
				M0_CONF_SERVICE_TYPE.cot_ftype.ft_id, 1, 20);
	bool                         found = false;

	spiel_ci_ut_init();
	/* Doing process quiesce */
	rc = m0_spiel_process_quiesce(&spiel, &process_fid);
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

struct m0_ut_suite spiel_ci_ut = {
	.ts_name  = "spiel-ci-ut",
	.ts_tests = {
		{ "service-cmds", test_spiel_service_cmds },
		{ "process-cmds", test_spiel_process_cmds },
		{ "service-order", test_spiel_service_order },
		{ "process-services-list", test_spiel_process_services_list },
		{ "device-cmds", test_spiel_device_cmds },
		{ NULL, NULL }
	}
};

#undef M0_TRACE_SUBSYSTEM
