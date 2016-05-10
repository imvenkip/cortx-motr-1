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
 * Original creation date: 07/03/2013
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM

#include "lib/trace.h"
#include "lib/misc.h"

#include "fid/fid.h"

#include "cm/proxy.h"

#include "sns/parity_repair.h"
#include "sns/cm/cm_utils.h"
#include "sns/cm/ag.h"
#include "sns/cm/file.h"
#include "pool/pool.h"
#include "ioservice/io_device.h"

M0_INTERNAL int m0_sns_cm_rebalance_ag_setup(struct m0_sns_cm_ag *sag,
					     struct m0_pdclust_layout *pl);

static bool is_spare_relevant(const struct m0_sns_cm *scm,
			      struct m0_poolmach *pm,
			      const struct m0_fid *gfid,
			      uint64_t group, uint32_t spare,
			      struct m0_pdclust_layout *pl,
			      struct m0_pdclust_instance *pi);

static uint64_t
rebalance_ag_max_incoming_units(const struct m0_sns_cm *scm,
				struct m0_poolmach *pm,
				const struct m0_cm_ag_id *id,
				struct m0_pdclust_layout *pl,
				struct m0_pdclust_instance *pi,
				struct m0_bitmap *proxy_in_map)
{
        struct m0_fid               cobfid;
        struct m0_fid               gfid;
        struct m0_pdclust_src_addr  sa;
        struct m0_pdclust_tgt_addr  ta;
	struct m0_cm_proxy         *pxy;
	const struct m0_cm         *cm;
	const char                 *ep;
        int32_t                     incoming_nr = 0;
        uint32_t                    tgt_unit;
        uint32_t                    tgt_unit_prev;
        uint64_t                    unit;
	uint64_t                    upg;
        int                         rc;

        M0_SET0(&sa);
	cm = &scm->sc_base;
        agid2fid(id, &gfid);
        sa.sa_group = agid2group(id);
	upg = m0_sns_cm_ag_size(pl);
	for (unit = 0; unit < upg; ++unit) {
		sa.sa_unit = unit;
		M0_SET0(&ta);
		M0_SET0(&cobfid);
		M0_ASSERT(pm != NULL);
		m0_sns_cm_unit2cobfid(pi, &sa, &ta, pm, &gfid, &cobfid);
		if (!m0_sns_cm_is_cob_failed(pm, ta.ta_obj))
			continue;
                if (!m0_sns_cm_is_local_cob(cm, pm->pm_pver, &cobfid))
                        continue;
		if (m0_pdclust_unit_classify(pl, unit) == M0_PUT_SPARE) {
			if (is_spare_relevant(scm, pm, &gfid, sa.sa_group,
					      unit, pl, pi))
				M0_CNT_INC(incoming_nr);
			continue;
		}
                rc = m0_sns_repair_spare_map(pm, &gfid, pl, pi, sa.sa_group,
					     unit, &tgt_unit, &tgt_unit_prev);
                if (rc != 0)
                        return ~0;
		M0_SET0(&ta);
		M0_SET0(&cobfid);
                sa.sa_unit = tgt_unit;
                m0_sns_cm_unit2cobfid(pi, &sa, &ta, pm, &gfid, &cobfid);
                if (!m0_sns_cm_is_local_cob(cm, pm->pm_pver, &cobfid)) {
			ep = m0_sns_cm_tgt_ep(cm, pm->pm_pver, &cobfid);
			pxy = m0_tl_find(proxy, pxy, &cm->cm_proxies,
					 m0_streq(ep, pxy->px_endpoint));
			if (!m0_bitmap_get(proxy_in_map, pxy->px_id))
				m0_bitmap_set(proxy_in_map, pxy->px_id, true);
			M0_CNT_INC(incoming_nr);
		}
	}
        return incoming_nr;
}

static uint64_t rebalance_ag_unit_start(const struct m0_pdclust_layout *pl)
{
	return m0_sns_cm_ag_spare_unit_nr(pl, 0);
}

static uint64_t rebalance_ag_unit_end(const struct m0_pdclust_layout *pl)
{
	return m0_sns_cm_ag_size(pl);
}

static int _tgt_check_and_change(struct m0_sns_cm_ag *sag, struct m0_poolmach *pm,
				 struct m0_pdclust_layout *pl,
				 uint64_t data_unit, uint32_t old_tgt_dev,
				 uint64_t *new_tgt_unit)
{
	uint64_t                   group = agid2group(&sag->sag_base.cag_id);
	uint32_t                   spare;
	uint32_t                   spare_prev;
	struct m0_sns_cm_file_ctx *fctx;
	int                        rc;

	fctx = sag->sag_fctx;
	rc = m0_sns_repair_spare_rebalancing(pm, &fctx->sf_fid, pl, fctx->sf_pi,
				     group, data_unit, &spare,
				     &spare_prev);
	if (rc == 0)
		*new_tgt_unit = spare;

	return M0_RC(rc);
}

M0_INTERNAL int m0_sns_cm_rebalance_tgt_info(struct m0_sns_cm_ag *sag,
					     struct m0_sns_cm_cp *scp)
{
	struct m0_pdclust_layout  *pl;
	struct m0_fid              cobfid;
	struct m0_sns_cm_file_ctx *fctx;
	struct m0_poolmach        *pm;
	uint64_t                   group = agid2group(&sag->sag_base.cag_id);
	uint64_t                   data_unit;
	uint64_t                   offset;
	uint32_t                   dev_idx;
	int                        rc = 0;

	fctx = sag->sag_fctx;
	pm = fctx->sf_pm;
	pl = m0_layout_to_pdl(fctx->sf_layout);
	data_unit = scp->sc_base.c_ag_cp_idx;
	dev_idx = m0_sns_cm_device_index_get(group, data_unit, fctx->sf_pi);
	if (!m0_sns_cm_is_cob_rebalancing(pm, dev_idx)) {
		rc = _tgt_check_and_change(sag, pm, pl, data_unit, dev_idx, &data_unit);
		if (rc != 0)
			return M0_ERR(rc);
		scp->sc_base.c_ag_cp_idx = data_unit;
	}

	rc = m0_sns_cm_ag_tgt_unit2cob(sag, scp->sc_base.c_ag_cp_idx,
				       pl, fctx->sf_pi, &cobfid);
	if (rc == 0) {
		offset = m0_sns_cm_ag_unit2cobindex(sag,
						    scp->sc_base.c_ag_cp_idx,
						    pl, fctx->sf_pi);
		/*
		 * Change the target cobfid and offset of the copy
		 * packet to write the data from spare unit back to
		 * previously failed data unit but now available data/
		 * parity unit in the aggregation group.
		 */
		m0_sns_cm_cp_tgt_info_fill(scp, &cobfid, offset,
					   scp->sc_base.c_ag_cp_idx);
	}

	return M0_RC(rc);
}

static bool is_spare_relevant(const struct m0_sns_cm *scm, struct m0_poolmach *pm,
			      const struct m0_fid *gfid,
			      uint64_t group, uint32_t spare,
			      struct m0_pdclust_layout *pl,
			      struct m0_pdclust_instance *pi)
{
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	struct m0_fid               cobfid;
	uint32_t                    dev_idx;
	uint64_t                    data;
	uint32_t                    spare_prev;
	int                         rc;

	rc = m0_sns_repair_data_map(pm, pl, pi, group, spare, &data);
	if (rc == 0) {
		dev_idx = m0_sns_cm_device_index_get(group, data, pi);
		if (m0_sns_cm_is_cob_repaired(pm, dev_idx)) {
			rc = m0_sns_repair_spare_map(pm, gfid, pl, pi, group,
						     data, &spare, &spare_prev);
			if (rc == 0) {
				sa.sa_unit = spare;
				sa.sa_group = group;
				m0_sns_cm_unit2cobfid(pi, &sa, &ta, pm, gfid, &cobfid);
				if (!m0_sns_cm_is_local_cob(&scm->sc_base, pm->pm_pver,
							    &cobfid))
					return true;
			}
		}
	}
	return false;
}

static bool rebalance_ag_is_relevant(struct m0_sns_cm *scm,
				     struct m0_poolmach *pm,
				     const struct m0_fid *gfid,
				     uint64_t group,
				     struct m0_pdclust_layout *pl,
				     struct m0_pdclust_instance *pi)
{
	uint32_t                    spare;
	uint32_t                    spare_prev;
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	struct m0_fid               cobfid;
	uint64_t                    upg;
	uint32_t                    i;
	uint32_t                    tunit;
	int                         rc;
	M0_PRE(pm != NULL);

	upg = m0_sns_cm_ag_size(pl);
	sa.sa_group = group;
	for (i = 0; i < upg; ++i) {
		sa.sa_unit = i;
		m0_sns_cm_unit2cobfid(pi, &sa, &ta, pm, gfid, &cobfid);
		if (m0_sns_cm_is_cob_rebalancing(pm, ta.ta_obj) &&
		    m0_sns_cm_is_local_cob(&scm->sc_base, pm->pm_pver,
			    &cobfid)) {
			tunit = sa.sa_unit;
			if (m0_pdclust_unit_classify(pl, tunit) ==
					M0_PUT_SPARE) {
				if (!is_spare_relevant(scm, pm, gfid, group, tunit, pl, pi))
					continue;
			}
			rc = m0_sns_repair_spare_map(pm, gfid,
					pl, pi, group, tunit,
					&spare, &spare_prev);
			if (rc != 0)
				return false;
			tunit = spare;
			sa.sa_unit = tunit;
			m0_sns_cm_unit2cobfid(pi, &sa, &ta, pm,
					gfid, &cobfid);
			if (!m0_sns_cm_is_local_cob(&scm->sc_base, pm->pm_pver,
						&cobfid))
				return true;
		}
	}

	return false;
}

/**
 * Reopen the REPAIRED stob devices.
 * When the disk is powered OFF and ON the disk device file (under /dev) gets
 * changed. Destroy & create the stob to reopen the underlying devices.
 */
M0_INTERNAL int m0_sns_reopen_stob_devices(struct m0_cm *cm)
{
	struct m0_sns_cm      *scm = cm2sns(cm);
	enum m0_pool_nd_state  state;
	int                    rc = 0;

	state = scm->sc_op == SNS_REPAIR ? M0_PNDS_SNS_REPAIRING :
		M0_PNDS_SNS_REBALANCING;
	M0_PRE(state == M0_PNDS_SNS_REBALANCING);
#if 0
	/*
	 * This piece of code need to be moved into HA callback.
	 * Please see MERO-1484 for references.
	 */
	struct m0_poolmach    *pm;
	pm = m0_ios_poolmach_get(scm->sc_base.cm_service.rs_reqh);
	rc = m0_pool_device_reopen(pm, scm->sc_base.cm_service.rs_reqh);
	if (rc != 0) {
		return M0_ERR(rc);
	}
#endif
	return M0_RC(rc);
}

int rebalance_cob_locate(struct m0_sns_cm *scm, struct m0_cob_domain *cdom,
		         struct m0_poolmach *pm, const struct m0_fid *cob_fid)
{
	return m0_sns_cm_cob_locate(cdom, cob_fid);
}

const struct m0_sns_cm_helpers rebalance_helpers = {
	.sch_ag_max_incoming_units  = rebalance_ag_max_incoming_units,
	.sch_ag_unit_start          = rebalance_ag_unit_start,
	.sch_ag_unit_end            = rebalance_ag_unit_end,
	.sch_ag_is_relevant         = rebalance_ag_is_relevant,
	.sch_ag_setup               = m0_sns_cm_rebalance_ag_setup,
	.sch_cob_locate             = rebalance_cob_locate
};

#undef M0_TRACE_SUBSYSTEM

/** @} endgroup SNSCM */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
