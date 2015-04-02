/* -*- C -*- */
/*
 * COPYRIGHT 2015 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Nikita Danilov <nikita.danilov@seagate.com>
 * Original creation date: 25-Feb-2015
 */

#pragma once

#ifndef __MERO_ADDB2_IDENTIFIER_H__
#define __MERO_ADDB2_IDENTIFIER_H__

/**
 * @defgroup XXX
 *
 * Identifiers list
 * ----------------
 *
 * addb2/identifier.h contains the list of well-known addb2 measurements and
 * label identifiers.
 *
 * @{
 */

enum m0_addb2_value_id {
	M0_AVI_NULL,

	M0_AVI_GENERAL_RANGE_START = 0x1000,
	/** Label: node fid. */
	M0_AVI_NODE,
	/** Measurement: current clock reading (m0_time_now()). */
	M0_AVI_CLOCK,

	M0_AVI_FOM_RANGE_START     = 0x2000,
	/** Label: locality number. */
	M0_AVI_LOCALITY,
	/** Label: thread handle. */
	M0_AVI_THREAD,
	/** Label: service fid. */
	M0_AVI_SERVICE,
	/** Label: fom address. */
	M0_AVI_FOM,
	/** Measurement: fom phase transition. */
	M0_AVI_PHASE,
	/** Measurement: fom state transition. */
	M0_AVI_STATE,
	/** Measurement: fom description. */
	M0_AVI_FOM_DESCR,
	/** Measurement: active foms in locality counter. */
	M0_AVI_FOM_ACTIVE,
	/** Measurement: run queue length. */
	M0_AVI_RUNQ,
	/** Measurement: wait list length. */
	M0_AVI_WAIL,
	/** Label: ast context. */
	M0_AVI_AST,
	/** Label: fom call-back context.. */
	M0_AVI_FOM_CB,

	M0_AVI_LIB_RANGE_START     = 0x3000,
	/** Measurement: memory allocation. */
	M0_AVI_ALLOC,

	M0_AVI_RM_RANGE_START      = 0x4000,
	M0_AVI_M0T1FS_RANGE_START  = 0x5000,
	M0_AVI_IOS_RANGE_START     = 0x6000,
	M0_AVI_STOB_RANGE_START    = 0x7000,

	M0_AVI_LAST                = 0x7fff,
	/** No data. */
	M0_AVI_NODATA = 0x00ffffffffffffffull,
};

/** @} end of addb2 group */

#endif /* __MERO_ADDB2_IDENTIFIER_H__ */

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
