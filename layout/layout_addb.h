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
 * Original creation date: 12/18/2012
 */

#pragma once

#ifndef __MERO_LAYOUT_LAYOUT_ADDB_H__
#define __MERO_LAYOUT_LAYOUT_ADDB_H__

#include "addb/addb.h"

/**
   @addtogroup layout
   @{
 */

/*
 ******************************************************************************
 * Layout ADDB context types.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_LAYOUT_MOD = 600,
	M0_ADDB_CTXID_LAYOUT_OBJ = 601,
};

M0_ADDB_CT(m0_addb_ct_layout_mod, M0_ADDB_CTXID_LAYOUT_MOD);
M0_ADDB_CT(m0_addb_ct_layout_obj, M0_ADDB_CTXID_LAYOUT_OBJ, "lid", "ltid");

/*
 ******************************************************************************
 * Layout ADDB posting locations, applicable to all contexts in this module.
 ******************************************************************************
 */
enum {
	M0_LAYOUT_ADDB_LOC_0 = 0,

	M0_LAYOUT_ADDB_LOC_ADD_1              = 10,
	M0_LAYOUT_ADDB_LOC_ADD_2              = 11,
	M0_LAYOUT_ADDB_LOC_DECODE_1           = 20,
	M0_LAYOUT_ADDB_LOC_DECODE_2           = 21,
	M0_LAYOUT_ADDB_LOC_DELETE_1           = 30,
	M0_LAYOUT_ADDB_LOC_DELETE_2           = 31,
	M0_LAYOUT_ADDB_LOC_DOM_INIT           = 40,
	M0_LAYOUT_ADDB_LOC_ENCODE             = 50,
	M0_LAYOUT_ADDB_LOC_ET_REG             = 60,
	M0_LAYOUT_ADDB_LOC_LIN_ALLOC          = 70,
	M0_LAYOUT_ADDB_LOC_LIST_ALLOC         = 80,
	M0_LAYOUT_ADDB_LOC_LIST_DECODE        = 90,
	M0_LAYOUT_ADDB_LOC_LIST_ENUM_BUILD    = 100,
	M0_LAYOUT_ADDB_LOC_LIST_REG_1         = 110,
	M0_LAYOUT_ADDB_LOC_LIST_REG_2         = 111,
	M0_LAYOUT_ADDB_LOC_LOOKUP_1           = 120,
	M0_LAYOUT_ADDB_LOC_LOOKUP_2           = 121,
	M0_LAYOUT_ADDB_LOC_LOOKUP_3           = 122,
	M0_LAYOUT_ADDB_LOC_LOOKUP_4           = 123,
	M0_LAYOUT_ADDB_LOC_LT_REG             = 130,
	M0_LAYOUT_ADDB_LOC_NON_INLINE_READ_1  = 140,
	M0_LAYOUT_ADDB_LOC_NON_INLINE_READ_2  = 141,
	M0_LAYOUT_ADDB_LOC_NON_INLINE_READ_3  = 142,
	M0_LAYOUT_ADDB_LOC_NON_INLINE_READ_4  = 143,
	M0_LAYOUT_ADDB_LOC_NON_INLINE_WRITE_1 = 150,
	M0_LAYOUT_ADDB_LOC_NON_INLINE_WRITE_2 = 151,
	M0_LAYOUT_ADDB_LOC_NON_INLINE_WRITE_3 = 152,
	M0_LAYOUT_ADDB_LOC_NON_INLINE_WRITE_4 = 153,
	M0_LAYOUT_ADDB_LOC_PAIR_INIT          = 160,
	M0_LAYOUT_ADDB_LOC_PDCLUST_ALLOC      = 170,
	M0_LAYOUT_ADDB_LOC_PDCLUST_INST_BUILD = 180,
	M0_LAYOUT_ADDB_LOC_UPDATE_1           = 190,
	M0_LAYOUT_ADDB_LOC_UPDATE_2           = 191,

	M0_LAYOUT_ADDB_LOC_NR
};

/** @} */ /* end of layout group */

#endif /* __MERO_LAYOUT_LAYOUT_ADDB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
