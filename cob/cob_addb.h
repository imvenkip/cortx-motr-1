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

#ifndef __MERO_COB_COB_ADDB_H__
#define __MERO_COB_COB_ADDB_H__

#include "addb/addb.h"

/**
   @addtogroup cob
   @{
 */

/*
 ******************************************************************************
 * Cob ADDB context types.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_COB_MOD = 1100,
};

M0_ADDB_CT(m0_addb_ct_cob_mod, M0_ADDB_CTXID_COB_MOD);

/*
 ******************************************************************************
 * Cob ADDB posting locations.
 ******************************************************************************
 */
enum {
	M0_COB_ADDB_LOC_ALLOC       = 10,
	M0_COB_ADDB_LOC_CREATE      = 20,
	M0_COB_ADDB_LOC_DELETE      = 30,
	M0_COB_ADDB_LOC_NAME_ADD    = 40,
	M0_COB_ADDB_LOC_NAME_DEL    = 50,
	M0_COB_ADDB_LOC_NAME_UPDATE = 60,
	M0_COB_ADDB_LOC_UPDATE      = 70,
	M0_COB_ADDB_LOC_EA_ADD      = 80,
	M0_COB_ADDB_LOC_EA_DEL      = 90,
};

/** @} */ /* end of cob group */

#endif /* __MERO_COB_COB_ADDB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
