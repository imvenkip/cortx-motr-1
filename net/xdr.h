/* -*- C -*- */
#ifndef __COLIBRI_NET_XDR_H__

#define __COLIBRI_NET_XDR_H__

/**
   @addtogroup netDep Networking (Deprecated Interfaces)
 */

struct c2_service_id;

/**
   XDR procedure to convert service_id from/to network representation
 */
bool c2_xdr_service_id (void *xdrs, struct c2_service_id *node);

/** @} end of net group */

#endif

