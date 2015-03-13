/* -*- c -*- */
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
 * Original author: Valery V. Vorotyntsev <valery_vorotyntsev@xyratex.com>
 * Original creation date: 17-Sep-2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_UT
#include "lib/trace.h"

#include "conf/confc.h"     /* m0_confc__open */
#include "conf/helpers.h"
#include "conf/preload.h"   /* M0_CONF_STR_MAXLEN */
#include "conf/obj_ops.h"   /* M0_CONF_DIREND */
#include "conf/preload.h"   /* M0_CONF_STR_MAXLEN */
#include "conf/dir_iter.h"
#include "ut/file_helpers.h"
#include "conf/ut/rpc_helpers.h"
#include "conf/ut/common.h"
#include "net/lnet/lnet.h"  /* m0_net_lnet_xprt */
#include "rpc/rpclib.h"     /* m0_rpc_server_ctx */
#include "ut/ut.h"
#include "conf/ut/confc.h"  /* m0_ut_conf_fids */

#include "pool/pool.h"
#include "pool/pool_machine.h"

static char g_confc_str[M0_CONF_STR_MAXLEN];
uint8_t     g_num;

static void root_open_test(struct m0_confc *confc)
{
	struct m0_conf_root *root_obj;
	int                  rc;

	rc = m0_conf_root_open(confc, &root_obj);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(m0_fid_eq(&root_obj->rt_obj.co_id,
	                       &m0_ut_conf_fids[M0_UT_CONF_ROOT]));
	M0_UT_ASSERT(root_obj->rt_verno == 1);

	m0_confc_close(&root_obj->rt_obj);
}

static void sync_open_test(struct m0_conf_obj *nodes_dir)
{
	struct m0_conf_obj  *obj;
	struct m0_conf_node *node;
	int                  rc;

	M0_PRE(m0_conf_obj_type(nodes_dir) == &M0_CONF_DIR_TYPE);

	rc = m0_confc_open_sync(&obj, nodes_dir,
				m0_ut_conf_fids[M0_UT_CONF_UNKNOWN_NODE]);
	M0_UT_ASSERT(rc == -ENOENT);

	rc = m0_confc_open_sync(&obj, nodes_dir,
				m0_ut_conf_fids[M0_UT_CONF_NODE]);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(obj->co_status == M0_CS_READY);
	M0_UT_ASSERT(obj->co_cache == nodes_dir->co_cache);
	M0_UT_ASSERT(m0_fid_eq(&obj->co_id, &m0_ut_conf_fids[M0_UT_CONF_NODE]));

	node = M0_CONF_CAST(obj, m0_conf_node);
	M0_UT_ASSERT(node->cn_memsize    == 16000);
	M0_UT_ASSERT(node->cn_nr_cpu     == 2);
	M0_UT_ASSERT(node->cn_last_state == 3);
	M0_UT_ASSERT(node->cn_flags      == 2);
	M0_UT_ASSERT(node->cn_pool != NULL);

	M0_UT_ASSERT(obj == &node->cn_obj);
	m0_confc_close(obj);
}

static void nodes_open(struct m0_conf_obj **result,
		       struct m0_confc     *confc)
{
	struct conf_ut_waiter w;
	int                   rc;

	conf_ut_waiter_init(&w, confc);
	rc = m0_confc_open(&w.w_ctx, NULL,
			   M0_CONF_ROOT_PROFILES_FID,
			   m0_ut_conf_fids[M0_UT_CONF_PROF],
			   M0_CONF_PROFILE_FILESYSTEM_FID,
			   M0_CONF_FILESYSTEM_NODES_FID);
	M0_UT_ASSERT(rc == 0);
	rc = conf_ut_waiter_wait(&w, result);
	M0_UT_ASSERT(rc == 0);
	conf_ut_waiter_fini(&w);

	M0_UT_ASSERT((*result)->co_status == M0_CS_READY);
	M0_UT_ASSERT((*result)->co_cache == &confc->cc_cache);
}

static void _proc_cores_add(const struct m0_conf_obj *obj)
{
	g_num += M0_CONF_CAST(obj, m0_conf_process)->pc_cores;
}

static bool _proc_has_services(const struct m0_conf_obj *obj)
{
	return M0_CONF_CAST(obj, m0_conf_process)->pc_services != NULL;
}

M0_BASSERT(M0_CONF_DIREND == 0);

/* This code originates from `confc-fspec-recipe4' in conf/confc.h. */
static int dir_entries_use(struct m0_conf_obj *dir,
			   void (*use)(const struct m0_conf_obj *),
			   bool (*stop_at)(const struct m0_conf_obj *))
{
	struct conf_ut_waiter  w;
	struct m0_conf_obj    *entry = NULL;
	int                    rc;

	conf_ut_waiter_init(&w, m0_confc_from_obj(dir));

	while ((rc = m0_confc_readdir(&w.w_ctx, dir, &entry)) > 0) {
		if (rc == M0_CONF_DIRNEXT) {
			/* The entry is available immediately. */
			M0_ASSERT(entry != NULL);

			use(entry);
			if (stop_at != NULL && stop_at(entry)) {
				rc = 0;
				break;
			}
			continue; /* Note, that `entry' will be closed
				   * by m0_confc_readdir(). */
		}

		/* Cache miss. */
		M0_ASSERT(rc == M0_CONF_DIRMISS);
		rc = conf_ut_waiter_wait(&w, &entry);
		if (rc != 0 /* error */ || entry == NULL /* end of directory */)
			break;

		use(entry);
		if (stop_at != NULL && stop_at(entry))
			break;

		/* Re-initialise m0_confc_ctx. */
		conf_ut_waiter_fini(&w);
		conf_ut_waiter_init(&w, m0_confc_from_obj(dir));
	}

	m0_confc_close(entry);
	conf_ut_waiter_fini(&w);
	return rc;
}

static void dir_test(struct m0_confc *confc)
{
	struct m0_conf_obj *procs_dir;
	struct m0_conf_obj *entry = NULL;
	int                 rc;

	rc = m0_confc_open_sync(&procs_dir, confc->cc_root,
				M0_CONF_ROOT_PROFILES_FID,
				m0_ut_conf_fids[M0_UT_CONF_PROF],
				M0_CONF_PROFILE_FILESYSTEM_FID,
				M0_CONF_FILESYSTEM_NODES_FID,
				m0_ut_conf_fids[M0_UT_CONF_NODE],
				M0_CONF_NODE_PROCESSES_FID);
	M0_UT_ASSERT(rc == 0);

	g_num = 0;
	rc = dir_entries_use(procs_dir, _proc_cores_add, NULL);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(g_num == 3);

	g_num = 0;
	rc = dir_entries_use(procs_dir, _proc_cores_add, _proc_has_services);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(g_num == 2);

	g_num = 0;
	while (m0_confc_readdir_sync(procs_dir, &entry) > 0)
		++g_num;
	M0_UT_ASSERT(g_num == 2);

	m0_confc_close(entry);
	m0_confc_close(procs_dir);
}

/*
 * We need this function in order for m0_confc_open() to be called in
 * a separate stack frame.
 */
static void _retrieval_initiate(struct m0_confc_ctx *ctx)
{
	int rc;

	rc = m0_confc_open(ctx, NULL,
			   M0_CONF_ROOT_PROFILES_FID,
			   m0_ut_conf_fids[M0_UT_CONF_PROF],
			   M0_CONF_PROFILE_FILESYSTEM_FID,
			   M0_CONF_FILESYSTEM_NODES_FID,
			   m0_ut_conf_fids[M0_UT_CONF_NODE],
			   M0_CONF_NODE_PROCESSES_FID,
			   m0_ut_conf_fids[M0_UT_CONF_PROCESS0]);
	M0_UT_ASSERT(rc == 0);
}

static void misc_test(struct m0_confc *confc)
{
	struct m0_conf_obj    *obj;
	struct conf_ut_waiter  w;

	/*
	 * We should be able to call m0_confc_ctx_fini() right after
	 * m0_confc_ctx_init().
	 *
	 * Application code may depend on this ability (e.g.,
	 * dir_entries_use() does).
	 */
	m0_confc_ctx_init(&w.w_ctx, confc);
	m0_confc_ctx_fini(&w.w_ctx);

	/*
	 * A path to configuration object is created on stack (see the
	 * definition of m0_confc_open()).  We need to allow this
	 * stack frame to be destructed even before the result of
	 * configuration retrieval operation is obtained.
	 *
	 * In other words, m0_confc_ctx::fc_path should be able to
	 * outlive the array of path components, constructed by
	 * m0_confc_open().
	 */
	conf_ut_waiter_init(&w, confc);
	_retrieval_initiate(&w.w_ctx);
	(void)conf_ut_waiter_wait(&w, &obj);
	m0_confc_close(obj);
	conf_ut_waiter_fini(&w);
}

static void confc_test(const char *confd_addr, struct m0_rpc_machine *rpc_mach,
		       const char *conf_str)
{
	struct m0_confc     confc = {};
	struct m0_conf_obj *nodes_dir;
	int                 rc;

	rc = m0_confc_init(&confc, &g_grp, confd_addr, rpc_mach, conf_str);
	M0_UT_ASSERT(rc == 0);

	root_open_test(&confc);
	dir_test(&confc);
	misc_test(&confc);
	nodes_open(&nodes_dir, &confc); /* tests asynchronous interface */
	sync_open_test(nodes_dir);

	m0_confc_close(nodes_dir);
	m0_confc_fini(&confc);
}

static void test_confc_local(void)
{
	struct m0_confc     confc = {};
	struct m0_conf_obj *obj;
	int                 rc;

	rc = m0_ut_file_read(M0_UT_CONF_PATH("conf-str.txt"), g_confc_str,
			     sizeof g_confc_str);
	M0_UT_ASSERT(rc == 0);

	rc = m0_confc_init(&confc, &g_grp, NULL, NULL,
			   "bad configuration string");
	M0_UT_ASSERT(rc == -EPROTO);

	rc = m0_confc_init(&confc, &g_grp, NULL, NULL, g_confc_str);
	M0_UT_ASSERT(rc == 0);
	/* normal case - profile exists in conf */
	rc = m0_confc_open_sync(&obj, confc.cc_root,
				M0_CONF_ROOT_PROFILES_FID,
				m0_ut_conf_fids[M0_UT_CONF_PROF]);
	M0_UT_ASSERT(rc == 0);
	m0_confc_close(obj);
	/* fail case - profile does not exist */
	rc = m0_confc_open_sync(&obj, confc.cc_root,
				M0_CONF_ROOT_PROFILES_FID,
				M0_FID_TINIT('p', 7, 7));
	M0_UT_ASSERT(rc == -ENOENT);
	m0_confc_fini(&confc);

	confc_test(NULL, NULL, g_confc_str);
}

static void test_confc_net(void)
{
	struct m0_rpc_machine mach;
	int                   rc;
#define NAME(ext) "ut_confd" ext
	char                    *argv[] = {
		NAME(""), "-T", "AD", "-D", NAME(".db"),
		"-S", NAME(".stob"), "-A", "linuxstob:"NAME("-addb_stob"),
		"-c", M0_UT_CONF_PATH("conf-str.txt"),
		"-w", "10", "-e", SERVER_ENDPOINT,
		"-s", "confd:<0x7300000000000001:1>",
		"-P", M0_UT_CONF_PROFILE
	};
	struct m0_rpc_server_ctx confd = {
		.rsx_xprts         = &g_xprt,
		.rsx_xprts_nr      = 1,
		.rsx_argv          = argv,
		.rsx_argc          = ARRAY_SIZE(argv),
		.rsx_log_file_name = NAME(".log")
	};
#undef NAME

	rc = m0_rpc_server_start(&confd);
	M0_UT_ASSERT(rc == 0);

	rc = m0_ut_rpc_machine_start(&mach, g_xprt, CLIENT_ENDPOINT_ADDR);
	M0_UT_ASSERT(rc == 0);

	confc_test(SERVER_ENDPOINT_ADDR, &mach, NULL);

	m0_ut_rpc_machine_stop(&mach);
	m0_rpc_server_stop(&confd);
}

static void test_confc_invalid_input(void)
{
	struct m0_confc confc = {};
	int             rc;

	rc = m0_ut_file_read(M0_UT_CONF_PATH("duplicated-ids.txt"),
			     g_confc_str, sizeof g_confc_str);
	M0_UT_ASSERT(rc == 0);

	rc = m0_confc_init(&confc, &g_grp, NULL, NULL, g_confc_str);
	M0_UT_ASSERT(rc == -EEXIST);
}

struct m0_ut_suite confc_ut = {
	.ts_name  = "confc-ut",
	.ts_init  = conf_ut_ast_thread_init,
	.ts_fini  = conf_ut_ast_thread_fini,
	.ts_tests = {
		{ "local",     test_confc_local },
		{ "net",       test_confc_net },
		{ "bad-input", test_confc_invalid_input },
		{ NULL, NULL }
	}
};

#undef M0_TRACE_SUBSYSTEM
