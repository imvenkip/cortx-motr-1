/* -*- C -*- */
#ifndef _C2_NET_XDR_H_

#define _C2_NET_XDR_H_

/**
 XDR procedure to convert node_id from/to network representation
 */
bool_t c2_xdr_node_id (XDR *xdrs, struct node_id *objp);

#endif

