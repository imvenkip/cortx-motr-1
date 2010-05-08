/* -*- C -*- */
#ifndef _C2_NET_H_

#define _C2_NET_H_

#include "lib/cdefs.h"

struct c2_node_id;
/**
 compare node identifiers

 @param c1 first node identifier
 @param c2 second node identifier

 @retval TRUE if node identifiers is same
 @retval FALSE if node identifiers is different
*/
bool c2_nodes_is_same(const struct c2_node_id *c1, const struct c2_node_id *c2);

struct c2_net_conn;

/**
 @defgroup net_conn logical network connection
 @{
 */


/**
 initialize global structures related to network connections
 */
void c2_net_conn_init(void);

/**
 release resources related to network connections
 */
void c2_net_conn_fini(void);

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
int c2_net_conn_create(const struct c2_node_id *nid, const unsigned long prgid, char *nn);

/**
 find connection to specified node.
 function is scan list of connections to find logical connection associated
 with that nid

 @param nid unique node identifier
 @param prgid program identifier, some unique identifier to identify service group.

 @retval NULL if none connections to the node
 @retval !NULL connection info pointer
 */
struct c2_net_conn *c2_net_conn_find(const struct c2_node_id *nid);

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

struct c2_rpc_op_table;
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
int c2_net_cli_call_sync(const struct c2_net_conn *conn,
			 const struct c2_rpc_op_table *rot,
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
int c2_net_cli_call_async(const struct c2_net_conn *conn,
			  const struct c2_rpc_op_table *rot,
			  int op, void *arg, c2_net_cli_cb cb, void *ret);


/**
 register table of operation to process incomming requests
 */
int c2_net_srv_ops_register(struct c2_rpc_op_table *ops);

/**
 initialize network service and attach incoming messages handler

 typical use is define custom handler and call svc_generic function with custom
 array of operations.

 @param program_num service identifier


 @pre must register table of operation
 
 @retval -EINVAL table of operation isn't registered
 */
int c2_net_srv_start(const unsigned long program_num);

int c2_net_srv_stop(const unsigned long program_num);


/**
 constructor for the network library
 */
int net_init(void);

/**
 destructor for the network library.
 release all allocated resources
 */
void net_fini(void);

#endif

