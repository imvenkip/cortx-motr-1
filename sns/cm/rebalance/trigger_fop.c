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

#include "fop/fop.h"
#include "fop/fop_item_type.h"

#include "sns/cm/cm.h"
#include "sns/cm/trigger_fop.h"
#include "sns/cm/trigger_fop_xc.h"
#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_SNSCM
#include "lib/trace.h"

/*
 * Implements a simplistic sns repair trigger FOM for corresponding trigger FOP.
 * This is solely for testing purpose and a separate trigger FOP/FOM will be
 * implemented later, which would be similar to this one.
 */

struct m0_fop_type rebalance_trigger_fopt;
struct m0_fop_type rebalance_trigger_rep_fopt;
extern struct m0_cm_type sns_rebalance_cmt;

M0_INTERNAL void m0_sns_cm_rebalance_trigger_fop_fini(void)
{
	m0_sns_cm_trigger_fop_fini(&rebalance_trigger_fopt);
	m0_sns_cm_trigger_fop_fini(&rebalance_trigger_rep_fopt);
}

M0_INTERNAL int m0_sns_cm_rebalance_trigger_fop_init(void)
{
	return m0_sns_cm_trigger_fop_init(&rebalance_trigger_fopt,
					  M0_SNS_REBALANCE_TRIGGER_OPCODE,
					  "sns rebalance trigger",
					  trigger_fop_xc,
					  M0_RPC_ITEM_TYPE_REQUEST |
					  M0_RPC_ITEM_TYPE_MUTABO,
					  &sns_rebalance_cmt) ?:
		m0_sns_cm_trigger_fop_init(&rebalance_trigger_rep_fopt,
					   M0_SNS_REBALANCE_TRIGGER_REP_OPCODE,
					   "sns rebalance trigger reply",
					   trigger_rep_fop_xc,
					   M0_RPC_ITEM_TYPE_REPLY,
					   &sns_rebalance_cmt);
}

#undef M0_TRACE_SUBSYSTEM
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
