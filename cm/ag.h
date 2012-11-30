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
 *                  Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 08/08/2012
 */

#pragma once

#ifndef __COLIBRI_CM_AG_H__
#define __COLIBRI_CM_AG_H__

#include "lib/atomic.h"
#include "lib/types.h"
#include "lib/tlist.h"

/**
   @defgroup CMAG Copy machine aggregation group
   @ingroup CM

   @{
 */

/** Unique aggregation group identifier. */
struct c2_cm_ag_id {
	struct c2_uint128 ai_hi;
	struct c2_uint128 ai_lo;
};

/** Copy Machine Aggregation Group. */
struct c2_cm_aggr_group {
	/** Copy machine to which this aggregation group belongs. */
	struct c2_cm                      *cag_cm;

	struct c2_cm_ag_id                 cag_id;

	const struct c2_cm_aggr_group_ops *cag_ops;

	struct c2_layout                  *cag_layout;

	/**
	 * Number of local copy packets that correspond to this aggregation
	 * group.
	 */
	uint64_t                           cag_cp_nr;

	/** Number of copy packets that have been transformed. */
	struct c2_atomic64		   cag_transformed_cp_nr;

	/** Number of copy packets that are freed. */
	struct c2_atomic64		   cag_freed_cp_nr;

	/**
	 * Linkage into the sorted sliding window queue of aggregation groups
	 * (c2_cm::cm_aggr_grps), sorted by indentifiers.
	 */
	struct c2_tlink			   cag_cm_linkage;

	uint64_t                           cag_magic;
};

struct c2_cm_aggr_group_ops {
	/** Performs aggregation group completion processing. */
	int (*cago_fini)(struct c2_cm_aggr_group *ag);

	/**
	 * Returns number of copy packets corresponding to the aggregation
	 * group on the local node.
	 */
	uint64_t (*cago_local_cp_nr)(const struct c2_cm_aggr_group *ag);
};

extern struct c2_bob_type aggr_grps_bob;

C2_INTERNAL void c2_cm_aggr_group_init(struct c2_cm_aggr_group *ag,
				       struct c2_cm *cm,
				       const struct c2_cm_ag_id *id,
				       const struct c2_cm_aggr_group_ops
				       *ag_ops);

C2_INTERNAL void c2_cm_aggr_group_fini(struct c2_cm_aggr_group *ag);

/**
 * 3-way comparision function to compare two aggregation group IDs.
 *
 * @retval   0 if id0 = id1.
 * @retval < 0 if id0 < id1.
 * @retval > 0 if id0 > id1.
 */
C2_INTERNAL int c2_cm_ag_id_cmp(const struct c2_cm_ag_id *id0,
				const struct c2_cm_ag_id *id1);
/**
 * Searches for an aggregation group for the given "id" in
 * c2_cm::cm_aggr_groups, creates a new one if not found and returns it.
 */
C2_INTERNAL struct c2_cm_aggr_group *c2_cm_aggr_group_find(struct c2_cm *cm,
							   const struct
							   c2_cm_ag_id *id);

/**
 * Adds an aggregation group to a copy machine's list of aggregation groups -
 * c2_cm::cm_aggr_groups. This list is sorted lexicographically based on
 * aggregation group ids.
 *
 * @pre c2_cm_is_locked(cm) == true
 *
*/
C2_INTERNAL void c2_cm_aggr_group_add(struct c2_cm *cm,
				      struct c2_cm_aggr_group *ag);

/**
 * Returns the aggregation group with the highest aggregation group id from the
 * aggregation group list.
 *
 * @pre cm != NULL && c2_cm_is_locked == true
 */
C2_INTERNAL struct c2_cm_aggr_group *c2_cm_ag_hi(struct c2_cm *cm);

/**
 * Returns the aggregation group with the lowest aggregation grou id from the
 * aggregation group list.
 *
 * @pre cm != NULL && c2_cm_is_locked == true
 */
C2_INTERNAL struct c2_cm_aggr_group *c2_cm_ag_lo(struct c2_cm *cm);

C2_TL_DESCR_DECLARE(aggr_grps, C2_INTERNAL);
C2_TL_DECLARE(aggr_grps, C2_INTERNAL, struct c2_cm_aggr_group);

/** @} CMAG */

#endif
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
