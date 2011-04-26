/* -*- C -*- */
#ifndef __COLIBRI_NET_BULK_SUNRPC_XPRT_PVT_H__
#define __COLIBRI_NET_BULK_SUNRPC_XPRT_PVT_H__

/**
   @addtogroup bulksunrpc

   @{
*/

#include "net/bulk_emulation/sunrpc_xprt.h"

int sunrpc_msg_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);
int sunrpc_get_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);
int sunrpc_put_handler(struct c2_fop *fop, struct c2_fop_ctx *ctx);

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
