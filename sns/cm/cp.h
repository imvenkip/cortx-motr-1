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
 * Original author: Dipak Dudhabhate <dipak_dudhabhate@xyratex.com>
 *                  Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 08/06/2012
 */

#pragma once

#ifndef __MERO_SNS_CM_CP_H__
#define __MERO_SNS_CM_CP_H__

#include "lib/ext.h"

#include "stob/stob_id.h"
#include "cm/cp.h"

/**
   @defgroup SNSCMCP SNS copy machine Copy packet
   @ingroup SNSCM

 */

struct m0_sns_cm_cp {
	struct m0_cm_cp        sc_base;

	/** Read/write stob id. */
	struct m0_stob_id      sc_sid;

	/** Offset within the stob. */
	m0_bindex_t            sc_index;

	/** Stob IO context. */
	struct m0_stob_io      sc_stio;

	/** Stob context. */
	struct m0_stob        *sc_stob;

	/** FOL record part for storage objects. */
	struct m0_fol_rec_part sc_fol_rec_part;
};

M0_INTERNAL struct m0_sns_cm_cp *cp2snscp(const struct m0_cm_cp *cp);

/**
 * Uses GOB fid key and parity group number to generate a scalar to
 * help select a request handler locality for copy packet FOM.
 */
M0_INTERNAL uint64_t cp_home_loc_helper(const struct m0_cm_cp *cp);

M0_INTERNAL bool m0_sns_cm_cp_invariant(const struct m0_cm_cp *cp);

extern const struct m0_cm_cp_ops m0_sns_cm_cp_ops;

/** Transformation phase function for copy packet. */
M0_INTERNAL int m0_sns_cm_cp_xform(struct m0_cm_cp *cp);

/** Transformation phase function for copy packet. */
M0_INTERNAL int m0_sns_cm_cp_xform_wait(struct m0_cm_cp *cp);

/** Copy packet read phase function. */
M0_INTERNAL int m0_sns_cm_cp_read(struct m0_cm_cp *cp);

/** Copy packet write phase function. */
M0_INTERNAL int m0_sns_cm_cp_write(struct m0_cm_cp *cp);

/** Copy packet IO wait phase function. */
M0_INTERNAL int m0_sns_cm_cp_io_wait(struct m0_cm_cp *cp);

M0_INTERNAL int m0_sns_cm_cp_send(struct m0_cm_cp *cp);

M0_INTERNAL int m0_sns_cm_cp_recv(struct m0_cm_cp *cp);

M0_INTERNAL int m0_sns_cm_cp_phase_next(struct m0_cm_cp *cp);

M0_INTERNAL int m0_sns_cm_cp_next_phase_get(int phase, struct m0_cm_cp *cp);

/** @} SNSCMCP */
#endif /* __MERO_SNS_CM_CP_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
