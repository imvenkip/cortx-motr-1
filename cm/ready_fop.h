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
 * Original author: Mandar Sawant <mandar_sawant@xyratex.com>
 * Original creation date: 03/07/2012
 */

#pragma once

#ifndef __MERO_CM_READY_FOP_H__
#define __MERO_CM_READY_FOP_H__

#include "lib/types.h"

#include "cm/ag.h"
#include "cm/ag_xc.h"
#include "xcode/xcode_attr.h"
#include "fop/fop.h"

/**
   @defgroup CMREADY copy machine ready fop
   @ingroup CM

   @{
 */

struct m0_rpc_conn;

/** Sequence of file sizes to be repaired. */
struct cm_endpoint {
	uint32_t  ep_size;
	char     *ep;
} M0_XCA_SEQUENCE;

struct m0_cm_ready {
	struct cm_endpoint r_cm_ep;

	/** Replica's sliding window. */
	struct m0_cm_ag_sw r_sw;
}M0_XCA_RECORD;

M0_INTERNAL int m0_cm_ready_fop_post(struct m0_fop *fop,
				     const struct m0_rpc_conn *conn);
/** @} CMREADY */

#endif /* __MERO_CM_READY_FOP_H__ */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
