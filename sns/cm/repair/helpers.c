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

#include "sns/parity_repair.h"
#include "sns/cm/cm_utils.h"
#include "sns/cm/ag.h"
#include "cm/proxy.h"

M0_INTERNAL int m0_sns_cm_repair_ag_setup(struct m0_sns_cm_ag *sag,
					  struct m0_pdclust_layout *pl);

static uint64_t repair_ag_max_incoming_units(const struct m0_sns_cm *scm,
					     const struct m0_cm_ag_id *id,
					     struct m0_pdclust_layout *pl,
					     struct m0_pdclust_instance *pi,
					     struct m0_bitmap *proxy_in_map)
{
	struct m0_poolmach         *pm = scm->sc_base.cm_pm;
	const struct m0_cm         *cm = &scm->sc_base;
	struct m0_pdclust_src_addr  sa;
        struct m0_pdclust_tgt_addr  ta;
        struct m0_fid               cobfid;
	struct m0_fid               spare_cob;
	struct m0_fid               gfid;
	struct m0_cm_proxy         *pxy;
	const char                 *ep;
	bool                        is_failed;
	uint32_t                    incoming = 0;
	uint32_t                    local_spares = 0;
	uint32_t                    tgt_unit_prev;
	uint64_t                    unit;
	int                         rc;

	M0_ENTRY();

	agid2fid(id, &gfid);
	sa.sa_group = agid2group(id);
	for (unit = 0; unit < m0_pdclust_N(pl) + 2 * m0_pdclust_K(pl); ++unit) {
		sa.sa_unit = unit;
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, &gfid,
				      &cobfid);
		is_failed = m0_sns_cm_is_cob_failed(scm, &cobfid);
		if (m0_sns_cm_unit_is_spare(scm, pl, pi, &gfid, sa.sa_group,
					    unit))
			continue;
		/* Count number of spares corresponding to the failures
		 * on a node. This is required to calculate exact number of
		 * incoming copy packets.
		 */
		if (is_failed && !m0_sns_cm_is_cob_repaired(scm, &cobfid)) {
			rc = m0_sns_repair_spare_map(pm, &gfid, pl, pi,
					sa.sa_group, unit, (unsigned *)&sa.sa_unit,
					&tgt_unit_prev);
			if (rc != 0)
				return M0_ERR(rc);

			m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, &gfid, &spare_cob);
			if (m0_sns_cm_unit_is_spare(scm, pl, pi, &gfid, sa.sa_group,
						    sa.sa_unit) &&
			    m0_sns_cm_is_local_cob(cm, &spare_cob)) {
				M0_CNT_INC(local_spares);
			}
		}
		if (!is_failed && !m0_sns_cm_is_local_cob(cm, &cobfid)) {
			ep = m0_sns_cm_tgt_ep(cm, &cobfid);
			pxy = m0_tl_find(proxy, pxy, &cm->cm_proxies, m0_streq(ep, pxy->px_endpoint));
			if (!m0_bitmap_get(proxy_in_map, pxy->px_id)) {
				m0_bitmap_set(proxy_in_map, pxy->px_id, true);
				M0_CNT_INC(incoming);
			}
		}
	}

	M0_LEAVE();
	return M0_RC(incoming * local_spares);
}

static uint64_t repair_ag_unit_start(const struct m0_pdclust_layout *pl)
{
	return 0;
}

static uint64_t repair_ag_unit_end(const struct m0_pdclust_layout *pl)
{
	return m0_pdclust_N(pl) + 2 * m0_pdclust_K(pl);
}

static bool repair_ag_is_relevant(struct m0_sns_cm *scm,
				  const struct m0_fid *gfid,
				  uint64_t group,
				  struct m0_pdclust_layout *pl,
				  struct m0_pdclust_instance *pi)
{
	struct m0_poolmach         *pm = scm->sc_base.cm_pm;
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	struct m0_fid               cobfid;
	uint32_t                    tgt_unit;
	uint32_t                    tgt_unit_prev;
	uint32_t                    N;
	uint32_t                    K;
	uint32_t                    j;
	bool                        result = false;
	int                         rc;
	M0_ENTRY();

	N = m0_pdclust_N(pl);
	K = m0_pdclust_K(pl);
	sa.sa_group = group;
	for (j = 0; j < N + 2 * K; ++j) {
		sa.sa_unit = j;
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, gfid, &cobfid);
		if (m0_sns_cm_unit_is_spare(scm, pl, pi, gfid, group, j))
			continue;
		if (!m0_sns_cm_is_cob_failed(scm, &cobfid))
			continue;
		if (m0_sns_cm_is_cob_repaired(scm, &cobfid))
			continue;
		rc = m0_sns_repair_spare_map(pm, gfid, pl, pi,
				group, j, &tgt_unit, &tgt_unit_prev);
		if (rc != 0)
			return M0_RC(rc);
		sa.sa_unit = tgt_unit;
		if (!m0_sns_cm_unit_is_spare(scm, pl, pi, gfid, group, tgt_unit))
			continue;
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, gfid, &cobfid);
		rc = m0_sns_cm_is_local_cob(&scm->sc_base, &cobfid);
		M0_LOG(M0_DEBUG, FID_F" local=%d", FID_P(&cobfid), rc);
		if (rc == true && !m0_sns_cm_is_cob_failed(scm, &cobfid)) {
			result = true;
			break;
		}
	}

	return M0_RC(result);
}

int repair_cob_locate(struct m0_sns_cm *scm, struct m0_cob_domain *cdom,
                     const struct m0_fid *cob_fid)
{
	int rc;

	rc = m0_sns_cm_cob_locate(cdom, cob_fid);
	if (rc == -ENOENT) {
		if (m0_sns_cm_is_local_cob(&scm->sc_base, cob_fid))
			rc = -ENODEV;
	}

	return rc;
}

const struct m0_sns_cm_helpers repair_helpers = {
	.sch_ag_max_incoming_units  = repair_ag_max_incoming_units,
	.sch_ag_unit_start          = repair_ag_unit_start,
	.sch_ag_unit_end            = repair_ag_unit_end,
	.sch_ag_is_relevant         = repair_ag_is_relevant,
	.sch_ag_setup               = m0_sns_cm_repair_ag_setup,
	.sch_cob_locate             = repair_cob_locate
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
