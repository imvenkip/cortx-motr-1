/* -*- C -*- */
#ifndef __COLIBRI_NET_SUNRPC_SUNRPC_H__
#define __COLIBRI_NET_SUNRPC_SUNRPC_H__

#include <rpc/xdr.h>

#include "lib/cdefs.h"

/**
   @addtogroup usunrpc User Level Sun RPC
   @{
 */

extern struct c2_net_xprt c2_net_usunrpc_xprt;

struct c2_fop_field_type;

bool_t c2_fop_type_uxdr(const struct c2_fop_field_type *ftype, 
			XDR *xdrs, void *obj);
bool_t c2_fop_uxdrproc(XDR *xdrs, struct c2_fop *fop);

/** @} end of group usunrpc */

/* __COLIBRI_NET_SUNRPC_SUNRPC_H__ */
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
