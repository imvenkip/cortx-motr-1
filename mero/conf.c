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

#include "lib/errno.h"
#include "lib/memory.h"
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_M0D
#include "lib/trace.h"

#include "lib/string.h"           /* m0_strdup */
#include "mero/setup.h"           /* cs_args */
#include "rpc/rpclib.h"           /* m0_rpc_client_ctx */
#include "conf/obj.h"             /* m0_conf_filesystem */
#include "conf/confc.h"           /* m0_confc */
#include "conf/schema.h"          /* m0_conf_service_type */
#include "mgmt/mgmt.h"            /* m0_mgmt_conf */

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
	[M0_CST_SS]  = "stats"       /* Stats service */
};

static char *service_name_dup(const struct m0_conf_service *svc)
{
	M0_ASSERT(svc->cs_type > 0 && svc->cs_type < ARRAY_SIZE(service_name));
	return m0_strdup(service_name[svc->cs_type]);
}

static void
service_options_add(struct cs_args *args, const struct m0_conf_service *svc)
{
	int   i;
	char *id = service_name_dup(svc);

	M0_ASSERT(id != NULL); /* XXX TODO: error checking */
	for (i = 0; svc->cs_endpoints != NULL && svc->cs_endpoints[i] != NULL;
	     ++i) {
		option_add(args, m0_strdup("-e"));
		option_add(args, m0_strdup(svc->cs_endpoints[i]));
	}

	option_add(args, m0_strdup("-s"));
	option_add(args, id);
}

static void
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

	rc = m0_confc_open_sync(&fs, confc.cc_root,
				M0_CONF_PROFILE_FILESYSTEM_FID);
	if (rc != 0)
		goto confc_fini;
	fs_options_add(dest, M0_CONF_CAST(fs, m0_conf_filesystem));

	rc = m0_confc_open_sync(&svc_dir, fs, M0_CONF_FILESYSTEM_SERVICES_FID);
	if (rc != 0)
		goto fs_close;

	for (svc = NULL; (rc = m0_confc_readdir_sync(svc_dir, &svc)) > 0; ) {
		struct m0_conf_obj *node;

		service_options_add(dest, M0_CONF_CAST(svc, m0_conf_service));

		rc = m0_confc_open_sync(&node, svc, M0_CONF_SERVICE_NODE_FID);
		if (rc != 0) {
			M0_LOG(M0_ERROR,
			       "Unable to obtain configuration of a node");
			break;
		}
		/*
		 * XXX FIXME: Options of a particular node should be
		 * added only once.
		 *
		 * Several services may be hosted on the same node. We
		 * should take this fact into consideration when
		 * adding node options (currently we are ignoring it).
		 */
		node_options_add(dest, M0_CONF_CAST(node, m0_conf_node));
		m0_confc_close(node);
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
				const char *profile)
{
	enum {
		MAX_RPCS_IN_FLIGHT = 32,
	};
	static struct m0_net_domain     client_net_dom;
	static struct m0_rpc_client_ctx cctx;
	static char                     client_ep[M0_NET_LNET_XEP_ADDR_LEN];
	static char                     server_ep[M0_NET_LNET_XEP_ADDR_LEN];
	static struct m0_net_xprt      *xprt = &m0_net_lnet_xprt;
	int                             rc;

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
	strcpy(client_ep, confd_addr);
	{
		char *se = strrchr(client_ep, ':');

		M0_ASSERT(se != NULL); /* XXX Not sure we can do this,
					* as the value of `confd_addr'
					* comes from user's input. */
		*(++se) = '*';
		*(++se) = 0;
	}

	rc = m0_net_xprt_init(xprt);
	if (rc != 0)
		return M0_RC(rc);

	rc = m0_net_domain_init(&client_net_dom, xprt, &m0_addb_proc_ctx);
	if (rc != 0)
		goto xprt;

	rc = m0_rpc_client_start(&cctx);
	if (rc != 0)
		goto net_dom;

	rc = conf_to_args(args, server_ep, profile, &cctx.rcx_rpc_machine);
	if (rc == 0)
		rc = m0_rpc_client_stop(&cctx);
	else
		(void)m0_rpc_client_stop(&cctx);
net_dom:
	m0_net_domain_fini(&client_net_dom);
xprt:
	m0_net_xprt_fini(xprt);
	return M0_RC(rc);
}

M0_INTERNAL int cs_genders_to_args(struct cs_args *args, const char *argv0,
				   const char *genders)
{
	struct m0_mgmt_conf            conf;
	struct m0_mgmt_node_conf       node;
	struct m0_mgmt_svc_conf       *svc;
	struct m0_mgmt_service_ep_conf sep;
	char                           nbuf[16];
	char                          *bp;
	size_t                         l;
	int                            i;
	int                            rc;

	M0_PRE(args != NULL && args->ca_argc == 0);
	rc = m0_mgmt_conf_init(&conf, genders);
	if (rc != 0)
		return rc;
	rc = m0_mgmt_node_get(&conf, NULL, &node);
	if (rc != 0)
		goto error;

	/* NB: allocation failures checked at end of block */
	option_add(args, m0_strdup(argv0));
	option_add(args, m0_strdup("-r"));
	l = strlen(node.mnc_m0d_ep) + strlen(m0_net_lnet_xprt.nx_name) + 2;
	bp = m0_alloc(l);
	if (bp != NULL)
		sprintf(bp, "%s:%s", m0_net_lnet_xprt.nx_name, node.mnc_m0d_ep);
	option_add(args, m0_strdup("-e"));
	option_add(args, bp);
	if (node.mnc_max_rpc_msg != 0) {
		option_add(args, m0_strdup("-m"));
		i = snprintf(nbuf, sizeof nbuf, "%lu", node.mnc_max_rpc_msg);
		if (i >= sizeof nbuf) {
			rc = -EINVAL;
			goto done;
		}
		option_add(args, m0_strdup(nbuf));
	}
	if (node.mnc_recvq_min_len != 0) {
		option_add(args, m0_strdup("-q"));
		i = snprintf(nbuf, sizeof nbuf, "%u", node.mnc_recvq_min_len);
		if (i >= sizeof nbuf) {
			rc = -EINVAL;
			goto done;
		}
		option_add(args, m0_strdup(nbuf));
	}

	rc = m0_mgmt_service_ep_get(&conf, service_name[M0_CST_MDS], &sep);
	if (rc == -ENOENT) {
		rc = 0;
	} else if (rc != 0) {
		goto done;
	} else {
		M0_ASSERT(sep.mse_ep_nr > 0);
		/** @todo use HA or something to determine correct instance */
		option_add(args, m0_strdup("-G"));
		l = strlen(sep.mse_ep[0]) +
		    strlen(m0_net_lnet_xprt.nx_name) + 2;
		bp = m0_alloc(l);
		if (bp != NULL)
			sprintf(bp, "%s:%s",
				m0_net_lnet_xprt.nx_name, sep.mse_ep[0]);
		option_add(args, bp);
		m0_mgmt_service_ep_free(&sep);
	}
	m0_tl_for(m0_mgmt_conf, &node.mnc_svc, svc) {
		option_add(args, m0_strdup("-s"));
		l = strlen(svc->msc_name) + strlen(svc->msc_uuid) + 2;
		bp = m0_alloc(l);
		if (bp != NULL)
			sprintf(bp, "%s:%s", svc->msc_name, svc->msc_uuid);
		option_add(args, bp);
		for (i = 0; i < svc->msc_argc; ++i)
			option_add(args, m0_strdup(svc->msc_argv[i]));
	} m0_tlist_endfor;
	/* detect any earlier memory allocation failures */
	for (i = 0; i < args->ca_argc && rc == 0; ++i) {
		if (args->ca_argv[i] == NULL)
			rc = -ENOMEM;
	}
done:
	m0_mgmt_node_free(&node);
error:
	m0_mgmt_conf_fini(&conf);
	return rc;
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
