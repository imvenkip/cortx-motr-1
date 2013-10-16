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

#include "sns/parity_repair.h"
#include "sns/cm/cm_utils.h"
#include "sns/cm/ag.h"

M0_INTERNAL int m0_sns_cm_rebalance_ag_setup(struct m0_sns_cm_ag *sag,
					     struct m0_pdclust_layout *pl);

static uint64_t rebalance_ag_nr_global_units(const struct m0_sns_cm_ag *sag,
					     struct m0_pdclust_layout *pl)
{
	return m0_pdclust_N(pl) + 2 * m0_pdclust_K(pl);
}

static uint64_t
rebalance_ag_max_incoming_units(const struct m0_sns_cm *scm,
				const struct m0_cm_ag_id *id,
				struct m0_pdclust_layout *pl)
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

M0_INTERNAL int m0_sns_cm_rebalance_tgt_info(struct m0_sns_cm_ag *sag,
					     struct m0_sns_cm_cp *scp)
{
	struct m0_cm               *cm;
	struct m0_poolmach         *pm;
	struct m0_pdclust_layout   *pl;
	struct m0_fid               gfid;
	struct m0_fid               cobfid;
        struct m0_layout_instance  *li;
        struct m0_pdclust_instance *pi;
	uint64_t                    group;
	uint32_t                    spare;
	uint64_t                    offset;
	int                         i;
	int                         rc = 0;

	cm = sag->sag_base.cag_cm;
	m0_cm_lock(cm);
	pm = sag->sag_base.cag_cm->cm_pm;
	pl = m0_layout_to_pdl(sag->sag_base.cag_layout);
	agid2fid(&sag->sag_base.cag_id, &gfid);
        rc = m0_layout_instance_build(&pl->pl_base.sl_base, &gfid, &li);
        if (rc != 0)
                return -ENOENT;

        pi = m0_layout_instance_to_pdi(li);

	group = agid2group(&sag->sag_base.cag_id);
	for (i = 0; i < sag->sag_fmap.b_nr; ++i) {
		if (!m0_bitmap_get(&sag->sag_fmap, i))
			continue;
		/*
		 * Find the spare unit for the given data/parity unit in the
		 * aggregation group.
		 */
		rc = m0_sns_repair_spare_map(pm, &gfid, pl,
				pi, group, i, &spare);
		if (rc != 0)
			break;
		/* Identify the COB hosting the spare unit. */
		rc = m0_sns_cm_ag_tgt_unit2cob(sag, spare, pl, &cobfid);
		if (rc != 0)
			break;
		/*
		 * Check if the copy packet corresponds to the calculated
		 * spare.
		 */
		if (cobfid.f_container == scp->sc_cobfid.f_container) {
			/*
			 * Identify the COB hosting the failed data/parity
			 * unit.
			 */
			rc = m0_sns_cm_ag_tgt_unit2cob(sag, i, pl, &cobfid);
			if (rc != 0)
				goto out;
			M0_ASSERT(m0_fid_is_set(&cobfid));
			offset = m0_sns_cm_ag_unit2cobindex(sag, i, pl);
			/*
			 * Change the target cobfid and offset of the copy
			 * packet to write the data from spare unit back to
			 * previously failed data unit but now available data/
			 * parity unit in the aggregation group.
			 */
			m0_sns_cm_cp_tgt_info_fill(scp, &cobfid, offset, i);
		}
	}
out:
	m0_cm_unlock(cm);
	m0_layout_instance_fini(&pi->pi_base);
	return rc;
}

static bool rebalance_ag_is_relevant(struct m0_sns_cm *scm,
				     const struct m0_fid *gfid,
				     uint64_t group,
				     struct m0_pdclust_layout *pl,
				     struct m0_pdclust_instance *pi)
{
	struct m0_sns_cm_iter      *it = &scm->sc_it;
	struct m0_poolmach         *pm = scm->sc_base.cm_pm;
	uint32_t                    spare;
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
	for (i = 0; i < N + K; ++i) {
		sa.sa_unit = i;
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, gfid, &cobfid);
		rc = m0_sns_cm_cob_locate(it->si_cob_dom,
					  &cobfid);
		if (rc == 0 && m0_sns_cm_is_cob_failed(scm, &cobfid)) {
			rc = m0_sns_repair_spare_map(pm, gfid, pl, pi,
					group, i, &spare);
			if (rc != 0)
				return false;
			sa.sa_unit = spare;
			/* Identify the COB hosting the spare unit. */
			m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, gfid, &cobfid);
			rc = m0_sns_cm_cob_locate(it->si_cob_dom,
						  &cobfid);
			if (rc != 0) {
				M0_LOG(M0_DEBUG, "true: %lu", group);
				result = true;
			}
		}

	}

	return result;
}

const struct m0_sns_cm_helpers rebalance_helpers = {
	.sch_ag_nr_global_units     = rebalance_ag_nr_global_units,
	.sch_ag_max_incoming_units  = rebalance_ag_max_incoming_units,
	.sch_ag_unit_start          = rebalance_ag_unit_start,
	.sch_ag_unit_end            = rebalance_ag_unit_end,
	.sch_ag_is_relevant         = rebalance_ag_is_relevant,
	.sch_ag_setup               = m0_sns_cm_rebalance_ag_setup,
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
