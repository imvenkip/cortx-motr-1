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
 * Original creation date: 16/04/2012
 */

#ifndef __COLIBRI_SNS_REPAIR_AG_H__
#define __COLIBRI_SNS_REPAIR_AG_H__

#include "cm/cm.h"
#include "cm/ag.h"

/**
   @defgroup SNSRepairAG SNS Repair aggregation group
   @ingroup SNSRepairCM

   @{
*/

struct c2_net_buffer;
struct c2_cm_cp;

struct c2_sns_repair_aggr_group {
	/** Base aggregation group. */
	struct c2_cm_aggr_group  sag_base;
	/** Transformed copy packet created by transformation function. */
	struct c2_cm_cp         *sag_cp;
};

/** @} SNSRepairAG */
/* __COLIBRI_SNS_REPAIR_AG_H__ */

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
