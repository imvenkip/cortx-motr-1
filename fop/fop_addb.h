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
 * Original creation date: 12/22/2012
 */

#pragma once

#ifndef __MERO_FOP_FOP_ADDB_H__
#define __MERO_FOP_FOP_ADDB_H__

#include "addb/addb.h"

/**
   @addtogroup fop
   @{
 */

/*
 ******************************************************************************
 * FOP/FOM ADDB context types
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_FOP_MOD = 1400,
};

M0_ADDB_CT(m0_addb_ct_fop_mod, M0_ADDB_CTXID_FOP_MOD);

/**
   fop/fom function failure macro using the global ADDB machine to post.
   @param rc Return code
   @param loc Location code - one of the FOP_ADDB_LOC_ enumeration constants
   @param ctx Runtime context pointer
   @pre rc < 0
 */
#define FOP_ADDB_FUNCFAIL(rc, loc, ctx)		                     \
M0_ADDB_FUNC_FAIL(&m0_addb_gmc, M0_FOP_ADDB_LOC_##loc, rc, ctx)

#define FOP_ALLOC_PTR(ptr, loc, ctx)			             \
M0_ALLOC_PTR_ADDB(ptr, &m0_addb_gmc, M0_FOP_ADDB_LOC_##loc, ctx)

#define FOP_ALLOC_ARR(ptr, nr, loc, ctx)                             \
M0_ALLOC_ARR_ADDB(ptr, nr, &m0_addb_gmc, M0_FOP_ADDB_LOC_##loc, ctx)

/*
 ******************************************************************************
 * fop/fom ADDB posting locations
 ******************************************************************************
 */
enum {
	M0_FOP_ADDB_LOC_LOC_THR_CREATE    = 10,
	M0_FOP_ADDB_LOC_FOM_DOMAIN_INIT   = 20
};

extern struct m0_addb_ctx m0_fop_addb_ctx;

/** @} */ /* end of fop group */

#endif /* __MERO_FOP_FOP_ADDB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
