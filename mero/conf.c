/* -*- C -*- */
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
 * Original author: Anatoliy Bilenko <anatoliy_bilenko@xyratex.com>
 * Original creation date: 07-Dec-2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_M0D
#include "lib/trace.h"

#include "mero/setup.h"           /* cs_args */
#include "rpc/rpclib.h"           /* m0_rpc_client_ctx */
#include "conf/obj.h"             /* m0_conf_filesystem */
#include "conf/buf_ext.h"         /* m0_buf_strdup */
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
}

static void
fs_options_add(struct cs_args *args, const struct m0_conf_filesystem *fs)
{
	int i;
	for (i = 0; fs->cf_params[i] != NULL; ++i)
		option_add(args, strdup(fs->cf_params[i]));
}

static char *service_name_dup(const struct m0_conf_service *svc)
{
	static const char *service_name[] = {
		[0]          = NULL,/* unused, enum declarations start from 1 */
		[M0_CST_MDS] = "mdservice",  /* Meta-data service. */
		[M0_CST_IOS] = "ioservice",  /* IO/data service. */
		[M0_CST_MGS] = "confd",      /* Management service (confd). */
		[M0_CST_DLM] = "dlm"         /* DLM service. */
	};

	M0_ASSERT(svc->cs_type > 0 && svc->cs_type < ARRAY_SIZE(service_name));
	return strdup(service_name[svc->cs_type]);
}

static void
service_options_add(struct cs_args *args, const struct m0_conf_service *svc)
{
	int   i;
	char *id = service_name_dup(svc);

	M0_ASSERT(id != NULL); /* XXX TODO: error checking */
	for (i = 0; svc->cs_endpoints != NULL && svc->cs_endpoints[i] != NULL;
	     ++i) {
		option_add(args, strdup("-e"));
		option_add(args, strdup(svc->cs_endpoints[i]));
	}

	option_add(args, strdup("-s"));
	option_add(args, id);
}

static void
node_options_add(struct cs_args *args, const struct m0_conf_node *node)
{
	char buf[64] = {0};

	option_add(args, strdup("-m"));
	(void)snprintf(buf, ARRAY_SIZE(buf) - 1, "%u", node->cn_memsize);
	option_add(args, strdup(buf));

	option_add(args, strdup("-q"));
	(void)snprintf(buf, ARRAY_SIZE(buf) - 1, "%lu", node->cn_flags);
	option_add(args, strdup(buf));
}

/** Uses confc API to generate CLI arguments. */
static int conf_to_args(struct cs_args *dest, const char *confd_addr,
			const char *profile, struct m0_rpc_machine *rpc_mach)
{
	struct m0_confc     confc;
	struct m0_conf_obj *fs;
	struct m0_conf_obj *svc_dir;
	struct m0_conf_obj *svc;
	int                 rc;

	M0_ENTRY();

	ast_thread_init();
	rc = m0_confc_init(&confc, &g_grp,
			   &(const struct m0_buf)M0_BUF_INITS((char *)profile),
			   confd_addr, rpc_mach, NULL);
	if (rc != 0)
		goto end;

	option_add(dest, strdup("lt-m0d")); /* XXX Does the value matter? */

	rc = m0_confc_open_sync(&fs, confc.cc_root, M0_BUF_INITS("filesystem"));
	if (rc != 0)
		goto confc_fini;
	fs_options_add(dest, M0_CONF_CAST(fs, m0_conf_filesystem));

	rc = m0_confc_open_sync(&svc_dir, fs, M0_BUF_INITS("services"));
	if (rc != 0)
		goto fs_close;

	for (svc = NULL; (rc = m0_confc_readdir_sync(svc_dir, &svc)) > 0; ) {
		struct m0_conf_obj *node;

		service_options_add(dest, M0_CONF_CAST(svc, m0_conf_service));

		rc = m0_confc_open_sync(&node, svc, M0_BUF_INITS("node"));
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
	M0_RETURN(rc);
}

/**
 * Establishes network connection with confd, fills CLI arguments (`args'),
 * disconnects from confd.
 */
M0_INTERNAL int cs_conf_to_args(struct cs_args *args, const char *confd_addr,
				const char *profile)
{
	enum {
		MAX_RPCS_IN_FLIGHT = 32,
		CLIENT_COB_DOM_ID  = 13,
		CONNECT_TIMEOUT    = 20,
		NR_SLOTS           = 1
	};
	static struct m0_net_domain     client_net_dom;
	static struct m0_dbenv          client_dbenv;
	static struct m0_cob_domain     client_cob_dom;
	static struct m0_rpc_client_ctx cctx;
	static char                     client_ep[M0_NET_LNET_XEP_ADDR_LEN];
	static char                     server_ep[M0_NET_LNET_XEP_ADDR_LEN];
	static const char              *client_db_file_name = "mero_client.db";
	static struct m0_net_xprt      *xprt = &m0_net_lnet_xprt;
	int                             rc;

	M0_ENTRY();
	M0_PRE(confd_addr != NULL && profile != NULL);

	M0_LOG(M0_DEBUG, "confd_addr=%s profile=%s", confd_addr, profile);

	cctx.rcx_net_dom               = &client_net_dom;
	cctx.rcx_local_addr            = client_ep;
	cctx.rcx_remote_addr           = server_ep;
	cctx.rcx_db_name               = client_db_file_name;
	cctx.rcx_dbenv                 = &client_dbenv;
	cctx.rcx_cob_dom_id            = CLIENT_COB_DOM_ID;
	cctx.rcx_cob_dom               = &client_cob_dom;
	cctx.rcx_nr_slots              = NR_SLOTS;
	cctx.rcx_timeout_s             = CONNECT_TIMEOUT;
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
		M0_RETURN(rc);

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
	M0_RETURN(rc);
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
