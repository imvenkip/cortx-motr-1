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

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_SNSREPAIR

#include "lib/trace.h"
#include "lib/memory.h"

#include "fid/fid.h"

#include "sns/repair/ag.h"
#include "sns/repair/cm.h"

/**
   @addtogroup SNSRepairAG

   @{
 */

struct c2_sns_repair_ag *ag2snsag(const struct c2_cm_aggr_group *ag)
{
	return container_of(ag, struct c2_sns_repair_ag, sag_base);
}

static int ag_fini(struct c2_cm_aggr_group *ag)
{
	struct c2_sns_repair_ag *sag;
	struct c2_sns_repair_cm *rcm;
	struct c2_cm		*cm;

	C2_ENTRY();
	C2_PRE(ag != NULL);

	sag = ag2snsag(ag);
	cm = ag->cag_cm;
	c2_cm_aggr_group_fini(ag);
	rcm = cm2sns(cm);
	c2_free(sag);
	C2_LEAVE();
	return 0;
}

void agid2fid(const struct c2_cm_aggr_group *ag, struct c2_fid *fid)
{
	C2_PRE(ag != NULL);
	C2_PRE(fid != NULL);

	fid->f_container = ag->cag_id.ai_hi.u_hi;
	fid->f_key       = ag->cag_id.ai_hi.u_lo;
}

uint64_t agid2group(const struct c2_cm_aggr_group *ag)
{
	C2_PRE(ag != NULL);

	return ag->cag_id.ai_lo.u_lo;
}

static uint64_t ag_local_cp_nr(const struct c2_cm_aggr_group *ag)
{
	struct c2_fid            fid;
	uint64_t                 group;
	struct c2_cm            *cm;
	struct c2_sns_repair_cm *rcm;

	C2_ENTRY();
	C2_PRE(ag != NULL);

	agid2fid(ag, &fid);
	group = agid2group(ag);

	cm = ag->cag_cm;
	C2_ASSERT(cm != NULL);
	rcm = cm2sns(cm);

	C2_LEAVE();
	return nr_local_units(rcm, &fid, group);
}

static const struct c2_cm_aggr_group_ops repair_ag_ops = {
	.cago_fini   = ag_fini,
	.cago_local_cp_nr = ag_local_cp_nr
};

struct c2_sns_repair_ag *c2_sns_repair_ag_find(struct c2_sns_repair_cm *rcm,
					       const struct c2_cm_ag_id *id)
{
	struct c2_cm            *cm;
	struct c2_cm_aggr_group *ag;
	struct c2_sns_repair_ag *sag;

	C2_ENTRY("rcm: %p, ag id:%p", rcm, id);
	C2_PRE(rcm != NULL);
	C2_PRE(id != NULL);

	cm = &rcm->rc_base;
	C2_PRE(cm != NULL);
	C2_PRE(c2_cm_is_locked(cm));

	ag = c2_cm_aggr_group_find(cm, id);
	if (ag != NULL) {
		/*
		 * Aggregation group is already present in the sliding window's
		 * list.
		 */
		sag = ag2snsag(ag);
		C2_ASSERT(sag != NULL);
	} else {
		/*
		 * Allocate new aggregation group and add it to the
		 * lexicographically sorted list of aggregation groups in the
		 * sliding window.
		 */
		C2_ALLOC_PTR(sag);
		if (sag != NULL) {
			c2_cm_aggr_group_init(&sag->sag_base, cm, id,
					      &repair_ag_ops);
			spare_unit_to_cob(sag);
			c2_cm_aggr_group_add(cm, &sag->sag_base);
		}
	}
	C2_LEAVE("ag: %p", sag);
	return sag;
}

/** @} SNSRepairAG */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
