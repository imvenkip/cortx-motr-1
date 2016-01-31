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

#include <unistd.h>                    /* usleep */

#include "conf/rconfc.h"
#include "conf/rconfc_internal.h"      /* rlock_ctx */
#include "conf/confd.h"                /* m0_confd_stype */
#include "conf/ut/common.h"            /* SERVER_ENDPOINT */
#include "conf/ut/confc.h"             /* m0_ut_conf_fids */
#include "conf/ut/rpc_helpers.h"       /* m0_ut_rpc_machine_start */
#include "rpc/rpclib.h"                /* m0_rpc_server_ctx */
#include "lib/finject.h"
#include "ut/misc.h"                   /* M0_UT_PATH */
#include "ut/ut.h"

static struct m0_semaphore  g_expired_sem;
static struct m0_reqh      *ut_reqh;

M0_EXTERN struct m0_confc_gate_ops  m0_rconfc_gate_ops;
M0_EXTERN struct m0_rm_incoming_ops m0_rconfc_ri_ops;

static int ut_mero_start(struct m0_rpc_machine    *mach,
			 struct m0_rpc_server_ctx *rctx)
{
	int rc;
#define NAME(ext) "rconfc-ut" ext
	char *argv[] = {
		NAME(""), "-T", "AD", "-D", NAME(".db"),
		"-S", NAME(".stob"), "-A", "linuxstob:"NAME("-addb.stob"),
		"-w", "10", "-e", SERVER_ENDPOINT,
		"-c", M0_UT_PATH("diter.xc"), "-P", M0_UT_CONF_PROFILE
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

static void test_no_quorum_exp_cb(struct m0_rconfc *rconfc)
{
	/*
	 * Expiration callback that should be called if
	 * test implies no quorum on rconfc init.
	 */
	M0_UT_ASSERT(rconfc->rc_ver == 0);
}

static void test_null_exp_cb(struct m0_rconfc *rconfc)
{
	/*
	 * Test expiration callback that shouldn't be called, because
	 * test doesn't imply reelection.
	 */
	M0_UT_ASSERT(0);
}

static void conflict_exp_cb(struct m0_rconfc *rconfc)
{
	m0_semaphore_up(&g_expired_sem);
}

static void test_init_fini(void)
{
	struct m0_rpc_machine    mach;
	struct m0_rpc_server_ctx rctx;
	struct m0_rconfc         rconfc;
	int                      rc;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_init(&rconfc, &g_grp, &mach, test_null_exp_cb);
	M0_UT_ASSERT(rc == 0);
	m0_rconfc_fini(&rconfc);
	ut_mero_stop(&mach, &rctx);
}

static void test_start_stop(void)
{
	struct m0_rpc_machine    mach;
	struct m0_rpc_server_ctx rctx;
	int                      rc;
	struct m0_rconfc         rconfc;
	uint64_t                 ver;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_init(&rconfc, &g_grp, &mach, test_null_exp_cb);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_start_sync(&rconfc);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rconfc.rc_ver != 0);
	ver = m0_rconfc_ver_max_read(&rconfc);
	M0_UT_ASSERT(ver == rconfc.rc_ver);
	/** @todo Check addresses used by rconfc */
	m0_rconfc_stop_sync(&rconfc);
	m0_rconfc_fini(&rconfc);
	ut_mero_stop(&mach, &rctx);
}

static void test_start_failures(void)
{
	struct m0_rpc_machine    mach;
	struct m0_rpc_server_ctx rctx;
	int                      rc;
	struct m0_rconfc         rconfc;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	m0_fi_enable_once("rlock_ctx_connect", "rm_conn_failed");
	rc = m0_rconfc_init(&rconfc, &g_grp, &mach, test_null_exp_cb);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_start_sync(&rconfc);
	/*
	 * If connection to RM fails, then rconfc will try to start from
	 * beginning, because it is possible that RM creditor has changed during
	 * connection. Second attempt will succeed.
	 */
	M0_UT_ASSERT(rc == 0);
	m0_rconfc_stop_sync(&rconfc);
	m0_rconfc_fini(&rconfc);

	m0_fi_enable_once("rconfc_read_lock_complete", "rlock_req_failed");
	rc = m0_rconfc_init(&rconfc, &g_grp, &mach, test_null_exp_cb);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_start_sync(&rconfc);
	M0_UT_ASSERT(rc == -ESRCH);
	m0_rconfc_stop_sync(&rconfc);
	m0_rconfc_fini(&rconfc);

	m0_fi_enable_once("rconfc__cb_quorum_test", "read_ver_failed");
	rc = m0_rconfc_init(&rconfc, &g_grp, &mach, test_no_quorum_exp_cb);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_start_sync(&rconfc);
	M0_UT_ASSERT(rc == -EPROTO);
	m0_rconfc_stop_sync(&rconfc);
	m0_rconfc_fini(&rconfc);

	m0_fi_enable_once("rconfc_conductor_iterate", "conductor_conn_fail");
	rc = m0_rconfc_init(&rconfc,&g_grp, &mach, test_null_exp_cb);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_start_sync(&rconfc);
	M0_UT_ASSERT(rc == -ENOENT);
	m0_rconfc_stop_sync(&rconfc);
	m0_rconfc_fini(&rconfc);

	ut_mero_stop(&mach, &rctx);
}

static void test_reading(void)
{
	struct m0_rpc_machine    mach;
	struct m0_rpc_server_ctx rctx;
	int                      rc;
	struct m0_rconfc         rconfc;
	uint64_t                 ver;
	struct m0_conf_obj      *cobj;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_init(&rconfc, &g_grp, &mach, test_null_exp_cb);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_start_sync(&rconfc);
	M0_UT_ASSERT(rc == 0);
	ver = m0_rconfc_ver_max_read(&rconfc);
	M0_UT_ASSERT(ver == rconfc.rc_ver);
	/* do regular path opening */
	rc = m0_confc_open_sync(&cobj, rconfc.rc_confc.cc_root,
				M0_CONF_ROOT_PROFILES_FID,
				m0_ut_conf_fids[M0_UT_CONF_PROF],
				M0_CONF_PROFILE_FILESYSTEM_FID);
	M0_UT_ASSERT(rc == 0);
	m0_confc_close(cobj);
	m0_rconfc_stop_sync(&rconfc);
	m0_rconfc_fini(&rconfc);
	ut_mero_stop(&mach, &rctx);
}

static bool quorum_impossible_clink_cb(struct m0_clink *cl)
{
	struct m0_rconfc *rconfc = container_of(cl->cl_chan, struct m0_rconfc,
						rc_sm.sm_chan);

	if (rconfc->rc_sm.sm_state == RCS_GET_RLOCK) {
		/*
		 * Override required quorum value to be greater then number of
		 * confd, so quorum is impossible.
		 */
		M0_PRE(rconfc->rc_quorum != 0);
		rconfc->rc_quorum *= 2;
		m0_clink_del(cl);
	}
	return true;
}

static void test_quorum_impossible(void)
{
	struct m0_rconfc         rconfc;
	struct m0_rpc_machine    mach;
	int                      rc;
	uint64_t                 ver;
	struct m0_rpc_server_ctx rctx;
	struct m0_clink          clink;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	M0_SET0(&rconfc);
	rc = m0_rconfc_init(&rconfc, &g_grp, &mach, test_no_quorum_exp_cb);
	M0_UT_ASSERT(rc == 0);
	m0_clink_init(&clink, quorum_impossible_clink_cb);
	m0_clink_add_lock(&rconfc.rc_sm.sm_chan, &clink);
	rc = m0_rconfc_start_sync(&rconfc);
	M0_UT_ASSERT(rc == -EPROTO);
	ver = m0_rconfc_ver_max_read(&rconfc);
	M0_UT_ASSERT(ver != rconfc.rc_ver);
	M0_UT_ASSERT(rconfc.rc_ver == M0_CONF_VER_UNKNOWN);
	m0_rconfc_stop_sync(&rconfc);
	m0_rconfc_fini(&rconfc);
	m0_clink_fini(&clink);
	ut_mero_stop(&mach, &rctx);
}

struct m0_semaphore gops_sem;

static void _drained(struct m0_rconfc *rconfc)
{
	m0_semaphore_up(&gops_sem);
}

static void test_gops(void)
{
	struct m0_rconfc         rconfc;
	struct m0_rpc_machine    mach;
	struct m0_confc_ctx      confc_ctx;
	int                      rc;
	struct m0_rpc_server_ctx rctx;
	bool                     check_res;
	struct rlock_ctx        *rlx;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	M0_SET0(&rconfc);
	rc = m0_rconfc_init(&rconfc, &g_grp, &mach, NULL);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_start_sync(&rconfc);
	M0_UT_ASSERT(rc == 0);
	rlx = rconfc.rc_rlock_ctx;

	/* imitate check op */
	m0_mutex_lock(&rconfc.rc_confc.cc_lock);
	check_res = rconfc.rc_gops.go_check(&rconfc.rc_confc);
	M0_UT_ASSERT(check_res == true);
	m0_mutex_unlock(&rconfc.rc_confc.cc_lock);

	/* imitate cache drain event */
	m0_semaphore_init(&gops_sem, 0);
	m0_rconfc_lock(&rconfc);
	m0_rconfc_drained_cb_set(&rconfc, _drained);
	m0_rconfc_unlock(&rconfc);
	m0_confc_ctx_init(&confc_ctx, &rconfc.rc_confc);
	m0_rconfc_ri_ops.rio_conflict(&rlx->rlc_req);
	/* release last confc ctx, so go_drain callback is called */
	m0_confc_ctx_fini(&confc_ctx);
	m0_semaphore_down(&gops_sem);
	/* make sure reelection is finished and reading is allowed  */
	m0_confc_ctx_init(&confc_ctx, &rconfc.rc_confc);
	m0_confc_ctx_fini(&confc_ctx);

	/* imitate skip op */
	rc = rconfc.rc_gops.go_skip(&rconfc.rc_confc);
	M0_UT_ASSERT(rc == -ENOENT);
	m0_rconfc_stop_sync(&rconfc);
	m0_rconfc_fini(&rconfc);
	ut_mero_stop(&mach, &rctx);
	m0_semaphore_fini(&gops_sem);
}

static void update_confd_version(struct m0_rpc_server_ctx *rctx,
				 uint64_t                  new_ver)
{
	struct m0_reqh         *reqh;
	struct m0_reqh_service *svc;
	struct m0_confd        *confd = NULL;
	struct m0_conf_cache   *confd_cache;
	struct m0_conf_root    *root_obj;

	/* Find confd instance through corresponding confd service */
	reqh = &rctx->rsx_mero_ctx.cc_reqh_ctx.rc_reqh;
	m0_tl_for(m0_reqh_svc, &reqh->rh_services, svc) {
		if (svc->rs_type == &m0_confd_stype) {
			confd = container_of(svc, struct m0_confd, d_reqh);
			break;
		}
	} m0_tl_endfor;
	M0_ASSERT(confd != NULL);

	confd_cache = confd->d_cache;
	confd_cache->ca_ver = new_ver;
	root_obj = M0_CONF_CAST(m0_conf_cache_lookup(confd_cache,
						     &M0_CONF_ROOT_FID),
				m0_conf_root);
	M0_ASSERT(root_obj != NULL);
	root_obj->rt_verno = new_ver;
}

static void test_version_change(void)
{
	struct m0_rconfc         rconfc;
	struct m0_rpc_machine    mach;
	int                      rc;
	struct m0_rpc_server_ctx rctx;
	struct rlock_ctx        *rlx;
	struct m0_conf_obj      *root_obj;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	m0_semaphore_init(&g_expired_sem, 0);

	M0_SET0(&rconfc);
	rc = m0_rconfc_init(&rconfc, &g_grp, &mach, conflict_exp_cb);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_start_sync(&rconfc);
	M0_UT_ASSERT(rc == 0);

	/* Check that version 1 in use */
	M0_UT_ASSERT(rconfc.rc_ver == 1);
	rc = m0_confc_open_sync(&root_obj, rconfc.rc_confc.cc_root, M0_FID0);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(M0_CONF_CAST(root_obj, m0_conf_root)->rt_verno == 1);
	m0_confc_close(root_obj);

	/* Update conf DB version and immitate read lock conflict */
	update_confd_version(&rctx, 2);
	rlx = rconfc.rc_rlock_ctx;
	m0_rconfc_ri_ops.rio_conflict(&rlx->rlc_req);

	/* Wait till version reelection is finished */
	m0_semaphore_down(&g_expired_sem);
	m0_sm_group_lock(rconfc.rc_sm.sm_grp);
	m0_sm_timedwait(&rconfc.rc_sm, M0_BITS(RCS_IDLE, RCS_FAILURE),
			M0_TIME_NEVER);
	m0_sm_group_unlock(rconfc.rc_sm.sm_grp);
	M0_UT_ASSERT(rconfc.rc_sm.sm_state == RCS_IDLE);

	/* Check that version in use is 2 */
	M0_UT_ASSERT(rconfc.rc_ver == 2);
	rc = m0_confc_open_sync(&root_obj, rconfc.rc_confc.cc_root, M0_FID0);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(M0_CONF_CAST(root_obj, m0_conf_root)->rt_verno == 2);
	m0_confc_close(root_obj);

	m0_rconfc_stop_sync(&rconfc);
	m0_rconfc_fini(&rconfc);
	m0_semaphore_fini(&g_expired_sem);
	ut_mero_stop(&mach, &rctx);
}

static void test_cache_drop(void)
{
	struct m0_rconfc         rconfc;
	struct m0_rpc_machine    mach;
	int                      rc;
	struct m0_rpc_server_ctx rctx;
	struct rlock_ctx        *rlx;
	struct m0_conf_obj      *root_obj;
	struct m0_confc         *confc;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	m0_semaphore_init(&g_expired_sem, 0);
	M0_SET0(&rconfc);
	rc = m0_rconfc_init(&rconfc, &g_grp, &mach, conflict_exp_cb);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_start_sync(&rconfc);
	M0_UT_ASSERT(rc == 0);
	/* Open root conf object */
	confc = &rconfc.rc_confc;
	rc = m0_confc_open_sync(&root_obj, confc->cc_root, M0_FID0);
	M0_UT_ASSERT(rc == 0);
	/*
	 * Imitate conflict for read lock, so rconfc asks its
	 * user to put all opened conf objects.
	 */
	rlx = rconfc.rc_rlock_ctx;
	m0_rconfc_ri_ops.rio_conflict(&rlx->rlc_req);
	m0_semaphore_down(&g_expired_sem);
	/* Sleep to make sure rconfc wait for us */
	while (usleep(200) == -1);
	/*
	 * Close root conf object, expecting rconfc to release
	 * read lock and start reelection process.
	 */
	m0_confc_close(root_obj);
	m0_semaphore_fini(&g_expired_sem);
	m0_rconfc_stop_sync(&rconfc);
	m0_rconfc_fini(&rconfc);
	ut_mero_stop(&mach, &rctx);
}

static void test_confc_ctx_block(void)
{
	struct m0_rconfc         rconfc;
	struct m0_rpc_machine    mach;
	int                      rc;
	struct m0_rpc_server_ctx rctx;
	struct rlock_ctx        *rlx;
	struct m0_confc_ctx      confc_ctx;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	M0_SET0(&rconfc);
	rc = m0_rconfc_init(&rconfc, &g_grp, &mach, NULL);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_start_sync(&rconfc);
	M0_UT_ASSERT(rc == 0);
	rlx = rconfc.rc_rlock_ctx;
	m0_rconfc_ri_ops.rio_conflict(&rlx->rlc_req);

	m0_confc_ctx_init(&confc_ctx, &rconfc.rc_confc);
	m0_confc_ctx_fini(&confc_ctx);

	m0_rconfc_stop_sync(&rconfc);
	m0_rconfc_fini(&rconfc);
	ut_mero_stop(&mach, &rctx);
}

static void test_reconnect_fail(void)
{
	struct m0_rpc_machine    mach;
	struct m0_rpc_server_ctx rctx;
	int                      rc;
	struct m0_rconfc         rconfc;
	uint64_t                 ver;
	struct m0_conf_obj      *cobj;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_init(&rconfc, &g_grp, &mach, test_null_exp_cb);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_start_sync(&rconfc);
	M0_UT_ASSERT(rc == 0);
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
	m0_rconfc_stop_sync(&rconfc);
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
	uint64_t                 ver;
	struct m0_conf_obj      *cobj;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_init(&rconfc, &g_grp, &mach, test_null_exp_cb);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_start_sync(&rconfc);
	M0_UT_ASSERT(rc == 0);
	ver = m0_rconfc_ver_max_read(&rconfc);
	M0_UT_ASSERT(ver == rconfc.rc_ver);
	/* imitate successful reconnection */
	m0_fi_enable_off_n_on_m("skip_confd_st_in", "force_reconnect_success",
				0, 1);
	m0_fi_enable_off_n_on_m("on_replied", "fail_rpc_reply", 0, 1);
	m0_rconfc_lock(&rconfc);
	rconfc.rc_gops.go_skip = _skip;
	m0_rconfc_unlock(&rconfc);
	/* do regular path opening with disconnected confc */
	rc = m0_confc_open_sync(&cobj, rconfc.rc_confc.cc_root,
				M0_CONF_ROOT_PROFILES_FID,
				m0_ut_conf_fids[M0_UT_CONF_PROF],
				M0_CONF_PROFILE_FILESYSTEM_FID);
	M0_UT_ASSERT(rc == 0);
	m0_confc_close(cobj);
	m0_fi_disable("skip_confd_st_in", "force_reconnect_success");
	m0_rconfc_stop_sync(&rconfc);
	m0_rconfc_fini(&rconfc);
	ut_mero_stop(&mach, &rctx);
}

static void _subscribe_to_service(struct m0_rconfc *rconfc,
				  struct m0_fid    *fid,
				  struct m0_clink  *clink)
{
	struct m0_conf_obj   *obj;
	struct m0_confc      *phony = &rconfc->rc_phony;
	struct m0_conf_cache *cache = &phony->cc_cache;

	obj = m0_conf_cache_lookup(cache, fid);
	M0_UT_ASSERT(obj != NULL);

	m0_clink_add_lock(&obj->co_ha_chan, clink);
}

struct _ha_notify_ctx {
	struct m0_clink     clink;
	struct m0_semaphore sem;
	struct m0_fid       fid;
};

static void _notify_cb(struct m0_sm_group *grp, struct m0_sm_ast *ast)
{
	struct _ha_notify_ctx *x    = ast->sa_datum;
	struct m0_ha_note      n1[] = { { x->fid, M0_NC_FAILED } };
	struct m0_ha_nvec      nvec = { ARRAY_SIZE(n1), n1 };

	m0_ha_state_accept(&nvec);
}

static bool _clink_cb(struct m0_clink *link)
{
	struct _ha_notify_ctx *x =
		container_of(link, struct _ha_notify_ctx, clink);
	struct m0_conf_obj    *obj =
		container_of(link->cl_chan, struct m0_conf_obj, co_ha_chan);

	/* now make sure the signal came from the right object ... */
	M0_UT_ASSERT(m0_fid_eq(&x->fid, &obj->co_id));
	M0_UT_ASSERT(obj->co_ha_state == M0_NC_FAILED);
	/* ... and let the test move on */
	m0_semaphore_up(&x->sem);
	return false;
}

static void test_ha_notify(void)
{
	struct m0_rpc_machine    mach;
	struct m0_rpc_server_ctx rctx;
	int                      rc;
	struct m0_rconfc         rconfc;
	struct m0_fid            rm_fid = M0_FID_TINIT('s', 1, 2);
	struct m0_sm_ast         notify_ast = {0};
	struct _ha_notify_ctx    hnx;

	rc = ut_mero_start(&mach, &rctx);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_init(&rconfc, &g_grp, &mach, NULL);
	M0_UT_ASSERT(rc == 0);
	rc = m0_rconfc_start_sync(&rconfc);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(rconfc.rc_ver != 0);

	/* make sure rconfc is ready */
	M0_UT_ASSERT(rconfc.rc_sm.sm_state == RCS_IDLE);
	/* prepare notification context */
	m0_semaphore_init(&hnx.sem, 0);
	m0_clink_init(&hnx.clink, _clink_cb);
	_subscribe_to_service(&rconfc, &rm_fid, &hnx.clink);
	hnx.fid = rm_fid;

	/* imitate HA note arrived from outside */
	notify_ast.sa_datum = &hnx;
	notify_ast.sa_cb = _notify_cb;
	m0_sm_ast_post(&g_grp, &notify_ast);

	/* now wait for notification fired ... */
	m0_semaphore_down(&hnx.sem);
	m0_semaphore_fini(&hnx.sem);
	/* ... unsubscribe ... */
	m0_clink_del_lock(&hnx.clink);
	m0_clink_fini(&hnx.clink);
	/* ... and leave */
	m0_rconfc_lock(&rconfc);
	m0_sm_timedwait(&rconfc.rc_sm, M0_BITS(RCS_FAILURE), M0_TIME_NEVER);
	m0_rconfc_unlock(&rconfc);
	m0_rconfc_stop_sync(&rconfc);
	m0_rconfc_fini(&rconfc);
	/*
	 * rconfc RM owner didn't return credits to creditor and now
	 * this owner is finalised. Drop this loan on creditor side.
	 */
	m0_fi_enable_once("owner_finalisation_check", "drop_loans");
	ut_mero_stop(&mach, &rctx);
}

static int rconfc_ut_init(void)
{
	return conf_ut_ast_thread_init();
}

static int rconfc_ut_fini(void)
{
	return conf_ut_ast_thread_fini();
}

struct m0_ut_suite rconfc_ut = {
	.ts_name  = "rconfc-ut",
	.ts_init  = rconfc_ut_init,
	.ts_fini  = rconfc_ut_fini,
	.ts_tests = {
		{ "init-fini",  test_init_fini },
		{ "start-stop", test_start_stop },
		{ "start-fail", test_start_failures },
		{ "reading",    test_reading },
		{ "impossible", test_quorum_impossible },
		{ "gate-ops",   test_gops },
		{ "change-ver", test_version_change },
		{ "cache-drop", test_cache_drop },
		{ "ctx-block",  test_confc_ctx_block },
		{ "reconnect",  test_reconnect_success },
		{ "recon-fail", test_reconnect_fail },
		{ "ha-notify",  test_ha_notify },
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
