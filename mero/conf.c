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
#include "conf/obj_ops.h"         /* M0_CONF_DIRNEXT */
#include "conf/diter.h"           /* m0_conf_diter_init */
#include "reqh/reqh_service.h"    /* m0_reqh_service_ctx */

/* ----------------------------------------------------------------
 * Mero options
 * ---------------------------------------------------------------- */

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
		[M0_CST_STS] = "-R",
		[M0_CST_HA]  = "",
		[M0_CST_SSS]  = ""
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
		if (opt == NULL)
			continue;
		option_add(args, m0_strdup(opt));
		option_add(args, strxdup(svc->cs_endpoints[i]));
	}
}

M0_UNUSED static void
node_options_add(struct cs_args *args, const struct m0_conf_node *node)
{
/*
 * @todo Node parameters cn_memsize and cn_flags options are not used currently.
 * Options '-m' and '-q' options are used for maximum RPC message size and
 * minimum length of TM receive queue.
 * If required, change the option names accordingly.
 */
/*
	char buf[64] = {0};

	option_add(args, m0_strdup("-m"));
	(void)snprintf(buf, ARRAY_SIZE(buf) - 1, "%u", node->cn_memsize);
	option_add(args, m0_strdup(buf));

	option_add(args, m0_strdup("-q"));
	(void)snprintf(buf, ARRAY_SIZE(buf) - 1, "%lu", node->cn_flags);
	option_add(args, m0_strdup(buf));
*/
}

static bool service_and_node(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_SERVICE_TYPE ||
	       m0_conf_obj_type(obj) == &M0_CONF_NODE_TYPE;
}

/** Uses confc API to generate CLI arguments. */
static int conf_to_args(struct cs_args *dest, struct m0_conf_filesystem *fs)
{
	struct m0_confc      *confc;
	struct m0_conf_diter  it;
	int                   rc;

	M0_ENTRY();

	confc = m0_confc_from_obj(&fs->cf_obj);
	M0_ASSERT(confc != NULL);

	option_add(dest, m0_strdup("lt-m0d")); /* XXX Does the value matter? */
	fs_options_add(dest, M0_CONF_CAST(fs, m0_conf_filesystem));

	rc = m0_conf_diter_init(&it, confc, &fs->cf_obj,
				M0_CONF_FILESYSTEM_NODES_FID,
				M0_CONF_NODE_PROCESSES_FID,
				M0_CONF_PROCESS_SERVICES_FID);
	if (rc != 0)
		goto cleanup;

	while ((rc = m0_conf_diter_next_sync(&it, service_and_node)) == M0_CONF_DIRNEXT) {
		struct m0_conf_obj *obj = m0_conf_diter_result(&it);
		if (m0_conf_obj_type(obj) == &M0_CONF_SERVICE_TYPE) {
			struct m0_conf_service *svc =
				M0_CONF_CAST(obj, m0_conf_service);
			service_options_add(dest, svc);
		} else if(m0_conf_obj_type(obj) == &M0_CONF_NODE_TYPE) {
			struct m0_conf_node *node =
				M0_CONF_CAST(obj, m0_conf_node);
			node_options_add(dest, node);
		}
	}

cleanup:
	m0_conf_diter_fini(&it);
	return M0_RC(rc);
}

/*
 * Read configuration from confd, fills CLI arguments (`args').
 */
M0_INTERNAL int cs_conf_to_args(struct cs_args *args, struct m0_conf_filesystem *fs)
{
	M0_ENTRY();
	M0_PRE(args != NULL && fs != NULL);

	return conf_to_args(args, fs);
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
