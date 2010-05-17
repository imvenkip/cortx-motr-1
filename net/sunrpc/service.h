/* -*- C -*- */
#ifndef __COLIBRI_NET_SUN_RPC_SERVICE_H__
#define __COLIBRI_NET_SUN_RPC_SERVICE_H__

#include "lib/cdefs.h"

/**
   @addtogroup sunrpc Sun RPC
   @{
 */

/**
   SUNRPC service identifier.
 */
struct c2_sunrpc_service_id {
	struct c2_service_id *ssi_id;
	char                 *ssi_host;
	unt16_t               ssi_port;
};

struct c2_sunrpc_service {
	/** pointers to threads... */
};

extern const struct c2_net_xprt       c2_net_sunrpc_xprt;

/** @} end of group sunrpc */

/* __COLIBRI_NET_SUN_RPC_SERVICE_H__ */
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
