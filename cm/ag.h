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

#ifndef __COLIBRI_CM_AG_H__
#define __COLIBRI_CM_AG_H__

#include "lib/types.h"
#include "lib/tlist.h"
#include "lib/mutex.h"
#include "lib/ext.h"
#include "lib/vec.h"

#include "cm/cm.h"

/**
   @defgroup CMAG Copy machine aggregation group
   @ingroup CM

   @{
 */
/** Aggregation group states */
enum c2_aggr_group_state {
	/**
	 * Aggregation group data structure is initialised and is ready for
	 * processing.
	 */
	C2_AGS_INITIALISED,
	/**
	 * The aggregation group is being processed by the copy packets in the
	 * pipeline.
	 */
	C2_AGS_IN_PROCESS,
	/** The restructuring for this aggregation group has been completed. */
	C2_AGS_FINALISED
};

/** Copy Machine Aggregation Group. */
struct c2_cm_aggr_group {
	/** Parent copy machine. */
	struct c2_cm                      *cag_cm;

	/** Aggregation group id */
	struct c2_uint128		   cag_id;

	/** Aggregation state. */
	enum c2_aggr_group_state           cag_state;

	/** Its operations. */
	const struct c2_cm_aggr_group_ops *cag_ops;

	/** Number of copy packets that correspond to this aggregation group. */
	uint64_t                           cag_cp_nr;

	/** Number of copy packets that are transformed. */
	uint64_t                           cag_transformed_cp_nr;

	/**
	 * Linkage into the sorted sliding window queue of aggregation group
	 * ids, Hanging to c2_cm_sw::sw_aggr_grps.
	 */
	struct c2_tlink			   cag_sw_linkage;

	/** Mutex lock to protect this group. */
	struct c2_mutex                    cag_lock;

	uint64_t                           cag_magic;
};

/** Colibri Copy Machine Aggregation Group Operations */
struct c2_cm_aggr_group_ops {
	/** Aggregation group processing completion notification. */
	int (*cago_completed)(struct c2_cm_aggr_group *ag);

	/**
	 * Returns number of copy packets corresponding to the aggregation
	 * group on the local node. Typically this is calculated as,
	 * number of data units per node * unit size / network buffer size.
	 */
	uint64_t (*cago_cp_nr)(struct c2_cm_aggr_group *ag);
};

C2_TL_DESCR_DECLARE(aggr_grps, extern);
C2_TL_DECLARE(aggr_grps, extern, struct c2_cm_aggr_group);

/** @} CMAG */
#endif
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
