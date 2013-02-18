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
 * Original author: Manish Honap <manish_honap@xyratex.com>
 * Original creation date: 13-Feb-2013
 */


#pragma once

#ifndef __MERO_RM_RMSERVICE_ADDB_H__
#define __MERO_RM_RMSERVICE_ADDB_H__

#include "addb/addb.h"

/**
 * @addtogroup rm_service
 *
 * @{
 */

/*
 ******************************************************************************
 * RM Service ADDB context types.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_RMS_MOD  = 1600,
	M0_ADDB_CTXID_RMS_SERV = 1601,
};

M0_ADDB_CT(m0_addb_ct_rms_mod, M0_ADDB_CTXID_RMS_MOD);
M0_ADDB_CT(m0_addb_ct_rms_serv, M0_ADDB_CTXID_RMS_SERV, "hi", "low");

/*
 ******************************************************************************
 * RM Service handler ADDB posting locations
 ******************************************************************************
 */
enum {
	M0_RMS_ADDB_LOC_ALLOCATE = 10,
};

/** @} end of rm_service group */

#endif /* __MERO_RM_RMSERVICE_ADDB_H__ */


/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
