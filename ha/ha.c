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
 * @{
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_HA
#include "lib/trace.h"

#include "ha/ha.h"

#include "lib/memory.h"         /* M0_ALLOC_PTR */
#include "lib/errno.h"          /* ENOMEM */
#include "lib/misc.h"           /* M0_IS0 */
#include "module/instance.h"    /* m0_get */
#include "rpc/rpclib.h"         /* m0_rpc_client_connect */
#include "ha/link.h"            /* m0_ha_link */
#include "ha/link_service.h"    /* m0_ha_link_service_init */

#include "ha/note.h"            /* XXX m0_ha_state_init */

struct m0_ha_module {
	int unused;
};

M0_INTERNAL int m0_ha_init(struct m0_ha *ha, struct m0_ha_cfg *ha_cfg)
{
	int rc;

	M0_ENTRY("ha=%p hcf_rpc_machine=%p hcf_reqh=%p",
	         ha, ha_cfg->hcf_rpc_machine, ha_cfg->hcf_reqh);
	M0_PRE(M0_IS0(ha));
	ha->h_cfg = *ha_cfg;
	rc = m0_ha_link_service_init(&ha->h_hl_service, ha->h_cfg.hcf_reqh);
	M0_ASSERT(rc == 0);
	return M0_RC(0);
}

M0_INTERNAL void m0_ha_fini(struct m0_ha *ha)
{
	M0_ENTRY("ha=%p", ha);
	m0_ha_link_service_fini(ha->h_hl_service);
	M0_LEAVE();
}

M0_INTERNAL struct m0_ha_link *m0_ha_connect(struct m0_ha *ha,
                                             const char   *ep)
{
	struct m0_ha_link     *hl;
	int                    rc;
	struct m0_ha_link_cfg  hl_cfg = {
		.hlc_reqh           = ha->h_cfg.hcf_reqh,
		.hlc_reqh_service   = ha->h_hl_service,
		.hlc_rpc_session    = &ha->h_rpc_session,
		.hlc_link_id_local  = M0_UINT128(0, 1),
		.hlc_link_id_remote = M0_UINT128(0, 2),
		.hlc_q_in_cfg       = {},
		.hlc_q_out_cfg      = {},
		.hlc_tag_even       = true,
	};

	M0_ENTRY("ha=%p ep=%s", ha, ep);
	M0_ALLOC_PTR(hl);
	M0_ASSERT(hl != NULL);

	rc = m0_rpc_client_connect(&ha->h_rpc_conn, &ha->h_rpc_session,
				   ha->h_cfg.hcf_rpc_machine,
				   ep, /* XXX */ NULL,
				   2 /* XXX MAX_RPCS_IN_FLIGHT */);
	M0_ASSERT(rc == 0);

	rc = m0_ha_link_init(hl, &hl_cfg);
	M0_ASSERT(rc == 0);
	m0_ha_link_start(hl);
	rc = m0_ha_state_init(&ha->h_rpc_session);
	M0_ASSERT(rc == 0);
	M0_LEAVE("hl=%p", hl);
	return hl;
}

M0_INTERNAL void m0_ha_disconnect(struct m0_ha      *ha,
                                  struct m0_ha_link *hl)
{
	int rc;

	M0_ENTRY("ha=%p hl=%p", ha, hl);
	m0_ha_state_fini();
	m0_ha_link_stop(hl);
	m0_ha_link_fini(hl);
	m0_free(hl);
	rc = m0_rpc_session_destroy(&ha->h_rpc_session, M0_TIME_NEVER);
	M0_ASSERT(rc == 0);
	rc = m0_rpc_conn_destroy(&ha->h_rpc_conn, M0_TIME_NEVER);
	M0_ASSERT(rc == 0);
	M0_LEAVE();
}

M0_INTERNAL int m0_ha_mod_init(void)
{
	struct m0_ha_module *ha_module;
	int                  rc;

	M0_PRE(m0_get()->i_ha_module == NULL);
	M0_ALLOC_PTR(ha_module);
	if (ha_module == NULL)
		return -ENOMEM;
	m0_get()->i_ha_module = ha_module;
	rc = m0_ha_link_mod_init();
	M0_ASSERT(rc == 0);
	rc = m0_ha_link_service_mod_init();
	M0_ASSERT(rc == 0);
	M0_LOG(M0_DEBUG, "i_ha_module=%p", ha_module);
	return 0;
}

M0_INTERNAL void m0_ha_mod_fini(void)
{
	M0_PRE(m0_get()->i_ha_module != NULL);

	m0_ha_link_service_mod_fini();
	m0_ha_link_mod_fini();
	m0_free(m0_get()->i_ha_module);
	m0_get()->i_ha_module = NULL;
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
