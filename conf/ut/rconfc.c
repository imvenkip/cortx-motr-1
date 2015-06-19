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
 * Original author: Igor Vartanov
 * Original creation date: 2015-03-11
 */

#include "conf/confc.h"
#include "conf/rconfc.h"
#include "rpc/rpclib.h"                /* m0_rpc_server_ctx */
#include "ut/file_helpers.h"
#include "conf/ut/rpc_helpers.h"
#include "conf/ut/confc.h"             /* m0_ut_conf_fids[] */
#include "conf/ut/common.h"
#include "lib/finject.h"
#include "lib/locality.h"              /* m0_locality0_get */
#include "ut/ut.h"
#include "rm/rm_rwlock.h"
#include "conf/rconfc_internal.h"

#define KICK_THREAD(func, type, arg)        \
static int _kick ## func(type arg) {        \
   static struct m0_thread tcb;             \
   return M0_THREAD_INIT(&tcb, type, NULL,  \
                         &func, arg,        \
                         "thread_" # func );\
}

static bool g_quorum_expected;

static struct m0_reqh *ut_reqh;

static int ut_mero_start(struct m0_rpc_machine    *mach,
			 struct m0_rpc_server_ctx *rctx)
{
	int rc;

#define NAME(ext) "rconfc-ut" ext
	char *argv[] = {
		NAME(""), "-T", "AD", "-D", NAME(".db"),
		"-S", NAME(".stob"), "-A", "linuxstob:"NAME("-addb.stob"),
		"-w", "10", "-e", SERVER_ENDPOINT,
		"-s", "confd:<0x7300000000000001:6>",
		"-s", "rmservice:<0x7300000000000001:3>",
		"-c", M0_UT_PATH("dir_iter_xc.txt"), "-P", M0_UT_CONF_PROFILE
	};
	*rctx = (struct m0_rpc_server_ctx) {
		.rsx_xprts         = &g_xprt,
		.rsx_xprts_nr      = 1,
		.rsx_argv          = argv,
		.rsx_argc          = ARRAY_SIZE(argv),
		.rsx_log_file_name = NAME(".log")
	};
#undef NAME

	M0_SET0(mach);
	rc = m0_rpc_server_start(rctx);
	M0_UT_ASSERT(rc == 0);

	rc = m0_ut_rpc_machine_start(mach, g_xprt, CLIENT_ENDPOINT_ADDR);
	M0_UT_ASSERT(rc == 0);
	ut_reqh = mach->rm_reqh;
	mach->rm_reqh = &rctx->rsx_mero_ctx.cc_reqh_ctx.rc_reqh;
	return rc;
}

static void ut_mero_stop(struct m0_rpc_machine    *mach,
			 struct m0_rpc_server_ctx *rctx)
{
	mach->rm_reqh = ut_reqh;
	m0_ut_rpc_machine_stop(mach);
	m0_rpc_server_stop(rctx);
}

static void test_rconfc_exp_cb(struct m0_rconfc *rconfc)
{
	bool quorum = m0_rconfc_quorum_is_reached(rconfc);
	M0_UT_ASSERT(quorum == g_quorum_expected);
}

static void test_init_fini(void)
{
	struct m0_rpc_machine    mach;
	struct m0_rpc_server_ctx rctx;
	int                      rc;
	struct m0_rconfc         rconfc;
	char                    *confd_addr[] = {SERVER_ENDPOINT_ADDR, NULL};
	bool                     quorum;
	uint64_t                 ver;
	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	g_quorum_expected = true;
	rc = m0_rconfc_init(&rconfc, (const char**) confd_addr,
			    SERVER_ENDPOINT_ADDR, &g_grp,
			    &mach, 0, test_rconfc_exp_cb);
	M0_UT_ASSERT(rc == 0);
	quorum = m0_rconfc_quorum_is_reached_lock(&rconfc);
	M0_UT_ASSERT(quorum == g_quorum_expected);
	ver = m0_rconfc_ver_max_read(&rconfc);
	M0_UT_ASSERT(ver == rconfc.rc_ver);
	m0_rconfc_fini(&rconfc);
	ut_mero_stop(&mach, &rctx);
}

static void test_reading(void)
{
	struct m0_rpc_machine    mach;
	struct m0_rpc_server_ctx rctx;
	int                      rc;
	struct m0_rconfc         rconfc;
	char                    *confd_addr[] = {SERVER_ENDPOINT_ADDR, NULL};
	bool                     quorum;
	uint64_t                 ver;
	struct m0_conf_obj      *cobj;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	g_quorum_expected = true;
	rc = m0_rconfc_init(&rconfc, (const char**) confd_addr,
			    SERVER_ENDPOINT_ADDR, &g_grp,
			    &mach, 0, test_rconfc_exp_cb);
	M0_UT_ASSERT(rc == 0);
	quorum = m0_rconfc_quorum_is_reached_lock(&rconfc);
	M0_UT_ASSERT(quorum == g_quorum_expected);
	ver = m0_rconfc_ver_max_read(&rconfc);
	M0_UT_ASSERT(ver == rconfc.rc_ver);
	/* do regular path opening */
	rc = m0_confc_open_sync(&cobj, rconfc.rc_confc.cc_root,
				M0_CONF_ROOT_PROFILES_FID,
				m0_ut_conf_fids[M0_UT_CONF_PROF],
				M0_CONF_PROFILE_FILESYSTEM_FID);
	M0_UT_ASSERT(rc == 0);
	m0_confc_close(cobj);
	m0_rconfc_fini(&rconfc);
	ut_mero_stop(&mach, &rctx);
}

static void test_quorum_impossible(void)
{
	struct m0_rconfc         rconfc;
	struct m0_rpc_machine    mach;
	char                    *confd_addr[] = {SERVER_ENDPOINT_ADDR, NULL};
	int                      rc;
	bool                     quorum;
	uint64_t                 ver;
	struct m0_rpc_server_ctx rctx;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	M0_SET0(&rconfc);
	g_quorum_expected = false; /* 2 is not possible with 1 confd */
	rc = m0_rconfc_init(&rconfc, (const char**) confd_addr,
			    (const char*) confd_addr[0], &g_grp,
			    &mach, 2, test_rconfc_exp_cb);
	M0_UT_ASSERT(rc == 0);
	quorum = m0_rconfc_quorum_is_reached_lock(&rconfc);
	M0_UT_ASSERT(quorum == g_quorum_expected);
	ver = m0_rconfc_ver_max_read(&rconfc);
	M0_UT_ASSERT(ver != rconfc.rc_ver);
	M0_UT_ASSERT(rconfc.rc_ver == CONF_VER_UNKNOWN);
	m0_rconfc_fini(&rconfc);
	ut_mero_stop(&mach, &rctx);
}

struct m0_semaphore gops_sem;
bool (*fn_drain)(struct m0_clink *clink);

static bool _drain(struct m0_clink *clink)
{
	bool ret;
	ret = fn_drain(clink);
	m0_semaphore_up(&gops_sem);
	return ret;
}

static void _drained(struct m0_rconfc *rconfc)
{
	m0_semaphore_up(&gops_sem);
}

M0_EXTERN struct m0_confc_gate_ops m0_rconfc_gate_ops;

static void test_gops(void)
{
	struct m0_rconfc         rconfc;
	struct m0_rpc_machine    mach;
	char                    *confd_addr[] = {SERVER_ENDPOINT_ADDR, NULL};
	int                      rc;
	bool                     quorum;
	struct m0_rpc_server_ctx rctx;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	M0_SET0(&rconfc);
	g_quorum_expected = true;
	rc = m0_rconfc_init(&rconfc, (const char**) confd_addr,
			    (const char*) confd_addr[0], &g_grp,
			    &mach, 0, test_rconfc_exp_cb);
	M0_UT_ASSERT(rc == 0);
	quorum = m0_rconfc_quorum_is_reached_lock(&rconfc);
	M0_UT_ASSERT(quorum == g_quorum_expected);
	/* imitate check op */
	rconfc.rc_gops.go_check(&rconfc.rc_confc);
	/* imitate cache drain event */
	m0_semaphore_init(&gops_sem, 0);
	fn_drain = m0_rconfc_gate_ops.go_drain;
	M0_RCONFC_CB_SET_LOCK(&rconfc, rc_gops.go_drain, _drain);
	m0_confc_gate_ops_set(&rconfc.rc_confc, &rconfc.rc_gops);
	m0_chan_signal_lock(&rconfc.rc_confc.cc_unattached);
	m0_semaphore_down(&gops_sem);
	/* imitate skip op */
	M0_RCONFC_CB_SET_LOCK(&rconfc, rc_drained_cb, _drained);
	rc = rconfc.rc_gops.go_skip(&rconfc.rc_confc);
	M0_UT_ASSERT(rc == -ENOENT);
	m0_semaphore_down(&gops_sem);
	m0_rconfc_fini(&rconfc);
	ut_mero_stop(&mach, &rctx);
	m0_semaphore_fini(&gops_sem);
}

struct m0_semaphore sem_conflict;
struct m0_semaphore sem_check;

static void _check(struct m0_rconfc *rconfc)
{
	struct rlock_ctx *rlx;

	rlx = rconfc->rc_rlock_ctx;
	rlx->rlc_allowed = false;
	m0_semaphore_up(&sem_conflict);
	rconfc->rc_gops.go_check(&rconfc->rc_confc);
	m0_semaphore_up(&sem_check);
}

KICK_THREAD(_check, struct m0_rconfc *, rconfc) /* declare _kick_check() */

M0_EXTERN struct m0_rm_incoming_ops m0_rconfc_ri_ops;

static void test_conflict(void)
{
	struct m0_rconfc         rconfc;
	struct m0_rpc_machine    mach;
	char                    *confd_addr[] = {SERVER_ENDPOINT_ADDR, NULL};
	int                      rc;
	bool                     quorum;
	struct m0_rpc_server_ctx rctx;
	struct rlock_ctx        *rlx;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	M0_SET0(&rconfc);
	g_quorum_expected = true;
	rc = m0_rconfc_init(&rconfc, (const char**) confd_addr,
			    (const char*) confd_addr[0], &g_grp,
			    &mach, 0, test_rconfc_exp_cb);
	M0_UT_ASSERT(rc == 0);
	quorum = m0_rconfc_quorum_is_reached_lock(&rconfc);
	M0_UT_ASSERT(quorum == g_quorum_expected);
	rlx = rconfc.rc_rlock_ctx;
	m0_semaphore_init(&sem_conflict, 0);
	m0_semaphore_init(&sem_check, 0);
	_kick_check(&rconfc);
	m0_semaphore_down(&sem_conflict);
	m0_rconfc_ri_ops.rio_conflict(&rlx->rlc_req);
	m0_semaphore_down(&sem_check);
	m0_semaphore_fini(&sem_check);
	m0_semaphore_fini(&sem_conflict);
	m0_rconfc_fini(&rconfc);
	ut_mero_stop(&mach, &rctx);
}

static void test_reconnect_fail(void)
{
	struct m0_rpc_machine    mach;
	struct m0_rpc_server_ctx rctx;
	int                      rc;
	struct m0_rconfc         rconfc;
	char                    *confd_addr[] = {SERVER_ENDPOINT_ADDR, NULL};
	bool                     quorum;
	uint64_t                 ver;
	struct m0_conf_obj      *cobj;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	g_quorum_expected = true;
	rc = m0_rconfc_init(&rconfc, (const char**) confd_addr,
			    SERVER_ENDPOINT_ADDR, &g_grp,
			    &mach, 0, test_rconfc_exp_cb);
	M0_UT_ASSERT(rc == 0);
	quorum = m0_rconfc_quorum_is_reached_lock(&rconfc);
	M0_UT_ASSERT(quorum == g_quorum_expected);
	ver = m0_rconfc_ver_max_read(&rconfc);
	M0_UT_ASSERT(ver == rconfc.rc_ver);
	/*
	 * Inject rpc failure on communicating with confd. This will cause
	 * rconfc reconnection when no other confd is known, i.e. no more confd
	 * to reconnect to. This will ultimately result in -ENOENT error code.
	 */
	m0_fi_enable_off_n_on_m("on_replied", "fail_rpc_reply", 0, 1);
	/* do regular path opening with disconnected confc */
	rc = m0_confc_open_sync(&cobj, rconfc.rc_confc.cc_root,
				M0_CONF_ROOT_PROFILES_FID,
				m0_ut_conf_fids[M0_UT_CONF_PROF],
				M0_CONF_PROFILE_FILESYSTEM_FID);
	M0_UT_ASSERT(rc == -ENOENT);
	m0_fi_disable("on_replied", "fail_rpc_reply");
	m0_rconfc_fini(&rconfc);
	ut_mero_stop(&mach, &rctx);
}

static int _skip(struct m0_confc *confc)
{
	m0_fi_disable("on_replied", "fail_rpc_reply");
	return m0_rconfc_gate_ops.go_skip(confc);
}

static void test_reconnect_success(void)
{
	struct m0_rpc_machine    mach;
	struct m0_rpc_server_ctx rctx;
	int                      rc;
	struct m0_rconfc         rconfc;
	char                    *confd_addr[] = {SERVER_ENDPOINT_ADDR, NULL};
	bool                     quorum;
	uint64_t                 ver;
	struct m0_conf_obj      *cobj;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	g_quorum_expected = true;
	rc = m0_rconfc_init(&rconfc, (const char**) confd_addr,
			    SERVER_ENDPOINT_ADDR, &g_grp,
			    &mach, 0, test_rconfc_exp_cb);
	M0_UT_ASSERT(rc == 0);
	quorum = m0_rconfc_quorum_is_reached_lock(&rconfc);
	M0_UT_ASSERT(quorum == g_quorum_expected);
	ver = m0_rconfc_ver_max_read(&rconfc);
	M0_UT_ASSERT(ver == rconfc.rc_ver);
	/* imitate successful reconnection */
	m0_fi_enable_off_n_on_m("skip_confd_st_in", "force_reconnect_success",
				0, 1);
	m0_fi_enable_off_n_on_m("on_replied", "fail_rpc_reply", 0, 1);
	M0_RCONFC_CB_SET_LOCK(&rconfc, rc_gops.go_skip, _skip);
	/* do regular path opening with disconnected confc */
	rc = m0_confc_open_sync(&cobj, rconfc.rc_confc.cc_root,
				M0_CONF_ROOT_PROFILES_FID,
				m0_ut_conf_fids[M0_UT_CONF_PROF],
				M0_CONF_PROFILE_FILESYSTEM_FID);
	M0_UT_ASSERT(rc == 0);
	m0_confc_close(cobj);
	m0_fi_disable("skip_confd_st_in", "force_reconnect_success");
	m0_rconfc_fini(&rconfc);
	ut_mero_stop(&mach, &rctx);
}

static int rconfc_ut_init(void)
{
	m0_rwlockable_domain_init();
	return conf_ut_ast_thread_init();
}

static int rconfc_ut_fini(void)
{
	m0_rwlockable_domain_fini();
	return conf_ut_ast_thread_fini();
}

struct m0_ut_suite rconfc_ut = {
	.ts_name  = "rconfc-ut",
	.ts_init  = rconfc_ut_init,
	.ts_fini  = rconfc_ut_fini,
	.ts_tests = {
		{ "init-fini",  test_init_fini },
		{ "reading",    test_reading },
		{ "impossible", test_quorum_impossible },
		{ "gate-ops",   test_gops },
		{ "conflict",   test_conflict },
		{ "reconnect",  test_reconnect_success },
		{ "recon-fail", test_reconnect_fail },
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
