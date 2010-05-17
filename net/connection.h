/* -*- C -*- */
#ifndef __COLIBRI_NET_CONNECTION_H__

#define __COLIBRI_NET_CONNECTION_H__

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
	struct c2_service_id	nc_id;
	/**
	 reference counter
	 */
	struct c2_ref		nc_refs;
	/**
	 service identifier
	 */
	enum c2_rpc_service_id	nc_prgid;
	/**
	 sun rpc transport
	 */
	CLIENT			*nc_cli;
};

#endif
