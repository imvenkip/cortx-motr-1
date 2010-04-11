/* -*- C -*- */
#ifndef _C2_NET_H_

#define _C2_NET_H_


/**
 unique node identifier, can send over network
 */
struct node_id {
	char uuid[40];
};

/**
 XDR procedure to convert node_id from/to network representation
 */
bool_t xdr_node_id (XDR *xdrs, node_id *objp);


/**
 compare node indentifiers
 
 @param c1 first node identifier
 @param c2 second node identifier
 
 @retval TRUE if node identifiers is same
 @retval FALSE if node identifiers is differents
*/ 
bool nodes_is_same(struct node_id *c1, struct node_id *c2);


#define NODE_NAME_SIZE	40

struct node_name {
	char	name[NODE_NAME_SIZE];
};

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
	struct node_id		nc_id;
	/**
	 node name (currently host name) to establish connection
	 */
	struct node_name 	nc_node;
	/**
	 structure reference countings
	 */
	struct c2_ref		nc_refs;
	/**
	 service identifer
	 */
	long			nc_prgid;
	/**
	 sun rpc transport
	 */
	CLIENT 		*nc_cli;
};

/**
 create network connection based in config info.
 function is allocate memory and connect tranport connection to some logical
 connection.
 that connection will used on send rpc for one or more sessions
 
 @param nid - unique node identifier
 @param prgid program identifier, some unique identifer to identify service group.
 @param nn node name, currently host name which hold service with node id 'nid'

 @retval 0 is OK
 @retval <0 error is hit
*/
int c2_net_connection_create(struct node_id *nid, long prgid, struct node_name *nn);

/**
 find connection to specificied node.
 function is scan list of connections to find logical connection associated
 with that nid

 @param nid unique node identifier
 @param prgid program identifier, some unique identifer to identify service group.
 
 @retval NULL if none connections to the node
 @retval !NULL connection info pointer
 */
struct c2_net_conn *c2_net_connection_find(struct node_id *nid, long prgid);

/**
 release connection after using.
*/ 
void c2_net_conn_release(struct c2_net_conn *conn);

/**
 disconnect transport connection(s) and disconnect from a connections list
 */
int c2_net_connection_destroy(struct c2_net_conn *conn);


struct c2_rpc_op {
	/**
	 operation identifier 
	 */
	int		ro_op;
	/**
	 XDR program to converting argument of remote procedure call
	 */
	xdrproc_t	ro_xdr_arg;
	/**
	 XDR program to converting result of remote procedure call
	 */
	xdrproc_t	ro_xdr_result;
	bool_t (*ro_shandler) (void *, void *));
}

typedef void (*rpc_handler_t)(struct svc_req *req, SVCXPRT *xptr);

void svc_generic(struct svc_req *req, SVCXPRT *xptr, struct rpc_op *ops);

/**
 initialize network service and attach rpc handler
 
 typical use is define custom hanlder and call svc_generic function with custom
 operations array
  
 */
int init_srv(unsigned long int program_num, unsigned long ver, rpc_handler_t handler);



#endif
