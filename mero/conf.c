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
#include "lib/locality.h"         /* m0_locality0_get */
#include "mero/setup.h"           /* cs_args */
#include "rpc/rpclib.h"           /* m0_rpc_client_ctx */
#include "conf/obj.h"             /* m0_conf_filesystem */
#include "conf/confc.h"           /* m0_confc */
#include "conf/schema.h"          /* m0_conf_service_type */
#include "conf/obj_ops.h"         /* M0_CONF_DIRNEXT */
#include "conf/helpers.h"         /* m0_conf_fs_get */
#include "conf/dir_iter.h"        /* m0_conf_diter_init */

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

M0_INTERNAL int m0_mero_conf_setup(struct m0_mero *mero, const char *local_conf,
				   const struct m0_fid *profile)
{
	struct m0_rpc_machine *rmach;
	struct m0_locality    *loc = m0_locality0_get();
	struct m0_confc       *confc = &mero->cc_confc;
	struct m0_conf_obj    *fs_obj;
	int                    rc;

	M0_ENTRY();

	M0_PRE((local_conf != NULL && m0_fid_is_set(profile)) ||
	       (mero->cc_profile != NULL && mero->cc_confd_addr != NULL));

	if (local_conf == NULL) {
		rmach = m0_mero_to_rmach(mero);
		rc = m0_conf_fs_get(mero->cc_profile, mero->cc_confd_addr,
				    rmach, loc->lo_grp, confc, &mero->cc_fs);
		if (rc != 0)
			goto out;
	} else {
		rc = m0_confc_init(confc, loc->lo_grp, NULL, NULL,
				   local_conf);
		if (rc != 0)
			goto out;
		rc = m0_confc_open_sync(&fs_obj, confc->cc_root,
					M0_CONF_ROOT_PROFILES_FID, *profile,
					M0_CONF_PROFILE_FILESYSTEM_FID);
		if (rc != 0) {
			m0_confc_fini(confc);
			goto out;
		}
		mero->cc_fs = M0_CONF_CAST(fs_obj, m0_conf_filesystem);
	}

	rc = m0_pools_common_init(&mero->cc_pools_common, NULL, mero->cc_fs);
	if (rc != 0)
		goto cleanup;
        rc = m0_pools_setup(&mero->cc_pools_common, mero->cc_fs, NULL, NULL, NULL);
        if (rc == 0)
		goto out;

	m0_pools_common_fini(&mero->cc_pools_common);
cleanup:
	m0_confc_close(&mero->cc_fs->cf_obj);
	m0_confc_fini(&mero->cc_confc);
	mero->cc_fs = NULL;
out:
	return M0_RC(rc);
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
