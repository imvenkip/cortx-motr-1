/* -*- C -*- */
#ifndef _RPC_CLI_SESSION_H_

#define _RPC_CLI_SESSION_H_

#include "lib/cdefs.h"
#include "lib/refs.h"

/**
 type to define sequence in a slot
 */
typedef uint32_t c2_seq_t;

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
	 maximal slot index
	 */
	uint32_t		sltbl_high_slot_id;
	/**
	 slots array
	 */
	struct cli_slot		sltbl_slots[C2_MAX_SLOTS];
	/**
	 to protecting access to slots array and high slot id.
	 */
	struct c2_rw_lock	sltbl_slheads_lock;
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
int c2_cli_sess_create(struct rpc_client const * cli,
		       struct c2_node_id const * srv);

/**
 * session destructor
 * release resources and destroy connection to server.
 *
 * @param sess - session structure
 *
 * @retval 0   success
 * @retval -ve failure, e.g., server don't connected, responded
 */
int c2_session_cli_destroy(const struct cli_session *sess);

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
int c2_cli_session_check(struct c2_cli_session const *sess);


/**
 server side slot definition
 */
struct srv_slot {
	/**
	 current sequence of operation
	 */
	c2_seq_t	srv_slot_seq;
	/**
	 index in global slot array.
	 */
	uint32_t	srv_slot_idx;
	/* need pointer to FOL */
};

/**
 server side slot table
 */
struct srv_slot_table {
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
struct srv_session {
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
	 *
	 */
	struct srv_slot_table *srvs_slots;
	/**
	 * link to server owned this session
	 */
	struct server		*srvs_server;
};

/**
 adjust session parameters (currently supported slot size)
 called by server if need change parameters on client side.

 @param session  - pointer to client session
 @param new_size - new number for the high slot id.

 @retval >0 size after adjusting
 @retval <0 any error hit (client not responded, or other)
 */
int c2_session_adjust(struct srv_session *session, uint32_t new_size);

#endif
