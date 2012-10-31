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

#ifndef __COLIBRI_SNS_REPAIR_CP_H__
#define __COLIBRI_SNS_REPAIR_CP_H__

#include "lib/ext.h"

#include "stob/stob_id.h"
#include "cm/cp.h"

/**
   @defgroup SNSRepairCP SNS Repair Copy packet
   @ingroup SNSRepairCM

 */

struct c2_sns_repair_cp {
	struct c2_cm_cp    rc_base;

	/** Read/write stob id. */
	struct c2_stob_id  rc_sid;

	/** Offset within the stob. */
	c2_bindex_t        rc_index;

	/** Stob IO context. */
	struct c2_stob_io  rc_stio;

	/** Stob context. */
	struct c2_stob     rc_stob;
};

struct c2_sns_repair_cp *cp2snscp(const struct c2_cm_cp *cp);

/**
 * Uses GOB fid key and parity group number to generate a scalar to
 * help select a request handler locality for copy packet FOM.
 */
uint64_t cp_home_loc_helper(const struct c2_cm_cp *cp);

extern const struct c2_cm_cp_ops c2_sns_repair_cp_ops;

/** Transformation phase function for copy packet. */
int c2_sns_repair_cp_xform(struct c2_cm_cp *cp);

/** Copy packet read phase function. */
int c2_sns_repair_cp_read(struct c2_cm_cp *cp);

/** Copy packet write phase function. */
int c2_sns_repair_cp_write(struct c2_cm_cp *cp);

/** Copy packet IO wait phase function. */
int c2_sns_repair_cp_io_wait(struct c2_cm_cp *cp);

int c2_sns_repair_cp_phase_next(struct c2_cm_cp *cp);

/** @} SNSRepairCP */
#endif /* __COLIBRI_SNS_REPAIR_CP_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
