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


struct c2_net_conn;

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
int c2_net_connection_create(struct node_id *nid, long prgid, char *nn);

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
 
 @param conn 

 @return none
*/ 
void c2_net_conn_release(struct c2_net_conn *conn);

/**
 disconnect transport connection(s) and remove from a connections list
 
 @param conn
 
 @retval 0 OK
 @retval <0 error hit.
 */
int c2_net_conn_destroy(struct c2_net_conn *conn);

/**
 
 */
int c2_net_cli_call_sync(struct c2_net_conn *conn, int op, void *arg, void *ret);

typedef void (*c2_net_cli_cb)(int32_t errno, void *arg, void *ret);

int c2_net_cli_call_async(struct c2_net_conn *conn, int op, void *arg, void *ret);


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
	/**
	 function to a handle operation on client side
	 */
	bool_t		(*ro_shandler) (void *, void *));
}

/**
 function prototype to handle incommind requests
*/
typedef void (*rpc_handler_t)(struct svc_req *req, SVCXPRT *xptr);

/**
 generic code to handle incomming requests
 */
void c2_net_srv_fn_generic(struct svc_req *req, SVCXPRT *xptr, struct rpc_op *ops);

/**
 initialize network service and attach incomming messages handleer
 
 typical use is define custom hanlder and call svc_generic function with custom
 array operations
  
 */
int c2_net_srv_start(unsigned long int program_num, unsigned long ver, rpc_handler_t handler);

int c2_net_srv_stop(unsigned long int program_num, unsigned long ver);

#endif
