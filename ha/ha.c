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
 * Original creation date: 26-Apr-2016
 */


/**
 * @addtogroup ha
 *
 * TODO add m0_module to m0_ha
 * TODO handle memory allocation errors
 * TODO handle all errors
 * TODO handle error when link_id_request is false and no link is established
 * TODO add magics for ha_links
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_HA
#include "lib/trace.h"

#include "ha/ha.h"

#include "lib/memory.h"         /* M0_ALLOC_PTR */
#include "lib/errno.h"          /* ENOMEM */
#include "lib/misc.h"           /* M0_IS0 */
#include "lib/semaphore.h"      /* m0_semaphore */

#include "module/instance.h"    /* m0_get */
#include "module/module.h"      /* m0_module */

#include "rpc/conn.h"           /* m0_rpc_conn */
#include "rpc/session.h"        /* m0_rpc_session */
#include "rpc/rpclib.h"         /* m0_rpc_client_connect */

#include "ha/link.h"            /* m0_ha_link */
#include "ha/link_service.h"    /* m0_ha_link_service_init */
#include "ha/entrypoint.h"      /* m0_ha_entrypoint_rep */
#include "ha/note.h"            /* XXX m0_ha_state_init */

struct m0_ha_module {
	struct m0_module hmo_module;
};

enum ha_link_ctx_type {
	HLX_INCOMING,
	HLX_OUTGOING,
};

struct ha_link_ctx {
	struct m0_ha_link      hlx_link;
	struct m0_clink        hlx_clink;
	struct m0_ha          *hlx_ha;
	struct m0_tlink        hlx_tlink;
	uint64_t               hlx_magic;
	enum ha_link_ctx_type  hlx_type;
	struct m0_rpc_session  hlx_rpc_session;
	struct m0_rpc_conn     hlx_rpc_conn;
	struct m0_semaphore    hlx_disconnect_sem;
};

M0_TL_DESCR_DEFINE(ha_links, "m0_ha::h_links_{incoming,outgoing}", static,
		   struct ha_link_ctx, hlx_tlink, hlx_magic,
		   7, 8);
M0_TL_DEFINE(ha_links, static, struct ha_link_ctx);

static bool ha_link_event_cb(struct m0_clink *clink)
{
	struct ha_link_ctx *hlx;
	struct m0_ha_link  *hl;
	struct m0_ha_msg   *msg;
	struct m0_ha       *ha;
	uint64_t            tag;

	hlx = container_of(clink, struct ha_link_ctx, hlx_clink);
	hl  = &hlx->hlx_link;
	ha  = hlx->hlx_ha;
	M0_ENTRY("hlx=%p hl=%p ha=%p", hlx, hl, ha);
	while ((msg = m0_ha_link_recv(hl, &tag)) != NULL)
		ha->h_cfg.hcf_ops.hao_msg_received(ha, hl, msg, tag);
	while ((tag = m0_ha_link_delivered_consume(hl)) !=
	       M0_HA_MSG_TAG_INVALID) {
		ha->h_cfg.hcf_ops.hao_msg_is_delivered(ha, hl, tag);
	}
	while ((tag = m0_ha_link_not_delivered_consume(hl)) !=
	       M0_HA_MSG_TAG_INVALID) {
		ha->h_cfg.hcf_ops.hao_msg_is_not_delivered(ha, hl, tag);
	}
	M0_LEAVE();
	return true;
}

static int ha_link_ctx_init_rpc(struct m0_ha           *ha,
                                struct ha_link_ctx    **hlx_ptr,
                                struct m0_rpc_machine  *rpc_machine,
                                const char             *rpc_endpoint)
{
	struct ha_link_ctx *hlx;
	int                 rc;

	M0_ALLOC_PTR(hlx);
	M0_ASSERT(hlx != NULL); /* XXX */

	rc = m0_rpc_client_connect(&hlx->hlx_rpc_conn, &hlx->hlx_rpc_session,
				   rpc_machine, rpc_endpoint, /* XXX */ NULL,
				   2 /* XXX MAX_RPCS_IN_FLIGHT */,
				   M0_TIME_NEVER);
	M0_ASSERT(rc == 0);

	*hlx_ptr = hlx;
	M0_LOG(M0_DEBUG, "hlx=%p", hlx);
	return M0_RC(0);
}

static void ha_link_ctx_fini_rpc(struct m0_ha *ha, struct ha_link_ctx *hlx)
{
	int rc;

	rc = m0_rpc_session_destroy(&hlx->hlx_rpc_session,
				    m0_time_from_now(5, 0));
	if (rc != 0)
		M0_LOG(M0_WARN, "m0_rpc_session_destroy() failed: rc=%d", rc);
	rc = m0_rpc_conn_destroy(&hlx->hlx_rpc_conn, m0_time_from_now(5, 0));
	if (rc != 0)
		M0_LOG(M0_WARN, "m0_rpc_conn_destroy() failed: rc=%d", rc);
	m0_free(hlx);
}

static int ha_link_ctx_init(struct m0_ha          *ha,
                            struct ha_link_ctx    *hlx,
                            struct m0_ha_link_cfg *hl_cfg,
                            enum ha_link_ctx_type  hlx_type)
{
	struct m0_ha_link  *hl = &hlx->hlx_link;
	int                 rc;

	M0_ENTRY("ha=%p hlx_type=%d", ha, hlx_type);

	M0_PRE(M0_IN(hlx_type, (HLX_INCOMING, HLX_OUTGOING)));

	rc = m0_ha_link_init(hl, hl_cfg);
	M0_ASSERT(rc == 0);
	m0_clink_init(&hlx->hlx_clink, &ha_link_event_cb);
	m0_clink_add_lock(m0_ha_link_chan(hl), &hlx->hlx_clink);
	m0_ha_link_start(hl);

	hlx->hlx_ha   = ha;
	hlx->hlx_type = hlx_type;
	ha_links_tlink_init_at_tail(hlx, hlx_type == HLX_INCOMING ?
				    &ha->h_links_incoming :
				    &ha->h_links_outgoing);
	m0_semaphore_init(&hlx->hlx_disconnect_sem, 0);
	return M0_RC(0);
}

static void ha_link_ctx_fini(struct m0_ha *ha, struct ha_link_ctx *hlx)
{
	M0_ENTRY("ha=%p hlx=%p", ha, hlx);

	m0_semaphore_fini(&hlx->hlx_disconnect_sem);
	ha_links_tlink_del_fini(hlx);

	m0_ha_link_stop(&hlx->hlx_link);
	m0_clink_del_lock(&hlx->hlx_clink);
	m0_clink_fini(&hlx->hlx_clink);
	m0_ha_link_fini(&hlx->hlx_link);
	M0_LEAVE();
}

static void
ha_request_received_cb(struct m0_ha_entrypoint_server    *hes,
                       const struct m0_ha_entrypoint_req *req,
                       const struct m0_uint128           *req_id)
{
	struct m0_ha *ha;

	ha = container_of(hes, struct m0_ha, h_entrypoint_server);
	M0_ENTRY("ha=%p hes=%p req=%p", ha, hes, req);
	ha->h_cfg.hcf_ops.hao_entrypoint_request(ha, req, req_id);
}

static bool ha_entrypoint_state_cb(struct m0_clink *clink)
{
	enum m0_ha_entrypoint_client_state  state;
	struct m0_ha_entrypoint_client     *ecl;
	struct m0_ha_entrypoint_rep        *rep;
	struct m0_ha                       *ha;
	bool                                consumed = true;

	M0_ENTRY();

	ha    = container_of(clink, struct m0_ha, h_clink);
	ecl   = &ha->h_entrypoint_client;
	state = m0_ha_entrypoint_client_state_get(ecl);

	switch (state) {
	case M0_HEC_AVAILABLE:
		rep = &ha->h_entrypoint_client.ecl_rep;
		M0_LOG(M0_DEBUG, "ha=%p rep->hae_rc=%d", ha, rep->hae_rc);
		if (rep->hae_rc != 0)
			m0_ha_entrypoint_client_request(ecl);
		else
			ha->h_cfg.hcf_ops.hao_entrypoint_replied(ha, rep);
		consumed = rep->hae_rc != 0;
		break;

	case M0_HEC_UNAVAILABLE:
		m0_ha_entrypoint_client_request(ecl);
		break;

	default:
		break;
	}

	M0_LEAVE();

	return consumed;
}

M0_INTERNAL int m0_ha_init(struct m0_ha *ha, struct m0_ha_cfg *ha_cfg)
{
	M0_ENTRY("ha=%p hcf_rpc_machine=%p hcf_reqh=%p",
	         ha, ha_cfg->hcf_rpc_machine, ha_cfg->hcf_reqh);
	M0_PRE(M0_IS0(ha));
	ha->h_cfg = *ha_cfg;
	ha_links_tlist_init(&ha->h_links_incoming);
	ha_links_tlist_init(&ha->h_links_outgoing);
	m0_clink_init(&ha->h_clink, ha_entrypoint_state_cb);
	ha->h_link_id_counter = 1;
	return M0_RC(0);
}

M0_INTERNAL int m0_ha_start(struct m0_ha *ha)
{
	int rc;

	M0_ENTRY("ha=%p", ha);
	rc = m0_ha_link_service_init(&ha->h_hl_service, ha->h_cfg.hcf_reqh);
	M0_ASSERT(rc == 0);
	ha->h_cfg.hcf_entrypoint_server_cfg =
				(struct m0_ha_entrypoint_server_cfg){
		.hesc_reqh = ha->h_cfg.hcf_reqh,
		.hesc_request_received = &ha_request_received_cb,
	};
	rc = m0_ha_entrypoint_server_init(&ha->h_entrypoint_server,
	                                  &ha->h_cfg.hcf_entrypoint_server_cfg);
	M0_ASSERT(rc == 0);
	m0_ha_entrypoint_server_start(&ha->h_entrypoint_server);
	return M0_RC(0);
}

M0_INTERNAL void m0_ha_stop(struct m0_ha *ha)
{
	struct ha_link_ctx *hlx;

	M0_ENTRY("ha=%p", ha);
	m0_tl_for(ha_links, &ha->h_links_incoming, hlx) {
		M0_LOG(M0_DEBUG, "hlx=%p", hlx);
		ha->h_cfg.hcf_ops.hao_link_is_disconnecting(ha, &hlx->hlx_link);
		m0_semaphore_down(&hlx->hlx_disconnect_sem);
		ha_link_ctx_fini(ha, hlx);
		ha->h_cfg.hcf_ops.hao_link_disconnected(ha, &hlx->hlx_link);
		ha_link_ctx_fini_rpc(ha, hlx);
	} m0_tl_endfor;
	m0_ha_entrypoint_server_stop(&ha->h_entrypoint_server);
	m0_ha_entrypoint_server_fini(&ha->h_entrypoint_server);
	m0_ha_link_service_fini(ha->h_hl_service);
	M0_LEAVE();
}

M0_INTERNAL void m0_ha_fini(struct m0_ha *ha)
{
	M0_ENTRY("ha=%p", ha);

	m0_clink_fini(&ha->h_clink);
	ha_links_tlist_fini(&ha->h_links_outgoing);
	ha_links_tlist_fini(&ha->h_links_incoming);
	M0_LEAVE();
}

M0_INTERNAL struct m0_ha_link *m0_ha_connect(struct m0_ha *ha,
                                             const char   *ep)
{
	struct m0_ha_entrypoint_rep *rep;
	struct ha_link_ctx          *hlx;
	struct m0_chan              *chan;
	int                          rc;
	struct m0_ha_link_cfg        hl_cfg;

	M0_ENTRY("ha=%p ep=%s", ha, ep);

	rc = ha_link_ctx_init_rpc(ha, &hlx, ha->h_cfg.hcf_rpc_machine, ep);
	M0_ASSERT(rc == 0);

	hl_cfg = (struct m0_ha_link_cfg){
		.hlc_reqh           = ha->h_cfg.hcf_reqh,
		.hlc_reqh_service   = ha->h_hl_service,
		.hlc_rpc_session    = &hlx->hlx_rpc_session,
		.hlc_q_in_cfg       = {},
		.hlc_q_out_cfg      = {},
	};

	ha->h_cfg.hcf_entrypoint_client_cfg =
				(struct m0_ha_entrypoint_client_cfg){
		.hecc_reqh        = ha->h_cfg.hcf_reqh,
		.hecc_rpc_machine = ha->h_cfg.hcf_rpc_machine,
		.hecc_process_fid = ha->h_cfg.hcf_process_fid,
		.hecc_profile_fid = ha->h_cfg.hcf_profile_fid,
	};
	rc = m0_ha_entrypoint_client_init(&ha->h_entrypoint_client, ep,
	                                  &ha->h_cfg.hcf_entrypoint_client_cfg);
	M0_ASSERT(rc == 0);
	ha->h_entrypoint_client.ecl_req.heq_link_id_request = true;
	chan = m0_ha_entrypoint_client_chan(&ha->h_entrypoint_client);
	m0_clink_add_lock(chan, &ha->h_clink);
	m0_ha_entrypoint_client_start(&ha->h_entrypoint_client);
	m0_chan_wait(&ha->h_clink);

	rc = m0_ha_state_init(&hlx->hlx_rpc_session);
	M0_ASSERT(rc == 0);

	rep = &ha->h_entrypoint_client.ecl_rep;
	hl_cfg.hlc_link_id_local  = rep->hae_link_id_local;
	hl_cfg.hlc_link_id_remote = rep->hae_link_id_remote;
	hl_cfg.hlc_tag_even       = rep->hae_link_tag_even;
	rc = ha_link_ctx_init(ha, hlx, &hl_cfg, HLX_OUTGOING);
	M0_ASSERT(rc == 0);
	return &hlx->hlx_link;
}

M0_INTERNAL void m0_ha_disconnect(struct m0_ha      *ha,
                                  struct m0_ha_link *hl)
{
	struct ha_link_ctx *hlx;

	hlx = container_of(hl, struct ha_link_ctx, hlx_link);
	M0_ENTRY("ha=%p hl=%p", ha, hl);
	ha_link_ctx_fini(ha, hlx);
	m0_ha_state_fini();
	m0_clink_del_lock(&ha->h_clink);
	m0_ha_entrypoint_client_stop(&ha->h_entrypoint_client);
	m0_ha_entrypoint_client_fini(&ha->h_entrypoint_client);
	ha_link_ctx_fini_rpc(ha, hlx);
	M0_LEAVE();
}

M0_INTERNAL void m0_ha_disconnect_incoming(struct m0_ha      *ha,
                                           struct m0_ha_link *hl)
{
	struct ha_link_ctx *hlx;

	hlx = container_of(hl, struct ha_link_ctx, hlx_link);
	M0_ENTRY("ha=%p hl=%p", ha, hl);
	m0_semaphore_up(&hlx->hlx_disconnect_sem);
	M0_LEAVE();
}

static void ha_link_id_next(struct m0_ha      *ha,
                            struct m0_uint128 *id)
{
	*id = M0_UINT128(0, ha->h_link_id_counter++);
}

void m0_ha_entrypoint_reply(struct m0_ha                       *ha,
                            const struct m0_uint128            *req_id,
			    const struct m0_ha_entrypoint_rep  *rep,
			    struct m0_ha_link                 **hl_ptr)
{
	const struct m0_ha_entrypoint_req *req;
	struct m0_ha_entrypoint_rep        rep_copy = *rep;
	struct m0_ha_link_cfg              hl_cfg;
	struct ha_link_ctx                *hlx;
	int                                rc;

	M0_ENTRY("ha=%p req_id="U128X_F" rep=%p", ha, U128_P(req_id), rep);
	req = m0_ha_entrypoint_server_request_find(&ha->h_entrypoint_server,
	                                           req_id);
	if (req->heq_link_id_request) {
		rc = ha_link_ctx_init_rpc(ha, &hlx, ha->h_cfg.hcf_rpc_machine,
					  req->heq_rpc_endpoint);
		M0_ASSERT(rc == 0);
		hl_cfg = (struct m0_ha_link_cfg){
			.hlc_reqh           = ha->h_cfg.hcf_reqh,
			.hlc_reqh_service   = ha->h_hl_service,
			.hlc_rpc_session    = &hlx->hlx_rpc_session,
			.hlc_q_in_cfg       = {},
			.hlc_q_out_cfg      = {},
			.hlc_tag_even       = true,
		};
		ha_link_id_next(ha, &hl_cfg.hlc_link_id_local);
		ha_link_id_next(ha, &hl_cfg.hlc_link_id_remote);
		rep_copy.hae_link_id_local  = hl_cfg.hlc_link_id_remote;
		rep_copy.hae_link_id_remote = hl_cfg.hlc_link_id_local;
		rep_copy.hae_link_tag_even  = !hl_cfg.hlc_tag_even;

		rc = ha_link_ctx_init(ha, hlx, &hl_cfg, HLX_INCOMING);
		M0_ASSERT(rc == 0);
		ha->h_cfg.hcf_ops.hao_link_connected(ha, req_id,
						     &hlx->hlx_link);
	} else {
		hlx = m0_tl_find(ha_links, hlx, &ha->h_links_incoming,
			m0_uint128_eq(&hlx->hlx_link.hln_cfg.hlc_link_id_local,
			              &req->heq_link_id_remote) &&
			m0_uint128_eq(&hlx->hlx_link.hln_cfg.hlc_link_id_remote,
			              &req->heq_link_id_local));
		M0_ASSERT(hlx != NULL); /* XXX */
		ha->h_cfg.hcf_ops.hao_link_reused(ha, req_id, &hlx->hlx_link);
	}
	if (hl_ptr != NULL)
		*hl_ptr = &hlx->hlx_link;
	m0_ha_entrypoint_server_reply(&ha->h_entrypoint_server,
				      req_id, &rep_copy);
	M0_LEAVE();
}

M0_INTERNAL void m0_ha_send(struct m0_ha           *ha,
                            struct m0_ha_link      *hl,
                            const struct m0_ha_msg *msg,
                            uint64_t               *tag)
{
	m0_ha_link_send(hl, msg, tag);
}

M0_INTERNAL void m0_ha_delivered(struct m0_ha      *ha,
                                 struct m0_ha_link *hl,
                                 struct m0_ha_msg  *msg)
{
	m0_ha_link_delivered(hl, msg);
}

enum m0_ha_mod_level {
	M0_HA_MOD_LEVEL_ASSIGNS,
	M0_HA_MOD_LEVEL_LINK_SERVICE,
	M0_HA_MOD_LEVEL_LINK,
	M0_HA_MOD_LEVEL_ENTRYPOINT,
	M0_HA_MOD_LEVEL_STARTED,
};

static int ha_mod_level_enter(struct m0_module *module)
{
	enum m0_ha_mod_level  level = module->m_cur + 1;
	struct m0_ha_module  *ha_module;

	ha_module = container_of(module, struct m0_ha_module, hmo_module);
	M0_ENTRY("ha_module=%p level=%d", ha_module, level);
	switch (level) {
	case M0_HA_MOD_LEVEL_ASSIGNS:
		M0_PRE(m0_get()->i_ha_module == NULL);
		m0_get()->i_ha_module = ha_module;
		return M0_RC(0);
	case M0_HA_MOD_LEVEL_LINK_SERVICE:
		return M0_RC(m0_ha_link_mod_init());
	case M0_HA_MOD_LEVEL_LINK:
		return M0_RC(m0_ha_link_service_mod_init());
	case M0_HA_MOD_LEVEL_ENTRYPOINT:
		return M0_RC(m0_ha_entrypoint_mod_init());
	case M0_HA_MOD_LEVEL_STARTED:
		M0_IMPOSSIBLE("can't be here");
		return M0_ERR(-ENOSYS);
	}
	return M0_ERR(-ENOSYS);
}

static void ha_mod_level_leave(struct m0_module *module)
{
	enum m0_ha_mod_level  level = module->m_cur;
	struct m0_ha_module  *ha_module;

	ha_module = container_of(module, struct m0_ha_module, hmo_module);
	M0_ENTRY("ha_module=%p level=%d", ha_module, level);
	switch (level) {
	case M0_HA_MOD_LEVEL_ASSIGNS:
		m0_get()->i_ha_module = NULL;
		break;
	case M0_HA_MOD_LEVEL_LINK_SERVICE:
		m0_ha_link_service_mod_fini();
		break;
	case M0_HA_MOD_LEVEL_LINK:
		m0_ha_link_mod_fini();
		break;
	case M0_HA_MOD_LEVEL_ENTRYPOINT:
		m0_ha_entrypoint_mod_fini();
		break;
	case M0_HA_MOD_LEVEL_STARTED:
		M0_IMPOSSIBLE("can't be here");
		break;
	}
	M0_LEAVE();
}

static const struct m0_modlev ha_mod_levels[] = {
	[M0_HA_MOD_LEVEL_ASSIGNS] = {
		.ml_name  = "M0_HA_MOD_LEVEL_ASSIGNS",
		.ml_enter = ha_mod_level_enter,
		.ml_leave = ha_mod_level_leave,
	},
	[M0_HA_MOD_LEVEL_LINK_SERVICE] = {
		.ml_name  = "M0_HA_MOD_LEVEL_LINK_SERVICE",
		.ml_enter = ha_mod_level_enter,
		.ml_leave = ha_mod_level_leave,
	},
	[M0_HA_MOD_LEVEL_LINK] = {
		.ml_name  = "M0_HA_MOD_LEVEL_LINK",
		.ml_enter = ha_mod_level_enter,
		.ml_leave = ha_mod_level_leave,
	},
	[M0_HA_MOD_LEVEL_ENTRYPOINT] = {
		.ml_name  = "M0_HA_MOD_LEVEL_ENTRYPOINT",
		.ml_enter = ha_mod_level_enter,
		.ml_leave = ha_mod_level_leave,
	},
	[M0_HA_MOD_LEVEL_STARTED] = {
		.ml_name  = "M0_HA_MOD_LEVEL_STARTED",
	},
};

M0_INTERNAL int m0_ha_mod_init(void)
{
	struct m0_ha_module *ha_module;
	int                  rc;

	M0_ALLOC_PTR(ha_module);
	if (ha_module == NULL)
		return M0_ERR(-ENOMEM);

	m0_module_setup(&ha_module->hmo_module, "m0_ha_module",
			ha_mod_levels, ARRAY_SIZE(ha_mod_levels), m0_get());
	rc = m0_module_init(&ha_module->hmo_module, M0_HA_MOD_LEVEL_STARTED);
	if (rc != 0) {
		m0_module_fini(&ha_module->hmo_module, M0_MODLEV_NONE);
		m0_free(ha_module);
		return M0_ERR(rc);
	}
	return M0_RC(0);
}

M0_INTERNAL void m0_ha_mod_fini(void)
{
	struct m0_ha_module *ha_module = m0_get()->i_ha_module;

	M0_PRE(ha_module != NULL);

	m0_module_fini(&ha_module->hmo_module, M0_MODLEV_NONE);
	m0_free(ha_module);
}

#undef M0_TRACE_SUBSYSTEM

/** @} end of ha group */

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
