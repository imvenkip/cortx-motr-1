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
 * Original author: Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 08/09/2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/trace.h"

#include "fid/fid.h"
#include "reqh/reqh.h"

#include "sns/cm/ag.h"
#include "sns/cm/cp.h"

/**
 * @addtogroup SNSCMCP
 * @{
 */

M0_INTERNAL int m0_sns_cm_rebalance_tgt_info(struct m0_sns_cm_ag *sag,
					     struct m0_sns_cm_cp *scp);

/**
 * Transformation function for sns rebalance.
 *
 * @pre cp != NULL && m0_fom_phase(&cp->c_fom) == M0_CCP_XFORM
 * @param cp Copy packet that has to be transformed.
 */
M0_INTERNAL int m0_sns_cm_rebalance_cp_xform(struct m0_cm_cp *cp)
{
	struct m0_sns_cm_ag     *sns_ag;
	struct m0_sns_cm_cp     *scp;
	struct m0_cm_aggr_group *ag;
	struct m0_cm_ag_id       id;
	int                      rc;

	M0_PRE(cp != NULL && m0_fom_phase(&cp->c_fom) == M0_CCP_XFORM);

	ag = cp->c_ag;
	id = ag->cag_id;
	sns_ag = ag2snsag(ag);
	scp = cp2snscp(cp);
	m0_cm_ag_lock(ag);

        M0_LOG(M0_DEBUG, "xform: id [%lu] [%lu] [%lu] [%lu] local_cp_nr: [%lu]\
	       transformed_cp_nr: [%lu] has_incoming: %d\n",
               id.ai_hi.u_hi, id.ai_hi.u_lo, id.ai_lo.u_hi, id.ai_lo.u_lo,
	       ag->cag_cp_local_nr, ag->cag_transformed_cp_nr,
	       ag->cag_has_incoming);

	/* Increment number of transformed copy packets in the accumulator. */
	M0_CNT_INC(ag->cag_transformed_cp_nr);
	if (!ag->cag_has_incoming)
		M0_ASSERT(ag->cag_transformed_cp_nr <= ag->cag_cp_local_nr);

	M0_ASSERT(m0_fid_is_set(&scp->sc_cobfid));
	rc = m0_sns_cm_rebalance_tgt_info(sns_ag, scp);
	M0_ASSERT(m0_fid_is_set(&scp->sc_cobfid));
	if (rc != 0) {
		m0_fom_phase_move(&cp->c_fom, rc, M0_CCP_FINI);
		rc = M0_FSO_WAIT;
	} else
		rc = cp->c_ops->co_phase_next(cp);
	m0_cm_ag_unlock(ag);

	return rc;
}

#undef M0_TRACE_SUBSYSTEM
/** @} SNSCMCP */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
