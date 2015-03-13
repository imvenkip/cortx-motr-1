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

#include "net/lnet/lnet.h"    /* m0_net_lnet_xprt */
#include "lib/finject.h"      /* m0_fi_enable_once */
#include "conf/obj_ops.h"
#include "rpc/rpc_machine.h"  /* m0_rpc_machine */
#include "rpc/rpclib.h"       /* m0_rpc_server_ctx */
#include "rm/rm_rwlock.h"     /* m0_rwlockable_domain_init,
				 m0_rwlockable_domain_fini */
#include "ut/ut.h"
#include "spiel/spiel.h"
#include "spiel/ut/spiel_ut_common.h"
#include "ut/file_helpers.h"  /* M0_UT_CONF_PATH */

#define SERVER_ENDPOINT_ADDR "0@lo:12345:34:1"
#define SERVER_ENDPOINT      "lnet:" SERVER_ENDPOINT_ADDR
#define CLIENT_ENDPOINT_ADDR "0@lo:12345:34:*"

static struct m0_reqh            g_reqh;
static struct m0_net_domain      g_net_dom;
static struct m0_net_buffer_pool g_buf_pool;

int spiel_proc_conf_obj_find(struct m0_spiel         *spl,
			     const struct m0_fid     *proc_fid,
			     struct m0_conf_process **proc_obj);

static void test_spiel_service_cmds(void)
{
	struct m0_rpc_server_ctx  confd_srv;
	struct m0_spiel           spiel;
	int                       rc;
	const char               *confd_eps[] = { SERVER_ENDPOINT_ADDR, NULL };
	const char               *profile = M0_UT_CONF_PROFILE;
	const struct m0_fid       svc_fid = M0_FID_TINIT('s', 1,  20);
	const struct m0_fid       svc_invalid_fid = M0_FID_TINIT('s', 1,  13);
	struct m0_spiel_ut_reqh   ut_reqh = {
		.sur_net_dom  = g_net_dom,
		.sur_buf_pool = g_buf_pool,
		.sur_reqh     = g_reqh,
	};
	const char               *rm_ep = confd_eps[0];

	rc = m0_spiel__ut_confd_start(&confd_srv, confd_eps[0],
				      M0_UT_CONF_PATH("conf-str.txt"));
	M0_UT_ASSERT(rc == 0);

	m0_spiel__ut_reqh_init(&ut_reqh, CLIENT_ENDPOINT_ADDR);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_start(&spiel, &ut_reqh.sur_reqh, confd_eps, rm_ep);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_cmd_profile_set(&spiel, profile);
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

	m0_spiel_stop(&spiel);

	m0_spiel__ut_reqh_fini(&ut_reqh);
	m0_spiel__ut_confd_stop(&confd_srv);
}

static void test_spiel_process_prepare_fini(struct m0_spiel     *spl,
					    const struct m0_fid *proc_fid)
{
	struct m0_mutex        *conf_cache_mutex;
	struct m0_conf_process *proc;
	struct m0_conf_obj     *obj;
	int                     rc = 0;

	M0_PRE(spl != NULL);
	M0_PRE(proc_fid != NULL);
	M0_PRE(m0_conf_fid_type(proc_fid) == &M0_CONF_PROCESS_TYPE);
	conf_cache_mutex = &spl->spl_rconfc.rc_confc.cc_lock;
	rc = spiel_proc_conf_obj_find(spl, proc_fid, &proc);
	if (rc != 0)
		return;

	m0_mutex_lock(conf_cache_mutex);
	m0_tl_for(m0_conf_dir, &proc->pc_services->cd_items, obj) {
		while (obj->co_nrefs > 0)
			m0_conf_obj_put(obj);
	} m0_tl_endfor;

	while (proc->pc_obj.co_nrefs > 0)
		m0_conf_obj_put(&proc->pc_obj);
	m0_mutex_unlock(conf_cache_mutex);
}

static void test_spiel_process_services_list(void)
{
	struct m0_rpc_server_ctx     confd_srv;
	struct m0_spiel              spiel;
	struct m0_spiel_running_svc *svcs;
	int                          rc;
	int                          i;
	const char                  *confd_eps[] = {
		SERVER_ENDPOINT_ADDR,
		NULL
	};
	const char *rm_ep = confd_eps[0];
	const char *profile = M0_UT_CONF_PROFILE;
	const struct m0_fid proc_fid = M0_FID_TINIT('r', 1,  5);

	struct m0_spiel_ut_reqh ut_reqh = {
		.sur_net_dom  = g_net_dom,
		.sur_buf_pool = g_buf_pool,
		.sur_reqh     = g_reqh,
	};
	rc = m0_spiel__ut_confd_start(&confd_srv, confd_eps[0],
				      M0_UT_CONF_PATH("conf-str.txt"));
	M0_UT_ASSERT(rc == 0);

	m0_spiel__ut_reqh_init(&ut_reqh, CLIENT_ENDPOINT_ADDR);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_start(&spiel, &ut_reqh.sur_reqh, confd_eps, rm_ep);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_cmd_profile_set(&spiel, profile);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_process_list_services(&spiel, &proc_fid, &svcs);
	M0_UT_ASSERT(rc > 0);
	M0_UT_ASSERT(svcs != NULL);
	for (i = 0; i < rc; ++i)
		m0_free(svcs[i].spls_name);
	m0_free(svcs);

	test_spiel_process_prepare_fini(&spiel, &proc_fid);
	m0_spiel_stop(&spiel);

	m0_spiel__ut_reqh_fini(&ut_reqh);
	m0_spiel__ut_confd_stop(&confd_srv);
}

static void test_spiel_process_cmds(void)
{
	struct m0_rpc_server_ctx  confd_srv;
	struct m0_spiel           spiel;
	int                       rc;
	const char              *confd_eps[] = {
		SERVER_ENDPOINT_ADDR,
		NULL
	};
	const char *rm_ep   = confd_eps[0];
	const char *profile = M0_UT_CONF_PROFILE;
	const struct m0_fid profile_fid = M0_FID_TINIT('r', 1, 5);
	const struct m0_fid profile_second_fid = M0_FID_TINIT('r', 1, 13);
	const struct m0_fid profile_invalid_fid = M0_FID_TINIT('s', 4, 15);

	struct m0_spiel_ut_reqh ut_reqh = {
		.sur_net_dom  = g_net_dom,
		.sur_buf_pool = g_buf_pool,
		.sur_reqh     = g_reqh,
	};
	rc = m0_spiel__ut_confd_start(&confd_srv, confd_eps[0],
				      M0_UT_CONF_PATH("conf-str.txt"));
	M0_UT_ASSERT(rc == 0);

	m0_spiel__ut_reqh_init(&ut_reqh, CLIENT_ENDPOINT_ADDR);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_start(&spiel, &ut_reqh.sur_reqh, confd_eps, rm_ep);
	M0_UT_ASSERT(rc == 0);

	rc = m0_spiel_cmd_profile_set(&spiel, profile);
	M0_UT_ASSERT(rc == 0);

	/* Reconfig */
	rc = m0_spiel_process_reconfig(&spiel, &profile_invalid_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	m0_fi_enable_once("m0_alloc", "fail_allocation");
	rc = m0_spiel_process_reconfig(&spiel, &profile_fid);
	M0_UT_ASSERT(rc == -ENOMEM);

	rc = m0_spiel_process_reconfig(&spiel, &profile_second_fid);
	M0_UT_ASSERT(rc == -ENOENT);

	m0_fi_enable_once("ss_process_reconfig", "unit_test");
	rc = m0_spiel_process_reconfig(&spiel, &profile_fid);
	M0_UT_ASSERT(rc == 0);

	/* Health */
	rc = m0_spiel_process_health(&spiel, &profile_invalid_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_process_health(&spiel, &profile_fid);
	M0_UT_ASSERT(rc == M0_HEALTH_GOOD);

	/* Quiesce */
	rc = m0_spiel_process_quiesce(&spiel, &profile_invalid_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	rc = m0_spiel_process_quiesce(&spiel, &profile_fid);
	M0_UT_ASSERT(rc == 0);

	/* Stop */
	rc = m0_spiel_process_stop(&spiel, &profile_invalid_fid);
	M0_UT_ASSERT(rc == -EINVAL);

	m0_fi_enable_once("m0_ss_process_stop_fop_release", "no_kill");
	rc = m0_spiel_process_stop(&spiel, &profile_fid);
	M0_UT_ASSERT(rc == 0);

	/* Finish test */
	m0_spiel_stop(&spiel);

	m0_spiel__ut_reqh_fini(&ut_reqh);
	m0_spiel__ut_confd_stop(&confd_srv);
}

int spiel_ci_ut_init(void)
{
	m0_rwlockable_domain_init();
	return 0;
}

int spiel_ci_ut_fini(void)
{
	m0_rwlockable_domain_fini();
	return 0;
}

struct m0_ut_suite spiel_ci_ut = {
	.ts_name  = "spiel-ci-ut",
	.ts_init  = spiel_ci_ut_init,
	.ts_fini  = spiel_ci_ut_fini,
	.ts_tests = {
		{ "service-cmds", test_spiel_service_cmds },
		{ "process-services-list", test_spiel_process_services_list },
		{ "process-cmds", test_spiel_process_cmds },
		{ NULL, NULL }
	}
};

#undef M0_TRACE_SUBSYSTEM
