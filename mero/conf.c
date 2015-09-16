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
#include "mero/setup_internal.h"  /* cs_ad_stob_create */
#include "rpc/rpclib.h"           /* m0_rpc_client_ctx */
#include "conf/obj.h"             /* m0_conf_filesystem */
#include "conf/confc.h"           /* m0_confc */
#include "conf/schema.h"          /* m0_conf_service_type */
#include "conf/obj_ops.h"         /* M0_CONF_DIRNEXT */
#include "conf/diter.h"           /* m0_conf_diter_init */
#include "conf/helpers.h"         /* m0_conf_fs_get */
#include "reqh/reqh_service.h"    /* m0_reqh_service_ctx */
#include "stob/linux.h"           /* m0_stob_linux_reopen */
#include "ioservice/storage_dev.h" /* m0_storage_dev_attach_by_conf */

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
		[M0_CST_MDS]     = "-G",
		[M0_CST_IOS]     = "-i",
		[M0_CST_STS]     = "-R",
		[M0_CST_HA]      = "",
		[M0_CST_SSS]     = "",
		[M0_CST_SNS_REP] = "",
		[M0_CST_SNS_REB] = "",
		[M0_CST_ADDB2]   = "",
		[M0_CST_DS1]     = "",
		[M0_CST_DS2]     = ""
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
/**
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

	while ((rc = m0_conf_diter_next_sync(&it, service_and_node)) ==
		M0_CONF_DIRNEXT) {
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
M0_INTERNAL int cs_conf_to_args(struct cs_args *args,
				struct m0_conf_filesystem *fs)
{
	M0_ENTRY();
	M0_PRE(args != NULL && fs != NULL);

	return conf_to_args(args, fs);
}

static bool is_local_service(const struct m0_conf_obj *obj)
{
	const char                   *lep;
	struct m0_mero               *cctx;
	const struct m0_conf_service *svc;
	const struct m0_conf_process *p;

	if (m0_conf_obj_type(obj) != &M0_CONF_SERVICE_TYPE)
		return false;
	svc = M0_CONF_CAST(obj, m0_conf_service);
	p = M0_CONF_CAST(svc->cs_obj.co_parent->co_parent,
			 m0_conf_process);
	cctx = m0_cs_ctx_get(m0_conf_obj2reqh(obj));
	lep = m0_rpc_machine_ep(m0_mero_to_rmach(cctx));
	M0_LOG(M0_DEBUG, "lep: %s svc ep: %s type:%d process:"FID_F"service:"
			 FID_F, lep,
			 p->pc_endpoint, svc->cs_type,
			 FID_P(&p->pc_obj.co_id),
			 FID_P(&svc->cs_obj.co_id));
	return m0_streq(lep, p->pc_endpoint);
}

static bool is_local_ios(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_SERVICE_TYPE &&
	       M0_CONF_CAST(obj, m0_conf_service)->cs_type == M0_CST_IOS &&
	       is_local_service(obj);
}

static bool is_device(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_SDEV_TYPE;
}

static int cs_conf_device_storage_init(struct m0_storage_devs *devs,
				       struct m0_confc        *confc,
				       struct m0_conf_service *svc)
{
	int                  rc;
	struct m0_conf_diter dev_it;

	M0_ENTRY();
	M0_LOG(M0_DEBUG, "service FID:"FID_F" ep:%s", FID_P(&svc->cs_obj.co_id),
			svc->cs_endpoints[0]);
	rc = m0_conf_diter_init(&dev_it, confc, &svc->cs_obj,
				M0_CONF_SERVICE_SDEVS_FID);
	while ((rc = m0_conf_diter_next_sync(&dev_it, is_device)) ==
		M0_CONF_DIRNEXT) {
		struct m0_conf_obj  *obj = m0_conf_diter_result(&dev_it);
		struct m0_conf_sdev *sdev= M0_CONF_CAST(obj, m0_conf_sdev);

		M0_LOG(M0_DEBUG, "sdev size:%ld path:%s FID:"FID_F,
				sdev->sd_size, sdev->sd_filename,
				FID_P(&sdev->sd_obj.co_id));
		rc = m0_storage_dev_attach_by_conf(devs, sdev);
		if (rc != 0)
			break;
	}
	m0_conf_diter_fini(&dev_it);
	return M0_RC(rc);
}

M0_INTERNAL int cs_conf_storage_init(struct cs_stobs        *stob,
				     struct m0_storage_devs *devs)
{
	int                        rc;
	struct m0_conf_diter       it;
	struct m0_conf_filesystem *fs;
	struct m0_mero            *cctx;
	struct m0_reqh_context    *rctx;
	struct m0_confc           *confc;

	M0_ENTRY();

	rctx = container_of(stob, struct m0_reqh_context, rc_stob);
	cctx = container_of(rctx, struct m0_mero, cc_reqh_ctx);
	M0_ASSERT(m0_strcaseeq(rctx->rc_stype, m0_cs_stypes[M0_AD_STOB]));
	confc = m0_mero2confc(cctx);
	rc = m0_conf_fs_get(&rctx->rc_reqh.rh_profile, confc, &fs);
	if (rc != 0)
		return M0_ERR_INFO(rc, "conf fs fail");;
	rc = m0_conf_diter_init(&it, confc, &fs->cf_obj,
				M0_CONF_FILESYSTEM_NODES_FID,
				M0_CONF_NODE_PROCESSES_FID,
				M0_CONF_PROCESS_SERVICES_FID);
	while ((rc = m0_conf_diter_next_sync(&it, is_local_ios)) ==
		M0_CONF_DIRNEXT) {
		struct m0_conf_obj     *obj = m0_conf_diter_result(&it);
		struct m0_conf_service *svc = M0_CONF_CAST(obj,
							   m0_conf_service);
		rc = cs_conf_device_storage_init(devs, confc, svc);
		if (rc != 0)
			break;
	}
	m0_conf_diter_fini(&it);
	m0_confc_close(&fs->cf_obj);
	return M0_RC(rc);
}

M0_INTERNAL int cs_conf_services_init(struct m0_mero *cctx)
{
	int                        rc;
	struct m0_conf_diter       it;
	struct m0_conf_filesystem *fs;
	struct m0_reqh_context    *rctx;
	struct m0_confc           *confc;

	M0_ENTRY();

	rctx = &cctx->cc_reqh_ctx;
	rctx->rc_nr_services = 0;
	confc = m0_mero2confc(cctx);
	rc = m0_conf_fs_get(&rctx->rc_reqh.rh_profile, confc, &fs);
	if (rc != 0)
		return M0_ERR_INFO(rc, "conf fs fail");;
	rc = m0_conf_diter_init(&it, confc, &fs->cf_obj,
				M0_CONF_FILESYSTEM_NODES_FID,
				M0_CONF_NODE_PROCESSES_FID,
				M0_CONF_PROCESS_SERVICES_FID);
	while ((rc = m0_conf_diter_next_sync(&it, is_local_service)) ==
		M0_CONF_DIRNEXT) {
		struct m0_conf_obj     *obj = m0_conf_diter_result(&it);
		struct m0_conf_service *svc = M0_CONF_CAST(obj,
							   m0_conf_service);
		M0_ASSERT(rctx->rc_nr_services < rctx->rc_max_services);
		/** @todo Check only one service of each service type is present
			  per endpoint in the configuration.
		    M0_ASSERT(rctx->rc_services[svc->cs_type] == NULL);
		*/
		rctx->rc_services[svc->cs_type] = m0_conf_service_name_dup(svc);
		if (rctx->rc_services[svc->cs_type] == NULL) {
			int i;
			rc = -ENOMEM;
			for (i = 0; i < rctx->rc_nr_services; ++i)
				m0_free(rctx->rc_services[i]);
			break;
		}
		rctx->rc_service_fids[svc->cs_type] = svc->cs_obj.co_id;
		M0_LOG(M0_DEBUG, "service:%s fid:" FID_F,
		       rctx->rc_services[svc->cs_type],
		       FID_P(&rctx->rc_service_fids[svc->cs_type]));
		M0_CNT_INC(rctx->rc_nr_services);
	}
	m0_conf_diter_fini(&it);
	m0_confc_close(&fs->cf_obj);
	return M0_RC(rc);
}

M0_INTERNAL int cs_conf_device_reopen(struct cs_stobs *stob, uint32_t dev_id)
{
	struct m0_mero         *cctx;
	struct m0_reqh_context *rctx;
	struct m0_confc        *confc;
	int                     rc;
	struct m0_fid           fid;
	struct m0_conf_sdev    *sdev;
	struct m0_stob_id       stob_id;

	M0_ENTRY();
	rctx = container_of(stob, struct m0_reqh_context, rc_stob);
	cctx = container_of(rctx, struct m0_mero, cc_reqh_ctx);
	confc = m0_mero2confc(cctx);
	fid = M0_FID_TINIT(M0_CONF_SDEV_TYPE.cot_ftype.ft_id, 1, dev_id);
	rc = m0_conf_device_get(confc, &fid, &sdev);
	if (rc == 0) {
		struct m0_conf_service *svc = M0_CONF_CAST(
					sdev->sd_obj.co_parent->co_parent,
					m0_conf_service);
		if (is_local_ios(&svc->cs_obj)) {
			M0_LOG(M0_DEBUG, "sdev size:%ld path:%s FID:"FID_F,
					sdev->sd_size, sdev->sd_filename,
					FID_P(&sdev->sd_obj.co_id));
			m0_stob_id_make(0, dev_id, &stob->s_sdom->sd_id,
					&stob_id);
			rc = m0_stob_linux_reopen(&stob_id, sdev->sd_filename);
		}
	}
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
