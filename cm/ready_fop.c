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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_CM

#include "fop/fop.h"
#include "rpc/rpc.h"
#include "cm/ready_fop.h"

M0_INTERNAL int m0_cm_ready_fop_post(struct m0_fop *fop,
				     const struct m0_rpc_conn *conn)
 {
      struct m0_rpc_item *item;

      M0_PRE(fop != NULL && conn != NULL);

      item              = m0_fop_to_rpc_item(fop);
      item->ri_ops      = NULL;
      item->ri_prio     = M0_RPC_ITEM_PRIO_MID;
      item->ri_deadline = 0;

      return m0_rpc_oneway_item_post(conn, item);
 }

#undef M0_TRACE_SUBSYSTEM

/** @} CMREADY */
/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
