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
	 size of incomming argument
	 */
	size_t		ro_arg_size;
	/**
	 XDR program to converting argument of remote procedure call
	 */
	c2_xdrproc_t	ro_xdr_arg;
	/**
	 size of reply
	 */
	size_t		ro_result_size;
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

/**
 find operation in table. function scanned table with operation to find
 requested operation.

 @param ops pointer filled operations table
 @param op rpc operation to find

 @retval NULL if pointer not exist or wrong parameters
 @retval !NULL if operations found in table
 */
const struct c2_rpc_op *c2_find_op(const struct c2_rpc_op_table *ops, int op);


#endif

