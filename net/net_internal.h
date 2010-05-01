/* -*- C -*- */
#ifndef _C2_NET_INTERNAL_H_

#define _C2_NET_INTERNAL_H_

#include "lib/c2list.h"
#include "lib/refs.h"

struct CLIENT;

/**
 logical network connection from that node to some node and service
 on that node.
 */
struct c2_net_conn {
	/**
	 entry to linkage structure into connection list
	 */
	struct c2_list_link	nc_link;
	/**
	 node identifier to establish connection
	 */
	struct c2_node_id	nc_id;
	/**
	 structure reference counting
	 */
	struct c2_ref		nc_refs;
	/**
	 service identifier
	 */
	long			nc_prgid;
	/**
	 sun rpc transport
	 */
	struct CLIENT		*nc_cli;
};

/**
 find operation in table. function scanned table with operation to find
 requested operation.

 @param ops pointer filled operations table
 @param op rpc operation to find

 @retval NULL if pointer not exist or wrong parameters
 @retval !NULL if operations found in table
 */
struct c2_rpc_op const *c2_find_op(struct c2_rpc_op_table const *ops, int op);

/**
 initialize global structures related to network connections
 */
void c2_net_conn_init(void);

/**
 release resources related to network connections
 */
void c2_net_conn_fini(void);

#endif
