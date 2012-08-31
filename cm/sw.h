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
 *                  Subhash Arya  <subhash_arya@xyratex.com>
 * Original creation date: 11/11/2011
 */

#pragma once

#ifndef __COLIBRI_CM_SW_H__
#define __COLIBRI_CM_SW_H__

#include "lib/tlist.h"  /* struct c2_tlink */

/**
   @defgroup CMSW Copy machine sliding window
   @ingroup CM
   @{

 */

enum c2_cm_sw_intent {
	C2_SW_ADVANCE = 1,
	C2_SW_SLIDE,
};

/**
 * While copy machine is processing a restructuring request, each replica
 * maintains a "sliding window" (SW), indicating how far it gets. This window is
 * a pair of group identifiers [LO, HI), with LO <= HI. The following invariant
 * is maintained:
 *
 * - the replica already completely processed all groups it had to process
 *   with identifiers less than LO;
 *
 * - the replica is ready to accept copy packets for groups with identifiers ID
 *   such that LO <= ID < HI.
 */
struct c2_cm_sw {
        /** Sliding window operations. */
        const struct c2_cm_sw_ops  *sw_ops;

        /** List of aggregation groups being processed by the copy machine.*/
        struct c2_tl                sw_aggr_grps;

        /** Upper bound of this sliding window. */
        struct c2_cm_aggr_group    *sw_high;

        /** Lower bound of this sliding window. */
        struct c2_cm_aggr_group    *sw_low;
};

/** Copy Machine sliding window operations. */
struct c2_cm_sw_ops {
	/**
	 * Increase the sw_high. Such that HI := NEXT (HI).
	 * Here, NEXT (X) = min{ id | id >= X and aggregation group has packets
	 * for this replica }
	 * NEXT (X) will run in a loop until it finds an aggregation group
	 * that needs processing.
	 */
	int  (*swo_advance)(struct c2_cm_sw *sw);
	/**
	 * Increase the sw_high and sw_low. Such that
	 * HI := NEXT (HI)
	 * LO := NEXT (LO + 1)
	 */
	int  (*swo_slide)(struct c2_cm_sw *sw);

	/**
	 * Performs checks if required resources are availble to create a new
	 * copy packet and returns true or false accordingly.
	 */
	bool (*swo_has_space)(struct c2_cm_sw *sw);

	/**
	 * Searches for an aggregation group in c2_cm_sw::sw_aggr_grps list for
	 * the given aggregation group id. If not found, creates a new
	 * aggregation group and updates the sliding window accordingly.
	 */
	struct c2_cm_aggr_group *(swo_ag_find)(struct c2_cm_sw *sw,
					       struct c2_uint128 *ag_id);
};

/** Initialises sliding window. */
int c2_cm_sw_init(struct c2_cm_sw *slw, const struct c2_cm_sw_ops *ops);

/** Finalises sliding window. */
void c2_cm_sw_fini(struct c2_cm_sw *slw);

void c2_cm_sw_update(struct c2_cm_sw *slw, struct c2_cm_aggr_group *ag,
		     int intent);
/** @} CMSW */
#endif /* __COLIBRI_CM_SW_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

