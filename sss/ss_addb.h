/* -*- C -*- */
/*
 * COPYRIGHT 2014 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Rajanikant Chirmade <rajanikant_chirmade@xyratex.com>
 * Original creation date: 04-Jun-2014
 */

#pragma once

#ifndef __MERO_SSS_SS_ADDB_H__
#define __MERO_SSS_SS_ADDB_H__

#include "addb/addb.h"

/**
 * @addtogroup ss_svc
 * @{
 */

/** Start service context types. */
enum {
	M0_ADDB_CTXID_SS_SVC = 1900,
	M0_ADDB_CTXID_SS_FOM = 1901,
};

M0_ADDB_CT(m0_addb_ct_ss_svc, M0_ADDB_CTXID_SS_SVC, "hi", "low");
M0_ADDB_CT(m0_addb_ct_ss_fom, M0_ADDB_CTXID_SS_FOM);

/** Start stop service ADDB posting locations. */
enum {
	/* ss service */
	/* @todo need to assign ids */
	M0_SS_SVC_ADDB_LOC_SERVICE_ALLOC,
	M0_SS_SVC_ADDB_LOC_FOM_ALLOC,
	M0_SS_SVC_ADDB_LOC_REP_FOP_ALLOC,
};

#define SS_ADDB_FUNCFAIL(rc, loc, ctx) \
	M0_ADDB_FUNC_FAIL(&m0_addb_gmc, M0_SS_SVC_ADDB_LOC_##loc, rc, ctx)

#define SS_ALLOC_PTR(ptr, ctx, loc) \
	M0_ALLOC_PTR_ADDB(ptr, &m0_addb_gmc, M0_SS_SVC_ADDB_LOC_##loc, ctx)

/** @} */ /* end of ss_svc*/

#endif /* __MERO_SSS_SS_ADDB_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
