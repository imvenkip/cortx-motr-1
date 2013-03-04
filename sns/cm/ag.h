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

/**
 * Represents target cob and offset within the cob.
 * This is populated on creation of the aggregation group.
 */
struct m0_sns_cm_ag_tgt_addr {
	/*
	 * cob fid containing the target unit for the aggregation
	 * group.
	 */
	struct m0_fid                tgt_cobfid;

	/** Target unit offset within the cob identified by tgt_cobfid. */
	uint64_t                     tgt_cob_index;
};

struct m0_sns_cm_ag {
	/** Base aggregation group. */
	struct m0_cm_aggr_group       sag_base;

	/** Total number of failure units in this aggregation group. */
	uint64_t                      sag_fnr;

	/**
	 * Accumulator copy packets.
	 * Number of accumulator copy packets is equivalent to the total
	 * number of failure units in this aggregation group.
	 */
	struct m0_sns_cm_cp          *sag_accs;

	/**
	 * Target unit cob id and offset within the cob.
	 * Number of targets are equivalent to number of failures in the
	 * aggregation group, i.e. m0_sns_cm_ag::sag_fnr.
	 */
	struct m0_sns_cm_ag_tgt_addr *sag_tgts;
};


/**
 * Allocates and initializes aggregation group for the given m0_cm_ag_id.
 * Every sns copy machine aggregation group maintains accumulator copy packets,
 * equivalent to the number of failed units in the aggregation group. During
 * initialisation, the buffers are acquired for the accumulator copy packets
 * from the copy machine buffer pool.
 * Caller is responsible to lock the copy machine before calling this function.
 * @pre m0_cm_is_locked(cm) == true
 */
M0_INTERNAL int m0_sns_cm_ag_alloc(struct m0_cm *cm,
				   const struct m0_cm_ag_id *id,
				   struct m0_cm_aggr_group **out);

/*
 * Configures accumulator copy packet, acquires buffer for accumulator copy
 * packet.
 * Increments struct m0_cm_aggr_group::cag_cp_local_nr for newly created
 * accumulator copy packets, so that aggregation group is not finalised before
 * the finalisation of accumulator copy packets.
 *
 * @see m0_sns_cm_acc_cp_setup()
 */
M0_INTERNAL int m0_sns_cm_ag_setup(struct m0_sns_cm_ag *ag);

M0_INTERNAL struct m0_sns_cm_ag *ag2snsag(const struct m0_cm_aggr_group *ag);

M0_INTERNAL void agid2fid(const struct m0_cm_aggr_group *ag,
			  struct m0_fid *fid);
M0_INTERNAL uint64_t agid2group(const struct m0_cm_aggr_group *ag);

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
