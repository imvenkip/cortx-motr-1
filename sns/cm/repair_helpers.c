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
#include "sns/cm/cm_utils.h"
#include "sns/cm/ag.h"


static uint64_t repair_ag_nr_global_units(const struct m0_sns_cm *scm,
					  const struct m0_pdclust_layout *pl)
{
	return m0_pdclust_N(pl) + m0_pdclust_K(pl);
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

static uint64_t repair_ag_target_unit(struct m0_sns_cm_ag *sag,
				      struct m0_pdclust_layout *pl,
				      uint64_t fdata, uint64_t fidx)
{
	return m0_sns_cm_ag_spare_unit_nr(pl, fidx);
}

static bool repair_ag_is_relevant(struct m0_sns_cm *scm,
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
	for (i = 0; i < K; ++i) {
		sa.sa_group = group;
		sa.sa_unit = N + K + i;
		m0_sns_cm_unit2cobfid(pl, pi, &sa, &ta, gfid, &cobfid);
		rc = m0_sns_cm_cob_locate(it->si_dbenv, it->si_cob_dom,
					  &cobfid);
		if (rc == 0 && !m0_sns_cm_is_cob_failed(scm, &cobfid))
			result = true;
	}

	return result;
}

static bool repair_ag_relevant_is_done(const struct m0_sns_cm_ag *sag,
				       uint64_t nr_cps_fini)
{
	return nr_cps_fini == sag->sag_base.cag_cp_global_nr - sag->sag_fnr;
}

static bool repair_ag_accumulator_is_full(const struct m0_sns_cm_ag *sag,
					  int xform_cp_nr)
{
	uint64_t global_cp_nr = sag->sag_base.cag_cp_global_nr;

	return xform_cp_nr == global_cp_nr - sag->sag_fnr ? true : false;
}

const struct m0_sns_cm_helpers repair_helpers = {
	.sch_ag_nr_global_units     = repair_ag_nr_global_units,
	.sch_ag_max_incoming_units  = repair_ag_max_incoming_units,
	.sch_ag_unit_start          = repair_ag_unit_start,
	.sch_ag_unit_end            = repair_ag_unit_end,
	.sch_ag_target_unit         = repair_ag_target_unit,
	.sch_ag_is_relevant         = repair_ag_is_relevant,
	.sch_ag_relevant_is_done    = repair_ag_relevant_is_done,
	.sch_ag_accumulator_is_full = repair_ag_accumulator_is_full
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
