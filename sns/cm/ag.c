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
#include "lib/errno.h"
#include "lib/cdefs.h"

#include "fid/fid.h"

#include "sns/cm/ag.h"
#include "sns/cm/cm.h"

/**
   @addtogroup SNSCMAG

   @{
 */

M0_INTERNAL void m0_sns_cm_acc_cp_init(struct m0_sns_cm_cp *scp,
				       struct m0_sns_cm_ag *ag);

M0_INTERNAL int m0_sns_cm_acc_cp_setup(struct m0_sns_cm_cp *scp,
                                       struct m0_sns_cm_ag_tgt_addr *tgt_addr);

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
	scm = cm2sns(cm);
	m0_free(sag->sag_tgts);
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

static const struct m0_cm_aggr_group_ops sns_cm_ag_ops = {
	.cago_fini        = ag_fini,
	.cago_local_cp_nr = ag_local_cp_nr
};

M0_INTERNAL int m0_sns_cm_ag_alloc(struct m0_cm *cm,
				   const struct m0_cm_ag_id *id,
				   struct m0_cm_aggr_group **out)
{
	struct m0_sns_cm        *sns_cm = cm2sns(cm);
	struct m0_sns_cm_ag     *sag;
	uint64_t                 f_nr;
	int                      i;

	M0_ENTRY("scm: %p, ag id:%p", cm, id);
	M0_PRE(cm != NULL && id != NULL && out != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	/*
	 * Allocate new aggregation group and add it to the
	 * lexicographically sorted list of aggregation groups in the
	 * sliding window.
	 */
	M0_ALLOC_PTR(sag);
	if (sag == NULL)
		return -ENOMEM;
	f_nr = m0_sns_cm_iter_failures_nr(&sns_cm->sc_it);
	M0_ASSERT(f_nr != 0);
	M0_ALLOC_ARR(sag->sag_accs, f_nr);
	if (sag->sag_accs == NULL) {
		m0_free(sag);
		return -ENOMEM;
	}
	M0_ALLOC_ARR(sag->sag_tgts, f_nr);
	if (sag->sag_tgts == NULL) {
		m0_free(sag->sag_accs);
		m0_free(sag);
		return -ENOMEM;
	}
	sag->sag_fnr = f_nr;
	m0_cm_aggr_group_init(&sag->sag_base, cm, id, &sns_cm_ag_ops);
	m0_sns_cm_iter_tgt_unit_to_cob(sag);
	for (i = 0; i < sag->sag_fnr; ++i)
		m0_sns_cm_acc_cp_init(&sag->sag_accs[i], sag);

	*out = &sag->sag_base;

	M0_LEAVE("ag: %p", sag);
	return 0;
}

M0_INTERNAL int m0_sns_cm_ag_setup(struct m0_sns_cm_ag *sag)
{
	int                  i;
	int                  rc;

	M0_PRE(sag != NULL);

	for (i = 0; i < sag->sag_fnr; ++i) {
		rc = m0_sns_cm_acc_cp_setup(&sag->sag_accs[i],
					    &sag->sag_tgts[i]);
		if (rc < 0)
			return rc;
		/*
		 * Increment local number of copy packets for newly created
		 * accumulator copy packet.
		 */
		M0_CNT_INC(sag->sag_base.cag_cp_local_nr);
	}
	return 0;
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
