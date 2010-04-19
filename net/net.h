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
bool_t xdr_node_id (XDR *xdrs, struct node_id *objp);


/**
 compare node identifiers

 @param c1 first node identifier
 @param c2 second node identifier

 @retval TRUE if node identifiers is same
 @retval FALSE if node identifiers is different
*/
bool nodes_is_same(struct node_id const *c1, struct node_id const *c2);

/**
 @defgroup net_conn logical network connection
 @{
 */
struct c2_net_conn;

/**
 create network connection based in config info.
 function is allocate memory and connect transport connection to some logical
 connection.
 that connection will used on send rpc for one or more sessions

 @param nid - unique node identifier
 @param prgid program identifier, some unique identifier to identify service group.
 @param nn node name, currently host name which hold service with node id 'nid'

 @retval 0 is OK
 @retval <0 error is hit
*/
int c2_net_connection_create(struct node_id const *nid, unsigned long prgid, char *nn);

/**
 find connection to specified node.
 function is scan list of connections to find logical connection associated
 with that nid

 @param nid unique node identifier
 @param prgid program identifier, some unique identifier to identify service group.

 @retval NULL if none connections to the node
 @retval !NULL connection info pointer
 */
struct c2_net_conn *c2_net_connection_find(struct node_id const *nid, unsigned long prgid);

/**
 release connection after using.
 function is release one reference to network connection, if that last reference
 connection is freed.

 @param conn pointer to network connection.

 @return none
*/
void c2_net_conn_release(struct c2_net_conn *conn);

/**
 disconnect transport connection(s) and remove from a connections list

 @param conn pointer to network connection.

 @retval 0 OK
 @retval <0 error hit.
 */
int c2_net_conn_destroy(struct c2_net_conn *conn);

/**
 @} end of net_conn group
*/

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
	xdrproc_t	ro_xdr_arg;
	/**
	 XDR program to converting result of remote procedure call
	 */
	xdrproc_t	ro_xdr_result;
	/**
	 function to a handle operation on server side
	 */
	bool		(*ro_shandler) (void *, void *);
}

struct c2_rpc_op_table {
	/**
	 number of operations in table
	 */
	int	rot_numops;
	/**
	 array of rpc operations
	 */
	struct c2_rpc_op	rot_ops[];
};

/**
 synchronous rpc call. client blocked until rpc finished.

 @param conn - network connection associated with replier
 @param pot - pointer to operations table associated with replier
 @param op - operation to call on replier
 @param arg - pointer to buffer with argument of operation
 @param ret - pointer to buffer to put reply from a replier

 @retval 0 OK
 @retval <0 ERROR
 */
int c2_net_cli_call_sync(struct c2_net_conn const *conn,
			 struct c2_rpc_op_table const *rot,
			 int op, void *arg, void *ret);

/**
 callback function to call after received a reply

 @param error - status of operation
 @param arg - pointer to argument of operation
 @param ret - pointer to buffer returned from a replier
 */
typedef void (*c2_net_cli_cb)(int32_t error, void *arg, void *ret);

/*
 asynchronous rpc call. client is continue after request queried
 to transfer.

 @param conn - network connection associated with replier
 @param pot - pointer to operations table associated with replier
 @param op - operation to call on replier
 @param cb - pointer to callback function called after hit a error in
	     in transfer, or reply is accepted.
 @param arg - pointer to buffer with argument of operation
 @param ret - pointer to buffer to put reply from a replier

 @retval 0 OK
 @retval <0 ERROR
*/
int c2_net_cli_call_async(struct c2_net_conn const *conn,
			  struct c2_rpc_op_table const *rot,
			  int op, void *arg, c2_net_cli_cb *cb, void *ret);


struct svc_req;

/**
 function prototype to handle incoming requests

 @param req - SUN RPC request
 @param xptr - SUN RPC transport
*/
typedef void (*rpc_handler_t)(struct svc_req *req, SVCXPRT *xptr);

/**
 generic code to handle incoming requests
 function scan \a ops table to find handler of operation and functions to
 convert incomming / outgoning data into correct order.

 @param req - SUN RPC request
 @param xptr - SUN RPC transport
 @param ops pointer to operations handled by that service
 @param arg pointer to buffer to store operation argument from a network.
 @param ret pointer to buffer to store operation result before send over network

 @return NONE
 */
void c2_net_srv_fn_generic(struct svc_req *req, SVCXPRT *xptr,
			   struct c2_rpc_op_table const *ops, void *arg, void *ret);

/**
 initialize network service and attach incoming messages handler

 typical use is define custom handler and call svc_generic function with custom
 array of operations.

 @param program_num -
 @param ver version of that handler
 @param handler - function to handle request.

 */
int c2_net_srv_start(unsigned long int program_num, unsigned long ver,
		     rpc_handler_t handler);

int c2_net_srv_stop(unsigned long int program_num, unsigned long ver);

#endif

