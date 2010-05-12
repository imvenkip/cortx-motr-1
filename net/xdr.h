/* -*- C -*- */
#ifndef __COLIBRI_NET_XDR_H__

#define __COLIBRI_NET_XDR_H__

struct c2_node_id;

/**
 XDR procedure to convert node_id from/to network representation
 */
bool c2_xdr_node_id (void *xdrs, struct c2_node_id *node);

#endif

