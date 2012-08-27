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

#ifndef __COLIBRI_SNS_REPAIR_AG_H__
#define __COLIBRI_SNS_REPAIR_AG_H__

#include "cm/ag.h"

/**
   @defgroup SNSRepairAG SNS Repair aggregation group
   @ingroup SNSRepairCM

   @{
 */

/**
 * SNS repair specific aggregation group.
 */
struct c2_sns_repair_ag {
	struct c2_cm_aggr_group  sag_base;
	/** Transformed copy packet created by transformation function. */
	struct c2_cm_cp         *sag_cp;
};

/** @} SNSRepairAG */

#endif /* __COLIBRI_SNS_REPAIR_AG_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
