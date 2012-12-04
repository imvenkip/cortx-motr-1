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

#ifndef __MERO_SNS_REPAIR_AG_H__
#define __MERO_SNS_REPAIR_AG_H__


#include "cm/ag.h"
#include "sns/repair/cm.h"

/**
   @defgroup SNSRepairAG SNS Repair aggregation group
   @ingroup SNSRepairCM

   @{
 */

struct m0_sns_repair_cm;

struct m0_sns_repair_ag {
	/** Base aggregation group. */
	struct m0_cm_aggr_group      sag_base;

	/** Transformed copy packet created by transformation function. */
	struct m0_cm_cp             *sag_cp;

	/**
	 * COB fid of the cob containing the spare unit for this aggregation
	 * group.
	 */
	struct m0_fid                sag_spare_cobfid;

	/** Spare unit index into the COB identified by sag_spare_cobfid. */
	uint64_t                     sag_spare_cob_index;
};


/**
 * Finds aggregation group for the given m0_cm_ag_id in m0_cm::cm_aggr_grps
 * list. If not found, a new aggregation group is allocated with the given id
 * and returned. Caller is responsible to lock the copy machine before calling
 * this function.
 * @pre m0_cm_is_locked(cm) == true
 */
M0_INTERNAL struct m0_sns_repair_ag *m0_sns_repair_ag_find(struct
							   m0_sns_repair_cm
							   *rcm,
							   const struct
							   m0_cm_ag_id *id);

M0_INTERNAL struct m0_sns_repair_ag *ag2snsag(const struct m0_cm_aggr_group
					      *ag);

M0_INTERNAL void agid2fid(const struct m0_cm_aggr_group *ag,
			  struct m0_fid *fid);
M0_INTERNAL uint64_t agid2group(const struct m0_cm_aggr_group *ag);

/** @} SNSRepairAG */

#endif /* __MERO_SNS_REPAIR_AG_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
