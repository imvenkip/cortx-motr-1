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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 *                  Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 08/06/2012
 */

#pragma once

#ifndef __MERO_SNS_REPAIR_CP_H__
#define __MERO_SNS_REPAIR_CP_H__

#include "lib/ext.h"

#include "stob/stob_id.h"
#include "cm/cp.h"

/**
   @defgroup SNSRepairCP SNS Repair Copy packet
   @ingroup SNSRepairCM

 */

struct m0_sns_repair_cp {
	struct m0_cm_cp    rc_base;

	/** Read/write stob id. */
	struct m0_stob_id  rc_sid;

	/** Offset within the stob. */
	m0_bindex_t        rc_index;

	/** Stob IO context. */
	struct m0_stob_io  rc_stio;

	/** Stob context. */
	struct m0_stob    *rc_stob;
};

M0_INTERNAL struct m0_sns_repair_cp *cp2snscp(const struct m0_cm_cp *cp);

/**
 * Uses GOB fid key and parity group number to generate a scalar to
 * help select a request handler locality for copy packet FOM.
 */
M0_INTERNAL uint64_t cp_home_loc_helper(const struct m0_cm_cp *cp);

extern const struct m0_cm_cp_ops m0_sns_repair_cp_ops;

/** Transformation phase function for copy packet. */
M0_INTERNAL int m0_sns_repair_cp_xform(struct m0_cm_cp *cp);

/** Copy packet read phase function. */
M0_INTERNAL int m0_sns_repair_cp_read(struct m0_cm_cp *cp);

/** Copy packet write phase function. */
M0_INTERNAL int m0_sns_repair_cp_write(struct m0_cm_cp *cp);

/** Copy packet IO wait phase function. */
M0_INTERNAL int m0_sns_repair_cp_io_wait(struct m0_cm_cp *cp);

M0_INTERNAL int m0_sns_repair_cp_send(struct m0_cm_cp *cp);

M0_INTERNAL int m0_sns_repair_cp_recv(struct m0_cm_cp *cp);

M0_INTERNAL int m0_sns_repair_cp_phase_next(struct m0_cm_cp *cp);

/** @} SNSRepairCP */
#endif /* __MERO_SNS_REPAIR_CP_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
