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
 * Original creation date: 12/04/2012
 */

#pragma once

#ifndef __MERO_MDS_MDS_ADDB_H__
#define __MERO_MDS_MDS_ADDB_H__

#include "addb/addb.h"

/**
   @addtogroup mdservice
   @{
 */

/*
 ******************************************************************************
 * MD Service ADDB context types.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_MDS_MOD  = 200,
	M0_ADDB_CTXID_MDS_SERV = 201,
};

M0_ADDB_CT(m0_addb_ct_mds_mod, M0_ADDB_CTXID_MDS_MOD);
M0_ADDB_CT(m0_addb_ct_mds_serv, M0_ADDB_CTXID_MDS_SERV, "hi", "low");

#define MDS_ALLOC_PTR(ptr, ctx, loc)			             \
M0_ALLOC_PTR_ADDB(ptr, &m0_addb_gmc, M0_MDS_ADDB_LOC_##loc, ctx)

/*
 ******************************************************************************
 * MDService handler ADDB posting locations
 ******************************************************************************
 */
enum {
	M0_MDS_ADDB_LOC_ALLOCATE         = 10,
	M0_MDS_ADDB_LOC_FOL_REC_PART_ADD = 20,
};

/** @} */ /* end of mdservice group */

#endif /* __MERO_MDS_MDS_ADDB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

