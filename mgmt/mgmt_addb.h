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
 * Original author: Carl Braganza <carl_braganza@xyratex.com>
 * Original creation date: 7-Mar-2013
 */

#pragma once

#ifndef __MERO_MGMT_MGMT_ADDB_H__
#define __MERO_MGMT_MGMT_ADDB_H__

#include "addb/addb.h"

/**
   @addtogroup mgmt
   @{
 */

/*
 ******************************************************************************
 * Management ADDB context types.
 * Do not change the numbering.
 ******************************************************************************
 */
enum {
	M0_ADDB_CTXID_MGMT_MOD = 1500,
};

M0_ADDB_CT(m0_addb_ct_mgmt_mod, M0_ADDB_CTXID_MGMT_MOD);

/*
 ******************************************************************************
 * Management ADDB record identifiers.
 * Do not change the numbering.
 ******************************************************************************
 */


/*
 ******************************************************************************
 * Management ADDB posting locations.
 * Do not change the numbering.
 ******************************************************************************
 */

/** @} */ /* end of mgmt group */

#endif /* __MERO_MGMT_MGMT_ADDB_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
