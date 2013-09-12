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
 * Original creation date: 09/11/2011
 */

#pragma once

#ifndef __MERO_SNS_CM_TRIGGER_FOP_H__
#define __MERO_SNS_CM_TRIGGER_FOP_H__

#include "lib/types.h"
#include "xcode/xcode_attr.h"
#include "rpc/rpc_opcodes.h"

#include "cm/cm.h"

struct failure_data {
	uint32_t  fd_nr;
	uint64_t *fd_index;
} M0_XCA_SEQUENCE;

/**
 * Simplistic implementation of sns repair trigger fop for testing purposes
 * only.
 */
struct trigger_fop {
	struct failure_data fdata;
	uint32_t            op;
} M0_XCA_RECORD;

struct trigger_rep_fop {
	uint32_t rc;
} M0_XCA_RECORD;

M0_INTERNAL int m0_sns_cm_trigger_fop_init(struct m0_fop_type *ft,
                                           enum M0_RPC_OPCODES op,
                                           const char *name,
                                           const struct m0_xcode_type *xt,
                                           uint64_t rpc_flags,
                                           struct m0_cm_type *cmt);

M0_INTERNAL void m0_sns_cm_trigger_fop_fini(struct m0_fop_type *ft);

#endif
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
