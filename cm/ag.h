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
 *                  Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 08/08/2012
 */

#pragma once

#ifndef __MERO_CM_AG_H__
#define __MERO_CM_AG_H__

#include "lib/atomic.h"
#include "lib/types.h"
#include "lib/types_xc.h"
#include "lib/tlist.h"
#include "lib/mutex.h"

/**
   @defgroup CMAG Copy machine aggregation group
   @ingroup CM

   @{
 */

/** Unique aggregation group identifier. */
struct m0_cm_ag_id {
	struct m0_uint128 ai_hi;
	struct m0_uint128 ai_lo;
} M0_XCA_RECORD;

/** Copy Machine Aggregation Group. */
struct m0_cm_aggr_group {
	/** Copy machine to which this aggregation group belongs. */
	struct m0_cm                      *cag_cm;

	struct m0_cm_ag_id                 cag_id;

	const struct m0_cm_aggr_group_ops *cag_ops;

	struct m0_layout                  *cag_layout;

	struct m0_mutex                    cag_mutex;

	/**
	 * Number of global copy packets that correspond to this aggregation
	 * group.
	 */
	uint64_t                           cag_cp_global_nr;

	/**
	 * Number of local copy packets that correspond to this aggregation
	 * group.
	 */
	uint64_t                           cag_cp_local_nr;

	/** Number of copy packets that have been transformed. */
	uint64_t                           cag_transformed_cp_nr;

	/** Number of copy packets that are freed. */
	uint64_t                           cag_freed_cp_nr;

	/**
	 * Linkage into the sorted sliding window queue of aggregation groups
	 * (m0_cm::cm_aggr_grps), sorted by indentifiers.
	 */
	struct m0_tlink			   cag_cm_linkage;

	uint64_t                           cag_magic;
};

struct m0_cm_aggr_group_ops {
	/** Performs aggregation group completion processing. */
	int (*cago_fini)(struct m0_cm_aggr_group *ag);

	/**
	 * Returns number of copy packets corresponding to the aggregation
	 * group on the local node.
	 */
	uint64_t (*cago_local_cp_nr)(const struct m0_cm_aggr_group *ag);
};

extern struct m0_bob_type aggr_grps_bob;

M0_INTERNAL void m0_cm_aggr_group_init(struct m0_cm_aggr_group *ag,
				       struct m0_cm *cm,
				       const struct m0_cm_ag_id *id,
				       const struct m0_cm_aggr_group_ops
				       *ag_ops);

M0_INTERNAL void m0_cm_aggr_group_fini(struct m0_cm_aggr_group *ag);

/**
 * 3-way comparision function to compare two aggregation group IDs.
 *
 * @retval   0 if id0 = id1.
 * @retval < 0 if id0 < id1.
 * @retval > 0 if id0 > id1.
 */
M0_INTERNAL int m0_cm_ag_id_cmp(const struct m0_cm_ag_id *id0,
				const struct m0_cm_ag_id *id1);
/**
 * Searches for an aggregation group for the given "id" in
 * m0_cm::cm_aggr_groups, creates a new one if not found and returns it.
 */
M0_INTERNAL struct m0_cm_aggr_group *m0_cm_aggr_group_find(struct m0_cm *cm,
							   const struct
							   m0_cm_ag_id *id);

/**
 * Adds an aggregation group to a copy machine's list of aggregation groups -
 * m0_cm::cm_aggr_groups. This list is sorted lexicographically based on
 * aggregation group ids.
 *
 * @pre m0_cm_is_locked(cm) == true
 *
*/
M0_INTERNAL void m0_cm_aggr_group_add(struct m0_cm *cm,
				      struct m0_cm_aggr_group *ag);

/**
 * Returns the aggregation group with the highest aggregation group id from the
 * aggregation group list.
 *
 * @pre cm != NULL && m0_cm_is_locked == true
 */
M0_INTERNAL struct m0_cm_aggr_group *m0_cm_ag_hi(struct m0_cm *cm);

/**
 * Returns the aggregation group with the lowest aggregation grou id from the
 * aggregation group list.
 *
 * @pre cm != NULL && m0_cm_is_locked == true
 */
M0_INTERNAL struct m0_cm_aggr_group *m0_cm_ag_lo(struct m0_cm *cm);

M0_INTERNAL void m0_cm_ag_lock(struct m0_cm_aggr_group *ag);
M0_INTERNAL void m0_cm_ag_unlock(struct m0_cm_aggr_group *ag);

M0_TL_DESCR_DECLARE(aggr_grps, M0_EXTERN);
M0_TL_DECLARE(aggr_grps, M0_INTERNAL, struct m0_cm_aggr_group);

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
