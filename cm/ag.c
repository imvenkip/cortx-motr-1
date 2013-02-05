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
 * Original author: Subhash Arya  <subhash_arya@xyratex.com>
 *                  Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 20/09/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CM

#include "lib/string.h" /* memcpy */
#include "lib/trace.h"
#include "lib/cdefs.h"  /* M0_UNUSED */
#include "mero/magic.h"

#include "cm/ag.h"
#include "cm/cm.h"

/**
   @addtogroup CMAG
 */

M0_TL_DESCR_DEFINE(aggr_grps, "aggregation groups", M0_INTERNAL,
		   struct m0_cm_aggr_group, cag_cm_linkage, cag_magic,
		   CM_AG_LINK_MAGIX, CM_AG_HEAD_MAGIX);

M0_TL_DEFINE(aggr_grps, M0_INTERNAL, struct m0_cm_aggr_group);

struct m0_bob_type ag_bob;

M0_BOB_DEFINE(static M0_UNUSED, &ag_bob, m0_cm_aggr_group);

M0_INTERNAL void m0_cm_ag_lock(struct m0_cm_aggr_group *ag)
{
	m0_mutex_lock(&ag->cag_mutex);
}

M0_INTERNAL void m0_cm_ag_unlock(struct m0_cm_aggr_group *ag)
{
	m0_mutex_unlock(&ag->cag_mutex);
}

M0_INTERNAL int m0_cm_ag_id_cmp(const struct m0_cm_ag_id *id0,
				const struct m0_cm_ag_id *id1)
{
	M0_PRE(id0 != NULL);
	M0_PRE(id1 != NULL);

	return m0_uint128_cmp(&id0->ai_hi, &id1->ai_hi) ?:
	       m0_uint128_cmp(&id0->ai_lo, &id1->ai_lo);
}

M0_INTERNAL void m0_cm_aggr_group_init(struct m0_cm_aggr_group *ag,
				       struct m0_cm *cm,
				       const struct m0_cm_ag_id *id,
				       const struct m0_cm_aggr_group_ops
				       *ag_ops)
{
	M0_ENTRY();
	M0_PRE(id != NULL);
	M0_PRE(cm != NULL);
	M0_PRE(ag != NULL);
	M0_PRE(ag_ops != NULL);

	ag->cag_cm = cm;
	m0_mutex_init(&ag->cag_mutex);
	ag->cag_id = *id;
	aggr_grps_tlink_init(ag);
	ag->cag_ops = ag_ops;
	ag->cag_cp_local_nr = ag->cag_ops->cago_local_cp_nr(ag);
	M0_LEAVE();
}

M0_INTERNAL void m0_cm_aggr_group_fini(struct m0_cm_aggr_group *ag)
{
	struct m0_cm *cm;

	M0_ENTRY();
	M0_ASSERT(ag != NULL);

	cm = ag->cag_cm;
	M0_ASSERT(m0_cm_is_locked(cm));
	aggr_grps_tlink_del_fini(ag);
	M0_POST(!aggr_grps_tlink_is_in(ag));
	if (!m0_cm_has_more_data(cm) &&
	    aggr_grps_tlist_is_empty(&cm->cm_aggr_grps))
		cm->cm_ops->cmo_complete(cm);
	m0_mutex_fini(&ag->cag_mutex);
	M0_LEAVE();
}

M0_INTERNAL struct m0_cm_aggr_group *m0_cm_aggr_group_find(struct m0_cm *cm,
							   const struct
							   m0_cm_ag_id *id)
{
	struct m0_cm_aggr_group *ag;

	M0_ENTRY("cm: %p", cm);
	M0_PRE(cm != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	m0_tl_for(aggr_grps, &cm->cm_aggr_grps, ag) {
		if (m0_cm_ag_id_cmp(id, &ag->cag_id) == 0) {
			M0_LEAVE("Found ag: %p", ag);
			return ag;
		}
	} m0_tl_endfor;
	M0_LEAVE("ag not found");
	return NULL;
}

M0_INTERNAL void m0_cm_aggr_group_add(struct m0_cm *cm,
				      struct m0_cm_aggr_group *ag)
{
	struct m0_cm_aggr_group *found;
	int                      val;

	M0_ENTRY("cm: %p, ag: %p", cm, ag);
	M0_PRE(cm != NULL);
	M0_PRE(ag != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	m0_tl_for(aggr_grps, &cm->cm_aggr_grps, found) {
		val = m0_cm_ag_id_cmp(&ag->cag_id, &found->cag_id);
		M0_ASSERT(val != 0);
		if(val < 0) {
			aggr_grps_tlist_add_before(found, ag);
			M0_LEAVE();
			return;
		}
	} m0_tl_endfor;
	aggr_grps_tlist_add_tail(&cm->cm_aggr_grps, ag);
	M0_LEAVE();
}

M0_INTERNAL struct m0_cm_aggr_group *m0_cm_ag_hi(struct m0_cm *cm)
{

	M0_PRE(cm != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	return aggr_grps_tlist_head(&cm->cm_aggr_grps);
}

M0_INTERNAL struct m0_cm_aggr_group *m0_cm_ag_lo(struct m0_cm *cm)
{
	M0_PRE(cm != NULL);
	M0_PRE(m0_cm_is_locked(cm));

	return aggr_grps_tlist_tail(&cm->cm_aggr_grps);
}

/** @} CMAG */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
