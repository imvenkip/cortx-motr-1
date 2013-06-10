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
	M0_RM_ADDB_LOC_CREDIT_ALLOC         = 10,
	M0_RM_ADDB_LOC_CAPITAL_ALLOC        = 11,
	M0_RM_ADDB_LOC_INCOMING_ALLOC       = 12,
	M0_RM_ADDB_LOC_LOAN_ALLOC           = 13,
	M0_RM_ADDB_LOC_REMOTE_ALLOC         = 14,
	M0_RM_ADDB_LOC_HELD_CREDIT_ALLOC    = 15,
	M0_RM_ADDB_LOC_PIN_ALLOC            = 16,
	M0_RM_ADDB_LOC_CREDIT_BUF_ALLOC     = 17,
	M0_RM_ADDB_LOC_RESOURCE_BUF_ALLOC   = 18,
	M0_RM_ADDB_LOC_RM_OUT_ALLOC         = 19,
	M0_RM_ADDB_LOC_REQ_FOM_ALLOC        = 20,
	M0_RM_ADDB_LOC_REMOTE_SESSION_ALLOC = 21,
	M0_RM_ADDB_LOC_OWNER_ALLOC          = 22,
	M0_RM_ADDB_LOC_SERVICE_ALLOC        = 23,
	M0_RM_ADDB_LOC_RESOURCE_TYPE_ALLOC  = 24,
	M0_RM_ADDB_LOC_OWNER_CREDIT_ALLOC   = 25,
	M0_RM_ADDB_LOC_FILE_ALLOC           = 26,

	/* Resource manager function fail locations */
	M0_RM_ADDB_LOC_CREDIT_GET_FAIL      = 30,
	M0_RM_ADDB_LOC_REVOKE_FAIL          = 31,
	M0_RM_ADDB_LOC_BORROW_FAIL          = 32,
	M0_RM_ADDB_LOC_CREDITOR_INVALID     = 33,
	M0_RM_ADDB_LOC_RESOURCE_LOCATE_FAIL = 34,

	/* Resource manager record identifiers */
	M0_ADDB_RECID_RM_BORROW_RATE        = 1600,
	M0_ADDB_RECID_RM_REVOKE_RATE        = 1601,
	M0_ADDB_RECID_RM_BORROW_TIMES       = 1602,
	M0_ADDB_RECID_RM_REVOKE_TIMES       = 1603,
	M0_ADDB_RECID_RM_CREDIT_TIMES       = 1604,
};

/* Counter for Number of borrows and revokes */
M0_ADDB_RT_CNTR(m0_addb_rt_rm_borrow_rate, M0_ADDB_RECID_RM_BORROW_RATE,
		100, 200, 300, 400, 500, 600, 700, 800, 900);

M0_ADDB_RT_CNTR(m0_addb_rt_rm_revoke_rate, M0_ADDB_RECID_RM_REVOKE_RATE,
		100, 200, 300, 400, 500, 600, 700, 800, 900);

/* Counter for timing required to perform borrow or revokes */
M0_ADDB_RT_CNTR(m0_addb_rt_rm_borrow_times,  M0_ADDB_RECID_RM_BORROW_TIMES,
		100, 200, 300, 400, 500, 600, 700, 800, 900);

M0_ADDB_RT_CNTR(m0_addb_rt_rm_revoke_times,  M0_ADDB_RECID_RM_REVOKE_TIMES,
		100, 200, 300, 400, 500, 600, 700, 800, 900);

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
