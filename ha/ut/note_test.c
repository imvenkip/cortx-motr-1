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
 * Original author: Atsuro Hoshino <atsuro_hoshino@xyratex.com>
 * Original creation date: 19-Sep-2013
 */

#include "conf/confc.h"
#include "conf/preload.h"   /* M0_CONF_STR_MAXLEN */
#include "ut/file_helpers.h"
#include "fid/fid.h"
#include "fop/fom_generic.h"
#include "lib/buf.h"
#include "lib/errno.h"
#include "lib/misc.h"       /* M0_QUOTE */
#include "lib/string.h"
#include "lib/time.h"
#include "lib/uuid.h"
#include "net/lnet/lnet.h"
#include "rpc/rpc.h"
#include "rpc/rpc_opcodes.h"
#include "rpc/rpclib.h"
#include "ut/cs_service.h"
#include "ut/ut.h"
#include "conf/ut/confc.h"  /* conf_ut_obj_find() */

#include "ha/note.c"
#include "ha/note_fops.h"
#include "ha/note_fops_xc.h"
#include "ha/note_xc.h"
#include "conf/helpers.h"  /* m0_conf_fs_get */

/* See "ha/ut/Makefile.sub" for M0_HA_UT_DIR */
#define M0_HA_UT_PATH(name)   M0_QUOTE(M0_CONF_UT_DIR) "/" name

#define CLIENT_DB_NAME        "ha_ut_client.db"
#define CLIENT_ENDPOINT_ADDR  "0@lo:12345:34:*"

#define SERVER_DB_NAME        "ha_ut_confd_server.db"
#define SERVER_STOB_NAME      "ha_ut_confd_server.stob"
#define SERVER_ADDB_STOB_NAME "linuxstob:ha_ut_confd_server.addb_stob"
#define SERVER_LOG_NAME       "ha_ut_confd_server.log"
#define SERVER_ENDPOINT_ADDR  "0@lo:12345:34:1"
#define SERVER_ENDPOINT       "lnet:" SERVER_ENDPOINT_ADDR

/* See "conf/ut/conf-str.txt" */
static const struct m0_fid conf_obj_id_fs = M0_FID_TINIT('f', 2, 1);

static struct m0_net_xprt    *xprt = &m0_net_lnet_xprt;
static struct m0_net_domain   client_net_dom;
static struct m0_rpc_session *session;
static char                   ut_ha_conf_str[M0_CONF_STR_MAXLEN];
struct m0_conf_filesystem    *fs;

enum {
	CLIENT_COB_DOM_ID  = 16,
	SESSION_SLOTS      = 1,
	MAX_RPCS_IN_FLIGHT = 1,
};

static struct m0_rpc_client_ctx cctx = {
	.rcx_net_dom            = &client_net_dom,
	.rcx_local_addr         = CLIENT_ENDPOINT_ADDR,
	.rcx_remote_addr        = SERVER_ENDPOINT_ADDR,
	.rcx_max_rpcs_in_flight = MAX_RPCS_IN_FLIGHT,
	.rcx_fid                = &g_process_fid,
};

static char *server_argv[] = {
	"ha_ut", "-T", "AD", "-D", SERVER_DB_NAME,
	"-S", SERVER_STOB_NAME, "-A", SERVER_ADDB_STOB_NAME,
	"-w", "10", "-e", SERVER_ENDPOINT,
	"-s", "ds1:<0x7300000000000001:1>",
	"-s", "ds2:<0x7300000000000001:2>",
	"-s", "addb2:<0x7300000000000001:3>",
	"-s", "confd:<0x7300000000000001:4>",
	"-c", M0_UT_PATH("conf-str.txt"), "-P", M0_UT_CONF_PROFILE
};

static struct m0_rpc_server_ctx sctx = {
	.rsx_xprts         = &xprt,
	.rsx_xprts_nr      = 1,
	.rsx_argv          = server_argv,
	.rsx_argc          = ARRAY_SIZE(server_argv),
	.rsx_log_file_name = SERVER_LOG_NAME,
};

extern struct m0_reqh_service_type m0_rpc_service_type;

static struct m0_mutex chan_lock;
static struct m0_chan  chan;
static struct m0_clink clink;

static struct m0_sm_group g_grp;

static struct {
	bool             run;
	struct m0_thread thread;
} g_ast;

/* ----------------------------------------------------------------
 * Auxiliary functions
 * ---------------------------------------------------------------- */

static void start_rpc_client_and_server(void)
{
	int rc;

	rc = m0_net_domain_init(&client_net_dom, xprt);
	M0_ASSERT(rc == 0);
	rc = m0_rpc_server_start(&sctx);
	M0_ASSERT(rc == 0);
	rc = m0_rpc_client_start(&cctx);
	M0_ASSERT(rc == 0);
}

static void stop_rpc_client_and_server(void)
{
	int rc;

	rc = m0_rpc_client_stop(&cctx);
	M0_ASSERT(rc == 0);
	m0_rpc_server_stop(&sctx);
	m0_net_domain_fini(&client_net_dom);
}

static void ast_thread(int _ M0_UNUSED)
{
	while (g_ast.run) {
		m0_chan_wait(&g_grp.s_clink);
		m0_sm_group_lock(&g_grp);
		m0_sm_asts_run(&g_grp);
		m0_sm_group_unlock(&g_grp);
	}
}

static int ast_thread_init(void)
{
	m0_sm_group_init(&g_grp);
	g_ast.run = true;
	return M0_THREAD_INIT(&g_ast.thread, int, NULL, &ast_thread, 0,
			      "ast_thread");
}

static void ast_thread_fini(void)
{
	g_ast.run = false;
	m0_clink_signal(&g_grp.s_clink);
	m0_thread_join(&g_ast.thread);
	m0_thread_fini(&g_ast.thread);
	m0_sm_group_fini(&g_grp);
}

static void done_get_chan_init(void)
{
	m0_mutex_init(&chan_lock);
	m0_chan_init(&chan, &chan_lock);
	m0_clink_init(&clink, NULL);
	m0_clink_add_lock(&chan, &clink);
}

static void done_get_chan_fini(void)
{
	m0_clink_del_lock(&clink);
	m0_clink_fini(&clink);
	m0_chan_fini_lock(&chan);
	m0_mutex_fini(&chan_lock);
}

static void local_confc_init(struct m0_confc *confc)
{
	int rc;

	rc = m0_ut_file_read(M0_UT_PATH("conf-str.txt"),
			     ut_ha_conf_str, sizeof ut_ha_conf_str);
	M0_UT_ASSERT(rc == 0);

	/*
	 * All configuration objects need to be preloaded, since
	 * m0_ha_state_accept() function traverses all the descendants appearing
	 * in preloaded data. When missing configuration object is found,
	 * unit test for m0_ha_state_accept() will likely to show an error
	 * similar to below:
	 *
	 *  FATAL : [lib/assert.c:43:m0_panic] panic:
	 *    obj->co_status == M0_CS_READY m0_conf_obj_put()
	 */
	rc = m0_confc_init(confc, &g_grp, SERVER_ENDPOINT_ADDR,
			   &(cctx.rcx_rpc_machine), ut_ha_conf_str);
	M0_UT_ASSERT(rc == 0);
}

static void compare_ha_state(struct m0_confc *confc,
			     enum m0_ha_obj_state state)
{
	struct m0_conf_obj *node_dir;
	struct m0_conf_obj *node;
	struct m0_conf_obj *svc_dir;
	struct m0_conf_obj *svc;
	int                 rc;

	rc = m0_confc_open_sync(&node_dir, confc->cc_root,
				M0_CONF_ROOT_PROFILES_FID,
				m0_ut_conf_fids[M0_UT_CONF_PROF],
				M0_CONF_PROFILE_FILESYSTEM_FID,
				M0_CONF_FILESYSTEM_NODES_FID);
	M0_UT_ASSERT(rc == 0);

	rc = m0_confc_open_sync(&node, node_dir, M0_FID_TINIT('n', 1, 2));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(node->co_ha_state == state);

	rc = m0_confc_open_sync(&svc_dir, node, M0_CONF_NODE_PROCESSES_FID,
				M0_FID_TINIT('r', 1, 5),
				M0_CONF_PROCESS_SERVICES_FID);
	M0_UT_ASSERT(rc == 0);

	rc = m0_confc_open_sync(&svc, svc_dir, M0_FID_TINIT('s', 1, 9));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(svc->co_ha_state == state);

	m0_confc_close(svc);

	rc = m0_confc_open_sync(&svc, svc_dir, M0_FID_TINIT('s', 1, 10));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(svc->co_ha_state == state);

	m0_confc_close(svc);
	m0_confc_close(svc_dir);
	m0_confc_close(node);
	m0_confc_close(node_dir);
}

/* ----------------------------------------------------------------
 * Unit tests
 * ---------------------------------------------------------------- */
static void test_ha_state_set_and_get(void)
{
	struct m0_confc   confc;
	int               rc;
	struct m0_ha_note n1[] = {
		{ M0_FID_TINIT('n', 1, 2), M0_NC_ONLINE },
		{ M0_FID_TINIT('s', 1, 9), M0_NC_ONLINE },
		{ M0_FID_TINIT('s', 1, 10), M0_NC_ONLINE},
	};
	struct m0_ha_nvec nvec = { ARRAY_SIZE(n1), n1 };

	local_confc_init(&confc);

	m0_ha_state_set(session, &nvec);

	n1[0].no_state = M0_NC_UNKNOWN;
	n1[1].no_state = M0_NC_UNKNOWN;
	n1[2].no_state = M0_NC_UNKNOWN;
	rc = m0_ha_state_get(session, &nvec, &chan);
	M0_UT_ASSERT(rc == 0);
	m0_chan_wait(&clink);
	m0_ha_state_accept(&confc, &nvec);
	compare_ha_state(&confc, M0_NC_ONLINE);

	m0_confc_fini(&confc);
}

static void test_ha_state_accept(void)
{
	struct m0_confc confc;
	struct m0_fid u[] = {
		M0_FID_TINIT('n', 1, 2),
		M0_FID_TINIT('s', 1, 9),
		M0_FID_TINIT('s', 1, 10),
	};
	struct m0_ha_note *n;

	M0_ALLOC_ARR(n, ARRAY_SIZE(u));
	struct m0_ha_nvec  nvec;
	int                i;

	local_confc_init(&confc);

	nvec.nv_nr   = ARRAY_SIZE(u);
	nvec.nv_note = n;

	/* To initialize */
	for (i = 0; i < ARRAY_SIZE(u); ++i) {
		n[i].no_id    = u[i];
		n[i].no_state = M0_NC_ONLINE;
	}
	m0_ha_state_accept(&confc, &nvec);
	compare_ha_state(&confc, M0_NC_ONLINE);

	/* To check updates */
	for (i = 0; i < ARRAY_SIZE(u); ++i) {
		n[i].no_state = M0_NC_FAILED;
	}
	m0_ha_state_accept(&confc, &nvec);
	compare_ha_state(&confc, M0_NC_FAILED);

	m0_confc_fini(&confc);
	m0_free(n);
}

static void failure_sets_build(struct m0_reqh *reqh, struct m0_ha_nvec *nvec)
{
	int                        rc;

	rc = m0_fid_sscanf(M0_UT_CONF_PROFILE, &reqh->rh_profile);
	M0_UT_ASSERT(rc == 0);
	local_confc_init(&reqh->rh_confc);
	m0_ha_state_set(session, nvec);

	rc = m0_conf_fs_get(&reqh->rh_profile, &reqh->rh_confc, &fs);
	M0_UT_ASSERT(rc == 0);

        rc = m0_conf_full_load(fs);
        M0_UT_ASSERT(rc == 0);

	rc = m0_conf_failure_sets_build(session, fs, &reqh->rh_failure_sets);
	M0_UT_ASSERT(rc == 0);
}

static void failure_sets_destroy(struct m0_reqh *reqh)
{
	m0_conf_failure_sets_destroy(&reqh->rh_failure_sets);
	m0_confc_close(&fs->cf_obj);
	m0_confc_fini(&reqh->rh_confc);
}

static void test_failure_sets(void)
{
	struct m0_reqh             reqh;
	struct m0_conf_obj        *obj;
	struct m0_ha_note n1[] = {
		{ M0_FID_TINIT('a', 1, 3),  M0_NC_FAILED },
		{ M0_FID_TINIT('e', 1, 7),  M0_NC_FAILED },
		{ M0_FID_TINIT('c', 1, 11), M0_NC_FAILED },
	};
	struct m0_ha_nvec nvec = { ARRAY_SIZE(n1), n1 };

	failure_sets_build(&reqh, &nvec);

	M0_UT_ASSERT(m0_conf_failure_sets_tlist_length(&reqh.rh_failure_sets) ==
 			ARRAY_SIZE(n1));

	m0_tl_for(m0_conf_failure_sets, &reqh.rh_failure_sets, obj) {
		M0_UT_ASSERT(obj->co_ha_state == M0_NC_FAILED);
	} m0_tlist_endfor;

	failure_sets_destroy(&reqh);
}

static void test_poolversion_get(void)
{
	int                        rc;
	struct m0_reqh             reqh;
	struct m0_conf_pver       *pver0 = NULL;
	struct m0_conf_pver       *pver1 = NULL;
	struct m0_conf_pver       *pver2 = NULL;
	struct m0_ha_note n1[] = {
		{ M0_FID_TINIT('a', 1, 3),  M0_NC_ONLINE },
		{ M0_FID_TINIT('e', 1, 7),  M0_NC_ONLINE },
		{ M0_FID_TINIT('c', 1, 11), M0_NC_ONLINE },
	};
	struct m0_ha_nvec nvec = { ARRAY_SIZE(n1), n1 };
	struct m0_ha_nvec nvec1 = { 1, n1 };

	failure_sets_build(&reqh, &nvec);

	rc = m0_conf_poolversion_get(&reqh.rh_profile, &reqh.rh_confc,
				     &reqh.rh_failure_sets, &pver0);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_fid_eq(&pver0->pv_obj.co_id, &M0_FID_TINIT('v', 1, 8)));

	/* Make rack from pool verison 0  FAILED */
	n1[0].no_id   = M0_FID_TINIT('a', 1, 3);
	n1[0].no_state = M0_NC_FAILED;
	m0_ha_state_accept(&reqh.rh_confc, &nvec1);

	rc = m0_conf_poolversion_get(&reqh.rh_profile, &reqh.rh_confc,
				     &reqh.rh_failure_sets, &pver1);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(pver0->pv_nfailed == 1);
	M0_UT_ASSERT(m0_fid_eq(&pver1->pv_obj.co_id,
		     &M0_FID_TINIT('v', 1, 57)));

	/* Make rack from pool verison 1  FAILED */
	n1[0].no_id   = M0_FID_TINIT('a', 1, 52);
	n1[0].no_state = M0_NC_FAILED;
	m0_ha_state_accept(&reqh.rh_confc, &nvec1);

	rc = m0_conf_poolversion_get(&reqh.rh_profile, &reqh.rh_confc,
				     &reqh.rh_failure_sets, &pver2);
	M0_UT_ASSERT(rc == -ENOENT);
	M0_UT_ASSERT(pver1->pv_nfailed == 1);

	/* Make encl from  pool verison 0  FAILED */
	n1[0].no_id   = M0_FID_TINIT('e', 1, 7);
	n1[0].no_state = M0_NC_FAILED;
	m0_ha_state_accept(&reqh.rh_confc, &nvec1);

	rc = m0_conf_poolversion_get(&reqh.rh_profile, &reqh.rh_confc,
				     &reqh.rh_failure_sets, &pver2);
	M0_UT_ASSERT(rc == -ENOENT);
	M0_UT_ASSERT(pver0->pv_nfailed == 2);

	/* Make rack from pool verison 0 ONLINE */
	n1[0].no_id   = M0_FID_TINIT('a', 1, 3);
	n1[0].no_state = M0_NC_ONLINE;
	m0_ha_state_accept(&reqh.rh_confc, &nvec1);

	rc = m0_conf_poolversion_get(&reqh.rh_profile, &reqh.rh_confc,
				     &reqh.rh_failure_sets, &pver2);
	M0_UT_ASSERT(rc == -ENOENT);
	M0_UT_ASSERT(pver0->pv_nfailed == 1);

	/* Make encl from pool verison 0 ONLINE */
	n1[0].no_id   = M0_FID_TINIT('e', 1, 7);
	n1[0].no_state = M0_NC_ONLINE;
	m0_ha_state_accept(&reqh.rh_confc, &nvec1);

	rc = m0_conf_poolversion_get(&reqh.rh_profile, &reqh.rh_confc,
				     &reqh.rh_failure_sets, &pver2);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(pver2 != NULL);
	M0_UT_ASSERT(pver0->pv_nfailed == 0);
	M0_UT_ASSERT(m0_fid_eq(&pver0->pv_obj.co_id, &pver2->pv_obj.co_id));

	failure_sets_destroy(&reqh);
}

/* -------------------------------------------------------------------
 * Test suite
 * ------------------------------------------------------------------- */

static int ha_state_ut_init(void)
{
	int rc;

	rc = ast_thread_init();
	M0_ASSERT(rc == 0);

	done_get_chan_init();
	start_rpc_client_and_server();
	session = &cctx.rcx_session;

	return 0;
}

static int ha_state_ut_fini(void)
{
	stop_rpc_client_and_server();
	done_get_chan_fini();
	ast_thread_fini();
	return 0;
}

struct m0_ut_suite ha_state_ut = {
	.ts_name = "ha_state-ut",
	.ts_init = ha_state_ut_init,
	.ts_fini = ha_state_ut_fini,
	.ts_tests = {
		{ "ha_state_set_and_get", test_ha_state_set_and_get },
		{ "ha_state_accept",      test_ha_state_accept },
		{ "ha-failure-sets",      test_failure_sets },
		{ "ha-poolversion_get",   test_poolversion_get },
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
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
