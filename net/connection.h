/* -*- C -*- */
#ifndef _C2_NET_CONNECTION_H_

#define _C2_NET_CONNECTION_H_

#include "lib/c2list.h"
#include "lib/refs.h"

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
	CLIENT			*nc_cli;
};

#endif
