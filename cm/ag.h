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

struct m0_cm_cp;

/** Unique aggregation group identifier. */
struct m0_cm_ag_id {
	struct m0_uint128 ai_hi;
	struct m0_uint128 ai_lo;
} M0_XCA_RECORD;

struct m0_cm_ag_sw {
	struct m0_cm_ag_id     sw_lo;
	struct m0_cm_ag_id     sw_hi;
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

	/** If this group has incoming copy packets or not. */
	bool                               cag_has_incoming;

	/**
	 * Linkage into the sorted queue of aggregation groups having incoming
	 * copy packets to this copy machine replica, sorted by identifiers.
	 *
	 * @see m0_cm::cm_aggr_groups_in
	 * @see m0_cm_aggr_group_add()
	 * @see m0_cm_aggr_group_locate()
	 */
	struct m0_tlink			   cag_cm_in_linkage;

	/**
	 * Linkage into the sorted queue of aggregation groups having outgoing
	 * copy packets from this copy machine replica, sorted by identifiers.
	 *
	 * @see m0_cm::cm_aggr_groups_out
	 */
	struct m0_tlink			   cag_cm_out_linkage;

	uint64_t                           cag_magic;
};

struct m0_cm_aggr_group_ops {
	/**
	 * Checks if aggregation group can be fianlised using members of the
	 * aggregation group and copy packet to be finalised.y
	 * @pre ag != NULL && cp != NULL
	 */
	bool (*cago_ag_can_fini) (struct m0_cm_aggr_group *ag,
				  struct m0_cm_cp *cp);

	/** Performs aggregation group completion processing. */
	void (*cago_fini)(struct m0_cm_aggr_group *ag);

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
				       bool has_incoming,
				       const struct m0_cm_aggr_group_ops
				       *ag_ops);

M0_INTERNAL void m0_cm_aggr_group_fini(struct m0_cm_aggr_group *ag);

/*
 * Finalises the given aggregation group and also updates the sliding window.
 * If there is no more data to be restructured, marks the operation as complete
 * by invoking m0_cm_ops::cmo_complete().
 */
M0_INTERNAL void m0_cm_aggr_group_fini_and_progress(struct m0_cm_aggr_group *ag);
/**
 * 3-way comparision function to compare two aggregation group IDs.
 *
 * @retval   0 if id0 = id1.
 * @retval < 0 if id0 < id1.
 * @retval > 0 if id0 > id1.
 */
M0_INTERNAL int m0_cm_ag_id_cmp(const struct m0_cm_ag_id *id0,
				const struct m0_cm_ag_id *id1);

M0_INTERNAL bool m0_cm_ag_id_is_set(const struct m0_cm_ag_id *id);

/**
 * Searches for an aggregation group for the given "id" in
 * m0_cm::cm_aggr_groups_in or m0_cm::cm_aggr_groups_out, depending on the value
 * of "has_incoming" function parameter. Thus if has_incoming == true, then the
 * given aggregation group has incoming copy packets from other replicas and it
 * should be present and searched in m0_cm::cm_aggr_groups_in.
 * If has_incoming == false, then the given aggregation group does not have any
 * incoming copy packets, and thus can be found in m0_cm::cm_aggr_groups_out.
 * There's also a possibility that initially it was discovered that the given
 * aggregation group has incoming copy packets and thus was added to m0_cm::
 * cm_aggr_groups_in, later it was discovered that the given aggregation group
 * also has outgoing copy packets from this copy machine replica, and the
 * m0_cm_aggr_group_locate() was invoked in such a situation with has_incoming =
 * false. Thus in this case as has_incoming == false, we look into m0_cm::
 * cm_aggr_groups_out first, if not found, we double check in m0_cm::
 * cm_aggr_groups_in list, if found in m0_cm::cm_aggr_groups_in list, and
 * the aggregation group also has outgoing copy packets, then the aggregation
 * group is also added to m0_cm::cm_aggr_groups_out list.
 *
 * @see struct m0_cm::cm_aggr_groups_in
 * @see struct m0_cm::cm_aggr_groups_out
 */
M0_INTERNAL struct m0_cm_aggr_group *m0_cm_aggr_group_locate(struct m0_cm *cm,
							     const struct
							     m0_cm_ag_id *id,
							     bool has_incoming);

/**
 * Allocates the aggregation group by invoking m0_cm_ops::cmo_ag_alloc() and
 * adds the aggregation group to m0_cm::cm_aggr_groups_in or m0_cm::
 * cm_aggr_groups_out list depending on the given "has_incoming" function
 * parameter.
 */
M0_INTERNAL int m0_cm_aggr_group_alloc(struct m0_cm *cm,
				       const struct m0_cm_ag_id *id,
				       bool has_incoming,
				       struct m0_cm_aggr_group **out);

/**
 * Adds an aggregation group to a copy machine's, m0_cm::cm_aggr_groups_in or
 * m0_cm::cm_aggr_groups_out list of aggregation groups depending on the given
 * value of "has_incoming" parameter. Thus if "has_incoming == true" then the
 * aggregation group is added to m0_cm::cm_aggr_groups_in, else to m0_cm::
 * cm_aggr_groups_out.
 * Aggregation groups list are sorted lexicographically based on aggregation
 * group ids.
 *
 * @pre m0_cm_is_locked(cm) == true
 *
*/
M0_INTERNAL void m0_cm_aggr_group_add(struct m0_cm *cm,
				      struct m0_cm_aggr_group *ag,
				      bool has_incoming);

/**
 * Returns the aggregation group with the highest aggregation group id from the
 * m0_cm::cm_aggr_groups_in aggregation group list.
 *
 * @pre cm != NULL && m0_cm_is_locked == true
 */
M0_INTERNAL struct m0_cm_aggr_group *m0_cm_ag_hi(struct m0_cm *cm);

/**
 * Returns the aggregation group with the lowest aggregation grou id from the
 * m0_cm::cm_aggr_groups_in aggregation group list.
 *
 * @pre cm != NULL && m0_cm_is_locked == true
 */
M0_INTERNAL struct m0_cm_aggr_group *m0_cm_ag_lo(struct m0_cm *cm);

/**
 * Advances the sliding window Hi as far as it can, by invoking m0_cm_ops::
 * cmo_ag_next() until it fails.
 * For every next relevant (i.e. having incoming copy packets) aggregation
 * group identifier an aggregation group is created and added to the
 * m0_cm::cm_aggr_group_in list.
 */
M0_INTERNAL int m0_cm_ag_advance(struct m0_cm *cm);

M0_INTERNAL bool m0_cm_aggr_group_tlists_are_empty(struct m0_cm *cm);

M0_INTERNAL void m0_cm_ag_lock(struct m0_cm_aggr_group *ag);
M0_INTERNAL void m0_cm_ag_unlock(struct m0_cm_aggr_group *ag);

M0_TL_DESCR_DECLARE(aggr_grps_in, M0_EXTERN);
M0_TL_DECLARE(aggr_grps_in, M0_INTERNAL, struct m0_cm_aggr_group);

M0_TL_DESCR_DECLARE(aggr_grps_out, M0_EXTERN);
M0_TL_DECLARE(aggr_grps_out, M0_INTERNAL, struct m0_cm_aggr_group);

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
