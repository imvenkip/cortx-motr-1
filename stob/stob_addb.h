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
 * Original author: Carl Braganza <Carl_Braganza@xyratex.com>
 * Original creation date: 12/20/2012
 */

#pragma once

#ifndef __MERO_STOB_STOB_ADDB_H__
#define __MERO_STOB_STOB_ADDB_H__

#include "addb/addb.h"

/**
   @addtogroup stob
   @{
 */

/*
 ******************************************************************************
 * Database ADDB context types.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_STOB_MOD = 1300,
};

M0_ADDB_CT(m0_addb_ct_stob_mod, M0_ADDB_CTXID_STOB_MOD);

/*
 ******************************************************************************
 * Database ADDB posting locations.
 ******************************************************************************
 */
enum {

	M0_STOB_ADDB_LOC_AD_CURSOR        = 10,
	M0_STOB_ADDB_LOC_AD_DOM_LOCATE    = 20,
	M0_STOB_ADDB_LOC_AD_INCACHE_INIT  = 30,
	M0_STOB_ADDB_LOC_AD_IO_INIT       = 40,
	M0_STOB_ADDB_LOC_AD_READ_LAUNCH_1 = 50,
	M0_STOB_ADDB_LOC_AD_READ_LAUNCH_2 = 51,
	M0_STOB_ADDB_LOC_AD_READ_LAUNCH_3 = 52,
	M0_STOB_ADDB_LOC_AD_READ_LAUNCH_4 = 53,
	M0_STOB_ADDB_LOC_AD_READ_LAUNCH_5 = 54,
	M0_STOB_ADDB_LOC_AD_VEC_ALLOC     = 80,
	M0_STOB_ADDB_LOC_AD_WRITE_LAUNCH  = 90,

	M0_STOB_ADDB_LOC_LAD_DOM_IO_INIT      = 100,
	M0_STOB_ADDB_LOC_LAD_IOQ_COMPLETE     = 110,
	M0_STOB_ADDB_LOC_LAD_IOQ_SUBMIT       = 120,
	M0_STOB_ADDB_LOC_LAD_IOQ_THREAD       = 130,
	M0_STOB_ADDB_LOC_LAD_STOB_IO_INIT     = 140,
	M0_STOB_ADDB_LOC_LAD_STOB_IO_LAUNCH_1 = 150,
	M0_STOB_ADDB_LOC_LAD_STOB_IO_LAUNCH_2 = 151,

	M0_STOB_ADDB_LOC_LS_DOM_LOCATE = 200,
	M0_STOB_ADDB_LOC_LS_STOB_FIND  = 210,

};

/*
 ******************************************************************************
 * Misc
 ******************************************************************************
 */
#define M0_STOB_FUNC_FAIL(loc, rc)					\
do {									\
	if (rc < 0)							\
		M0_ADDB_FUNC_FAIL(&m0_addb_gmc, M0_STOB_ADDB_LOC_##loc, \
				  rc, &m0_stob_mod_ctx);		\
} while (0)

#define M0_STOB_OOM(loc)						\
M0_ADDB_OOM(&m0_addb_gmc, M0_STOB_ADDB_LOC_##loc, &m0_stob_mod_ctx)

M0_EXTERN struct m0_addb_ctx m0_stob_mod_ctx;

/** @} */ /* end of stob group */

#endif /* __MERO_STOB_STOB_ADDB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
