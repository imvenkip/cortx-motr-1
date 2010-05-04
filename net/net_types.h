/* -*- C -*- */
#ifndef _C2_NET_TYPES_H_

#define _C2_NET_TYPES_H_

#include "lib/cdefs.h"

/**
 unique service identifier.
 each service have own identifiers.
 if different services run on single physical node,
 he will have different c2_node_id value.
 */
struct c2_node_id {
	char uuid[40];
};


typedef	bool (*c2_xdrproc_t)(void *, void *, unsigned int);

/**
 rpc commands associated with service thread
 */
struct c2_rpc_op {
	/**
	 operation identifier
	 */
	int		ro_op;
	/**
	 XDR program to converting argument of remote procedure call
	 */
	c2_xdrproc_t	ro_xdr_arg;
	/**
	 XDR program to converting result of remote procedure call
	 */
	c2_xdrproc_t	ro_xdr_result;
	/**
	 function to a handle operation on server side
	 */
	bool		(*ro_shandler) (void *, void *);
};

struct c2_rpc_op_table {
	/**
	 number of operations in table
	 */
	int	rot_numops;
	/**
	 array of rpc operations
	 */
	struct c2_rpc_op	rot_ops[0];
};

#endif

