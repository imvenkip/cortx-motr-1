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
 * Original author: Rohan Puri <Rohan_Puri@xyratex.com>
 * Original creation date: 12/04/2012
 */

#pragma once

#ifndef __MERO_SNS_SNS_ADDB_H__
#define __MERO_SNS_SNS_ADDB_H__

#include "addb/addb.h"

/**
   @addtogroup SNSRepairCM
   @{
 */

/*
 ******************************************************************************
 * SNS REPAIR Service ADDB context types.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_SNS_MOD         = 400,
	M0_ADDB_CTXID_SNS_REPAIR_SERV = 401,
};

M0_ADDB_CT(m0_addb_ct_sns_mod, M0_ADDB_CTXID_SNS_MOD);
M0_ADDB_CT(m0_addb_ct_sns_repair_serv, M0_ADDB_CTXID_SNS_REPAIR_SERV,
	   "hi", "low");


/** @} */ /* end of SNSRepairCM group */

#endif /* __MERO_SNS_REPAIR_ADDB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

