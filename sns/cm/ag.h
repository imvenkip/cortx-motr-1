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
 * Original author: Anup Barve <anup_barve@xyratex.com>
 * Original creation date: 04/16/2012
 */

#pragma once

#ifndef __MERO_SNS_CM_AG_H__
#define __MERO_SNS_CM_AG_H__


#include "cm/ag.h"
#include "sns/cm/cm.h"
#include "sns/cm/cp.h"

/**
   @defgroup SNSCMAG SNS copy machine aggregation group
   @ingroup SNSCM

   @{
 */

struct m0_sns_cm;

struct m0_sns_cm_ag {
	/** Base aggregation group. */
	struct m0_cm_aggr_group          sag_base;

	/** Total number of failure units in this aggregation group. */
	uint32_t                         sag_fnr;

	/**
	 * Actual number of incoming data/parity units for this aggregation
	 * group.
	 */
	uint32_t                         sag_incoming_nr;

	/** Bitmap of failed units in the aggregation group. */
	struct m0_bitmap                 sag_fmap;

	/** If this aggregation group has local spare units on the replica. */
	bool                             sag_is_relevant;
};

/**
 * Initialises given sns specific generic aggregation group.
 * Invokes m0_cm_aggr_group_init().
 */
M0_INTERNAL int m0_sns_cm_ag_init(struct m0_sns_cm_ag *sag,
				  struct m0_cm *cm,
				  const struct m0_cm_ag_id *id,
				  const struct m0_cm_aggr_group_ops *ag_ops,
				  bool has_incoming);

/**
 * Finalises given sns specific generic aggregation group.
 * Invokes m0_cm_aggr_group_fini().
 */
M0_INTERNAL void m0_sns_cm_ag_fini(struct m0_sns_cm_ag *sag);

/**
 * Returns number of copy packets corresponding to the units local to the
 * given node for an aggregation group.
 */
M0_INTERNAL uint64_t m0_sns_cm_ag_local_cp_nr(const struct m0_cm_aggr_group *ag);

M0_INTERNAL struct m0_sns_cm_ag *ag2snsag(const struct m0_cm_aggr_group *ag);

M0_INTERNAL void agid2fid(const struct m0_cm_ag_id *id,
			  struct m0_fid *fid);

M0_INTERNAL uint64_t agid2group(const struct m0_cm_ag_id *id);

M0_INTERNAL void m0_sns_cm_ag_agid_setup(const struct m0_fid *gob_fid, uint64_t group,
					 struct m0_cm_ag_id *agid);

M0_INTERNAL struct m0_cm *snsag2cm(const struct m0_sns_cm_ag *sag);

/** @} SNSCMAG */

#endif /* __MERO_SNS_CM_AG_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
