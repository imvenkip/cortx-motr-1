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
 * Original creation date: 08/08/2012
 */

#pragma once

#ifndef __COLIBRI_CM_AG_H__
#define __COLIBRI_CM_AG_H__

#include "lib/types.h"
#include "lib/tlist.h"

/**
   @defgroup CMAG Copy machine aggregation group
   @ingroup CM

   @{
 */

struct c2_cm_ag_id {
	struct c2_uint128 ai_hi;
	struct c2_uint128 ai_lo;
};

/** Copy Machine Aggregation Group. */
struct c2_cm_aggr_group {
	struct c2_cm                      *cag_cm;

	struct c2_cm_ag_id                 cag_id;

	const struct c2_cm_aggr_group_ops *cag_ops;

	struct c2_layout                  *cag_layout;

	/** Number of copy packets that correspond to this aggregation group. */
	uint64_t                           cag_cp_nr;

	/** Number of copy packets that are transformed. */
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

/** Colibri Copy Machine Aggregation Group Operations */
struct c2_cm_aggr_group_ops {
	/** Aggregation group processing completion notification. */
	int (*cago_completed)(struct c2_cm_aggr_group *ag);

	/**
	 * Returns number of copy packets corresponding to the aggregation
	 * group on the local node. For example, for sns repair copy machine,
	 * this is calculated as
	 * number of data units per node * unit size / network buffer size.
	 */
	uint64_t (*cago_cp_nr)(struct c2_cm_aggr_group *ag);

	/**
	 * 3-way comparision function for comparing aggregation groups.
	 * retval -1 if ag1 < ag2.
	 * retval 0  if ag1 = ag2.
	 * retval 1  if ag1 > ag2.
	 */
	int (*cago_ag_cmp)(const struct c2_cm_aggr_group *ag1,
			   const struct c2_cm_aggr_group *ag2);
};

int c2_cm_ag_id_cmp(const struct c2_cm_ag_id *id0, const struct c2_cm_ag_id *id1);

/**
 * Searches for an aggregation group for the given "id" in c2_cm::cm_aggr_groups,
 * creates a new one if not found.
 */
struct c2_cm_aggr_group *c2_cm_aggr_group_find(const struct c2_cm *cm,
					       const struct c2_cm_ag_id *id);
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
