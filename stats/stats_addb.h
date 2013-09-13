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
 * Original author: Rajanikant Chirmade <rajanikant_chirmade@xyratex.com>
 * Original creation date: 07/05/2013
 */

#pragma once

#ifndef __MERO_STATS_ADDB_H__
#define __MERO_STATS_ADDB_H__

#include "addb/addb.h"

/**
 * @addtogroup stats_svc
 * @{
 */

/*
 * Stats ADDB context types.
 */
enum {
	M0_ADDB_CTXID_STATS_SVC		   = 1700,
	M0_ADDB_CTXID_STATS_UPDATE_FOM	   = 1701,
	M0_ADDB_CTXID_STATS_QUERY_FOM	   = 1702,
	M0_ADDB_CTXID_MONITORS_MOD         = 1703,
};

M0_ADDB_CT(m0_addb_ct_stats_svc, M0_ADDB_CTXID_STATS_SVC, "hi", "low");
M0_ADDB_CT(m0_addb_ct_stats_update_fom, M0_ADDB_CTXID_STATS_UPDATE_FOM);
M0_ADDB_CT(m0_addb_ct_stats_query_fom, M0_ADDB_CTXID_STATS_QUERY_FOM);

M0_ADDB_CT(m0_addb_ct_monitors_mod, M0_ADDB_CTXID_MONITORS_MOD);

/*
 * Stats ADDB posting locations.
 */
enum {
	/* stats service */
	M0_STATS_SVC_ADDB_LOC_SERVICE_ALLOC = 1,
};

#define STATS_ADDB_FUNCFAIL(rc, loc, ctx)		             \
M0_ADDB_FUNC_FAIL(&m0_addb_gmc, M0_STATS_ADDB_LOC_##loc, rc, ctx)

#define STATS_ALLOC_PTR(ptr, ctx, loc)			             \
M0_ALLOC_PTR_ADDB(ptr, &m0_addb_gmc, M0_STATS_ADDB_LOC_##loc, ctx)

/*
 ******************************************************************************
 * Stats ADDB posting locations
 ******************************************************************************
 */
enum {
	M0_STATS_ADDB_LOC_SVC_CONN_ESTABLISH_1 = 10,
	M0_STATS_ADDB_LOC_SVC_CONN_ESTABLISH_2 = 11,
	M0_STATS_ADDB_LOC_SVC_CONN_FINI_1      = 20,
	M0_STATS_ADDB_LOC_SVC_CONN_FINI_2      = 21,
};

/** @} */ /* end of stats */

#endif /* __MERO_STATS_ADDB_H_ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
