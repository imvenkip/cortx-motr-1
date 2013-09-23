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

M0_INTERNAL int m0_sns_cm_repair_ag_setup(struct m0_sns_cm_ag *sag,
					  struct m0_pdclust_layout *pl);

static uint64_t repair_ag_nr_global_units(const struct m0_sns_cm *scm,
					  const struct m0_pdclust_layout *pl)
{
	uint64_t dpupg;
	uint64_t upg;
	int      unit;

	dpupg = m0_pdclust_N(pl) + m0_pdclust_K(pl);
	upg = m0_pdclust_N(pl) + 2 * m0_pdclust_K(pl);
	for (unit = dpupg; unit < upg; ++unit) {
		if (!m0_sns_cm_unit_is_spare(scm, pl, unit))
			M0_CNT_INC(dpupg);
	}

	return dpupg;
}

static uint64_t repair_ag_max_incoming_units(const struct m0_sns_cm *scm,
					     const struct m0_pdclust_layout *pl)
{
	return m0_pdclust_N(pl) + m0_pdclust_K(pl) - scm->sc_failures_nr;
}

static uint64_t repair_ag_unit_start(const struct m0_pdclust_layout *pl)
{
	return 0;
}

static uint64_t repair_ag_unit_end(const struct m0_pdclust_layout *pl)
{
	return m0_pdclust_N(pl) + m0_pdclust_K(pl);
}

static bool repair_ag_is_relevant(struct m0_sns_cm *scm,
				  const struct m0_fid *gfid,
				  uint64_t group,
				  struct m0_pdclust_layout *pl,
				  struct m0_pdclust_instance *pi)
{
	struct m0_sns_cm_iter      *it = &scm->sc_it;
	struct m0_poolmach         *pm = scm->sc_base.cm_pm;
	struct m0_pdclust_src_addr  sa;
	struct m0_pdclust_tgt_addr  ta;
	struct m0_fid               cobfid;
	uint32_t                    tgt_unit;
	uint32_t                    N;
	uint32_t                    K;
	//uint32_t                    i;
	uint32_t                    j;
	bool                        result = false;
	int                         rc;

	N = m0_pdclust_N(pl);
	K = m0_pdclust_K(pl);
	sa.sa_group = group;
	for (j = 0; j < N + K; ++j) {
		sa.sa_unit = j;
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, gfid, &cobfid);
		if (!m0_sns_cm_is_cob_failed(scm, &cobfid))
			continue;
		rc = m0_sns_repair_spare_map(pm, gfid, pl, group, j, &tgt_unit);
		if (rc != 0)
			return rc;
		sa.sa_unit = tgt_unit;
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, gfid, &cobfid);
		//rc = m0_sns_cm_ag_tgt_unit2cob(sag, tgt_unit, pl, &cobfid);
	//	if (rc != 0)
	//		return rc;
		rc = m0_sns_cm_cob_locate(it->si_dbenv, it->si_cob_dom,
					  &cobfid);
		if (rc == 0 && !m0_sns_cm_is_cob_failed(scm, &cobfid)) {
			result = true;
			break;
		}
	}

	return result;
}

const struct m0_sns_cm_helpers repair_helpers = {
	.sch_ag_nr_global_units     = repair_ag_nr_global_units,
	.sch_ag_max_incoming_units  = repair_ag_max_incoming_units,
	.sch_ag_unit_start          = repair_ag_unit_start,
	.sch_ag_unit_end            = repair_ag_unit_end,
	.sch_ag_is_relevant         = repair_ag_is_relevant,
	.sch_ag_setup               = m0_sns_cm_repair_ag_setup,
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
