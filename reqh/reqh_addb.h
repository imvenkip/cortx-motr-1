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
 * Original creation date: 11/29/2012
 */

#pragma once

#ifndef __MERO_REQH_REQH_ADDB_H__
#define __MERO_REQH_REQH_ADDB_H__

#include "addb/addb.h"

/**
   @addtogroup reqh
   @{
 */

/*
 ******************************************************************************
 * Request Handler ADDB context types
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_REQH_MOD = 50,
};

M0_ADDB_CT(m0_addb_ct_reqh_mod, M0_ADDB_CTXID_REQH_MOD);

/**
   Reqh function failure macro using the global ADDB machine to post.
   @param rc Return code
   @param loc Location code - one of the REQH_ADDB_LOC_ enumeration constants
   @param ctx Runtime context pointer
   @pre rc < 0
 */
#define REQH_ADDB_FUNCFAIL(rc, loc, ctx)		             \
M0_ADDB_FUNC_FAIL(&m0_addb_gmc, M0_REQH_ADDB_LOC_##loc, rc, ctx)

#define REQH_ALLOC_PTR(ptr, ctx, loc)			             \
M0_ALLOC_PTR_ADDB(ptr, &m0_addb_gmc, M0_REQH_ADDB_LOC_##loc, ctx)

/*
 ******************************************************************************
 * Request handler ADDB posting locations
 ******************************************************************************
 */
enum {
	M0_REQH_ADDB_LOC_FOM_CREATE   = 10,
	M0_REQH_ADDB_LOC_FOP_HANDLE_1 = 20,
	M0_REQH_ADDB_LOC_FOP_HANDLE_2 = 21,
	M0_REQH_ADDB_LOC_KEY_FIND     = 30,
};

/** @} */ /* end of reqh group */

#endif /* __MERO_REQH_REQH_ADDB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

