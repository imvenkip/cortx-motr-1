/* -*- C -*- */
#ifndef _RPC_CLI_SESSION_H_

#define _RPC_CLI_SESSION_H_

#include "lib/cdefs.h"
#include "lib/refs.h"

#include "rpc/rpc_types.h"

/**
 @page rpc-srv-session

*/

/**
 server side slot definition
 */
struct c2_srv_slot {
	/**
	 current sequence in the slot
	 */
	c2_seq_t	srv_slot_seq;
};

/**
 server side slot table
 */
struct c2_srv_slot_table {
	/**
	 protect high slot id and structure resizing
	 */
	struct c2_rw_lock srvst_lock;
	/**
	 * maximal slot index
	 */
	uint32_t	srvst_high_slot_id;
	/**
	 * slots array
	 */
	struct srv_slot srvst_slots[0];
};


/**
 server size session structure
 */
struct c2_srv_session {
	/**
	 linking to global list
	 */
	struct c2_list_link	srvs_link;
	/**
	 session reference protection
	 */
	struct c2_ref		srvs_ref;
	/**
	 client identifier
	 */
	struct c2_node_id	srvs_cli;
	/**
	 * server assigned session id
	 */
	struct c2_session_id	srvs_id;
	/**
	 server side slot table
	 */
	struct c2_srv_slot_table *srvs_slots;
	/**
	 link to server owned this session
	 */
	struct c2_rpc_server	*srvs_server;
};

/**
 create new session on a server.
 
 @param srv - server to create new session
 @param sess - ponter to new created session, with 2 references.
               need a call c2_srv_session release after using.

 @retval 0 - creation OK
 @retval -ENOMEM not have enogth memory
 */
int c2_srv_session_init(struct c2_rpc_server *srv, struct c2_srv_session **sess);

/**
 unlink session from a list and release one reference
 */
void c2_srv_session_unlink(struct c2_srv_session *sess);

/**
 find session by session id and grab one reference to the session.

 @param srv - server to find session
 @param ss_id - session identifier
 
 @retval NULL - session not exist or unlinked
 @retval !NULL - session with that identifier
*/
struct c2_srv_session *c2_srv_session_find_by_id(struct c2_rpc_server *srv,
						 const c2_session_id *ss_id);

/**
 release one reference from session.
 if that will be last reference - session will be freed.
 session should be unlinked from a list before free.
*/
void c2_srv_session_release(struct c2_srv_session *sess);

#endif
