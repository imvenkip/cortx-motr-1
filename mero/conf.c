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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 07-Dec-2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_M0D
#include "lib/trace.h"

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/string.h"           /* m0_strdup */
#include "mero/setup.h"           /* cs_args */
#include "rpc/rpclib.h"           /* m0_rpc_client_ctx */
#include "conf/obj.h"             /* m0_conf_filesystem */
#include "conf/confc.h"           /* m0_confc */
#include "conf/schema.h"          /* m0_conf_service_type */

/* ----------------------------------------------------------------
 * Mero options
 * ---------------------------------------------------------------- */

static struct m0_sm_group g_grp;

static void ast_thread_init(void);
static void ast_thread_fini(void);

/* Note: `s' is believed to be heap-allocated. */
static void option_add(struct cs_args *args, char *s)
{
	M0_PRE(0 <= args->ca_argc && args->ca_argc < ARRAY_SIZE(args->ca_argv));
	args->ca_argv[args->ca_argc++] = s;
	M0_LOG(M0_DEBUG, "%02d %s", args->ca_argc, s);
}

static void
fs_options_add(struct cs_args *args, const struct m0_conf_filesystem *fs)
{
	int i;
	for (i = 0; fs->cf_params[i] != NULL; ++i)
		option_add(args, m0_strdup(fs->cf_params[i]));
}

static const char *service_name[] = {
	[0]          = NULL,/* unused, enum declarations start from 1 */
	[M0_CST_MDS] = "mdservice",  /* Meta-data service. */
	[M0_CST_IOS] = "ioservice",  /* IO/data service. */
	[M0_CST_MGS] = "confd",      /* Management service (confd). */
	[M0_CST_RMS] = "rmservice",  /* RM service. */
	[M0_CST_SS]  = "stats",      /* Stats service */
	[M0_CST_HA]  = "haservice"   /* HA service */
};

M0_UNUSED static char *
service_name_dup(const struct m0_conf_service *svc)
{
	M0_ASSERT(svc->cs_type > 0 && svc->cs_type < ARRAY_SIZE(service_name));
	return m0_strdup(service_name[svc->cs_type]);
}

static char *
strxdup(const char *addr)
{
	static const char  xpt[] = "lnet:";
	char		  *s;

	s = m0_alloc(strlen(addr) + sizeof(xpt));
	if (s != NULL)
		sprintf(s, "%s%s", xpt, addr);

	return s;
}

static void
service_options_add(struct cs_args *args, const struct m0_conf_service *svc)
{
	static const char *opts[] = {
		[M0_CST_MDS] = "-G",
		[M0_CST_IOS] = "-i",
		[M0_CST_RMS] = "",
		[M0_CST_SS]  = "-R"
	};
	int         i;
	const char *opt;

	if (svc->cs_endpoints == NULL)
		return;

	for (i = 0; svc->cs_endpoints[i] != NULL; ++i) {
		if (!IS_IN_ARRAY(svc->cs_type, opts)) {
			M0_LOG(M0_ERROR, "invalid service type %d, ignoring",
			       svc->cs_type);
			break;
		}
		opt = opts[svc->cs_type];
		M0_ASSERT(opt != NULL);
		if (*opt == '\0')
			continue;
		option_add(args, m0_strdup(opt));
		option_add(args, strxdup(svc->cs_endpoints[i]));
	}
}

M0_UNUSED static void
node_options_add(struct cs_args *args, const struct m0_conf_node *node)
{
	char buf[64] = {0};

	option_add(args, m0_strdup("-m"));
	(void)snprintf(buf, ARRAY_SIZE(buf) - 1, "%u", node->cn_memsize);
	option_add(args, m0_strdup(buf));

	option_add(args, m0_strdup("-q"));
	(void)snprintf(buf, ARRAY_SIZE(buf) - 1, "%lu", node->cn_flags);
	option_add(args, m0_strdup(buf));
}

/** Uses confc API to generate CLI arguments. */
static int conf_to_args(struct cs_args *dest, const char *confd_addr,
			const char *profile, struct m0_rpc_machine *rpc_mach)
{
	struct m0_confc     confc;
	struct m0_conf_obj *fs;
	struct m0_conf_obj *svc_dir;
	struct m0_conf_obj *svc;
	struct m0_fid       prof_fid;
	int                 rc;

	M0_ENTRY();

	rc = m0_fid_sscanf(profile, &prof_fid);
	if (rc != 0) {
		M0_LOG(M0_FATAL, "Cannot parse profile `%s'", profile);
		goto profile_err;
	}

	ast_thread_init();
	rc = m0_confc_init(&confc, &g_grp, &prof_fid,
			   confd_addr, rpc_mach, NULL);
	if (rc != 0)
		goto end;

	option_add(dest, m0_strdup("lt-m0d")); /* XXX Does the value matter? */

	M0_LOG(M0_DEBUG, "fs_fid: "FID_F,
	       FID_P(&M0_CONF_PROFILE_FILESYSTEM_FID));
	rc = m0_confc_open_sync(&fs, confc.cc_root,
				M0_CONF_PROFILE_FILESYSTEM_FID);
	if (rc != 0)
		goto confc_fini;
	fs_options_add(dest, M0_CONF_CAST(fs, m0_conf_filesystem));

	rc = m0_confc_open_sync(&svc_dir, fs, M0_CONF_FILESYSTEM_SERVICES_FID);
	if (rc != 0)
		goto fs_close;

	for (svc = NULL; (rc = m0_confc_readdir_sync(svc_dir, &svc)) > 0; ) {

		service_options_add(dest, M0_CONF_CAST(svc, m0_conf_service));

#if 0
		/*
		 * XXX FIXME: Options of a particular node should be
		 * added only once.
		 *
		 * Several services may be hosted on the same node. We
		 * should take this fact into consideration when
		 * adding node options (currently we are ignoring it).
		 */
		rc = m0_confc_open_sync(&node, svc, M0_CONF_SERVICE_NODE_FID);
		if (rc != 0) {
			M0_LOG(M0_ERROR,
			       "Unable to obtain configuration of a node");
			break;
		}
		node_options_add(dest, M0_CONF_CAST(node, m0_conf_node));
		m0_confc_close(node);
#endif
	}

	m0_confc_close(svc);
	m0_confc_close(svc_dir);
fs_close:
	m0_confc_close(fs);
confc_fini:
	m0_confc_fini(&confc);
end:
	ast_thread_fini();
profile_err:
	return M0_RC(rc);
}

/*
 * Establishes network connection with confd, fills CLI arguments (`args'),
 * disconnects from confd.
 */
M0_INTERNAL int cs_conf_to_args(struct cs_args *args, const char *confd_addr,
				const char *profile, const char *local_addr,
				unsigned timeout, unsigned retry)
{
	enum { MAX_RPCS_IN_FLIGHT = 32 };
	static struct m0_net_domain      client_net_dom;
	static struct m0_rpc_client_ctx  cctx;
	static char                      client_ep[M0_NET_LNET_XEP_ADDR_LEN];
	static char                      server_ep[M0_NET_LNET_XEP_ADDR_LEN];
	int                              rc;
	unsigned                         i;

	M0_ENTRY();
	M0_PRE(confd_addr != NULL && profile != NULL);

	M0_LOG(M0_DEBUG, "confd_addr=%s profile=%s", confd_addr, profile);

	cctx.rcx_net_dom               = &client_net_dom;
	cctx.rcx_local_addr            = client_ep;
	cctx.rcx_remote_addr           = server_ep;
	cctx.rcx_max_rpcs_in_flight    = MAX_RPCS_IN_FLIGHT;
	cctx.rcx_recv_queue_min_length = M0_NET_TM_RECV_QUEUE_DEF_LEN;
	cctx.rcx_max_rpc_msg_size      = M0_RPC_DEF_MAX_RPC_MSG_SIZE;

	strcpy(server_ep, confd_addr);
	strcpy(client_ep, local_addr);
	{
		char *se = strrchr(client_ep, ':');
		if (se == NULL)
			return M0_ERR(-EINVAL, "Invalid value of confd_addr:"
				      " `%s'", confd_addr);
		*(++se) = '*';
		*(++se) = 0;
	}

	rc = m0_net_domain_init(&client_net_dom, &m0_net_lnet_xprt,
				&m0_addb_proc_ctx);
	if (rc != 0)
		return M0_RC(rc);
	/*
	 * confd service should be started before other services.
	 * Following loop checks for availability of confd service, retrying
	 * for 10 minutes. If for 10 minutes, it cannot connect to confd,
	 * it exits.
	 *
	 * XXX FIXME: This solution is a workaround, introduced during AAA, and
	 * is not the right way to handle the dependency of services on confd.
	 */
	for (i = 0; i < retry; ++i) {
		rc = m0_rpc_client_start(&cctx);
		if (rc == -EHOSTUNREACH)
			m0_nanosleep(m0_time_from_now(timeout, 0), NULL);
		else
			break;
	}
	if (rc != 0)
		goto net_dom;

	rc = conf_to_args(args, server_ep, profile, &cctx.rcx_rpc_machine);
	if (rc == 0)
		rc = m0_rpc_client_stop(&cctx);
	else
		(void)m0_rpc_client_stop(&cctx);
net_dom:
	m0_net_domain_fini(&client_net_dom);
	return M0_RC(rc);
}

/* ----------------------------------------------------------------
 * AST thread
 * ---------------------------------------------------------------- */

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

static void ast_thread_init(void)
{
	m0_sm_group_init(&g_grp);
	g_ast.run = true;
	M0_ASSERT(M0_THREAD_INIT(&g_ast.thread, int, NULL, &ast_thread, 0,
				 "ast_thread") == 0);
}

static void ast_thread_fini(void)
{
	g_ast.run = false;
	m0_clink_signal(&g_grp.s_clink);
	m0_thread_join(&g_ast.thread);
	m0_sm_group_fini(&g_grp);
}

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
