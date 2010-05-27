/* -*- C -*- */
#ifndef __COLIBRI_RPC_RPCLIB_H__

#define __COLIBRI_RPC_RPCLIB_H__

#include <lib/cdefs.h>
#include <lib/refs.h>
#include <lib/c2list.h>
#include <lib/rwlock.h>
#include <lib/cache.h>

#include <net/net.h>
#include <rpc/rpc_types.h>

/**
 @page rpc-lib
*/

/**
 rpc client structure.

 structure describe rpc client.
 
 one structure is describe one service on server side, 
 structure is created when user application want to send
 operations from client side to server.
 structure have reference counter protection and will freed
 after upper layers will call c2_rpc_client function and all rpc's
 are finished and release own sessions.
 */
struct c2_rpc_client {
	/**
	 global client list linkage
	*/
	struct c2_list_link	rc_link;
	/**
	 remote service identifier
	 */
	struct c2_service_id	rc_id;
	/**
	 reference counter
	 */
	struct c2_ref		rc_ref;
	/**
	 protect concurrency access to session list
	 */
	struct c2_rwlock	rc_sessions_lock;
	/**
	 sessions list
	 */
	struct c2_list		rc_sessions;
	/**
	 network logical connection assigned to a client
	 */
	struct c2_net_conn	*rc_netlink;
};

/**
 create rpc client instance and add into system list

 @param id remote service identifier

 @return pointer to allocated rpc structure or NULL if not have enough memory
 */
struct c2_rpc_client *c2_rpc_client_init(const struct c2_service_id *id);
struct c2_rpc_client *c2_rpc_client_create(const struct c2_service_id *id);

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
struct c2_rpc_client *c2_rpc_client_find(const struct c2_service_id *id);


/**
 RPC server structure (need store in memory pool db)
 
 structure is describe one service on server side.
 service have responsible to process incoming requests.
 
 structure created by request of configure options and
 and live until service shutdown will called.
 to prevent to an early freed - structure has reference counter
 protection. 
 */
struct c2_rpc_server {
	/**
	 linkage to global server list
	 */
	struct c2_list_link	rs_link;
	/**
	 unique server identifier
	 */
	struct c2_service_id	rs_id;
	/**
	 reference counter
	 */
	struct c2_ref		rs_ref;
	/**
	 DB environment with transaction support
	*/
	DB_ENV			*rs_env;
	/**
	 DB transaction used to execute sequence of operation
	*/
	/**
	 persistent session cache
	 */
	struct c2_cache		rs_sessions;
	/**
	 persistent reply cache
	 */
	struct c2_cache		rs_cache;
};

/**
 create instance of rpc server object

 @param srv_id server identifier

 @return pointer to rpc server object, or NULL if don't possible to create
 */
struct c2_rpc_server *c2_rpc_server_create(const struct c2_service_id *srv_id);

/**
 register rpc server object in system, notify transport layer about ability
 to accept requests.

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
struct c2_rpc_server *c2_rpc_server_find(const struct c2_service_id *srv_id);

/**
 constructor for rpc library
 */
void c2_rpclib_init(void);

/**
 destructor for rpc library
 */
void c2_rpclib_fini(void);
#endif
