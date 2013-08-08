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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 06/07/2013
 */

#pragma once

#ifndef __MERO_CM_SW_H__
#define __MERO_CM_SW_H__

#include "lib/types.h"

#include "cm/ag.h"
//XXX_BE_DB #include "cm/ag_xc.h"
#include "xcode/xcode_attr.h"
#include "fop/fop.h"

/**
   @defgroup CMSW copy machine sliding window
   @ingroup CM

   @{
 */

struct m0_rpc_conn;

struct m0_cm_sw {
	struct m0_cm_ag_id     sw_lo;
	struct m0_cm_ag_id     sw_hi;
} M0_XCA_RECORD;

/** Copy machine replica's local endpoint. */
struct m0_cm_local_ep {
	uint32_t  ep_size;
	char     *ep;
} M0_XCA_SEQUENCE;

/**
 * Copy machine's sliding window update to be sent to a
 * remote copy machine replica.
 */
struct m0_cm_sw_onwire {
	/** Replica's local endpoint. */
	struct m0_cm_local_ep swo_cm_ep;
	/** Replica's sliding window. */
	struct m0_cm_sw       swo_sw;
}M0_XCA_RECORD;

M0_INTERNAL int m0_cm_sw_onwire_init(struct m0_cm_sw_onwire *sw_onwire,
				     const char *ep, const struct m0_cm_sw *sw);

M0_INTERNAL void m0_cm_sw_set(struct m0_cm_sw *dst,
			      const struct m0_cm_ag_id *lo,
			      const struct m0_cm_ag_id *hi);

M0_INTERNAL void m0_cm_sw_copy(struct m0_cm_sw *dst,
			       const struct m0_cm_sw *src);

M0_INTERNAL int m0_cm_sw_local_update(struct m0_cm *cm);

M0_INTERNAL int m0_cm_sw_remote_update(struct m0_cm *cm);

/** @} CMSW */

#endif /* __MERO_CM_SW_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
