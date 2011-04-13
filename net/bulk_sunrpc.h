/* -*- C -*- */
#ifndef __COLIBRI_NET_BULK_SUNRPC_H__
#define __COLIBRI_NET_BULK_SUNRPC_H__

#include "net/net.h"

/**
@defgroup bulksunrpc Sunrpc Messaging and Bulk Transfer Emulation Transport

   @brief This module provides a network transport with messaging and bulk
   transfer capabilites implemented over the legacy and now deprecated
   Sunrpc transport.  The bulk transfer support does not provide 0 copy
   semantics.

   @{
**/

extern struct c2_net_xprt c2_net_bulk_sunrpc_xprt;

/**
   @}
*/

#endif /* __COLIBRI_NET_BULK_SUNRPC_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 79
 *  scroll-step: 1
 *  End:
 */
