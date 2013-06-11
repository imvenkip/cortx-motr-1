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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 *                  Subhash Arya  <subhash_arya@xyratex.com>
 * Original creation date: 03/08/2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CM

#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/trace.h"
#include "lib/time.h"

#include "rpc/rpc.h"
#include "rpc/session.h"
#include "mero/magic.h"
#include "mero/setup.h" /* CS_MAX_EP_ADDR_LEN */
#include "fop/fom.h"

#include "cm/cm.h"
#include "cm/cp.h"
#include "cm/proxy.h"

/**
   @addtogroup CM

   @{
 */

M0_TL_DESCR_DEFINE(proxy, "copy machine proxy", M0_INTERNAL,
		   struct m0_cm_proxy, px_linkage, px_magic,
		   CM_PROXY_LINK_MAGIC, CM_PROXY_HEAD_MAGIC);

M0_TL_DEFINE(proxy, M0_INTERNAL, struct m0_cm_proxy);

M0_TL_DESCR_DEFINE(proxy_cp, "pending copy packets", M0_INTERNAL,
		   struct m0_cm_cp, c_cm_proxy_linkage, c_magix,
		   CM_CP_MAGIX, CM_PROXY_CP_HEAD_MAGIX);

M0_TL_DEFINE(proxy_cp, M0_INTERNAL, struct m0_cm_cp);

static const struct m0_bob_type proxy_bob = {
	.bt_name = "cm proxy",
	.bt_magix_offset = M0_MAGIX_OFFSET(struct m0_cm_proxy, px_magic),
	.bt_magix = CM_PROXY_LINK_MAGIC,
	.bt_check = NULL
};

M0_BOB_DEFINE(static, &proxy_bob, m0_cm_proxy);

static bool cm_proxy_invariant(const struct m0_cm_proxy *pxy)
{
	/**
	 * @todo : Add checks for pxy::px_id when uid mechanism is implemented.
	 */
	return pxy != NULL &&  m0_cm_proxy_bob_check(pxy) &&
	       pxy->px_endpoint != NULL &&
	       (m0_cm_ag_id_cmp(&pxy->px_sw.sw_hi, &pxy->px_sw.sw_lo) >= 0);
}

M0_INTERNAL int m0_cm_proxy_alloc(uint64_t px_id,
				  struct m0_cm_ag_id *lo,
                                  struct m0_cm_ag_id *hi,
				  const char *endpoint,
                                  struct m0_cm_proxy **pxy)
{
	struct m0_cm_proxy *proxy;

	M0_PRE(pxy != NULL && lo != NULL && hi != NULL && endpoint != NULL);

	M0_ALLOC_PTR(proxy);
	if (proxy == NULL)
		return -ENOMEM;

	m0_cm_proxy_bob_init(proxy);

	proxy->px_id = px_id;
	proxy->px_sw.sw_lo = *lo;
	proxy->px_sw.sw_hi = *hi;
	proxy->px_endpoint = endpoint;
	proxy_tlink_init(proxy);
	m0_mutex_init(&proxy->px_mutex);
	proxy_cp_tlist_init(&proxy->px_pending_cps);
	*pxy = proxy;
	M0_POST(cm_proxy_invariant(*pxy));
	return 0;
}

M0_INTERNAL void m0_cm_proxy_add(struct m0_cm *cm, struct m0_cm_proxy *pxy)
{
	M0_PRE(m0_cm_is_locked(cm));
	M0_PRE(!proxy_tlink_is_in(pxy));
	pxy->px_cm = cm;
	proxy_tlist_add_tail(&cm->cm_proxies, pxy);
	M0_CNT_INC(cm->cm_proxy_nr);
	M0_ASSERT(proxy_tlink_is_in(pxy));
	M0_POST(cm_proxy_invariant(pxy));
}

M0_INTERNAL void m0_cm_proxy_del(struct m0_cm *cm, struct m0_cm_proxy *pxy)
{
	M0_PRE(m0_cm_is_locked(cm));
	M0_PRE(proxy_tlink_is_in(pxy));
	proxy_tlist_del(pxy);
	M0_CNT_DEC(cm->cm_proxy_nr);
	M0_ASSERT(!proxy_tlink_is_in(pxy));
	M0_POST(cm_proxy_invariant(pxy));
}

M0_INTERNAL void m0_cm_proxy_cp_add(struct m0_cm_proxy *pxy,
				    struct m0_cm_cp *cp)
{
	m0_mutex_lock(&pxy->px_mutex);
	M0_PRE(!proxy_cp_tlink_is_in(cp));
	proxy_cp_tlist_add_tail(&pxy->px_pending_cps, cp);
	M0_LOG(M0_DEBUG, "proxy: [%s] ag_id: [%lu] [%lu] [%lu] [%lu]",
	       pxy->px_endpoint,
	       cp->c_ag->cag_id.ai_hi.u_hi, cp->c_ag->cag_id.ai_hi.u_lo,
               cp->c_ag->cag_id.ai_lo.u_hi, cp->c_ag->cag_id.ai_lo.u_lo);
	M0_POST(proxy_cp_tlink_is_in(cp));
	m0_mutex_unlock(&pxy->px_mutex);
}

static void cm_proxy_cp_del(struct m0_cm_proxy *pxy,
			    struct m0_cm_cp *cp)
{
	M0_PRE(m0_mutex_is_locked(&pxy->px_mutex));
	M0_PRE(proxy_cp_tlink_is_in(cp));
	proxy_cp_tlist_del(cp);
	M0_POST(!proxy_cp_tlink_is_in(cp));
}

M0_INTERNAL struct m0_cm_proxy *m0_cm_proxy_locate(struct m0_cm *cm,
                                                   const char *ep)
{
	struct m0_cm_proxy *pxy;

	m0_tl_for(proxy, &cm->cm_proxies, pxy) {
		if(strncmp(pxy->px_endpoint, ep, CS_MAX_EP_ADDR_LEN) == 0) {
			M0_ASSERT(cm_proxy_invariant(pxy));
			return pxy;
		}
	} m0_tl_endfor;

	return NULL;
}

static void __wake_up_pending_cps(struct m0_cm_proxy *pxy)
{
	struct m0_cm_cp *cp;

	m0_tl_for(proxy_cp, &pxy->px_pending_cps, cp) {
		cm_proxy_cp_del(pxy, cp);
		/* wakeup pending copy packet foms */
		m0_fom_wakeup(&cp->c_fom);
	} m0_tl_endfor;

}

M0_INTERNAL void m0_cm_proxy_update(struct m0_cm_proxy *pxy,
				    struct m0_cm_ag_id *lo,
				    struct m0_cm_ag_id *hi)
{
	M0_PRE(pxy != NULL && lo != NULL && hi != NULL);

	m0_mutex_lock(&pxy->px_mutex);
	pxy->px_sw.sw_lo = *lo;
	pxy->px_sw.sw_hi = *hi;
        M0_LOG(M0_DEBUG, "proxy [%s] lo [%lu] [%lu] [%lu] [%lu]",
	       pxy->px_endpoint,
               pxy->px_sw.sw_lo.ai_hi.u_hi, pxy->px_sw.sw_lo.ai_hi.u_lo,
               pxy->px_sw.sw_lo.ai_lo.u_hi, pxy->px_sw.sw_lo.ai_lo.u_lo);
        M0_LOG(M0_DEBUG, "proxy [%s] hi [%lu] [%lu] [%lu] [%lu]",
	       pxy->px_endpoint,
               pxy->px_sw.sw_hi.ai_hi.u_hi, pxy->px_sw.sw_hi.ai_hi.u_lo,
               pxy->px_sw.sw_hi.ai_lo.u_hi, pxy->px_sw.sw_hi.ai_lo.u_lo);
	__wake_up_pending_cps(pxy);
	M0_ASSERT(cm_proxy_invariant(pxy));
	m0_mutex_unlock(&pxy->px_mutex);
}

M0_INTERNAL int m0_cm_proxy_remote_update(struct m0_cm_proxy *proxy,
					  struct m0_cm_sw *sw)
{
	struct m0_cm            *cm;
        struct m0_rpc_machine   *rmach;
        struct m0_rpc_conn      *conn;
        struct m0_fop           *fop;
        m0_time_t                deadline;
        const char              *ep;
	int                      rc;

	M0_ENTRY("proxy: %p", proxy);
	M0_PRE(proxy != NULL);
	cm = proxy->px_cm;
	M0_PRE(m0_cm_is_locked(cm));

        rmach = proxy->px_conn.c_rpc_machine;
        ep = rmach->rm_tm.ntm_ep->nep_addr;
        conn = &proxy->px_conn;
        fop = cm->cm_ops->cmo_sw_update_fop_alloc(cm, sw, ep);
        if (fop == NULL)
                return -ENOMEM;
        deadline = m0_time(1, 0);
	M0_LOG(M0_DEBUG, "Sending to %s hi: [%lu] [%lu] [%lu] [%lu]",
		ep, sw->sw_hi.ai_hi.u_hi, sw->sw_hi.ai_hi.u_lo,
		sw->sw_hi.ai_lo.u_hi, sw->sw_hi.ai_lo.u_lo);
	M0_LOG(M0_DEBUG, "proxy last updated  hi: [%lu] [%lu] [%lu] [%lu]",
		proxy->px_last_sw_update_sent.sw_hi.ai_hi.u_hi,
		proxy->px_last_sw_update_sent.sw_hi.ai_hi.u_lo,
		proxy->px_last_sw_update_sent.sw_hi.ai_lo.u_hi,
		proxy->px_last_sw_update_sent.sw_hi.ai_lo.u_lo);

        rc = m0_cm_sw_update_fop_post(fop, conn, deadline);
        m0_sm_group_lock(&rmach->rm_sm_grp);
        m0_fop_put(fop);
        m0_sm_group_unlock(&rmach->rm_sm_grp);
	m0_cm_sw_copy(&proxy->px_last_sw_update_sent, sw);

	return rc;
}

M0_INTERNAL void m0_cm_proxy_rpc_conn_close(struct m0_cm_proxy *pxy)
{
	int rc;

	rc = m0_rpc_session_destroy(&pxy->px_session, M0_TIME_NEVER);
	if (rc != 0)
		M0_LOG(M0_ERROR, "Failed to terminate session %d", rc);

	rc = m0_rpc_conn_destroy(&pxy->px_conn, M0_TIME_NEVER);
	if (rc != 0)
		M0_LOG(M0_ERROR, "Failed to terminate connection %d", rc);
}

M0_INTERNAL void m0_cm_proxy_fini(struct m0_cm_proxy *pxy)
{
	M0_PRE(pxy != NULL);
	M0_PRE(proxy_cp_tlist_is_empty(&pxy->px_pending_cps));
	proxy_cp_tlist_fini(&pxy->px_pending_cps);
	proxy_tlink_del_fini(pxy);
	M0_ASSERT(!proxy_tlink_is_in(pxy));
	m0_cm_proxy_bob_fini(pxy);
	m0_cm_proxy_rpc_conn_close(pxy);
	m0_mutex_fini(&pxy->px_mutex);
}

M0_INTERNAL uint64_t m0_cm_proxy_nr(struct m0_cm *cm)
{
	M0_PRE(m0_cm_is_locked(cm));

	return proxy_tlist_length(&cm->cm_proxies);
}

M0_INTERNAL bool m0_cm_proxy_agid_is_in_sw(struct m0_cm_proxy *pxy,
					   struct m0_cm_ag_id *id)
{
	bool result;

	m0_mutex_lock(&pxy->px_mutex);
	result =  m0_cm_ag_id_cmp(id, &pxy->px_sw.sw_lo) >= 0 &&
		  m0_cm_ag_id_cmp(id, &pxy->px_sw.sw_hi) <= 0;
	m0_mutex_unlock(&pxy->px_mutex);

	return result;
}

#undef M0_TRACE_SUBSYSTEM

/** @} CM */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
