/* -*- C -*- */
#ifndef _RPC_COMMON_H_

#define _RPC_COMMON_H_

/**
 unique session identifier
 */
struct session_id {
	uint64_t id;
};

/**
 compare sessions indentifierer
 
 @param s1 first session identifier
 @param s2 second session identifier
 
 @retval TRUE if session identifiers is same
 @retval FALSE if sssion identifiers is differents
*/ 
bool session_is_same(struct session_id *s1, struct session_id *s2);

/**
 rpc client stucture
 */
struct rpc_client {
	/**
	 global client list likage
	*/
	struct c2_list_link	rc_link;
	/**
	 unique client identifier
	 */
	struct client_id	rc_id;
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
};

/**
 create rpc client instance and add into system list
 
 @param id pointer to client identifier
 
 @return pointer to allocated rpc structure or NULL if not have enough memory
 */
struct rpc_client *rpc_client_create(const struct client_id *id);

/**
 unlink rpc client instance from system list. structure will 
 free after release last reference
 
 @param cli pointer to rpc client instance
 
 @return none
 */
void rpc_client_unlink(struct rpc_client *cli);


/**
 find rpc client instance in system list.
 
 @param id pointer to client identifier

 @return pointer to rpc client instance of NULL if rpc client not found
*/
struct rpc_client *rpc_client_find(const struct client_id *id);

/**
 RPC server strucutre
 */
struct rpc_server {
	/**
	 global server list linkage
	 */
	struct c2_list_link	rs_link;
	/**
	 unique server identifier
	 */
	struct client_id	rs_id;
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
};

/**
 create instance of rpc server object

 @param srv_id server identifier

 @return pointer to rpc server object
 */
struct rpc_server *rpc_server_create(const struct client_id *srv_id);

/**
 register rpc server object in system
 
 @param srv rpc server object pointer
 
 @return none
 */
void rpc_server_register(struct rpc_server *srv);

/**
 unregister rpc server object in system
 
 @param srv rpc server object pointer
 
 @return none
 */
void rpc_server_unregister(struct rpc_server *srv);


/**
 find registered rpc server object in system
 
 @param srv rpc server object pointer
 
 @return rpc server object pointer, or NULL if not found
 */
struct rpc_server *rpc_server_find(const struct client_id *srv_id);



void rpclib_init(void);

#endif
