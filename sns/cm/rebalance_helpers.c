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

#include "sns/cm/cm_utils.h"
#include "sns/cm/ag.h"

static uint64_t rebalance_ag_nr_global_units(const struct m0_sns_cm *scm,
					     const struct m0_pdclust_layout *pl)
{
	return m0_pdclust_N(pl) + 2 * m0_pdclust_K(pl);
}

static uint64_t
rebalance_ag_max_incoming_units(const struct m0_sns_cm *scm,
				const struct m0_pdclust_layout *pl)
{
	return m0_pdclust_K(pl);
}

static uint64_t rebalance_ag_unit_start(const struct m0_pdclust_layout *pl)
{
	return m0_sns_cm_ag_spare_unit_nr(pl, 0);
}

static uint64_t rebalance_ag_unit_end(const struct m0_pdclust_layout *pl)
{
	return m0_pdclust_N(pl) + 2 * m0_pdclust_K(pl);
}

static uint64_t rebalance_ag_target_unit(struct m0_sns_cm_ag *sag,
					 struct m0_pdclust_layout *pl,
					 uint64_t fdata, uint64_t group_fidx)
{
	struct m0_pdclust_instance *pi;
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	struct m0_fid               gfid;
	struct m0_fid               cobfid;
	uint64_t                    tgt_unit = 0;
	uint64_t                    dpupg = m0_pdclust_N(pl) + m0_pdclust_K(pl);
	int                         rc;
	size_t                      i;

	agid2fid(&sag->sag_base.cag_id, &gfid);
	sa.sa_group = agid2group(&sag->sag_base.cag_id);
        rc = m0_sns_cm_fid_layout_instance(pl, &pi, &gfid);
        if (rc != 0)
                return ~0;
	for (i = 0; i < dpupg; ++i) {
		sa.sa_unit = i;
		M0_SET0(&ta);
		M0_SET0(&cobfid);
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, &gfid, &cobfid);
		if (cobfid.f_container == fdata) {
			tgt_unit = i;
			break;
		}
	}
	m0_layout_instance_fini(&pi->pi_base);	

	return tgt_unit;
}

static bool rebalance_ag_is_relevant(struct m0_sns_cm *scm,
				     const struct m0_fid *gfid,
				     uint64_t group,
				     struct m0_pdclust_layout *pl,
				     struct m0_pdclust_instance *pi)
{
	struct m0_sns_cm_iter      *it = &scm->sc_it;
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	struct m0_fid               cobfid;
	uint32_t                    N;
	uint32_t                    K;
	uint32_t                    i;
	bool                        result = false;
	int                         rc;

	N = m0_pdclust_N(pl);
	K = m0_pdclust_K(pl);
	sa.sa_group = group;
	for (i = N + K; i < N + 2 * K; ++i) {
		/* Check if the spare unit of this aggregation group is local. */
		sa.sa_unit = i;
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, gfid, &cobfid);
		rc = m0_sns_cm_cob_locate(it->si_dbenv, it->si_cob_dom, &cobfid);
		if (rc != 0)
			result = true;
	}

	if (!result)
		return result;
	/*
	 * Reset result and check if we have any of the aggregation group's
	 * failed unit on the failed COB.
	 */
	result = false;
	for (i = 0; i < N + K; ++i) {
		sa.sa_unit = i;
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, gfid, &cobfid);
		rc = m0_sns_cm_cob_locate(it->si_dbenv, it->si_cob_dom,
				&cobfid);
		if (rc == 0 && m0_sns_cm_is_cob_failed(scm, &cobfid)) {
			M0_LOG(M0_DEBUG, "true: %lu", group);
			result = true;
		}

	}

	return result;
}

static bool rebalance_ag_relevant_is_done(const struct m0_sns_cm_ag *sag,
					  uint64_t nr_cps_fini)
{
	struct m0_layout *l = sag->sag_base.cag_layout;

	return nr_cps_fini == m0_pdclust_K(m0_layout_to_pdl(l));
}

static bool rebalance_ag_accumulator_is_full(const struct m0_sns_cm_ag *sag,
					     int xform_cp_nr)
{
	struct m0_pdclust_layout *pl;

	pl = m0_layout_to_pdl(sag->sag_base.cag_layout);
	return xform_cp_nr == m0_pdclust_K(pl) ? true : false;
}

const struct m0_sns_cm_helpers rebalance_helpers = {
	.sch_ag_nr_global_units     = rebalance_ag_nr_global_units,
	.sch_ag_max_incoming_units  = rebalance_ag_max_incoming_units,
	.sch_ag_unit_start          = rebalance_ag_unit_start,
	.sch_ag_unit_end            = rebalance_ag_unit_end,
	.sch_ag_target_unit         = rebalance_ag_target_unit,
	.sch_ag_is_relevant         = rebalance_ag_is_relevant,
	.sch_ag_relevant_is_done    = rebalance_ag_relevant_is_done,
	.sch_ag_accumulator_is_full = rebalance_ag_accumulator_is_full
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
