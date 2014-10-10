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

#include "conf/confc.h"
#include "conf/obj_ops.h"   /* M0_CONF_DIREND */
#include "conf/ut/file_helpers.h"
#include "conf/ut/rpc_helpers.h"
#include "net/lnet/lnet.h"  /* m0_net_lnet_xprt */
#include "rpc/rpclib.h"     /* m0_rpc_server_ctx */
#include "ut/ut.h"

#define SERVER_ENDPOINT_ADDR "0@lo:12345:34:1"
#define SERVER_ENDPOINT      "lnet:" SERVER_ENDPOINT_ADDR
#define CLIENT_ENDPOINT_ADDR "0@lo:12345:34:*"

static struct m0_sm_group  g_grp;
static uint8_t             g_num;
static struct m0_net_xprt *g_xprt = &m0_net_lnet_xprt;

struct waiter {
	struct m0_confc_ctx w_ctx;
	struct m0_clink     w_clink;
};

/* Filters out intermediate state transitions of m0_confc_ctx::fc_mach. */
static bool _filter(struct m0_clink *link)
{
	return !m0_confc_ctx_is_completed(&container_of(link, struct waiter,
							w_clink)->w_ctx);
}

static void waiter_init(struct waiter *w, struct m0_confc *confc)
{
	m0_confc_ctx_init(&w->w_ctx, confc);
	m0_clink_init(&w->w_clink, _filter);
	m0_clink_add_lock(&w->w_ctx.fc_mach.sm_chan, &w->w_clink);
}

static void waiter_fini(struct waiter *w)
{
	m0_clink_del_lock(&w->w_clink);
	m0_clink_fini(&w->w_clink);
	m0_confc_ctx_fini(&w->w_ctx);
}

static int waiter_wait(struct waiter *w, struct m0_conf_obj **result)
{
	int rc;

	while (!m0_confc_ctx_is_completed(&w->w_ctx))
		m0_chan_wait(&w->w_clink);

	rc = m0_confc_ctx_error(&w->w_ctx);
	if (rc == 0 && result != NULL)
		*result = m0_confc_ctx_result(&w->w_ctx);

	return rc;
}

/* ----------------------------------------------------------------
 * Source of configuration data: conf/ut/conf-str.txt
 *
 * fid table:
 *
 * profile (prof)      ('p', 2, 0)
 * filesystem (fs)     ('f', 2, 1)
 * service (svc-0)     ('s', 2, 2)
 * service (svc-1)     ('s', 2, 3)
 * service (svc-2)     ('s', 2, 4)
 * node    (node-0)    ('n', 2, 5)
 * node    (node-1)    ('n', 2, 9)
 * nic     (nic-0)     ('i', 2, 6)
 * nic     (nic-1)     ('i', 2, 7)
 * sdev    (sdev-0)    ('d', 2, 8)
 *
 * ---------------------------------------------------------------- */

enum {
	PROF,
	FS,
	SVC0,
	SVC1,
	SVC2,
	NODE0,
	NODE1,
	NIC0,
	NIC1,
	SDEV0,

	NR
};

static const struct m0_fid fids[NR] = {
	[PROF]       = M0_FID_TINIT('p', 2, 0),
	[FS]         = M0_FID_TINIT('f', 2, 1),
	[SVC0]       = M0_FID_TINIT('s', 2, 2),
	[SVC1]       = M0_FID_TINIT('s', 2, 3),
	[SVC2]       = M0_FID_TINIT('s', 2, 4),
	[NODE0]      = M0_FID_TINIT('n', 2, 5),
	[NODE1]      = M0_FID_TINIT('n', 2, 9),
	[NIC0]       = M0_FID_TINIT('i', 2, 6),
	[NIC1]       = M0_FID_TINIT('i', 2, 7),
	[SDEV0]      = M0_FID_TINIT('d', 2, 8)
};

static void sync_open_test(struct m0_conf_obj *svc_dir)
{
	struct m0_conf_obj     *obj;
	struct m0_conf_service *svc;
	int                     rc;

	M0_PRE(m0_conf_obj_type(svc_dir) == &M0_CONF_DIR_TYPE);

	rc = m0_confc_open_sync(&obj, svc_dir, M0_FID_TINIT('s', 5, 5));
	M0_UT_ASSERT(rc == -ENOENT);

	/* There is no configuration data for the node of "svc-2". */
	rc = m0_confc_open_sync(&obj, svc_dir, fids[SVC2],
				M0_CONF_SERVICE_NODE_FID);
	M0_UT_ASSERT(rc == -ENOENT);

	rc = m0_confc_open_sync(&obj, svc_dir, fids[SVC0]);
	M0_UT_ASSERT(rc == 0);

	svc = M0_CONF_CAST(obj, m0_conf_service);
	M0_UT_ASSERT(svc->cs_obj.co_status == M0_CS_READY);
	M0_UT_ASSERT(svc->cs_obj.co_cache == svc_dir->co_cache);
	M0_UT_ASSERT(m0_fid_eq(&svc->cs_obj.co_id, &fids[SVC0]));
	M0_UT_ASSERT(svc->cs_type == 1);
	M0_UT_ASSERT(strcmp(svc->cs_endpoints[0], "addr0") == 0);
	M0_UT_ASSERT(svc->cs_endpoints[1] == NULL);
	M0_UT_ASSERT(m0_fid_eq(&svc->cs_node->cn_obj.co_id, &fids[NODE0]));

	M0_UT_ASSERT(obj == &svc->cs_obj);
	rc = m0_confc_open_sync(&obj, obj, M0_CONF_SERVICE_NODE_FID);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(obj != &svc->cs_obj);
	M0_UT_ASSERT(obj == &svc->cs_node->cn_obj);

	m0_confc_close(&svc->cs_obj);
	m0_confc_close(obj);
}

static void services_open(struct m0_conf_obj **result, struct m0_confc *confc)
{
	struct waiter w;
	int           rc;

	waiter_init(&w, confc);
	rc = m0_confc_open(&w.w_ctx, NULL, M0_CONF_PROFILE_FILESYSTEM_FID,
			   M0_CONF_FILESYSTEM_SERVICES_FID);
	M0_UT_ASSERT(rc == 0);
	rc = waiter_wait(&w, result);
	M0_UT_ASSERT(rc == 0);
	waiter_fini(&w);

	M0_UT_ASSERT((*result)->co_status == M0_CS_READY);
	M0_UT_ASSERT((*result)->co_cache == &confc->cc_cache);
}

static void _svc_type_add(const struct m0_conf_obj *obj)
{
	g_num += M0_CONF_CAST(obj, m0_conf_service)->cs_type;
}

static bool _is_mgmt(const struct m0_conf_obj *obj)
{
	return M0_CONF_CAST(obj, m0_conf_service)->cs_type == M0_CST_MGS;
}

M0_BASSERT(M0_CONF_DIREND == 0);

/* This code originates from `confc-fspec-recipe4' in conf/confc.h. */
static int dir_entries_use(struct m0_conf_obj *dir,
			   void (*use)(const struct m0_conf_obj *),
			   bool (*stop_at)(const struct m0_conf_obj *))
{
	struct waiter       w;
	int                 rc;
	struct m0_conf_obj *entry = NULL;

	waiter_init(&w, m0_confc_from_obj(dir));

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
		rc = waiter_wait(&w, &entry);
		if (rc != 0 /* error */ || entry == NULL /* end of directory */)
			break;

		use(entry);
		if (stop_at != NULL && stop_at(entry))
			break;

		/* Re-initialise m0_confc_ctx. */
		waiter_fini(&w);
		waiter_init(&w, m0_confc_from_obj(dir));
	}

	m0_confc_close(entry);
	waiter_fini(&w);
	return rc;
}

/*
 * We need this function in order for m0_confc_open() to be called in
 * a separate stack frame.
 */
static void _retrieval_initiate(struct m0_confc_ctx *ctx)
{
	int rc;

	rc = m0_confc_open(ctx, NULL, M0_CONF_PROFILE_FILESYSTEM_FID,
			   M0_CONF_FILESYSTEM_SERVICES_FID, fids[SVC1],
			   M0_CONF_SERVICE_NODE_FID, M0_CONF_NODE_NICS_FID,
			   fids[NIC0]);
	M0_UT_ASSERT(rc == 0);
}

static void misc_test(struct m0_confc *confc)
{
	struct waiter w;

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
	waiter_init(&w, confc);
	_retrieval_initiate(&w.w_ctx);
	(void)waiter_wait(&w, NULL);
	waiter_fini(&w);
}

static void dir_test(struct m0_confc *confc)
{
	struct m0_conf_obj *fs;
	struct m0_conf_obj *svc_dir;
	struct m0_conf_obj *entry;
	int                 rc;

#if 1 /* XXX will fail */
	/*
	 * Opening in two steps -- first "filesystem" object, then
	 * "services" directory -- reveals a bug that would remain
	 * unseen if the directory was opened by single
	 * m0_confc_open_sync() call.
	 *
	 * M0_IMPOSSIBLE() statement in dir_readdir() is invoked.
	 *
	 * Note, that dir_test() should precede services_open() in
	 * order for the bug to manifest itself. (See the code of
	 * confc_test() below.)
	 */
	rc = m0_confc_open_sync(&fs, confc->cc_root,
				M0_CONF_PROFILE_FILESYSTEM_FID) ?:
		m0_confc_open_sync(&svc_dir, fs,
				   M0_CONF_FILESYSTEM_SERVICES_FID);
#else
	rc = m0_confc_open_sync(&svc_dir, confc->cc_root,
				M0_CONF_PROFILE_FILESYSTEM_FID,
				M0_CONF_FILESYSTEM_SERVICES_FID);
	fs = NULL;
#endif
	M0_UT_ASSERT(rc == 0);

	g_num = 0;
	rc = dir_entries_use(svc_dir, _svc_type_add, NULL);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(g_num == 6);

	g_num = 0;
	rc = dir_entries_use(svc_dir, _svc_type_add, _is_mgmt);
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(g_num == 4);

	g_num = 0;
	for (entry = NULL; (rc = m0_confc_readdir_sync(svc_dir, &entry)) > 0; )
		++g_num;
	M0_UT_ASSERT(g_num == 3);

	m0_confc_close(entry);
	m0_confc_close(svc_dir);
	m0_confc_close(fs);
}

static void confc_test(const char *confd_addr, struct m0_rpc_machine *rpc_mach,
		       const char *local_conf)
{
	struct m0_confc     confc;
	struct m0_conf_obj *svc_dir;
	int                 rc;

	rc = m0_confc_init(&confc, &g_grp, &fids[PROF],
			   confd_addr, rpc_mach, local_conf);
	M0_UT_ASSERT(rc == 0);

	dir_test(&confc);
	misc_test(&confc);
	services_open(&svc_dir, &confc); /* tests asynchronous interface */
	sync_open_test(svc_dir);

	m0_confc_close(svc_dir);
	m0_confc_fini(&confc);
}

static void test_confc_local(void)
{
	char            local_conf[4096];
	struct m0_confc confc;
	int             rc;

	rc = m0_ut_file_read(M0_CONF_UT_PATH("conf-str.txt"), local_conf,
			     sizeof local_conf);
	M0_UT_ASSERT(rc == 0);

	rc = m0_confc_init(&confc, &g_grp, &fids[PROF],
			   NULL, NULL, "bad configuration string");
	M0_UT_ASSERT(rc == -EPROTO);

	rc = m0_confc_init(&confc, &g_grp, &M0_FID_TINIT('p', 7, 7),
			   NULL, NULL, local_conf);
	M0_UT_ASSERT(rc == -ENODATA);

	confc_test(NULL, NULL, local_conf);
}

static void test_confc_net(void)
{
	struct m0_rpc_machine    mach;
	int                      rc;
#define NAME(ext) "ut_confd" ext
	char                    *argv[] = {
		NAME(""), "-T", "AD", "-D", NAME(".db"),
		"-S", NAME(".stob"), "-A", "linuxstob:"NAME("-addb.stob"),
		"-w", "10",
		"-e", SERVER_ENDPOINT, "-s", "confd",
		"-c", M0_CONF_UT_PATH("conf-str.txt")
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
	char            local_conf[4096];
	struct m0_confc confc;
	int             rc;

	rc = m0_ut_file_read(M0_CONF_UT_PATH("duplicated-ids.txt"), local_conf,
			     sizeof local_conf);
	M0_UT_ASSERT(rc == 0);

	rc = m0_confc_init(&confc, &g_grp, &fids[PROF], NULL, NULL, local_conf);
	M0_UT_ASSERT(rc == -EEXIST);
}

/* ------------------------------------------------------------------ */

static struct {
	bool             run;
	struct m0_thread thread;
} g_ast;

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

static int ast_thread_fini(void)
{
	g_ast.run = false;
	m0_clink_signal(&g_grp.s_clink);
	m0_thread_join(&g_ast.thread);
	m0_sm_group_fini(&g_grp);

	return 0;
}

struct m0_ut_suite confc_ut = {
	.ts_name  = "confc-ut",
	.ts_init  = ast_thread_init,
	.ts_fini  = ast_thread_fini,
	.ts_tests = {
		{ "local",     test_confc_local },
		{ "net",       test_confc_net },
		{ "bad-input", test_confc_invalid_input },
		{ NULL, NULL }
	}
};

#undef M0_TRACE_SUBSYSTEM
