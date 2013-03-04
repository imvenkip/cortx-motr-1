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
 * Original creation date: 12/09/2012
 */

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM

#include "lib/trace.h"
#include "lib/memory.h"

#include "fid/fid.h"

#include "sns/cm/ag.h"
#include "sns/cm/cm.h"

/**
   @addtogroup SNSCMAG

   @{
 */

M0_INTERNAL struct m0_sns_cm_ag *ag2snsag(const struct m0_cm_aggr_group *ag)
{
	return container_of(ag, struct m0_sns_cm_ag, sag_base);
}

static int ag_fini(struct m0_cm_aggr_group *ag)
{
	struct m0_sns_cm_ag *sag;

	M0_ENTRY();
	M0_PRE(ag != NULL);

	sag = ag2snsag(ag);
	m0_cm_aggr_group_fini(ag);
	m0_free(sag);
	M0_LEAVE();
	return 0;
}

M0_INTERNAL void agid2fid(const struct m0_cm_aggr_group *ag, struct m0_fid *fid)
{
	M0_PRE(ag != NULL);
	M0_PRE(fid != NULL);

        m0_fid_set(fid, ag->cag_id.ai_hi.u_hi, ag->cag_id.ai_hi.u_lo);
}

M0_INTERNAL uint64_t agid2group(const struct m0_cm_aggr_group *ag)
{
	M0_PRE(ag != NULL);

	return ag->cag_id.ai_lo.u_lo;
}

static uint64_t ag_local_cp_nr(const struct m0_cm_aggr_group *ag)
{
	struct m0_fid      fid;
	uint64_t           group;
	struct m0_cm      *cm;
	struct m0_sns_cm  *scm;

	M0_ENTRY();
	M0_PRE(ag != NULL);

	agid2fid(ag, &fid);
	group = agid2group(ag);

	cm = ag->cag_cm;
	M0_ASSERT(cm != NULL);
	scm = cm2sns(cm);

	M0_LEAVE();
	return nr_local_units(scm, &fid, group);
}

static const struct m0_cm_aggr_group_ops repair_ag_ops = {
	.cago_fini        = ag_fini,
	.cago_local_cp_nr = ag_local_cp_nr
};

M0_INTERNAL struct m0_sns_cm_ag *m0_sns_cm_ag_find(struct m0_sns_cm *scm,
						   const struct m0_cm_ag_id *id)
{
	struct m0_cm            *cm;
	struct m0_cm_aggr_group *ag;
	struct m0_sns_cm_ag     *sag;

	M0_ENTRY("scm: %p, ag id:%p", scm, id);
	M0_PRE(scm != NULL);
	M0_PRE(id != NULL);

	cm = &scm->sc_base;
	M0_PRE(cm != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	ag = m0_cm_aggr_group_find(cm, id);
	if (ag != NULL) {
		/*
		 * Aggregation group is already present in the sliding window's
		 * list.
		 */
		sag = ag2snsag(ag);
		M0_ASSERT(sag != NULL);
	} else {
		/*
		 * Allocate new aggregation group and add it to the
		 * lexicographically sorted list of aggregation groups in the
		 * sliding window.
		 */
		M0_ALLOC_PTR(sag);
		if (sag != NULL) {
			m0_cm_aggr_group_init(&sag->sag_base, cm, id,
					      &repair_ag_ops);
			target_unit_to_cob(sag);
			m0_cm_aggr_group_add(cm, &sag->sag_base);
		}
	}
	M0_LEAVE("ag: %p", sag);
	return sag;
}

#undef M0_TRACE_SUBSYSTEM

/** @} SNSCMAG */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
