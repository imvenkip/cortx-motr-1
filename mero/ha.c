/* -*- C -*- */
/*
 * COPYRIGHT 2016 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Maxim Medved <max.medved@seagate.com>
 * Original creation date: 9-Jun-2016
 */


/**
 * @addtogroup mero
 *
 * TODO find include for M0_CONF_SERVICE_TYPE
 * TODO add magics for mero_ha_handlers
 * TODO s/container_of/bob_of/g
 * TODO make a flag to forbid handler detach before stop()
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_HA
#include "lib/trace.h"

#include "mero/ha.h"

#include "lib/string.h"         /* m0_strdup */
#include "lib/memory.h"         /* m0_free */
#include "lib/assert.h"         /* M0_ASSERT */
#include "lib/errno.h"          /* EHOSTUNREACH */

#include "conf/helpers.h"       /* m0_conf_service_open */
#include "conf/schema.h"        /* M0_CST_MGS */
#include "conf/cache.h"         /* m0_conf_cache_lock */
#include "conf/obj_ops.h"       /* m0_conf_obj_put */
#include "conf/obj.h"           /* m0_conf_obj_type */
#include "conf/diter.h"         /* m0_conf_diter_init */
#include "conf/confc.h"         /* m0_confc_close */

#include "fid/fid.h"            /* m0_fid_is_set */
#include "module/instance.h"    /* m0_get */
#include "reqh/reqh.h"          /* m0_reqh2confc */
#include "ha/entrypoint_fops.h" /* m0_ha_entrypoint_rep */
#include "ha/note.h"            /* M0_NC_ONLINE */
#include "ha/link.h"            /* m0_ha_link_flush */


M0_INTERNAL void m0_mero_ha_cfg_make(struct m0_mero_ha_cfg *mha_cfg,
				     struct m0_reqh        *reqh,
				     struct m0_rpc_machine *rmach,
				     const char            *addr)
{
	M0_ENTRY("reqh=%p rmach=%p", reqh, rmach);
	M0_PRE(reqh != NULL);
	M0_PRE(rmach != NULL);
	*mha_cfg = (struct m0_mero_ha_cfg){
		.mhc_dispatcher_cfg = {
			.hdc_enable_note      = true,
			.hdc_enable_keepalive = true,
		},
		.mhc_addr           = addr,
		.mhc_rpc_machine    = rmach,
		.mhc_reqh           = reqh,
		.mhc_process_fid    = M0_FID_INIT(0, 0),
		.mhc_profile_fid    = M0_FID_INIT(0, 0),
	};
	M0_LEAVE();
}

static int mero_ha_entrypoint_rep_confd_fill(const struct m0_fid *profile,
					     struct m0_confc     *confc,
					     struct m0_fid       *confd_fid,
					     char               **confd_ep)
{
	struct m0_conf_service *confd_svc;
	int                     rc;

	if (!m0_fid_is_set(profile))
		return -EAGAIN;
	rc = m0_conf_service_open(confc, profile, NULL, M0_CST_MGS, &confd_svc);
	if (rc != 0)
		return M0_ERR(rc);

	/*
	 * This code is executed only for testing purposes if there is no real
	 * HA service in cluster. Assume that there is always only one confd
	 * server in such case.
	 */
	*confd_ep = m0_strdup(confd_svc->cs_endpoints[0]);
	*confd_fid = confd_svc->cs_obj.co_id;
	m0_conf_cache_lock(&confc->cc_cache);
	m0_conf_obj_put(&confd_svc->cs_obj);
	m0_conf_cache_unlock(&confc->cc_cache);
	return M0_RC(rc);
}

static bool mero_ha_online_service_filter(const struct m0_conf_obj *obj)
{
	return m0_conf_obj_type(obj) == &M0_CONF_SERVICE_TYPE &&
	       obj->co_ha_state == M0_NC_ONLINE;
}

static int mero_ha_entrypoint_rep_rm_fill(const struct m0_fid  *profile,
                                          struct m0_confc      *confc,
                                          struct m0_fid        *active_rm_fid,
                                          char                **active_rm_ep)
{
	struct m0_conf_filesystem *fs;
	struct m0_conf_obj        *obj;
	struct m0_conf_service    *s;
	struct m0_conf_diter       it;
	int                        rc;

	/*
	 * This code is executed only for testing purposes if there is no real
	 * HA service in cluster.
	 */
	rc = m0_conf_fs_get(profile, confc, &fs);
	if (rc != 0)
		return M0_ERR(rc);

	rc = m0_conf_diter_init(&it, confc, &fs->cf_obj,
				M0_CONF_FILESYSTEM_NODES_FID,
				M0_CONF_NODE_PROCESSES_FID,
				M0_CONF_PROCESS_SERVICES_FID);
	if (rc != 0) {
		m0_confc_close(&fs->cf_obj);
		return M0_ERR(rc);
	}

	*active_rm_ep = NULL;
	while ((rc = m0_conf_diter_next_sync(&it,
				    mero_ha_online_service_filter)) > 0) {
		obj = m0_conf_diter_result(&it);
		s = M0_CONF_CAST(obj, m0_conf_service);
		if (s->cs_type == M0_CST_RMS && m0_conf_service_is_top_rms(s)) {
			*active_rm_fid = s->cs_obj.co_id;
			*active_rm_ep = m0_strdup(s->cs_endpoints[0]);
			break;
		}
	}
	m0_conf_diter_fini(&it);
	m0_confc_close(&fs->cf_obj);
	if (*active_rm_ep == NULL)
		return M0_ERR(-EHOSTUNREACH);

	return M0_RC(0);
}

static void
mero_ha_entrypoint_request_cb(struct m0_ha                      *ha,
                              const struct m0_ha_entrypoint_req *req,
                              const struct m0_uint128           *req_id)
{
	struct m0_reqh  *reqh    = ha->h_cfg.hcf_reqh;
	struct m0_confc *confc   = m0_reqh2confc(reqh);
	struct m0_fid   *profile = &reqh->rh_profile;
	struct m0_fid                confd_fid;
	const char                  *confd_eps[2] = {NULL, NULL};
	char                        *confd_ep = NULL;
	struct m0_ha_entrypoint_rep  rep = {
		.hae_quorum = 1,
		.hae_confd_fids = {
			.af_count = 1,
			.af_elems = &confd_fid,
		},
		.hae_confd_eps = confd_eps,
	};

	rep.hae_rc = mero_ha_entrypoint_rep_confd_fill(profile, confc,
	                                               &confd_fid, &confd_ep) ?:
		     mero_ha_entrypoint_rep_rm_fill(profile, confc,
		                                    &rep.hae_active_rm_fid,
		                                    &rep.hae_active_rm_ep);
	M0_ASSERT(ergo(rep.hae_rc == 0, confd_ep != NULL));
	confd_eps[0] = confd_ep;
	if (rep.hae_rc != 0)
		rep.hae_confd_fids.af_count = 0;
	M0_LOG(M0_DEBUG, "request");
	m0_ha_entrypoint_reply(ha, req_id, &rep, NULL);
	m0_free(confd_ep);
	m0_free(rep.hae_active_rm_ep);
}

static void mero_ha_entrypoint_replied_cb(struct m0_ha                *ha,
                                          struct m0_ha_entrypoint_rep *hep)
{
	M0_LOG(M0_DEBUG, "replied");
}

static void mero_ha_msg_received_cb(struct m0_ha      *ha,
                                    struct m0_ha_link *hl,
                                    struct m0_ha_msg  *msg,
                                    uint64_t           tag)
{
	struct m0_mero_ha *mha;

	mha = container_of(ha, struct m0_mero_ha, mh_ha);
	m0_ha_dispatcher_handle(&mha->mh_dispatcher, ha, hl, msg, tag);
	m0_ha_delivered(ha, hl, msg);
}

void mero_ha_msg_is_delivered_cb(struct m0_ha      *ha,
                                 struct m0_ha_link *hl,
                                 uint64_t           tag)
{
}

void mero_ha_msg_is_not_delivered_cb(struct m0_ha      *ha,
                                     struct m0_ha_link *hl,
                                     uint64_t           tag)
{
}

void mero_ha_link_connected_cb(struct m0_ha            *ha,
                               const struct m0_uint128 *req_id,
                               struct m0_ha_link       *hl)
{
}

void mero_ha_link_reused_cb(struct m0_ha            *ha,
                            const struct m0_uint128 *req_id,
                            struct m0_ha_link       *hl)
{
}

void mero_ha_link_is_disconnecting_cb(struct m0_ha      *ha,
                                      struct m0_ha_link *hl)
{
	m0_ha_disconnect_incoming(ha, hl);
}

void mero_ha_link_disconnected_cb(struct m0_ha      *ha,
                                  struct m0_ha_link *hl)
{
}

const struct m0_ha_ops m0_mero_ha_ops = {
	.hao_entrypoint_request    = &mero_ha_entrypoint_request_cb,
	.hao_entrypoint_replied    = &mero_ha_entrypoint_replied_cb,
	.hao_msg_received          = &mero_ha_msg_received_cb,
	.hao_msg_is_delivered      = &mero_ha_msg_is_delivered_cb,
	.hao_msg_is_not_delivered  = &mero_ha_msg_is_not_delivered_cb,
	.hao_link_connected        = &mero_ha_link_connected_cb,
	.hao_link_reused           = &mero_ha_link_reused_cb,
	.hao_link_is_disconnecting = &mero_ha_link_is_disconnecting_cb,
	.hao_link_disconnected     = &mero_ha_link_disconnected_cb,
};

enum mero_ha_level {
	MERO_HA_LEVEL_HA_INIT,
	MERO_HA_LEVEL_DISPATCHER,
	MERO_HA_LEVEL_INITIALISED,
	MERO_HA_LEVEL_HA_START,
	MERO_HA_LEVEL_INSTANCE_SET_HA,
	MERO_HA_LEVEL_STARTED,
	MERO_HA_LEVEL_CONNECT,
	MERO_HA_LEVEL_INSTANCE_SET_HA_LINK,
	MERO_HA_LEVEL_CONNECTED,
};

static int mero_ha_level_enter(struct m0_module *module)
{
	enum mero_ha_level  level = module->m_cur + 1;
	struct m0_mero_ha  *mha;

	mha = container_of(module, struct m0_mero_ha, mh_module);
	M0_ENTRY("mha=%p level=%d", mha, level);
	switch (level) {
	case MERO_HA_LEVEL_HA_INIT:
		return M0_RC(m0_ha_init(&mha->mh_ha, &(struct m0_ha_cfg){
				.hcf_ops         = m0_mero_ha_ops,
				.hcf_rpc_machine = mha->mh_cfg.mhc_rpc_machine,
				.hcf_addr        = mha->mh_cfg.mhc_addr,
				.hcf_reqh        = mha->mh_cfg.mhc_reqh,
				.hcf_process_fid = mha->mh_cfg.mhc_process_fid,
				.hcf_profile_fid = mha->mh_cfg.mhc_profile_fid,
			                }));
	case MERO_HA_LEVEL_DISPATCHER:
		return M0_RC(m0_ha_dispatcher_init(&mha->mh_dispatcher,
					   &mha->mh_cfg.mhc_dispatcher_cfg));
	case MERO_HA_LEVEL_HA_START:
		return M0_RC(m0_ha_start(&mha->mh_ha));
	case MERO_HA_LEVEL_INSTANCE_SET_HA:
		M0_ASSERT(m0_get()->i_ha == NULL);
		m0_get()->i_ha = &mha->mh_ha;
		return M0_RC(0);
	case MERO_HA_LEVEL_CONNECT:
		mha->mh_link = m0_ha_connect(&mha->mh_ha);
		/*
		 * Currently m0_ha_connect() is synchronous and always
		 * successful. It may be changed in the future.
		 */
		M0_ASSERT(mha->mh_link != NULL);
		return M0_RC(0);
	case MERO_HA_LEVEL_INSTANCE_SET_HA_LINK:
		M0_ASSERT(m0_get()->i_ha_link == NULL);
		m0_get()->i_ha_link = mha->mh_link;
		m0_ha_state_init(m0_ha_outgoing_session(&mha->mh_ha));
		return M0_RC(0);
	case MERO_HA_LEVEL_INITIALISED:
	case MERO_HA_LEVEL_STARTED:
	case MERO_HA_LEVEL_CONNECTED:
		M0_IMPOSSIBLE("can't be here");
	}
	return M0_ERR(-ENOSYS);
}

static void mero_ha_level_leave(struct m0_module *module)
{
	enum mero_ha_level  level = module->m_cur;
	struct m0_mero_ha  *mha;

	mha = container_of(module, struct m0_mero_ha, mh_module);
	M0_ENTRY("mha=%p level=%d", mha, level);
	switch (level) {
	case MERO_HA_LEVEL_HA_INIT:
		m0_ha_fini(&mha->mh_ha);
		break;
	case MERO_HA_LEVEL_DISPATCHER:
		m0_ha_dispatcher_fini(&mha->mh_dispatcher);
		break;
	case MERO_HA_LEVEL_HA_START:
		m0_ha_stop(&mha->mh_ha);
		break;
	case MERO_HA_LEVEL_INSTANCE_SET_HA:
		M0_ASSERT(m0_get()->i_ha == &mha->mh_ha);
		m0_get()->i_ha = NULL;
		break;
	case MERO_HA_LEVEL_CONNECT:
		m0_ha_state_fini();
		m0_ha_link_flush(mha->mh_link);
		m0_ha_disconnect(&mha->mh_ha, mha->mh_link);
		break;
	case MERO_HA_LEVEL_INSTANCE_SET_HA_LINK:
		M0_ASSERT(m0_get()->i_ha_link != NULL);
		M0_ASSERT(m0_get()->i_ha_link == mha->mh_link);
		m0_get()->i_ha_link = NULL;
		break;
	case MERO_HA_LEVEL_INITIALISED:
	case MERO_HA_LEVEL_STARTED:
	case MERO_HA_LEVEL_CONNECTED:
		M0_IMPOSSIBLE("can't be here");
	}
	M0_LEAVE();

}

static const struct m0_modlev mero_ha_levels[] = {
	[MERO_HA_LEVEL_HA_INIT] = {
		.ml_name  = "MERO_HA_LEVEL_HA_INIT",
		.ml_enter = mero_ha_level_enter,
		.ml_leave = mero_ha_level_leave,
	},
	[MERO_HA_LEVEL_DISPATCHER] = {
		.ml_name  = "MERO_HA_LEVEL_DISPATCHER",
		.ml_enter = mero_ha_level_enter,
		.ml_leave = mero_ha_level_leave,
	},
	[MERO_HA_LEVEL_INITIALISED] = {
		.ml_name  = "MERO_HA_LEVEL_INITIALISED",
	},
	[MERO_HA_LEVEL_HA_START] = {
		.ml_name  = "MERO_HA_LEVEL_HA_START",
		.ml_enter = mero_ha_level_enter,
		.ml_leave = mero_ha_level_leave,
	},
	[MERO_HA_LEVEL_INSTANCE_SET_HA] = {
		.ml_name  = "MERO_HA_LEVEL_INSTANCE_SET_HA",
		.ml_enter = mero_ha_level_enter,
		.ml_leave = mero_ha_level_leave,
	},
	[MERO_HA_LEVEL_STARTED] = {
		.ml_name  = "MERO_HA_LEVEL_STARTED",
	},
	[MERO_HA_LEVEL_CONNECT] = {
		.ml_name  = "MERO_HA_LEVEL_CONNECT",
		.ml_enter = mero_ha_level_enter,
		.ml_leave = mero_ha_level_leave,
	},
	[MERO_HA_LEVEL_INSTANCE_SET_HA_LINK] = {
		.ml_name  = "MERO_HA_LEVEL_INSTANCE_SET_HA_LINK",
		.ml_enter = mero_ha_level_enter,
		.ml_leave = mero_ha_level_leave,
	},
	[MERO_HA_LEVEL_CONNECTED] = {
		.ml_name  = "MERO_HA_LEVEL_CONNECTED",
	},
};

M0_INTERNAL int m0_mero_ha_init(struct m0_mero_ha     *mha,
                                struct m0_mero_ha_cfg *mha_cfg)
{
	char *addr_dup;
	int   rc;

	M0_ENTRY("mha=%p ha=%p mhc_addr=%s mhc_rpc_machine=%p mhc_reqh=%p "
	         "mhc_process_fid="FID_F" mhc_profile_fid="FID_F,
	         mha, &mha->mh_ha, mha_cfg->mhc_addr, mha_cfg->mhc_rpc_machine,
		 mha_cfg->mhc_reqh, FID_P(&mha_cfg->mhc_process_fid),
		 FID_P(&mha_cfg->mhc_process_fid));

	mha->mh_cfg = *mha_cfg;
	addr_dup = m0_strdup(mha_cfg->mhc_addr);
	if (addr_dup == NULL)
		return M0_ERR(-ENOMEM);
	mha->mh_cfg.mhc_addr = addr_dup;

	m0_module_setup(&mha->mh_module, "m0_mero_ha",
			mero_ha_levels, ARRAY_SIZE(mero_ha_levels), m0_get());
	rc = m0_module_init(&mha->mh_module, MERO_HA_LEVEL_INITIALISED);
	if (rc != 0) {
		m0_module_fini(&mha->mh_module, M0_MODLEV_NONE);
		m0_free(addr_dup);
		return M0_ERR(rc);
	}
	return M0_RC(0);
}

M0_INTERNAL void m0_mero_ha_fini(struct m0_mero_ha *mha)
{
	M0_ENTRY("mha=%p", mha);
	m0_module_fini(&mha->mh_module, M0_MODLEV_NONE);
	/*
	 * Removing "const" here is safe because the string is allocated in
	 * m0_mero_ha_init() using m0_strdup().
	 */
	m0_free((char *)mha->mh_cfg.mhc_addr);
	M0_LEAVE();
}

M0_INTERNAL int m0_mero_ha_start(struct m0_mero_ha *mha)
{
	int rc;

	M0_ENTRY("mha=%p", mha);
	rc = m0_module_init(&mha->mh_module, MERO_HA_LEVEL_STARTED);
	if (rc != 0)
		m0_module_fini(&mha->mh_module, MERO_HA_LEVEL_INITIALISED);
	return rc == 0 ? M0_RC(0) : M0_ERR(rc);
}

M0_INTERNAL void m0_mero_ha_stop(struct m0_mero_ha *mha)
{
	M0_ENTRY("mha=%p", mha);
	m0_module_fini(&mha->mh_module, MERO_HA_LEVEL_INITIALISED);
	M0_LEAVE();
}

M0_INTERNAL void m0_mero_ha_connect(struct m0_mero_ha *mha)
{
	int rc;
	M0_ENTRY("mha=%p", mha);
	rc = m0_module_init(&mha->mh_module, MERO_HA_LEVEL_CONNECTED);
	/* connect can't fail */
	M0_ASSERT(rc == 0);
	M0_LEAVE();
}

M0_INTERNAL void m0_mero_ha_disconnect(struct m0_mero_ha *mha)
{
	M0_ENTRY("mha=%p", mha);
	m0_module_fini(&mha->mh_module, MERO_HA_LEVEL_STARTED);
	M0_LEAVE();
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of mero group */

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
