/* -*- C -*- */
#ifndef __COLIBRI_RPC_SESSION_CLI_H__

#define __COLIBRI_RPC_SESSION_CLI_H__

#include "lib/cdefs.h"
#include "lib/refs.h"

#include "rpc/rpc_types.h"

/**
 @page rpc-cli-session client side session part
*/


/**
 client side part of session.
 session isn't visible outside of rpc layer.

 one session always created for each server connection, if client node need more
 parallel RPC's additional sessions can be created.

 session live until rpc code is ask to destroy session via calling function
 c2_cli_session_destroy.

 session have a reference counting protection.
*/
struct c2_cli_session {
	/**
	 linking into list of sessions assigned to rpc client
	 */
	struct c2_list_link	sess_link;
	/**
	 reference counter
	 */
	struct c2_refs		sess_ref;
	/**
	 server identifier
	 */
	struct c2_node_id	sess_srv;
	/**
	 server assigned session id
	 */
	struct c2_session_id	sess_id;
	/**
	 session slot table
	 */
	struct c2_cli_slot_table sess_slots;
};

/**
 session constructor.
 allocate slot's memory and connect session to server.
 if server is unreachable, function is return error without allocate new session.

 @param cli - rpc client to create new session.
 @param srv - server identifier

 @retval 0   success
 @retval -ve failure, e.g., server don't connected
 */
int c2_cli_session_create(const struct c2_rpc_client * cli,
			  const struct c2_node_id * srv);

/**
 * session destructor
 * release resources and destroy connection to server.
 *
 * @param sess - session structure
 *
 * @retval 0   success
 * @retval -ve failure, e.g., server don't connected, responded
 */
int c2_cli_session_destroy(struct c2_cli_session *sess);

/**
 * find session associated with server
 *
 * @param cli_uuid - client identifier
 * @param srv_uuid - server identifier
 *
 * @retval NULL, session don't found or don't init correctly
 * @retval !NULL, OK
 */
struct c2_cli_session *c2_cli_session_find(const struct c2_rpc_client *cli,
					   const struct c2_node_id *srv_uuid);

/**
 verify session @a sess by sending single "sequence" op and check response.
 RFC suggested method to check service livnes.

 @param sess - pointer to fully inited session object

 @retval 0   success
 @retval -ve failure, e.g., server don't connected
 */
int c2_cli_session_check(const struct c2_cli_session *sess);

#endif
