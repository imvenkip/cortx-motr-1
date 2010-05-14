/* -*- C -*- */
#ifndef __COLIBRI_NET_TYPES_H__

#define __COLIBRI_NET_TYPES_H__

#include "lib/cdefs.h"

/**
 unique service identifier.
 each service have own identifiers.
 if different services run on single physical node,
 she must have different c2_node_id value.
 */
struct c2_node_id {
	char uuid[40];
};

/**
 compare node identifiers

 @param c1 first node identifier
 @param c2 second node identifier

 @retval TRUE if node identifiers is same
 @retval FALSE if node identifiers is different
*/
bool c2_nodes_are_same(const struct c2_node_id *c1, const struct c2_node_id *c2);


/**
 structure to desctribe running service
 */
struct c2_service {
	int s_child;
};

/**
 services unique identifier
 */
enum c2_rpc_service_id {
	C2_SESSION_PROGRAM = 0x20000001
};

/**
 XXX make version for all sun rpc calls to be const
*/
static const int C2_DEF_RPC_VER = 1;

/**
 generic hanlder for XDR transformations

 @param x pointer to XDR object
 @param data pointer to memory region which consist data to store inside XDR,
	     or have enough memory to extract network data from XDR.

 @retval true iif conversion finished OK
 @retval false at other cases
 */
typedef	bool (*c2_xdrproc_t)(void *xdr, void *data);

/**
 server side RPC handler.

 @param arg - incoming argument
 @param ret - pointer to memory area to store result. this area allocated
		before call the hanlder.

 @retval true if ret pointed to correct reply and can send over wire
 @retval false if ret consist invalid data and system error should be returned
	       to client
 */
typedef	bool (*c2_rpc_srv_handler) (void *arg, void *ret);

/**
 rpc commands associated with service thread
 */
struct c2_rpc_op {
	/**
	 operation identifier
	 */
	int		ro_op;
	/**
	 size of incoming argument
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
	c2_rpc_srv_handler ro_handler;
};

/**
 we want to use same structure for build server and client code, bu
 if we build client code we can't have pointer to server side handler.
*/
#ifdef C2_RPC_CLIENT
#define C2_RPC_SRV_PROC(name)	(NULL)
#else
#define C2_RPC_SRV_PROC(name)	((c2_rpc_srv_handler)(name))
#endif

struct c2_rpc_op_table;

int c2_rpc_op_table_init(struct c2_rpc_op_table **table);

void c2_rpc_op_table_fini(struct c2_rpc_op_table *table);

int c2_rpc_op_register(struct c2_rpc_op_table *table, const struct c2_rpc_op *op);

/**
 find operation in table. function scanned table with operation to find
 requested operation.

 @param ops pointer filled operations table
 @param op rpc operation to find

 @retval NULL if pointer not exist or wrong parameters
 @retval !NULL if operations found in table
 */
const struct c2_rpc_op *c2_rpc_op_find(struct c2_rpc_op_table *ops, int op);


#endif

