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
 * TODO handle errors in m0_mero_ha_init()
 * TODO find include for M0_CONF_SERVICE_TYPE
 * TODO add magics for mero_ha_handlers
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


M0_TL_DESCR_DEFINE(mero_ha_handlers, "m0_mero_ha::mh_handlers", static,
		   struct m0_mero_ha_handler, mhf_link, mhf_magic,
		   21, 22);
M0_TL_DEFINE(mero_ha_handlers, static, struct m0_mero_ha_handler);

M0_INTERNAL void m0_mero_ha_cfg_make(struct m0_mero_ha_cfg *mha_cfg,
				     struct m0_reqh        *reqh,
				     struct m0_rpc_machine *rmach,
				     const char            *addr)
{
	M0_ENTRY("reqh=%p rmach=%p", reqh, rmach);
	M0_PRE(reqh != NULL);
	M0_PRE(rmach != NULL);
	*mha_cfg = (struct m0_mero_ha_cfg){
		.mhc_addr        = addr,
		.mhc_rpc_machine = rmach,
		.mhc_reqh        = reqh,
	};
	M0_LEAVE();
}

static int mero_ha_entrypoint_rep_confd_fill(const struct m0_fid  *profile,
                                             struct m0_confc      *confc,
                                             struct m0_fid        *confd_fid,
                                             char                **confd_ep)
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
	confd_eps[0] = confd_ep;
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
	struct m0_mero_ha_handler *mhf;
	struct m0_mero_ha         *mha;

	mha = container_of(ha, struct m0_mero_ha, mh_ha);
	m0_tl_for(mero_ha_handlers, &mha->mh_handlers, mhf) {
		mhf->mhf_msg_received_cb(mha, mhf, msg, hl, mhf->mhf_data);
	} m0_tl_endfor;
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

M0_INTERNAL int m0_mero_ha_init(struct m0_mero_ha     *mha,
                                struct m0_mero_ha_cfg *mha_cfg)
{
	int rc;

	M0_ENTRY("mha=%p ha=%p mhc_addr=%s mhc_rpc_machine=%p mhc_reqh=%p",
	         mha, &mha->mh_ha, mha_cfg->mhc_addr, mha_cfg->mhc_rpc_machine,
		 mha_cfg->mhc_reqh);

	mha->mh_cfg = *mha_cfg;
	mha->mh_cfg.mhc_addr = m0_strdup(mha_cfg->mhc_addr);
	M0_ASSERT(mha->mh_cfg.mhc_addr != NULL);
	rc = m0_ha_init(&mha->mh_ha, &(struct m0_ha_cfg){
				.hcf_ops         =  m0_mero_ha_ops,
				.hcf_rpc_machine =  mha->mh_cfg.mhc_rpc_machine,
				.hcf_reqh        =  mha->mh_cfg.mhc_reqh,
			});
	M0_ASSERT(rc == 0);
	mero_ha_handlers_tlist_init(&mha->mh_handlers);
	mha->mh_can_add_handler = true;
	M0_ALLOC_PTR(mha->mh_note_handler);
	M0_ASSERT(mha->mh_note_handler != NULL);
	rc = m0_ha_note_handler_init(mha->mh_note_handler, mha);
	M0_ASSERT(rc == 0);
	M0_ALLOC_PTR(mha->mh_keepalive_handler);
	M0_ASSERT(mha->mh_keepalive_handler != NULL);
	rc = m0_ha_keepalive_handler_init(mha->mh_keepalive_handler, mha);
	M0_ASSERT(rc == 0);
	M0_ASSERT(m0_get()->i_mero_ha == NULL);
	m0_get()->i_mero_ha = mha;
	return M0_RC(0);
}

M0_INTERNAL int m0_mero_ha_start(struct m0_mero_ha *mha)
{
	int rc;

	M0_ENTRY("mha=%p", mha);
	mha->mh_can_add_handler = false;
	rc = m0_ha_start(&mha->mh_ha);
	M0_ASSERT(rc == 0);
	M0_ASSERT(m0_get()->i_ha == NULL);
	m0_get()->i_ha = &mha->mh_ha;
	return M0_RC(0);
}

M0_INTERNAL void m0_mero_ha_stop(struct m0_mero_ha *mha)
{
	M0_ENTRY("mha=%p", mha);
	M0_ASSERT(m0_get()->i_ha == &mha->mh_ha);
	m0_get()->i_ha = NULL;
	m0_ha_stop(&mha->mh_ha);
	M0_LEAVE();
}

M0_INTERNAL void m0_mero_ha_fini(struct m0_mero_ha *mha)
{
	M0_ENTRY("mha=%p", mha);

	M0_ASSERT(m0_get()->i_mero_ha == mha);
	m0_get()->i_mero_ha = NULL;
	m0_ha_keepalive_handler_fini(mha->mh_keepalive_handler);
	m0_free(mha->mh_keepalive_handler);
	m0_ha_note_handler_fini(mha->mh_note_handler);
	m0_free(mha->mh_note_handler);
	mero_ha_handlers_tlist_fini(&mha->mh_handlers);
	m0_ha_fini(&mha->mh_ha);
	/*
	 * Removing "const" here is safe because the string is allocated in
	 * m0_mero_ha_init() using m0_strdup().
	 */
	m0_free((char *)mha->mh_cfg.mhc_addr);
	M0_LEAVE();
}

M0_INTERNAL void m0_mero_ha_connect(struct m0_mero_ha *mha)
{
	M0_ENTRY("mha=%p", mha);

	mha->mh_link = m0_ha_connect(&mha->mh_ha, mha->mh_cfg.mhc_addr);
	M0_ASSERT(mha->mh_link != NULL);        /* XXX */

	M0_ASSERT(m0_get()->i_ha_link == NULL);
	m0_get()->i_ha_link = mha->mh_link;
	m0_ha_state_init(mha->mh_link->hln_cfg.hlc_rpc_session);

	M0_LEAVE();
}

M0_INTERNAL void m0_mero_ha_disconnect(struct m0_mero_ha *mha)
{
	M0_ENTRY("mha=%p", mha);

	m0_ha_state_fini();
	M0_ASSERT(m0_get()->i_ha_link != NULL);
	M0_ASSERT(m0_get()->i_ha_link == mha->mh_link);
	m0_get()->i_ha_link = NULL;

	m0_ha_link_flush(mha->mh_link);
	m0_ha_disconnect(&mha->mh_ha, mha->mh_link);

	M0_LEAVE();
}

M0_INTERNAL void m0_mero_ha_handler_attach(struct m0_mero_ha         *mha,
                                           struct m0_mero_ha_handler *mhf)
{
	M0_PRE(mha->mh_can_add_handler);
	mero_ha_handlers_tlink_init_at_tail(mhf, &mha->mh_handlers);
}

M0_INTERNAL void m0_mero_ha_handler_detach(struct m0_mero_ha         *mha,
                                           struct m0_mero_ha_handler *mhf)
{
	mero_ha_handlers_tlink_del_fini(mhf);
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
