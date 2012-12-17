/* -*- c -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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

#include "net/lnet/lnet.h"  /* m0_net_lnet_xprt */
#include "rpc/rpclib.h"     /* m0_rpc_server_ctx */
#include "ut/rpc.h"         /* M0_RPC_SERVER_CTX_DEFINE */
#include "conf/confc.h"
#include "conf/buf_ext.h"   /* m0_buf_streq */
#include "conf/ut/rpc_helpers.h"
#include "lib/ut.h"
#include "conf/confd_hack.h" /* XXX m0_conf_str, m0_confd_hack_mode, M0_CM_UT */

#define SERVER_ENDPOINT_ADDR "0@lo:12345:34:1"
#define SERVER_ENDPOINT      "lnet:" SERVER_ENDPOINT_ADDR
#define CLIENT_ENDPOINT_ADDR "0@lo:12345:34:*"

static struct m0_sm_group  g_grp;
static struct m0_net_xprt *g_xprt = &m0_net_lnet_xprt;

static void _confc_test(const char *confd_addr, struct m0_rpc_machine *rpc_mach,
			const char *local_conf);

static int service_start(struct m0_rpc_server_ctx *sctx)
{
	return m0_net_xprt_init(g_xprt) ?: m0_rpc_server_start(sctx);
}

static void service_stop(struct m0_rpc_server_ctx *sctx)
{
	m0_rpc_server_stop(sctx);
	m0_net_xprt_fini(g_xprt);
}

static void test_confc_local(void)
{
	struct m0_confc confc;
	const char     *local_conf;
	int             rc;

	rc = m0_conf_str(M0_CM_UT, &local_conf);
	M0_UT_ASSERT(rc == 0);

	rc = m0_confc_init(&confc, &g_grp,
			   &(const struct m0_buf)M0_BUF_INITS("prof"),
			   NULL, NULL, "bad configuration string");
	M0_UT_ASSERT(rc == -EPROTO);

	rc = m0_confc_init(&confc, &g_grp,
			   &(const struct m0_buf)M0_BUF_INITS("bad profile"),
			   NULL, NULL, local_conf);
	M0_UT_ASSERT(rc == -EBADF);

	_confc_test(NULL, NULL, local_conf);
}

static void test_confc_net(void)
{
	struct m0_rpc_machine mach;  /* client's RPC machine */
	int rc;
#define NAME(ext) "ut_confd" ext
	char *argv[] = {
		NAME(""), "-r", "-p", "-T", "AD", "-D", NAME(".db"),
		"-S", NAME(".stob"), "-e", SERVER_ENDPOINT, "-s", "confd"
	};
	M0_RPC_SERVER_CTX_DEFINE(confd, &g_xprt, 1, argv, ARRAY_SIZE(argv),
				 NULL, 0, NAME(".log"));
#undef NAME

	rc = service_start(&confd);
	M0_UT_ASSERT(rc == 0);
	m0_confd_hack_mode = M0_CM_UT; /* XXX DELETEME */

	rc = m0_ut_rpc_machine_start(&mach, g_xprt, CLIENT_ENDPOINT_ADDR,
				     "ut_confc.db");
	M0_UT_ASSERT(rc == 0);

	_confc_test(SERVER_ENDPOINT_ADDR, &mach, NULL);

	m0_ut_rpc_machine_stop(&mach);
	service_stop(&confd);
}

/* ------------------------------------------------------------------ */

struct waiter {
	struct m0_confc_ctx w_ctx;
	struct m0_clink     w_clink;
};

static void waiter_init(struct waiter *w, struct m0_confc *confc);
static void waiter_fini(struct waiter *w);

static bool streq(const char *s1, const char *s2)
{
	return strcmp(s1, s2) == 0;
}

static void services_open(struct m0_conf_obj **result, struct m0_confc *confc)
{
	struct waiter w;
	int           rc;

	waiter_init(&w, confc);

	m0_confc_open(&w.w_ctx, NULL, M0_BUF_INITS("filesystem"),
		      M0_BUF_INITS("services"));

	while (!m0_confc_ctx_is_completed(&w.w_ctx))
		m0_chan_wait(&w.w_clink);

	rc = m0_confc_ctx_error(&w.w_ctx);
	M0_UT_ASSERT(rc == 0);

	*result = m0_confc_ctx_result(&w.w_ctx);
	M0_UT_ASSERT((*result)->co_status == M0_CS_READY);
	M0_UT_ASSERT((*result)->co_confc == confc);

	waiter_fini(&w);
}

static void synchronous_api_test(struct m0_conf_obj *dir)
{
	struct m0_conf_obj     *obj;
	struct m0_conf_service *svc;
	int                     rc;

	M0_PRE(dir->co_type == M0_CO_DIR);

	rc = m0_confc_open_sync(&obj, dir, M0_BUF_INITS("svc-0"));
	M0_UT_ASSERT(rc == 0);

	svc = M0_CONF_CAST(obj, m0_conf_service);
	M0_UT_ASSERT(svc->cs_obj.co_status == M0_CS_READY);
	M0_UT_ASSERT(svc->cs_obj.co_confc == dir->co_confc);
	M0_UT_ASSERT(m0_buf_streq(&svc->cs_obj.co_id, "svc-0"));
	M0_UT_ASSERT(svc->cs_type == 1);
	M0_UT_ASSERT(streq(svc->cs_endpoints[0], "addr0"));
	M0_UT_ASSERT(svc->cs_endpoints[1] == NULL);
	M0_UT_ASSERT(m0_buf_streq(&svc->cs_node->cn_obj.co_id, "node-0"));

	M0_UT_ASSERT(obj == &svc->cs_obj);
	rc = m0_confc_open_sync(&obj, obj, M0_BUF_INITS("node"));
	M0_UT_ASSERT(rc == 0);
	M0_UT_ASSERT(obj != &svc->cs_obj);
	M0_UT_ASSERT(obj == &svc->cs_node->cn_obj);

	m0_confc_close(&svc->cs_obj);
	m0_confc_close(obj);
}

static void _confc_test(const char *confd_addr, struct m0_rpc_machine *rpc_mach,
			const char *local_conf)
{
	struct m0_confc     confc;
	struct m0_conf_obj *svc_dir;
	int                 rc;

	rc = m0_confc_init(&confc, &g_grp,
			   &(const struct m0_buf)M0_BUF_INITS("prof"),
			   confd_addr, rpc_mach, local_conf);
	M0_UT_ASSERT(rc == 0);

	services_open(&svc_dir, &confc); /* tests asynchronous interface */
	synchronous_api_test(svc_dir);

#if 0 /* XXX <<<<<<< */
	{ /* Directories */
		struct m0_conf_obj *dir;

		rc = m0_confc_open_sync(&dir, confc.cc_root,
					M0_BUF_INITS("filesystem"),
					M0_BUF_INITS("services"));
		M0_UT_ASSERT(rc == 0);

		g_counter = 0;
		rc = dir_entries_use(dir, svc_type_add, NULL);
		M0_UT_ASSERT(rc == 0);
		M0_UT_ASSERT(g_counter = 6);

		g_counter = 0;
		rc = dir_entries_use(dir, svc_type_add, is_mgmt);
		M0_UT_ASSERT(rc == 0);
		M0_UT_ASSERT(g_counter = 4);

		m0_confc_close(dir);
	}
#endif /* XXX >>>>>>> */
	m0_confc_close(svc_dir);
	m0_confc_fini(&confc);
}

#if 0 /* XXX <<<<<<< */
static uint8_t g_counter;

static void svc_type_add(const struct m0_conf_obj *obj)
{
	g_counter += M0_CONF_CAST(obj, m0_conf_service)->cs_type;
}

static bool is_mgmt(const struct m0_conf_obj *obj)
{
	return M0_CONF_CAST(obj, m0_conf_service)->cs_type ==
		M0_CFG_SERVICE_MGMT;
}

M0_BASSERT(M0_CONF_DIREND == 0);
static int dir_entries_use(struct m0_conf_obj *dir,
			   void (*use)(const struct m0_conf_obj *),
			   bool (*stop_at)(const struct m0_conf_obj *))
{
	struct sm_waiter    w;
	int                 rc;
	struct m0_conf_obj *entry = NULL;

	sm_waiter_init(&w, dir->co_confc);

	while ((rc = m0_confc_readdir(&w.w_ctx, dir, &entry)) > 0) {
		if (rc == M0_CONF_DIRNEXT) {
			/* The entry is available immediately. */
			M0_ASSERT(entry != NULL);
			use(entry);
			continue; /* Note, that `entry' will be closed
				   * by m0_confc_readdir(). */
		}

		/* Cache miss. */
		M0_ASSERT(rc == M0_CONF_DIRMISS);
		while (!m0_confc_ctx_is_completed(&w.w_ctx))
			m0_chan_wait(&w.w_clink);

		rc = m0_confc_ctx_error(&w.w_ctx);
		if (rc != 0)
			break; /* error */

		entry = m0_confc_ctx_result(&w.w_ctx);
		if (entry == NULL)
			break; /* end of directory */

		use(entry);
		if (stop_at != NULL && stop_at(entry))
			break;

		/* Re-initialise m0_confc_ctx. */
		sm_waiter_fini(&w);
		sm_waiter_init(&w);
	}

	m0_confc_close(entry);
	sm_waiter_fini(&w);
	return rc;
}
#endif /* XXX >>>>>>> */

static struct {
	bool             run;
	struct m0_thread thread;
} g_ast;

static void ast_thread(int _ __attribute__((unused)))
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
	M0_ASSERT(M0_THREAD_INIT(&g_ast.thread, int, NULL, &ast_thread, 0,
				 "ast_thread") == 0);
	return 0;
}

static int ast_thread_fini(void)
{
	g_ast.run = false;
	m0_clink_signal(&g_grp.s_clink);
	m0_thread_join(&g_ast.thread);
	m0_sm_group_fini(&g_grp);

	return 0;
}

/* Filters out intermediate state transitions of m0_confc_ctx::fc_mach. */
static bool filter(struct m0_clink *link)
{
	return !m0_confc_ctx_is_completed(&container_of(link, struct waiter,
							w_clink)->w_ctx);
}

static void waiter_init(struct waiter *w, struct m0_confc *confc)
{
	m0_confc_ctx_init(&w->w_ctx, confc);
	m0_clink_init(&w->w_clink, filter);
	m0_clink_add(&w->w_ctx.fc_mach.sm_chan, &w->w_clink);
}

static void waiter_fini(struct waiter *w)
{
	m0_clink_del(&w->w_clink);
	m0_clink_fini(&w->w_clink);
	m0_confc_ctx_fini(&w->w_ctx);
}

const struct m0_test_suite confc_ut = {
	.ts_name  = "confc-ut",
	.ts_init  = ast_thread_init,
	.ts_fini  = ast_thread_fini,
	.ts_tests = {
		{ "confc-local", test_confc_local },
		{ "confc-net",   test_confc_net },
		{ NULL, NULL }
	}
};
