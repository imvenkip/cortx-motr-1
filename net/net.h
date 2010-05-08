/* -*- C -*- */
#ifndef _C2_NET_H_

#define _C2_NET_H_

#include "lib/cdefs.h"
#include "net/net_types.h"

/**
 @defgroup net_conn logical network connection
 @{
 */

/**
 Logical network connection.

 Logical connection should be created as part of configuration process, if upper
layers what to use connection - he should be find by node_id and fail if
connection not found.

 */

struct c2_net_conn;


/**
 initialize global structures related to network connections
 */
void c2_net_conn_init(void);

/**
 release resources related to network connections
 */
void c2_net_conn_fini(void);

/**
 create network connection based on config info.
 function is allocate memory and connect transport connection to some logical
 connection.
 that connection will used on send rpc for one or more sessions.

 @param nid - unique node identifier
 @param prgid program identifier, some unique identifier to identify service group.
 @param nn node name, currently host name which hold service with node id 'nid'

 @retval 0 is OK
 @retval <0 error is hit
*/
int c2_net_conn_create(const struct c2_node_id *nid,
			const enum c2_rpc_service_id prgid, char *nn);

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
 function is release one reference from network connection.
 reference to transport connection is released if that is last reference
 to network connection

 @param conn pointer to network connection.

 @return none
*/
void c2_net_conn_release(struct c2_net_conn *conn);

/**
 unlink connection from connection list.
 transport connection(s) will released when last user of that connection 
 will release that logical connection.

 @param conn pointer to network connection.
 */
void c2_net_conn_unlink(struct c2_net_conn *conn);

/**
 @} end of net_conn group
*/

struct c2_rpc_op_table;
/**
 synchronous rpc call. client blocked until rpc finished.

 @param conn - network connection associated with replier
 @param rot - pointer to operations table associated with replier
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
 @param rot - pointer to operations table associated with replier
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
 @defgroup net_service network services
 @{
 */

/**
 initialize network service and attach incoming messages handler

 typical use is define custom handler and call svc_generic function with custom
 array of operations.

 @param id service identifier

 */
int c2_net_service_start(enum c2_rpc_service_id id, struct c2_rpc_op_table *ops,
			 struct c2_service *service);

int c2_net_service_stop(struct c2_service *service);

/**
 }@ end of net_service group
 */

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

