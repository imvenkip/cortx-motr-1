/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
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
 * Original author: Carl Braganza <Carl_Braganza@us.xyratex.com>,
 *                  Dave Cohrs <Dave_Cohrs@us.xyratex.com>
 * Original creation date: 04/12/2011
 */
#ifndef __COLIBRI_NET_BULK_SUNRPC_XPRT_PVT_H__
#define __COLIBRI_NET_BULK_SUNRPC_XPRT_PVT_H__

/**
   @addtogroup bulksunrpc

   @{
*/

#include "lib/time.h"
#include "net/bulk_emulation/sunrpc_xprt.h"

/* forward references to other static functions */
static int sunrpc_ep_init_sid(struct c2_service_id *sid,
			      struct c2_net_domain *rpc_dom,
			      struct c2_net_end_point *ep);
static int sunrpc_desc_create(struct c2_net_buf_desc *desc,
			      struct c2_net_transfer_mc *tm,
			      enum c2_net_queue_type qt,
			      c2_bcount_t buflen,
			      int64_t buf_id);
static int sunrpc_msg_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);
static int sunrpc_get_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);
static int sunrpc_put_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);
static void sunrpc_wi_add(struct c2_net_bulk_mem_work_item *wi,
			  struct c2_net_bulk_mem_tm_pvt *tp);
static bool sunrpc_buffer_in_bounds(const struct c2_net_buffer *nb);
static struct c2_net_transfer_mc *sunrpc_find_tm(uint32_t sid);
static void sunrpc_xo_buf_del(struct c2_net_buffer *nb);

/**
   @}
*/

#endif /* __COLIBRI_NET_BULK_MEM_XPRT_PVT_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
