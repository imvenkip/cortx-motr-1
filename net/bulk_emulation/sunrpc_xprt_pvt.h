/* -*- C -*- */
#ifndef __COLIBRI_NET_BULK_SUNRPC_XPRT_PVT_H__
#define __COLIBRI_NET_BULK_SUNRPC_XPRT_PVT_H__

/**
   @addtogroup bulksunrpc

   @{
*/

#include "net/bulk_emulation/sunrpc_xprt.h"

/* forward references to other static functions */
static int sunrpc_desc_create(struct c2_net_buf_desc *desc,
			      struct c2_net_end_point *ep,
			      struct c2_net_transfer_mc *tm,
			      enum c2_net_queue_type qt,
			      c2_bcount_t buflen,
			      int64_t buf_id);
static int sunrpc_msg_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);
static int sunrpc_get_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);
static int sunrpc_put_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);
static void sunrpc_wi_add(struct c2_net_bulk_mem_work_item *wi,
			  struct c2_net_bulk_sunrpc_tm_pvt *tp);
static bool sunrpc_buffer_in_bounds(struct c2_net_buffer *nb);
static int sunrpc_ep_mutex_initialized;
static struct c2_mutex sunrpc_ep_mutex;

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
