/* -*- C -*- */
#ifndef _RPC_COMMON_H_

#define _RPC_COMMON_H_

#include "lib/cdefs.h"
#include "lib/refs.h"
#include "lib/c2list.h"
#include "lib/cc.h"

#include "net/net.h"

/**
 unique session identifier
 - generated by server and and should be don't used until we will be know that
   client will newer connected.
 */
struct c2_session_id {
	uint64_t id;
};

/**
 compare sessions identifier

 @param s1 first session identifier
 @param s2 second session identifier

 @retval true if session identifiers is same
 @retval false if session identifiers is different
*/
bool c2_session_is_same(struct session_id const *s1, struct session_id const *s2);

/**
 rpc client structure.

 structure describe rpc client
 */
struct c2_rpc_client {
	/**
	 global client list linkage
	*/
	struct c2_list_link	rc_link;
	/**
	 unique client identifier
	 */
	struct c2_node_id	rc_id;
	/**
	 client structure reference counter protection
	 */
	struct c2_ref		rc_ref;
	/**
	 concurrency access to session list protection
	 */
	struct c2_rw_lock	rc_sessions_lock;
	/**
	 sessions list
	 */
	struct c2_list		rc_sessions;
	/**
	 network logical connection assigned to a client
	 */
	struct c2_net_conn	*rc_netlink;
	/**
	 operation to send from that client
	 */
	struct c2_rpc_op_table	*rc_ops;
};

/**
 create rpc client instance and add into system list

 @param id pointer to client identifier

 @return pointer to allocated rpc structure or NULL if not have enough memory
 */
struct c2_rpc_client *c2_rpc_client_init(struct node_id const *id);

/**
 unlink rpc client from system list. structure will
 freed after release last reference

 @param cli pointer to rpc client instance

 @return none
 */
void c2_rpc_client_unlink(struct c2_rpc_client *cli);


/**
 find rpc client instance in system list.

 @param id pointer to client identifier

 @return pointer to rpc client instance of NULL if rpc client not found
*/
struct c2_rpc_client *c2_rpc_client_find(struct c2_node_id *id);

/**
 RPC server structure
 */
struct c2_rpc_server {
	/**
	 global server list linkage
	 */
	struct c2_list_link	rs_link;
	/**
	 unique server identifier
	 */
	struct c2_node_id	rs_id;
	/**
	 server structure reference counter protection
	 */
	struct c2_ref		rs_ref;
	/**
	 concurrency access to session list protection
	 */
	struct c2_rw_lock	rs_sessions_lock;
	/**
	 sessions list
	 */
	struct c2_list		rs_sessions;
	/**
	 operation to send from that client
	 */
	struct c2_rpc_op_table	*rc_ops;

};

/**
 create instance of rpc server object

 @param srv_id server identifier

 @return pointer to rpc server object
 */
struct rpc_server *c2_rpc_server_create(struct c2_node_id const *srv_id);

/**
 register rpc server object in system

 @param srv rpc server object pointer

 @return none
 */
void c2_rpc_server_register(struct c2_rpc_server *srv);

/**
 unregister rpc server object in system

 @param srv rpc server object pointer

 @return none
 */
void c2_rpc_server_unregister(struct c2_rpc_server *srv);


/**
 find registered rpc server object in system

 @param srv rpc server object pointer

 @return rpc server object pointer, or NULL if not found
 */
struct c2_rpc_server *c2_rpc_server_find(struct c2_node_id const *srv_id);


void c2_rpclib_init(void);

void c2_rpclib_fini(void);
#endif
