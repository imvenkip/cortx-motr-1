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

#ifndef __MERO_MDSTORE_MDSTORE_ADDB_H__
#define __MERO_MDSTORE_MDSTORE_ADDB_H__

#include "addb/addb.h"

/**
   @addtogroup mdstore
   @{
 */

/*
 ******************************************************************************
 * Mdstore ADDB context types.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_MDSTORE_MOD = 800,
};

M0_ADDB_CT(m0_addb_ct_mdstore_mod, M0_ADDB_CTXID_MDSTORE_MOD);

/*
 ******************************************************************************
 * Meta-data store ADDB posting locations.
 ******************************************************************************
 */
enum {
	M0_MDSTORE_ADDB_LOC_CLOSE   = 10,
	M0_MDSTORE_ADDB_LOC_CREATE  = 20,
	M0_MDSTORE_ADDB_LOC_GETATTR = 30,
	M0_MDSTORE_ADDB_LOC_INIT_1  = 40,
	M0_MDSTORE_ADDB_LOC_INIT_2  = 41,
	M0_MDSTORE_ADDB_LOC_LINK    = 50,
	M0_MDSTORE_ADDB_LOC_OPEN    = 60,
	M0_MDSTORE_ADDB_LOC_READDIR = 70,
	M0_MDSTORE_ADDB_LOC_RENAME  = 80,
	M0_MDSTORE_ADDB_LOC_SETATTR = 90,
	M0_MDSTORE_ADDB_LOC_UNLINK  = 100,
};

/** @} */ /* end of mdstore group */

#endif /* __MERO_MDSTORE_MDSTORE_ADDB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
