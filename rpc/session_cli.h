/* -*- C -*- */
#ifndef _RPC_CLI_SESSION_H_

#define _RPC_CLI_SESSION_H_

#include "lib/cdefs.h"
#include "lib/refs.h"

#include "rpc/rpc_types.h"
/**
 @page rpc-cli-session client side session part
*/


/**
 initial slot count value, requested from a server
 while session is created
 */
#define C2_SLOTS_INIT_COUNT	32

/**
 client side slot definition
 */
struct c2_cli_slot {
	/**
	 sequence assigned to the slot
	 */
	c2_seq_t sl_seq;
	/**
	 slots flags
	*/
	unsigned long sl_busy:1; /** slots a busy with sending request */
};

/**
 slot table associated with session
 */
struct c2_cli_slot_table {
	/**
	 to protecting access to slots array and high slot id.
	 */
	struct c2_rw_lock	sltbl_slheads_lock;
	/**
	 maximal slot index
	 */
	uint32_t		sltbl_high_slot_id;
	/**
	 slots array
	 */
	struct cli_slot		sltbl_slots[0];
};

struct c2_cli_session {
	/**
	 linking into list of sessions assigned to client
	 */
	struct c2_list_link	sess_link;
	/**
	 client session reference count protection
	 */
	struct c2_refs		sess_ref;
	/**
	 * server identifier
	 */
	struct c2_node_id	sess_srv;
	/**
	 * server assigned session id
	 */
	struct session_id	sess_id;
	/**
	 session slot table
	 */
	struct c2_cli_slot_table sess_slots;
};

/**
 session constructor.
 allocate slot's memory and connect session to server.
 if server is unreachable function is return error without allocate new session.

 @param cli - rpc client to create new session.
 @param srv - server identifier

 @retval 0   success
 @retval -ve failure, e.g., server don't connected
 */
int c2_cli_sess_create(const struct rpc_client * cli,
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
int c2_session_cli_destroy(struct cli_session *sess);

/**
 * find session associated with server
 *
 * @param cli_uuid - client identifier
 * @param srv_uuid - server identifier
 *
 * @retval NULL, session don't found or don't init correctly
 * @retval !NULL, OK
 */
struct c2_cli_session *c2_session_cli_find(const struct rpc_client *cli,
					   const struct client_id *srv_uuid);

/**
 verify session @a sess by sending "sequence" op and check response.
 RFC suggested method to check service livnes.
 
 @param sess - pointer to fully inited session object
 
 @retval 0   success
 @retval -ve failure, e.g., server don't connected
 */
int c2_cli_session_check(const struct c2_cli_session * sess);

#endif
