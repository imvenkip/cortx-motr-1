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

#ifndef __MERO_RM_RM_ADDB_H__
#define __MERO_RM_RM_ADDB_H__

#include "addb/addb.h"
#include "addb2/identifier.h"

/**
 * @addtogroup rm
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
	M0_ADDB_CTXID_RM_MOD    = 1600,
	M0_ADDB_CTXID_RMS_SERV  = 1601,
};

M0_ADDB_CT(m0_addb_ct_rm_mod, M0_ADDB_CTXID_RM_MOD);
M0_ADDB_CT(m0_addb_ct_rms_serv, M0_ADDB_CTXID_RMS_SERV, "hi", "low");

/*
 ******************************************************************************
 * Resource Manager ADDB posting locations
 ******************************************************************************
 */
enum {
	/* Resource manager allocation locations */
	M0_RM_ADDB2_CREDIT_ALLOC = M0_AVI_RM_RANGE_START + 1,
	M0_RM_ADDB2_CAPITAL_ALLOC,
	M0_RM_ADDB2_INCOMING_ALLOC,
	M0_RM_ADDB2_LOAN_ALLOC,
	M0_RM_ADDB2_REMOTE_ALLOC,
	M0_RM_ADDB2_HELD_CREDIT_ALLOC,
	M0_RM_ADDB2_PIN_ALLOC,
	M0_RM_ADDB2_CREDIT_BUF_ALLOC,
	M0_RM_ADDB2_RESOURCE_BUF_ALLOC,
	M0_RM_ADDB2_RM_OUT_ALLOC,
	M0_RM_ADDB2_REQ_FOM_ALLOC,
	M0_RM_ADDB2_REMOTE_SESSION_ALLOC,
	M0_RM_ADDB2_OWNER_ALLOC,
	M0_RM_ADDB2_SERVICE_ALLOC,
	M0_RM_ADDB2_RESOURCE_TYPE_ALLOC,
	M0_RM_ADDB2_OWNER_CREDIT_ALLOC,
	M0_RM_ADDB2_FILE_ALLOC,
	M0_RM_ADDB2_RMSVC_OWNER_ALLOC,

	/* Resource manager function fail locations */
	M0_RM_ADDB_LOC_CREDIT_GET_FAIL      = 30,
	M0_RM_ADDB_LOC_REVOKE_FAIL          = 31,
	M0_RM_ADDB_LOC_BORROW_FAIL          = 32,
	M0_RM_ADDB_LOC_CREDITOR_INVALID     = 33,
	M0_RM_ADDB_LOC_RESOURCE_LOCATE_FAIL = 34,

	/* Resource manager record identifiers */
	M0_ADDB_RECID_RM_LOCAL_RATE         = 1600,
	M0_ADDB_RECID_RM_BORROW_RATE        = 1601,
	M0_ADDB_RECID_RM_REVOKE_RATE        = 1602,
	M0_ADDB_RECID_RM_LOCAL_TIMES        = 1603,
	M0_ADDB_RECID_RM_BORROW_TIMES       = 1604,
	M0_ADDB_RECID_RM_REVOKE_TIMES       = 1605,
	M0_ADDB_RECID_RM_CREDIT_TIMES       = 1606,
};

/**
 * @todo local_rate and local_time counters are only initialised,
 * update them appropriately.
 */

/* Counter for Number of local requests, borrows and revokes */
M0_ADDB_RT_CNTR(m0_addb_rt_rm_local_rate, M0_ADDB_RECID_RM_LOCAL_RATE,
		100, 200, 300, 400, 500, 600, 700, 800, 900);

M0_ADDB_RT_CNTR(m0_addb_rt_rm_borrow_rate, M0_ADDB_RECID_RM_BORROW_RATE,
		100, 200, 300, 400, 500, 600, 700, 800, 900);

M0_ADDB_RT_CNTR(m0_addb_rt_rm_revoke_rate, M0_ADDB_RECID_RM_REVOKE_RATE,
		100, 200, 300, 400, 500, 600, 700, 800, 900);

/* Counter for timing required to perform local requests, borrow or revokes */
M0_ADDB_RT_CNTR(m0_addb_rt_rm_local_times,  M0_ADDB_RECID_RM_LOCAL_TIMES,
		100, 200, 300, 400, 500, 600, 700, 800, 900);

M0_ADDB_RT_CNTR(m0_addb_rt_rm_borrow_times,  M0_ADDB_RECID_RM_BORROW_TIMES,
		100, 200, 300, 400, 500, 600, 700, 800, 900);

M0_ADDB_RT_CNTR(m0_addb_rt_rm_revoke_times,  M0_ADDB_RECID_RM_REVOKE_TIMES,
		100, 200, 300, 400, 500, 600, 700, 800, 900);

/**
 * Time for which credit was held
 * T(m0_rm_credit_put) - T(m0_rm_credit_get)
 */
M0_ADDB_RT_CNTR(m0_addb_rt_rm_credit_times,  M0_ADDB_RECID_RM_CREDIT_TIMES,
		100, 200, 300, 400, 500, 600, 700, 800, 900);

/** @} end of rm group */

#endif /* __MERO_RM_RM_ADDB_H__ */


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
