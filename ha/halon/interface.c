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
 * Original creation date: 5-May-2016
 */


/**
 * @addtogroup ha
 *
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_HA
#include "lib/trace.h"

#include "ha/halon/interface.h"

#include <stdlib.h>             /* calloc */

#include "lib/types.h"          /* m0_bcount_t */
#include "lib/misc.h"           /* M0_IS0 */
#include "lib/memory.h"         /* M0_ALLOC_PTR */
#include "lib/bob.h"            /* M0_BOB_DEFINE */
#include "lib/errno.h"          /* ENOSYS */
#include "lib/string.h"         /* strcmp */

#include "net/net.h"            /* M0_NET_TM_RECV_QUEUE_DEF_LEN */
#include "net/lnet/lnet.h"      /* m0_net_lnet_xprt */
#include "net/buffer_pool.h"    /* m0_net_buffer_pool */
#include "fid/fid.h"            /* m0_fid */
#include "module/instance.h"    /* m0 */
#include "reqh/reqh.h"          /* m0_reqh */
#include "rpc/rpc.h"            /* m0_rpc_bufs_nr */
#include "rpc/rpc_machine.h"    /* m0_rpc_machine */

#include "mero/init.h"          /* m0_init */
#include "mero/magic.h"         /* M0_HALON_INTERFACE_MAGIC */
#include "mero/version.h"       /* m0_build_info_get */

struct m0_halon_interface_cfg {
	const char   *hic_build_git_rev_id;
	const char   *hic_build_configure_opts;
	bool          hic_disable_compat_check;
	char         *hic_local_rpc_endpoint;
	void        (*hic_entrypoint_request_cb)
		(struct m0_halon_interface         *hi,
		 const struct m0_uint128           *req_id,
		 const char             *remote_rpc_endpoint);
	void        (*hic_msg_received_cb)
		(struct m0_halon_interface *hi,
		 struct m0_ha_link         *hl,
		 struct m0_ha_msg          *msg,
		 uint64_t                   tag);
	void        (*hic_msg_is_delivered_cb)
		(struct m0_halon_interface *hi,
		 struct m0_ha_link         *hl,
		 uint64_t                   tag);
	void        (*hic_msg_is_not_delivered_cb)
		(struct m0_halon_interface *hi,
		 struct m0_ha_link         *hl,
		 uint64_t                   tag);
	void        (*hic_link_connected_cb)
		(struct m0_halon_interface *hi,
		 const struct m0_uint128   *req_id,
		 struct m0_ha_link         *link);
	void        (*hic_link_reused_cb)
		(struct m0_halon_interface *hi,
		 const struct m0_uint128   *req_id,
		 struct m0_ha_link         *link);
	void        (*hic_link_is_disconnecting_cb)
		(struct m0_halon_interface *hi,
		 struct m0_ha_link         *link);
	void        (*hic_link_disconnected_cb)
		(struct m0_halon_interface *hi,
		 struct m0_ha_link         *link);

	/** XXX pass as parameter */
	struct m0_fid hic_process_fid;
	uint32_t      hic_tm_nr;
	uint32_t      hic_bufs_nr;
	uint32_t      hic_colour;
	m0_bcount_t   hic_max_msg_size;
	uint32_t      hic_queue_len;
};

enum m0_halon_interface_level {
	M0_HALON_INTERFACE_LEVEL_ASSIGNS,
	M0_HALON_INTERFACE_LEVEL_NET_DOMAIN,
	M0_HALON_INTERFACE_LEVEL_NET_BUFFER_POOL,
	M0_HALON_INTERFACE_LEVEL_REQH_INIT,
	M0_HALON_INTERFACE_LEVEL_REQH_START,
	M0_HALON_INTERFACE_LEVEL_RPC_MACHINE,
	M0_HALON_INTERFACE_LEVEL_STARTED,
};

struct m0_halon_interface_internal {
	struct m0                     hii_instance;
	struct m0_halon_interface_cfg hii_cfg;
	struct m0_module              hii_module;
	struct m0_net_domain          hii_net_domain;
	struct m0_net_buffer_pool     hii_net_buffer_pool;
	struct m0_reqh                hii_reqh;
	struct m0_rpc_machine         hii_rpc_machine;
	uint64_t                      hii_magix;
};

static const struct m0_bob_type halon_interface_bob_type = {
	.bt_name         = "halon interface",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct m0_halon_interface_internal,
	                                   hii_magix),
	.bt_magix        = M0_HALON_INTERFACE_MAGIC,
};
M0_BOB_DEFINE(static, &halon_interface_bob_type, m0_halon_interface_internal);

static bool
halon_interface_is_compatible(struct m0_halon_interface *hi,
                              const char                *build_git_rev_id,
                              const char                *build_configure_opts,
                              bool                       disable_compat_check)
{
	const struct m0_build_info *bi = m0_build_info_get();

	M0_ENTRY("build_git_rev_id=%s build_configure_opts=%s "
	         "disable_compat_check=%d",
	         build_git_rev_id, build_configure_opts, disable_compat_check);
	if (disable_compat_check)
		return true;
	if (strcmp(bi->bi_git_rev_id, build_git_rev_id) != 0) {
		M0_LOG(M0_ERROR, "The loaded mero library (%s) "
		       "is not the expected one (%s)", bi->bi_git_rev_id,
		       build_git_rev_id);
		return false;
	}
	if (strcmp(bi->bi_configure_opts, build_configure_opts) != 0) {
		M0_LOG(M0_ERROR, "The configuration options of the loaded "
		       "mero library (%s) do not match the expected ones (%s)",
		       bi->bi_configure_opts, build_configure_opts);
		return false;
	}
	return true;
}

int m0_halon_interface_init(struct m0_halon_interface *hi,
                            const char                *build_git_rev_id,
                            const char                *build_configure_opts,
                            bool                       disable_compat_check)
{
	int rc;

	M0_PRE(M0_IS0(hi));

	if (!halon_interface_is_compatible(hi, build_git_rev_id,
	                                   build_configure_opts,
					   disable_compat_check))
		return M0_ERR(-EINVAL);

	/* M0_ALLOC_PTR() can't be used before m0_init() */
	hi->hif_internal = calloc(1, sizeof *hi->hif_internal);
	if (hi->hif_internal == NULL)
		return M0_ERR(-ENOMEM);
	/*
	 * TODO allocate m0_halon_inteface here. Replace
	 * m0_halon_interface_internal with m0_halon_interface.
	 */
	m0_halon_interface_internal_bob_init(hi->hif_internal);
	rc = m0_init(&hi->hif_internal->hii_instance);
	if (rc != 0) {
		free(hi->hif_internal);
		return M0_ERR(rc);
	}
	return M0_RC(0);
}

void m0_halon_interface_fini(struct m0_halon_interface *hi)
{
	M0_ENTRY("hi=%p", hi);
	m0_fini();
	m0_halon_interface_internal_bob_fini(hi->hif_internal);
	free(hi->hif_internal);
	M0_LEAVE();
}

static int halon_interface_level_enter(struct m0_module *module)
{
	struct m0_halon_interface_internal *hii;
	enum m0_halon_interface_level       level = module->m_cur + 1;

	hii = bob_of(module, struct m0_halon_interface_internal, hii_module,
	             &halon_interface_bob_type);
	M0_ENTRY("hii=%p level=%d", hii, level);
	switch (level) {
	case M0_HALON_INTERFACE_LEVEL_ASSIGNS:
		/* TODO zero all data structures to allow second start() */
		hii->hii_cfg.hic_process_fid  = M0_FID_TINIT('r', 0, 1);
		hii->hii_cfg.hic_tm_nr        = 1;
		hii->hii_cfg.hic_bufs_nr      =
			m0_rpc_bufs_nr(M0_NET_TM_RECV_QUEUE_DEF_LEN,
			               hii->hii_cfg.hic_tm_nr);
		hii->hii_cfg.hic_colour       = M0_BUFFER_ANY_COLOUR;
		hii->hii_cfg.hic_max_msg_size = 1UL << 17;
		hii->hii_cfg.hic_queue_len    = 2;
		return M0_RC(0);
	case M0_HALON_INTERFACE_LEVEL_NET_DOMAIN:
		return M0_RC(m0_net_domain_init(&hii->hii_net_domain,
		                                &m0_net_lnet_xprt));
	case M0_HALON_INTERFACE_LEVEL_NET_BUFFER_POOL:
		return M0_RC(m0_rpc_net_buffer_pool_setup(
		                &hii->hii_net_domain, &hii->hii_net_buffer_pool,
		                hii->hii_cfg.hic_bufs_nr,
				hii->hii_cfg.hic_tm_nr));
	case M0_HALON_INTERFACE_LEVEL_REQH_INIT:
		return M0_RC(M0_REQH_INIT(&hii->hii_reqh,
		                          .rhia_dtm          = (void*)1,
		                          .rhia_mdstore      = (void*)1,
		                          .rhia_fid          =
						&hii->hii_cfg.hic_process_fid));
	case M0_HALON_INTERFACE_LEVEL_REQH_START:
		m0_reqh_start(&hii->hii_reqh);
		return M0_RC(0);
	case M0_HALON_INTERFACE_LEVEL_RPC_MACHINE:
		return M0_RC(m0_rpc_machine_init(
		                &hii->hii_rpc_machine, &hii->hii_net_domain,
				 hii->hii_cfg.hic_local_rpc_endpoint,
				 &hii->hii_reqh, &hii->hii_net_buffer_pool,
				 hii->hii_cfg.hic_colour,
				 hii->hii_cfg.hic_max_msg_size,
				 hii->hii_cfg.hic_queue_len));
	case M0_HALON_INTERFACE_LEVEL_STARTED:
		return M0_ERR(-ENOSYS);
	}
	return M0_ERR(-ENOSYS);
}

static void halon_interface_level_leave(struct m0_module *module)
{
	struct m0_halon_interface_internal *hii;
	enum m0_halon_interface_level       level = module->m_cur;

	hii = bob_of(module, struct m0_halon_interface_internal, hii_module,
	             &halon_interface_bob_type);
	M0_ENTRY("hii=%p level=%d", hii, level);
	switch (level) {
	case M0_HALON_INTERFACE_LEVEL_ASSIGNS:
		break;
	case M0_HALON_INTERFACE_LEVEL_NET_DOMAIN:
		m0_net_domain_fini(&hii->hii_net_domain);
		break;
	case M0_HALON_INTERFACE_LEVEL_NET_BUFFER_POOL:
		m0_rpc_net_buffer_pool_cleanup(&hii->hii_net_buffer_pool);
		break;
	case M0_HALON_INTERFACE_LEVEL_REQH_INIT:
		m0_reqh_fini(&hii->hii_reqh);
		break;
	case M0_HALON_INTERFACE_LEVEL_REQH_START:
		m0_reqh_services_terminate(&hii->hii_reqh);
		break;
	case M0_HALON_INTERFACE_LEVEL_RPC_MACHINE:
		m0_reqh_shutdown_wait(&hii->hii_reqh);
		m0_rpc_machine_fini(&hii->hii_rpc_machine);
		break;
	case M0_HALON_INTERFACE_LEVEL_STARTED:
		M0_IMPOSSIBLE("can't be here");
		break;
	}
	M0_LEAVE();
}

static const struct m0_modlev halon_interface_levels[] = {
	[M0_HALON_INTERFACE_LEVEL_ASSIGNS] = {
		.ml_name  = "M0_HALON_INTERFACE_LEVEL_ASSIGNS",
		.ml_enter = halon_interface_level_enter,
		.ml_leave = halon_interface_level_leave,
	},
	[M0_HALON_INTERFACE_LEVEL_NET_DOMAIN] = {
		.ml_name  = "M0_HALON_INTERFACE_LEVEL_NET_DOMAIN",
		.ml_enter = halon_interface_level_enter,
		.ml_leave = halon_interface_level_leave,
	},
	[M0_HALON_INTERFACE_LEVEL_NET_BUFFER_POOL] = {
		.ml_name  = "M0_HALON_INTERFACE_LEVEL_NET_BUFFER_POOL",
		.ml_enter = halon_interface_level_enter,
		.ml_leave = halon_interface_level_leave,
	},
	[M0_HALON_INTERFACE_LEVEL_REQH_INIT] = {
		.ml_name  = "M0_HALON_INTERFACE_LEVEL_REQH_INIT",
		.ml_enter = halon_interface_level_enter,
		.ml_leave = halon_interface_level_leave,
	},
	[M0_HALON_INTERFACE_LEVEL_REQH_START] = {
		.ml_name  = "M0_HALON_INTERFACE_LEVEL_REQH_START",
		.ml_enter = halon_interface_level_enter,
		.ml_leave = halon_interface_level_leave,
	},
	[M0_HALON_INTERFACE_LEVEL_RPC_MACHINE] = {
		.ml_name  = "M0_HALON_INTERFACE_LEVEL_RPC_MACHINE",
		.ml_enter = halon_interface_level_enter,
		.ml_leave = halon_interface_level_leave,
	},
	[M0_HALON_INTERFACE_LEVEL_STARTED] = {
		.ml_name  = "M0_HALON_INTERFACE_LEVEL_STARTED",
	},
};

int m0_halon_interface_start(struct m0_halon_interface *hi,
                             const char                *local_rpc_endpoint,
                             void                     (*entrypoint_request_cb)
				(struct m0_halon_interface         *hi,
				 const struct m0_uint128           *req_id,
				 const char             *remote_rpc_endpoint),
			     void                     (*msg_received_cb)
				(struct m0_halon_interface *hi,
				 struct m0_ha_link         *hl,
				 struct m0_ha_msg          *msg,
				 uint64_t                   tag),
			     void                     (*msg_is_delivered_cb)
				(struct m0_halon_interface *hi,
				 struct m0_ha_link         *hl,
				 uint64_t                   tag),
			     void                     (*msg_is_not_delivered_cb)
				(struct m0_halon_interface *hi,
				 struct m0_ha_link         *hl,
				 uint64_t                   tag),
			     void                    (*link_connected_cb)
			        (struct m0_halon_interface *hi,
				 const struct m0_uint128   *req_id,
			         struct m0_ha_link         *link),
			     void                    (*link_reused_cb)
			        (struct m0_halon_interface *hi,
				 const struct m0_uint128   *req_id,
			         struct m0_ha_link         *link),
			     void                    (*link_is_disconnecting_cb)
			        (struct m0_halon_interface *hi,
			         struct m0_ha_link         *link),
			     void                     (*link_disconnected_cb)
			        (struct m0_halon_interface *hi,
			         struct m0_ha_link         *link))
{
	struct m0_halon_interface_internal *hii = hi->hif_internal;
	char                               *ep;
	int                                 rc;

	M0_PRE(m0_halon_interface_internal_bob_check(hii));
	M0_PRE(m0_get() == &hii->hii_instance);

	M0_ENTRY("hi=%p local_rpc_endpoint=%s", hi, local_rpc_endpoint);

	ep = m0_strdup(local_rpc_endpoint);
	if (ep == NULL)
		return M0_ERR(-ENOMEM);

	hii->hii_cfg.hic_local_rpc_endpoint       = ep;
	hii->hii_cfg.hic_entrypoint_request_cb    = entrypoint_request_cb;
	hii->hii_cfg.hic_msg_received_cb          = msg_received_cb;
	hii->hii_cfg.hic_msg_is_delivered_cb      = msg_is_delivered_cb;
	hii->hii_cfg.hic_msg_is_not_delivered_cb  = msg_is_not_delivered_cb;
	hii->hii_cfg.hic_link_connected_cb        = link_connected_cb;
	hii->hii_cfg.hic_link_reused_cb           = link_reused_cb;
	hii->hii_cfg.hic_link_is_disconnecting_cb = link_is_disconnecting_cb;
	hii->hii_cfg.hic_link_disconnected_cb     = link_disconnected_cb;

	m0_module_setup(&hii->hii_module, "m0_halon_interface",
			halon_interface_levels,
			ARRAY_SIZE(halon_interface_levels),
			&hii->hii_instance);
	rc = m0_module_init(&hii->hii_module, M0_HALON_INTERFACE_LEVEL_STARTED);
	if (rc != 0) {
		m0_module_fini(&hii->hii_module, M0_MODLEV_NONE);
		m0_free(ep);
		return M0_ERR(rc);
	}
	return M0_RC(rc);
}

void m0_halon_interface_stop(struct m0_halon_interface *hi)
{
	struct m0_halon_interface_internal *hii = hi->hif_internal;

	M0_ASSERT(m0_halon_interface_internal_bob_check(hii));
	M0_ASSERT(m0_get() == &hii->hii_instance);

	M0_ENTRY("hi=%p", hi);
	m0_free(hii->hii_cfg.hic_local_rpc_endpoint);
	m0_module_fini(&hii->hii_module, M0_MODLEV_NONE);
	M0_LEAVE();
}

void m0_halon_interface_entrypoint_reply(
                struct m0_halon_interface  *hi,
                const struct m0_uint128    *req_id,
                int                         rc,
                int                         confd_fid_size,
                const struct m0_fid        *confd_fid_data,
                int                         confd_eps_size,
                const char                **confd_eps_data,
                const struct m0_fid        *rm_fid,
                const char                 *rm_eps)
{
}

void m0_halon_interface_send(struct m0_halon_interface *hi,
                             struct m0_ha_link         *hl,
                             struct m0_ha_msg          *msg,
                             uint64_t                  *tag)
{
}

void m0_halon_interface_delivered(struct m0_halon_interface *hi,
                                  struct m0_ha_link         *hl,
                                  struct m0_ha_msg          *msg)
{
}

void m0_halon_interface_disconnect(struct m0_halon_interface *hi,
                                   struct m0_ha_link         *hl)
{
}

struct m0_rpc_machine *
m0_halon_interface_rpc_machine(struct m0_halon_interface *hi)
{
	struct m0_halon_interface_internal *hii = hi->hif_internal;

	M0_PRE(m0_halon_interface_internal_bob_check(hii));
	M0_PRE(m0_get() == &hii->hii_instance);

	return &hii->hii_rpc_machine;
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
