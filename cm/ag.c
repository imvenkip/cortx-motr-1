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
 * Original author: Subhash Arya  <subhash_arya@xyratex.com>
 *                  Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 20/09/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define C2_TRACE_SUBSYSTEM C2_TRACE_SUBSYS_CM

#include "lib/string.h" /* memcpy */
#include "lib/trace.h"
#include "colibri/magic.h"

#include "cm/ag.h"
#include "cm/cm.h"

/**
   @addtogroup CMAG
 */

C2_TL_DESCR_DEFINE(aggr_grps, "aggregation groups", ,struct c2_cm_aggr_group,
		   cag_cm_linkage, cag_magic, CM_AG_LINK_MAGIX,
		   CM_AG_HEAD_MAGIX);

C2_TL_DEFINE(aggr_grps, , struct c2_cm_aggr_group);

struct c2_bob_type ag_bob;

C2_BOB_DEFINE( ,&ag_bob, c2_cm_aggr_group);

int c2_cm_ag_id_cmp(const struct c2_cm_ag_id *id0,
		    const struct c2_cm_ag_id *id1)
{
	C2_PRE(id0 != NULL);
	C2_PRE(id1 != NULL);

	return c2_uint128_cmp(&id0->ai_hi, &id1->ai_hi) ?:
	       c2_uint128_cmp(&id0->ai_lo, &id1->ai_lo);
}

void c2_cm_aggr_group_init(struct c2_cm_aggr_group *ag, struct c2_cm *cm,
			   const struct c2_cm_ag_id *id,
			   const struct c2_cm_aggr_group_ops *ag_ops)
{
	C2_ENTRY();
	C2_PRE(id != NULL);
	C2_PRE(cm != NULL);
	C2_PRE(ag != NULL);
	C2_PRE(ag_ops != NULL);

	ag->cag_cm = cm;
	c2_atomic64_set(&ag->cag_transformed_cp_nr, 0);
	c2_atomic64_set(&ag->cag_freed_cp_nr, 0);
	ag->cag_id = *id;
	aggr_grps_tlink_init(ag);
	ag->cag_ops = ag_ops;
	ag->cag_cp_nr = ag->cag_ops->cago_local_cp_nr(ag);
	C2_LEAVE();
}

void c2_cm_aggr_group_fini(struct c2_cm_aggr_group *ag)
{
	struct c2_cm *cm;

	C2_ENTRY();
	C2_ASSERT(ag != NULL);

	cm = ag->cag_cm;
	C2_ASSERT(c2_cm_is_locked(cm));
	aggr_grps_tlink_del_fini(ag);
	C2_POST(!aggr_grps_tlink_is_in(ag));
	if (!c2_cm_has_more_data(cm) &&
	    aggr_grps_tlist_is_empty(&cm->cm_aggr_grps))
		cm->cm_ops->cmo_complete(cm);
	C2_LEAVE();
}

struct c2_cm_aggr_group *c2_cm_aggr_group_find(struct c2_cm *cm,
					       const struct c2_cm_ag_id *id)
{
	struct c2_cm_aggr_group *ag;

	C2_ENTRY("cm: %p", cm);
	C2_PRE(cm != NULL);
	C2_PRE(c2_cm_is_locked(cm));

	c2_tl_for(aggr_grps, &cm->cm_aggr_grps, ag) {
		if (c2_cm_ag_id_cmp(id, &ag->cag_id) == 0) {
			C2_LEAVE("Found ag: %p", ag);
			return ag;
		}
	} c2_tl_endfor;
	C2_LEAVE("ag not found");
	return NULL;
}

void c2_cm_aggr_group_add(struct c2_cm *cm, struct c2_cm_aggr_group *ag)
{
	struct c2_cm_aggr_group *found;
	int                      val;

	C2_ENTRY("cm: %p, ag: %p", cm, ag);
	C2_PRE(cm != NULL);
	C2_PRE(ag != NULL);
	C2_PRE(c2_cm_is_locked(cm));

	c2_tl_for(aggr_grps, &cm->cm_aggr_grps, found) {
		val = c2_cm_ag_id_cmp(&ag->cag_id, &found->cag_id);
		C2_ASSERT(val != 0);
		if(val < 0) {
			aggr_grps_tlist_add_before(found, ag);
			C2_LEAVE();
			return;
		}
	} c2_tl_endfor;
	aggr_grps_tlist_add_tail(&cm->cm_aggr_grps, ag);
	C2_LEAVE();
}

struct c2_cm_aggr_group *c2_cm_ag_hi(struct c2_cm *cm)
{

	C2_PRE(cm != NULL);
	C2_PRE(c2_cm_is_locked(cm));

	return aggr_grps_tlist_head(&cm->cm_aggr_grps);
}

struct c2_cm_aggr_group *c2_cm_ag_lo(struct c2_cm *cm)
{
	C2_PRE(cm != NULL);
	C2_PRE(c2_cm_is_locked(cm));

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
