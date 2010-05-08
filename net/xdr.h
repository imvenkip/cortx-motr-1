/* -*- C -*- */
#ifndef _C2_NET_XDR_H_

#define _C2_NET_XDR_H_

struct c2_node_id;

/**
 XDR procedure to convert node_id from/to network representation
 */
bool c2_xdr_node_id (void *xdrs, void *node);

#endif

