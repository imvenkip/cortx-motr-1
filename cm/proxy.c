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
#include "lib/misc.h"
#include "lib/locality.h"

#include "rpc/rpc.h"
#include "rpc/session.h"
#include "mero/magic.h"
#include "mero/setup.h" /* CS_MAX_EP_ADDR_LEN */
#include "fop/fom.h"

#include "cm/cm.h"
#include "cm/cp.h"
#include "cm/proxy.h"
#include "cm/ag.h"

/**
   @addtogroup CMPROXY

   @{
 */

enum {
	PROXY_WAIT = 1
};

M0_TL_DESCR_DEFINE(proxy, "copy machine proxy", M0_INTERNAL,
		   struct m0_cm_proxy, px_linkage, px_magic,
		   CM_PROXY_LINK_MAGIC, CM_PROXY_HEAD_MAGIC);

M0_TL_DEFINE(proxy, M0_INTERNAL, struct m0_cm_proxy);

M0_TL_DESCR_DEFINE(proxy_fail, "copy machine proxy", M0_INTERNAL,
		   struct m0_cm_proxy, px_fail_linkage, px_magic,
		   CM_PROXY_LINK_MAGIC, CM_PROXY_HEAD_MAGIC);

M0_TL_DEFINE(proxy_fail, M0_INTERNAL, struct m0_cm_proxy);

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
	return _0C(pxy != NULL) &&  _0C(m0_cm_proxy_bob_check(pxy)) &&
	       _0C(m0_cm_is_locked(pxy->px_cm)) &&
	       _0C(pxy->px_endpoint != NULL);
}

M0_INTERNAL int m0_cm_proxy_init(struct m0_cm_proxy *proxy, uint64_t px_id,
				 struct m0_cm_ag_id *lo, struct m0_cm_ag_id *hi,
				 const char *endpoint)
{
	M0_PRE(proxy != NULL && lo != NULL && hi != NULL && endpoint != NULL);

	m0_cm_proxy_bob_init(proxy);
	proxy_tlink_init(proxy);
	proxy_fail_tlink_init(proxy);
	m0_mutex_init(&proxy->px_mutex);
	proxy_cp_tlist_init(&proxy->px_pending_cps);
	proxy->px_id = px_id;
	proxy->px_sw.sw_lo = *lo;
	proxy->px_sw.sw_hi = *hi;
	proxy->px_endpoint = endpoint;
	proxy->px_is_done = false;
	return 0;
}

M0_INTERNAL void m0_cm_proxy_add(struct m0_cm *cm, struct m0_cm_proxy *pxy)
{
	M0_ENTRY("cm: %p proxy: %p", cm, pxy);
	M0_PRE(m0_cm_is_locked(cm));
	M0_PRE(!proxy_tlink_is_in(pxy));
	pxy->px_cm = cm;
	proxy_tlist_add_tail(&cm->cm_proxies, pxy);
	M0_CNT_INC(cm->cm_proxy_nr);
	M0_ASSERT(proxy_tlink_is_in(pxy));
	M0_POST(cm_proxy_invariant(pxy));
	M0_LEAVE();
}

M0_INTERNAL void m0_cm_proxy_del(struct m0_cm *cm, struct m0_cm_proxy *pxy)
{
	M0_ENTRY("cm: %p proxy: %p", cm, pxy);
	M0_PRE(m0_cm_is_locked(cm));
	M0_PRE(proxy_tlink_is_in(pxy));
	if (proxy_fail_tlink_is_in(pxy))
		proxy_fail_tlist_del(pxy);
	proxy_fail_tlink_fini(pxy);
	proxy_tlink_del_fini(pxy);
	M0_ASSERT(!proxy_tlink_is_in(pxy));
	M0_POST(cm_proxy_invariant(pxy));
	M0_LEAVE();
}

M0_INTERNAL void m0_cm_proxy_cp_add(struct m0_cm_proxy *pxy,
				    struct m0_cm_cp *cp)
{
	M0_ENTRY("proxy: %p cp: %p ep: %s", pxy, cp, pxy->px_endpoint);
	m0_mutex_lock(&pxy->px_mutex);
	M0_PRE(!proxy_cp_tlink_is_in(cp));
	proxy_cp_tlist_add_tail(&pxy->px_pending_cps, cp);
	ID_LOG("proxy ag_id", &cp->c_ag->cag_id);
	M0_POST(proxy_cp_tlink_is_in(cp));
	m0_mutex_unlock(&pxy->px_mutex);
	M0_LEAVE();
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
	return m0_tl_find(proxy, pxy, &cm->cm_proxies,
			  strncmp(pxy->px_endpoint, ep,
				  CS_MAX_EP_ADDR_LEN) == 0);
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
				    struct m0_cm_ag_id *hi,
				    struct m0_cm_ag_id *last_out,
				    uint32_t px_status,
				    m0_time_t px_epoch)
{
	struct m0_cm    *cm;
	struct m0_cm_sw  sw;

	M0_ENTRY("proxy: %p lo: %p hi: %p ep: %s", pxy, lo, hi,
		 pxy->px_endpoint);
	M0_PRE(pxy != NULL && lo != NULL && hi != NULL);

	cm = pxy->px_cm;
        M0_LOG(M0_DEBUG, "Recvd from :%s status: %u curr_status: %u"
			 "nr_updates: %u", pxy->px_endpoint, px_status,
			 pxy->px_status, (unsigned)cm->cm_proxy_init_updated);

	m0_mutex_lock(&pxy->px_mutex);
	switch (pxy->px_status) {
	case M0_PX_INIT :
		if (px_status == M0_PX_READY && m0_cm_is_ready(cm)) {
			pxy->px_epoch = px_epoch;
			/*
			 * Here we select the minimum of the sliding window
			 * starting point provided by each remote copy machine,
			 * from which this copy machine will start in-order to
			 * keep all the copy machines in sync.
			 */
			if (m0_cm_ag_id_is_set(hi) &&
			    ((m0_cm_ag_id_cmp(hi, &cm->cm_sw_last_updated_hi) < 0) ||
			    !m0_cm_ag_id_is_set(&cm->cm_sw_last_updated_hi))) {
				cm->cm_sw_last_updated_hi = *hi;
			}
			M0_CNT_INC(cm->cm_proxy_init_updated);
			/*
			 * Notify copy machine on receiving initial updates
			 * from all the remote replicas.
			 */
			if (cm->cm_proxy_init_updated == cm->cm_proxy_nr)
				m0_cm_notify(cm);
			M0_SET0(&sw);
			sw.sw_hi = cm->cm_sw_last_persisted_hi;
			pxy->px_status = px_status;
		}
		break;
	case M0_PX_READY:
	case M0_PX_ACTIVE:
	case M0_PX_COMPLETE:
	case M0_PX_STOP:
		if (px_epoch != pxy->px_epoch) {
			M0_LOG(M0_WARN, "Mismatch Epoch,"
			       "current: %llu" "received: %llu",
			       (unsigned long long)pxy->px_epoch,
			       (unsigned long long)px_epoch);
			break;
		}
		pxy->px_status = px_status;
		if (M0_IN(px_status, (M0_PX_ACTIVE, M0_PX_COMPLETE, M0_PX_STOP))) {
			pxy->px_sw.sw_lo = *lo;
			pxy->px_sw.sw_hi = *hi;
			pxy->px_last_out_recvd = *last_out;
			ID_LOG("proxy lo", &pxy->px_sw.sw_lo);
			ID_LOG("proxy hi", &pxy->px_sw.sw_hi);
			__wake_up_pending_cps(pxy);
			if ((cm->cm_abort || cm->cm_quiesce) &&
			    M0_IN(px_status, (M0_PX_COMPLETE, M0_PX_STOP))) {
				m0_cm_frozen_ag_cleanup(cm, pxy);
			}
			M0_ASSERT(cm_proxy_invariant(pxy));
		}
		break;
	default:
		break;
	}
	m0_mutex_unlock(&pxy->px_mutex);

	M0_LEAVE();
}

static void proxy_sw_onwire_ast_cb(struct m0_sm_group *grp,
				   struct m0_sm_ast *ast)
{
	struct m0_cm_proxy      *proxy = container_of(ast, struct m0_cm_proxy,
						      px_sw_onwire_ast);
	struct m0_cm            *cm    = proxy->px_cm;
	struct m0_cm_aggr_group *lo;
	struct m0_cm_aggr_group *hi;
	struct m0_cm_ag_id       id_lo;
	struct m0_cm_ag_id       id_hi;
	struct m0_cm_sw          sw;

	M0_ASSERT(cm_proxy_invariant(proxy));

	M0_SET0(&id_lo);
	M0_SET0(&id_hi);
	lo = m0_cm_ag_lo(cm);
	if (lo != NULL)
		id_lo = lo->cag_id;
	hi = m0_cm_ag_hi(cm);
	if (hi != NULL)
		id_hi = hi->cag_id;
	if (m0_cm_is_ready(cm))
		id_hi = cm->cm_sw_last_updated_hi;
	m0_cm_sw_set(&sw, &id_lo, &id_hi);
	M0_LOG(M0_DEBUG, "proxy ep: %s, cm->cm_aggr_grps_in_nr %lu",
			 proxy->px_endpoint, cm->cm_aggr_grps_in_nr);
	ID_LOG("proxy last updated hi", &proxy->px_last_sw_onwire_sent.sw_hi);

	if (!cm->cm_done || !M0_IN(proxy->px_status, (M0_PX_STOP, M0_PX_FAILED))) {
		m0_cm_proxy_remote_update(proxy, &sw);
	} else {
		proxy->px_is_done = true;
		if (proxy->px_status != M0_PX_FAILED) {
			/* Send one final notification to the proxy. */
			m0_cm_proxy_remote_update(proxy, &sw);
		}
		M0_CNT_DEC(cm->cm_proxy_nr);
	}
	/*
	 * Handle service/node failure during sns-repair/rebalance.
	 * Cannot send updates to dead proxy, all the aggregation groups,
	 * frozen on that proxy must be destroyed.
	 */
	if (proxy->px_status == M0_PX_FAILED) {
		m0_mutex_lock(&proxy->px_mutex);
		__wake_up_pending_cps(proxy);
		m0_mutex_unlock(&proxy->px_mutex);
		m0_cm_fail(cm, -EHOSTDOWN);
		m0_cm_abort(cm);
		m0_cm_frozen_ag_cleanup(cm, proxy);
		m0_cm_notify(cm);
	}
}

static void proxy_sw_onwire_item_sent_cb(struct m0_rpc_item *item)
{
	struct m0_cm_proxy_sw_onwire *swu_fop;
	struct m0_cm_proxy           *proxy;

	M0_ENTRY("%p", item);

	swu_fop = M0_AMB(swu_fop, m0_rpc_item_to_fop(item), pso_fop);
	proxy = swu_fop->pso_proxy;
	M0_ASSERT(m0_cm_proxy_bob_check(proxy));
	proxy->px_sw_onwire_ast.sa_cb = proxy_sw_onwire_ast_cb;
	M0_LOG(M0_DEBUG, "Posting ast for %s", proxy->px_endpoint);
	m0_sm_ast_post(&proxy->px_cm->cm_sm_group, &proxy->px_sw_onwire_ast);
	m0_cm_ast_run_fom_wakeup(proxy->px_cm);

	M0_LEAVE();
}

const struct m0_rpc_item_ops proxy_sw_onwire_item_ops = {
	.rio_sent = proxy_sw_onwire_item_sent_cb
};

static void cm_proxy_sw_onwire_post(const struct m0_cm_proxy *proxy,
				    struct m0_fop *fop,
				    const struct m0_rpc_conn *conn,
				    m0_time_t deadline)
{
	struct m0_rpc_item *item;

	M0_ENTRY("fop: %p conn: %p", fop, conn);
	M0_PRE(fop != NULL && conn != NULL);

	item              = m0_fop_to_rpc_item(fop);
	item->ri_ops      = m0_cm_proxy_is_done(proxy) ? NULL :
			    &proxy_sw_onwire_item_ops;
	item->ri_prio     = M0_RPC_ITEM_PRIO_MID;
	item->ri_deadline = deadline;

	m0_rpc_oneway_item_post(conn, item);
}

static void proxy_sw_onwire_release(struct m0_ref *ref)
{
	struct m0_cm_proxy_sw_onwire  *pso_fop;
	struct m0_fop                     *fop;

	fop = container_of(ref, struct m0_fop, f_ref);
	pso_fop = container_of(fop, struct m0_cm_proxy_sw_onwire, pso_fop);
	M0_ASSERT(pso_fop != NULL);
	m0_fop_fini(fop);
	m0_free(pso_fop);
}

M0_INTERNAL int m0_cm_proxy_remote_update(struct m0_cm_proxy *proxy,
					  struct m0_cm_sw *sw)
{
	struct m0_cm                 *cm;
	struct m0_rpc_machine        *rmach;
	struct m0_rpc_conn           *conn;
	struct m0_cm_proxy_sw_onwire *sw_fop;
	struct m0_fop                *fop;
	m0_time_t                     deadline;
	const char                   *ep;
	int                           rc;

	M0_ENTRY("proxy: %p sw: %p", proxy, sw);
	M0_PRE(proxy != NULL);
	cm = proxy->px_cm;
	M0_PRE(m0_cm_is_locked(cm));

	M0_ALLOC_PTR(sw_fop);
	if (sw_fop == NULL)
		return M0_ERR(-ENOMEM);
	fop = &sw_fop->pso_fop;
	rmach = proxy->px_conn->c_rpc_machine;
	ep = rmach->rm_tm.ntm_ep->nep_addr;
	conn = proxy->px_conn;
	rc = cm->cm_ops->cmo_sw_onwire_fop_setup(cm, fop,
						 proxy_sw_onwire_release,
						 ep, sw, &cm->cm_last_out_hi);
	if (rc != 0) {
		m0_fop_put_lock(fop);
		m0_free(sw_fop);
		return M0_ERR(rc);
	}
	sw_fop->pso_proxy = proxy;
	deadline = m0_time_from_now(1, 0);
	ID_LOG("proxy last updated hi", &proxy->px_last_sw_onwire_sent.sw_hi);

	cm_proxy_sw_onwire_post(proxy, fop, conn, deadline);
	m0_fop_put_lock(fop);
	m0_cm_sw_copy(&proxy->px_last_sw_onwire_sent, sw);
	M0_LOG(M0_DEBUG, "Sending to %s hi: ["M0_AG_F"]",
	       proxy->px_endpoint, M0_AG_P(&sw->sw_hi));
	return M0_RC(0);
}

M0_INTERNAL bool m0_cm_proxy_is_done(const struct m0_cm_proxy *pxy)
{
	return pxy->px_is_done;
}

M0_INTERNAL void m0_cm_proxy_fini(struct m0_cm_proxy *pxy)
{
	M0_ENTRY("%p", pxy);
	M0_PRE(pxy != NULL);
	M0_PRE(proxy_cp_tlist_is_empty(&pxy->px_pending_cps));
	proxy_cp_tlist_fini(&pxy->px_pending_cps);
	m0_cm_proxy_bob_fini(pxy);
	if (m0_clink_is_armed(&pxy->px_ha_link)) {
		m0_clink_del_lock(&pxy->px_ha_link);
		m0_clink_fini(&pxy->px_ha_link);
	}
	m0_mutex_fini(&pxy->px_mutex);
	M0_LEAVE();
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

M0_INTERNAL void m0_cm_proxy_pending_cps_wakeup(struct m0_cm *cm)
{
	struct m0_cm_proxy *pxy;

	m0_tl_for(proxy, &cm->cm_proxies, pxy) {
		__wake_up_pending_cps(pxy);
	} m0_tl_endfor;
}

static bool proxy_clink_cb(struct m0_clink *clink)
{
	struct m0_cm_proxy *pxy = M0_AMB(pxy, clink, px_ha_link);
	struct m0_conf_obj *svc_obj = container_of(clink->cl_chan,
						   struct m0_conf_obj,
						   co_ha_chan);

	M0_PRE(m0_conf_obj_type(svc_obj) == &M0_CONF_SERVICE_TYPE);

	if (M0_IN(svc_obj->co_ha_state, (M0_NC_FAILED, M0_NC_TRANSIENT))) {
		m0_mutex_lock(&pxy->px_mutex);
		pxy->px_status = M0_PX_FAILED;
		proxy_fail_tlist_add_tail(&pxy->px_cm->cm_failed_proxies, pxy);
		m0_mutex_unlock(&pxy->px_mutex);
	} else if (svc_obj->co_ha_state == M0_NC_ONLINE &&
		   pxy->px_status == M0_PX_FAILED) {
		/* XXX Need to check repair/rebalance status. */
		pxy->px_status = M0_PX_INIT;
		pxy->px_is_done = false;
	}

	return true;
}

M0_INTERNAL void m0_cm_proxy_event_handle_register(struct m0_cm_proxy *pxy,
						   struct m0_conf_obj *svc_obj)
{
	m0_clink_init(&pxy->px_ha_link, proxy_clink_cb);
	m0_clink_add_lock(&svc_obj->co_ha_chan, &pxy->px_ha_link);
}

#undef M0_TRACE_SUBSYSTEM

/** @} CMPROXY */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
