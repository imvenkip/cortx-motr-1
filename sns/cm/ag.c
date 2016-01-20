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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 12/09/2012
 * Revision: Anup Barve <anup_barve@xyratex.com>
 * Revision date: 08/05/2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/trace.h"

#include "lib/memory.h"
#include "lib/errno.h"
#include "lib/misc.h"

#include "fid/fid.h"
#include "ioservice/io_service.h" /* m0_ios_cdom_get */
#include "reqh/reqh.h"
#include "sns/parity_repair.h"

#include "sns/cm/cm_utils.h"
#include "sns/cm/ag.h"
#include "sns/cm/cp.h"
#include "sns/cm/cm.h"
#include "sns/cm/iter.h"
#include "sns/cm/file.h"
#include "cm/proxy.h"

/**
   @addtogroup SNSCMAG

   @{
 */

enum ag_iter_state {
	AIS_FID_NEXT,
	AIS_FID_LOCK,
	AIS_FID_ATTR,
	AIS_GROUP_NEXT,
	AIS_FINI,
	AIS_NR
};

static struct m0_sm_state_descr ai_sd[AIS_NR] = {
	[AIS_FID_LOCK] = {
		.sd_flags   = M0_SDF_INITIAL,
		.sd_name    = "ag iter fid lock",
		.sd_allowed = M0_BITS(AIS_FID_ATTR, AIS_FID_NEXT, AIS_FINI)
	},
	[AIS_FID_NEXT] = {
		.sd_flags   = 0,
		.sd_name    = "ag iter fid next",
		.sd_allowed = M0_BITS(AIS_FID_LOCK, AIS_FINI)
	},
	[AIS_FID_ATTR] = {
		.sd_flags   = 0,
		.sd_name    = "ag iter fid attr",
		.sd_allowed = M0_BITS(AIS_GROUP_NEXT, AIS_FID_LOCK, AIS_FINI)
	},
	[AIS_GROUP_NEXT] = {
		.sd_flags   = 0,
		.sd_name   = "ag iter group next",
		.sd_allowed = M0_BITS(AIS_FID_NEXT, AIS_FID_LOCK, AIS_FINI)
	},
	[AIS_FINI] = {
		.sd_flags   = M0_SDF_TERMINAL,
		.sd_name    = "ag iter fini",
		.sd_allowed = 0
	}
};

static const struct m0_sm_conf ai_sm_conf = {
	.scf_name      = "sm: ai_conf",
	.scf_nr_states = ARRAY_SIZE(ai_sd),
	.scf_state     = ai_sd
};

static enum ag_iter_state ai_state(struct m0_sns_cm_ag_iter *ai)
{
	return ai->ai_sm.sm_state;
}

static struct m0_sns_cm *ai2sns(struct m0_sns_cm_ag_iter *ai)
{
	return container_of(ai, struct m0_sns_cm, sc_ag_it);
}

static void ai_state_set(struct m0_sns_cm_ag_iter *ai, int state)
{
	m0_sm_state_set(&ai->ai_sm, state);
}

static bool _is_fid_valid(struct m0_sns_cm_ag_iter *ai, struct m0_fid *fid)
{
	struct m0_fid         fid_out = {0, 0};
	struct m0_cob_domain *cdom = ai->ai_cdom;
	struct m0_sns_cm     *scm = ai2sns(ai);
	int                   rc;

	if (!m0_sns_cm_fid_is_valid(scm, fid))
		return false;
	rc = m0_cob_ns_next_of(&cdom->cd_namespace, fid, &fid_out);
	if (rc == 0 && m0_fid_eq(fid, &fid_out))
		return true;
	return false;
}

static int ai_group_next(struct m0_sns_cm_ag_iter *ai)
{
	struct m0_cm_ag_id        ag_id = {};
	struct m0_layout         *l = ai->ai_fctx->sf_layout;
	struct m0_pdclust_layout *pl = m0_layout_to_pdl(l);
	struct m0_sns_cm         *scm = ai2sns(ai);
	struct m0_cm             *cm = &scm->sc_base;
	struct m0_cm_aggr_group  *ag;
	struct m0_fom            *fom;
	struct m0_sns_cm_file_ctx *fctx = ai->ai_fctx;
	uint64_t                  fsize = fctx->sf_attr.ca_size;
	uint64_t                  nr_groups = m0_sns_cm_nr_groups(pl, fsize);
	uint64_t                  group = agid2group(&ai->ai_id_curr);
	uint64_t                  i;
	int                       rc = 0;

	if (m0_cm_ag_id_is_set(&ai->ai_id_curr))
		++group;
	fom = &fctx->sf_scm->sc_base.cm_sw_update.swu_fom;
	for (i = group; i < nr_groups; ++i) {
		m0_sns_cm_ag_agid_setup(&ai->ai_fid, i, &ag_id);
		if (!m0_sns_cm_ag_is_relevant(scm, fctx->sf_pm, pl,
					      fctx->sf_pi, &ag_id))
			continue;
		m0_net_buffer_pool_lock(&scm->sc_ibp.sb_bp);
		if (!cm->cm_ops->cmo_has_space(cm, &ag_id, l)) {
			m0_fom_wait_on(fom, &scm->sc_ibp.sb_wait, &fom->fo_cb);
			rc = -ENOBUFS;
		}
		m0_net_buffer_pool_unlock(&scm->sc_ibp.sb_bp);
		if (rc == 0) {
			ai->ai_id_next = ag_id;
			ag = m0_cm_aggr_group_locate(cm, &ag_id, true);
			if (ag == NULL) {
				rc = m0_cm_aggr_group_alloc(cm, &ag_id,
							    true, &ag);
				if (rc != 0)
					return M0_ERR(rc);
			}
			rc = M0_FSO_AGAIN;
		}
		return M0_RC(rc);
	}

	m0_mutex_lock(&scm->sc_file_ctx_mutex);
	m0_sns_cm_file_unlock(scm, &ai->ai_fid);
	m0_mutex_unlock(&scm->sc_file_ctx_mutex);
	ai_state_set(ai, AIS_FID_NEXT);

	return M0_RC(rc);
}

static int ai_fid_attr(struct m0_sns_cm_ag_iter *ai)
{
	struct m0_sns_cm_file_ctx *fctx = ai->ai_fctx;
	struct m0_fom             *fom;
	int                        rc;

	rc = m0_sns_cm_file_attr_and_layout(fctx);
	if (rc == -EAGAIN) {
		fom = &fctx->sf_scm->sc_base.cm_sw_update.swu_fom;
		m0_sns_cm_file_attr_and_layout_wait(fctx, fom);
		return M0_RC(M0_FSO_WAIT);
	}
	if (rc == 0)
		ai_state_set(ai, AIS_GROUP_NEXT);

	return M0_RC(rc);
}

static int __file_lock(struct m0_sns_cm *scm, const struct m0_fid *fid,
                       struct m0_sns_cm_file_ctx **fctx)
{
	struct m0_fom *fom;
	int            rc;

	fom = &scm->sc_base.cm_sw_update.swu_fom;
	rc = m0_sns_cm_file_lock(scm, fid, &fom->fo_loc->fl_group, fctx);
	if (rc == -EAGAIN) {
		M0_ASSERT(*fctx != NULL);
		rc = m0_sns_cm_file_lock_wait(*fctx, fom);
	}

	return M0_RC(rc);
}

static int ai_fid_lock(struct m0_sns_cm_ag_iter *ai)
{
	struct m0_sns_cm *scm = ai2sns(ai);
	int               rc;

	if (!_is_fid_valid(ai, &ai->ai_fid)) {
		ai_state_set(ai, AIS_FID_NEXT);
		return M0_RC(0);
	}

	m0_mutex_lock(&scm->sc_file_ctx_mutex);
	rc = __file_lock(scm, &ai->ai_fid, &ai->ai_fctx);
	m0_mutex_unlock(&scm->sc_file_ctx_mutex);

	if (rc == 0)
		ai_state_set(ai, AIS_FID_ATTR);
	return M0_RC(rc);
}

static int ai_fid_next(struct m0_sns_cm_ag_iter *ai)
{
	struct m0_fid     fid = {0, 0};
	struct m0_fid     fid_curr = ai->ai_fid;
	int               rc;

	do {
		M0_CNT_INC(fid_curr.f_key);
		rc = m0_cob_ns_next_of(&ai->ai_cdom->cd_namespace, &fid_curr,
				       &fid);
		fid_curr = fid;
	} while (rc == 0 &&
		 (m0_fid_eq(&fid, &M0_COB_ROOT_FID) ||
		  m0_fid_eq(&fid, &M0_MDSERVICE_SLASH_FID)));

	if (rc == 0) {
		M0_SET0(&ai->ai_id_curr);
		ai->ai_fid = fid;
		ai_state_set(ai, AIS_FID_LOCK);
	}
	return M0_RC(rc);
}

static int (*ai_action[])(struct m0_sns_cm_ag_iter *ai) = {
	[AIS_FID_NEXT]   = ai_fid_next,
	[AIS_FID_LOCK]   = ai_fid_lock,
	[AIS_FID_ATTR]   = ai_fid_attr,
	[AIS_GROUP_NEXT] = ai_group_next,
};

M0_INTERNAL int m0_sns_cm_ag__next(struct m0_sns_cm *scm,
				   const struct m0_cm_ag_id *id_curr,
				   struct m0_cm_ag_id *id_next)
{
	struct m0_sns_cm_ag_iter  *ai = &scm->sc_ag_it;
	struct m0_cm              *cm = &scm->sc_base;
	struct m0_sns_cm_file_ctx *fctx;
	struct m0_fid              fid;
	int                        rc;

	if (ai_state(ai) > AIS_GROUP_NEXT)
		return -ENOENT;
	ai->ai_id_curr = *id_curr;
	agid2fid(&ai->ai_id_curr, &fid);
	fctx = ai->ai_fctx;
	/*
	 * Reset ai->ai_id_curr if ag_next iterator has reached to higher fid
	 * through AIS_FID_NEXT than @id_curr in-order to start processing from
	 */
	if (m0_fid_cmp(&ai->ai_fid, &fid) > 0)
		M0_SET0(&ai->ai_id_curr);
	if (m0_fid_cmp(&ai->ai_fid, &fid) < 0) {
		if (fctx != NULL &&
		    m0_sns_cm_fctx_state_get(fctx) >= M0_SCFS_LOCK_WAIT) {
			m0_mutex_lock(&scm->sc_file_ctx_mutex);
			m0_sns_cm_file_unlock(scm, &ai->ai_fid);
			m0_mutex_unlock(&scm->sc_file_ctx_mutex);
		}
		ai->ai_fid = fid;
		if (ai_state(ai) != AIS_FID_LOCK)
			ai_state_set(ai, AIS_FID_LOCK);
	}

	do {
		if ((cm->cm_quiesce || cm->cm_abort) && (M0_IN(ai_state(ai),
							 (AIS_FID_LOCK,
							  AIS_FID_NEXT, AIS_GROUP_NEXT)))) {
			M0_LOG(M0_WARN, "%lu: Got %s cmd", cm->cm_id,
					 cm->cm_quiesce ? "QUIESCE" : "ABORT");
			return M0_RC(-ENODATA);
		}
		rc = ai_action[ai_state(ai)](ai);
	} while (rc == 0);

	if (rc == M0_FSO_AGAIN) {
		*id_next = ai->ai_id_next;
		rc = 0;
	}
	if (rc == -EAGAIN)
		rc = M0_FSO_WAIT;

	return M0_RC(rc);
}

M0_INTERNAL int m0_sns_cm_ag_iter_init(struct m0_sns_cm_ag_iter *ai)
{
	struct m0_sns_cm     *scm = ai2sns(ai);
	int                   rc;

	M0_SET0(ai);
	ai->ai_cdom = NULL;
	rc = m0_ios_cdom_get(m0_sns_cm2reqh(scm), &ai->ai_cdom);
	if (rc == 0)
	     m0_sm_init(&ai->ai_sm, &ai_sm_conf, AIS_FID_LOCK,
			&scm->sc_base.cm_sm_group);

	return M0_RC(rc);
}

M0_INTERNAL void m0_sns_cm_ag_iter_fini(struct m0_sns_cm_ag_iter *ai)
{
	ai_state_set(ai, AIS_FINI);
	m0_sm_fini(&ai->ai_sm);
}

M0_INTERNAL struct m0_cm *snsag2cm(const struct m0_sns_cm_ag *sag)
{
	return sag->sag_base.cag_cm;
}

M0_INTERNAL struct m0_sns_cm_ag *ag2snsag(const struct m0_cm_aggr_group *ag)
{
	return container_of(ag, struct m0_sns_cm_ag, sag_base);
}

M0_INTERNAL void m0_sns_cm_ag_agid_setup(const struct m0_fid *gob_fid, uint64_t group,
                                         struct m0_cm_ag_id *agid)
{
        agid->ai_hi.u_hi = gob_fid->f_container;
        agid->ai_hi.u_lo = gob_fid->f_key;
        agid->ai_lo.u_hi = 0;
        agid->ai_lo.u_lo = group;
}

M0_INTERNAL void agid2fid(const struct m0_cm_ag_id *id, struct m0_fid *fid)
{
	M0_PRE(id != NULL);
	M0_PRE(fid != NULL);

        m0_fid_set(fid, id->ai_hi.u_hi, id->ai_hi.u_lo);
}

M0_INTERNAL uint64_t agid2group(const struct m0_cm_ag_id *id)
{
	M0_PRE(id != NULL);

	return id->ai_lo.u_lo;
}

M0_INTERNAL uint64_t m0_sns_cm_ag_local_cp_nr(const struct m0_cm_aggr_group *ag)
{
	struct m0_fid              fid;
	struct m0_cm              *cm;
	struct m0_sns_cm          *scm;
	struct m0_sns_cm_ag       *sag = ag2snsag(ag);
	struct m0_sns_cm_file_ctx *fctx = sag->sag_fctx;;
	struct m0_pdclust_layout  *pl = m0_layout_to_pdl(fctx->sf_layout);
	uint64_t                   group;

	M0_ENTRY();
	M0_PRE(ag != NULL);

	agid2fid(&ag->cag_id, &fid);
	group = agid2group(&ag->cag_id);

	cm = ag->cag_cm;
	M0_ASSERT(cm != NULL);
	scm = cm2sns(cm);

	M0_LEAVE();
	return m0_sns_cm_ag_nr_local_units(scm, fctx->sf_pm, &fid, pl,
					   fctx->sf_pi, group);
}

M0_INTERNAL void m0_sns_cm_ag_fini(struct m0_sns_cm_ag *sag)
{
        struct m0_cm_aggr_group *ag;
        struct m0_cm            *cm;
        struct m0_sns_cm        *scm;

        M0_ENTRY();
        M0_PRE(sag != NULL);

	ag = &sag->sag_base;
        cm = ag->cag_cm;
        M0_ASSERT(cm != NULL);
	scm = cm2sns(cm);
	m0_bitmap_fini(&sag->sag_fmap);
	m0_bitmap_fini(&sag->sag_proxy_incoming_map);
	m0_sns_cm_fctx_put(scm, &ag->cag_id);
	m0_cm_aggr_group_fini_and_progress(ag);
        M0_LEAVE();
}

M0_INTERNAL int m0_sns_cm_ag_init(struct m0_sns_cm_ag *sag,
				  struct m0_cm *cm,
				  const struct m0_cm_ag_id *id,
				  const struct m0_cm_aggr_group_ops *ag_ops,
				  bool has_incoming)
{
	struct m0_sns_cm           *scm = cm2sns(cm);
	struct m0_fid               gfid;
	struct m0_sns_cm_file_ctx  *fctx;
	struct m0_pdclust_layout   *pl;
	struct m0_pdclust_instance *pi;
	uint64_t                    upg;
	uint64_t                    f_nr;
	int                         rc = 0;

	M0_ENTRY("scm: %p, ag id:%p", cm, id);
	M0_PRE(sag != NULL && cm != NULL && id != NULL && ag_ops != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	agid2fid(id, &gfid);
	fctx = m0_sns_cm_fctx_get(scm, id);
	M0_ASSERT(fctx != NULL && fctx->sf_layout != NULL);
	pl = m0_layout_to_pdl(fctx->sf_layout);
	pi = fctx->sf_pi;
	upg = m0_pdclust_N(pl) + 2 * m0_pdclust_K(pl);
	m0_bitmap_init(&sag->sag_fmap, upg);
	m0_bitmap_init(&sag->sag_proxy_incoming_map, scm->sc_base.cm_proxy_nr);

	sag->sag_fctx = fctx;
	/* calculate actual failed number of units in this group. */
	f_nr = m0_sns_cm_ag_failures_nr(scm, fctx->sf_pm, &gfid, pl, pi,
					id->ai_lo.u_lo, &sag->sag_fmap);
	if (f_nr == 0 || f_nr > m0_pdclust_K(pl)) {
		m0_bitmap_fini(&sag->sag_fmap);
		m0_bitmap_fini(&sag->sag_proxy_incoming_map);
		m0_sns_cm_fctx_put(scm, id);
		return M0_ERR(-EINVAL);
	}
	sag->sag_fnr = f_nr;
	if (has_incoming)
		sag->sag_incoming_nr = m0_sns_cm_ag_max_incoming_units(scm, fctx->sf_pm, id,
								       pl, pi,
								       &sag->sag_proxy_incoming_map);
	m0_cm_aggr_group_init(&sag->sag_base, cm, id, has_incoming,
			      ag_ops);
	sag->sag_base.cag_cp_global_nr = m0_sns_cm_ag_nr_global_units(sag, pl);
	M0_LEAVE("ag: %p", sag);
	return M0_RC(rc);
}

M0_INTERNAL bool m0_sns_cm_ag_has_incoming_from(struct m0_cm_aggr_group *ag,
						struct m0_cm_proxy *proxy)
{
	struct m0_sns_cm_ag *sag = ag2snsag(ag);
	return m0_bitmap_get(&sag->sag_proxy_incoming_map, proxy->px_id);
}

M0_INTERNAL bool m0_sns_cm_ag_is_frozen_on(struct m0_cm_aggr_group *ag, struct m0_cm_proxy *pxy)
{
	struct m0_cm        *cm = ag->cag_cm;
	struct m0_sns_cm_ag *sag = ag2snsag(ag);
	uint32_t             not_coming = 0;
	uint32_t             local_cps = 0;
	uint32_t             expected_free = 0;

	/*
	 * Find out if there are any incoming copy packets from the given proxy that
	 * is already completed and the copy packets will no longer be arriving.
	 */
	if (ag->cag_ops->cago_has_incoming_from(ag, pxy)) {
		if (M0_IN(pxy->px_status, (M0_PX_COMPLETE, M0_PX_STOP)) &&
		    m0_cm_ag_id_cmp(&pxy->px_last_out_recvd, &ag->cag_id) < 0) {
			m0_bitmap_set(&sag->sag_proxy_incoming_map, pxy->px_id, false);
			M0_CNT_INC(sag->sag_not_coming);
		}
	}

	/*
	 * Calculate the expected copy packets that can be created and finalised
	 * in-order to mark aggregation group as frozen.
	 */
	if (sag->sag_not_coming > 0 || (m0_cm_cp_pump_is_complete(&cm->cm_cp_pump) &&
					sag->sag_cp_created_nr != ag->cag_cp_local_nr)) {
		not_coming = sag->sag_not_coming * sag->sag_local_tgts_nr;
		if (ag->cag_cp_local_nr > 0) {
			local_cps = ag->cag_cp_local_nr + sag->sag_outgoing_nr;
			if (m0_cm_cp_pump_is_complete(&cm->cm_cp_pump) &&
			    sag->sag_cp_created_nr != ag->cag_cp_local_nr) {
				local_cps = sag->sag_cp_created_nr;
			}
		}
		expected_free =  local_cps + sag->sag_incoming_nr - not_coming;

		return expected_free == ag->cag_freed_cp_nr;
	} else
		return false;
}

/** @} SNSCMAG */
#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
