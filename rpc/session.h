/* -*- C -*- */
#ifndef _RPC_CLI_SESSION_H_

#define _RPC_CLI_SESSION_H_

#define C2_MAX_SLOTS	32

#include "lib/refs.h"

/**
 client side slot definition
 */
struct c2_cli_slot {
	/**
	 * sequence assigned to the slot
	 */
	uint32_t sl_seq;
};

/**
 slot table associated with session
 */
struct c2_cli_slot_table {
	/**
	 maximal slot index
	 */
	uint32_t sltbl_high_slot_id;
	/**
	 slots map - used (bit set), unused (bit is clear)
	*/
	unsigned long sltbl_slots_bitmap;
	/**
	 slots array
	 */
	struct cli_slot sltbl_slots[C2_MAX_SLOTS];
	/**
	 to protecting access to slots bitmap & high slot id.
	 */
	struct spinlock sltbl_slheads_lock;
};

struct c2_cli_session {
	/**
	 * linking entry
	*/
	struct c2_list_link	sess_link;
	/**
	 * client session reference count
	 */
	struct c2_refs		sess_ref;
	/**
	 * server identifier
	 */
	client_id		sess_srv;
	/**
	 * server assigned session id
	 */
	struct session_id	sess_id;
	/**
	 *
	 */
	struct c2_cli_slot_table sess_slots;
	/**
	 * rpc client entry associated with this session
	 */
	CLIENT *cli;
};

/**
 * session constructor.
 * allocate slot's memory and connect session to server.
 *
 * \param cli_uuid - client identifier
 * \param srv_uuid - server identifier
 * \param cli - pointer to fully setup transport session
 *
 * \retval 0   success
 * \retval -ve failure, e.g., server don't conencted
 */
int c2_session_cli_create(const struct rpc_client * cli,
			  const struct client_id * srv_uuid, CLIENT * cli);

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
 * verify session \a sess by sending sequence op and check response
 *
 * @param sess - pointer to fully inited session object
 *
 * @retval 0   success
 * @retval -ve failure, e.g., server don't connected
 */
int c2_session_check(const struct c2_cli_session *sess);


/**
 server side slot definition
 */
struct srv_slot {
	/**
	 * current sequence of operation
	 */
	uint32_t	srv_slot_seq;
	/**
	 * index in global slot array.
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
	struct srv_slot srvst_slots[C2_MAX_SLOTS];
};


/**
 server size session structure
 */
struct srv_session {
	/**
	 * linking to glibal list
	 */
	struct c2_list_link	srvs_link;
	/**
	 * session reference count
	 */
	struct c2_ref		srvs_ref;
	/**
	 * client identifier
	 */
	struct client_id	srvs_cli;
	/**
	 * server assigned session id
	 */
	uint32_t		srvs_id;
	/**
	 *
	 */
	struct srv_slot_table srvs_slots;
	/**
	 * link to server owned this session
	 */
	struct server		*srvs_server;
	/**
	 * session last used time
	*/
	uint64_t		srvs_last_used;
};

/**
 * adjust slot numbers for given session
 *
 * \param session  - pointer to client session
 * \param new_size - new number for the high slot id.
 *
 * \retval >0 size after adjusting
 * \retval <0 any error hit (client not responded, or other)
 */
int c2_session_adjust(struct srv_session *session, uint32_t new_size);

/** rpc handlers */
/**
 * server handler for the SESSION_CREATE cmd.
 * create new session
 *
 * \param in  - structure with arguments from client to creation session.
 * \param out - structure returned to client
 *
 * \retval 0  - create is OK
 * \retval <0 - create is fail (no memery, already exist, or other)
 */
int c2_session_create_svc(struct session_create_arg *in,
			  struct session_create_ret *out);

/**
 * server handler for the SESSION_DESTROY cmd.
 * destroy session on server side.
 *
 * @param in  - session id + parameters to destroy session from client
 * @param out - resulting info to send to client
 *
 * @retval 0  - destroy is OK
 * @retval <0 - destroy is fail (no memory, not found, or other)
 */
int c2_session_destroy_svc(struct session_destroy_arg *in,
			   struct session_destroy_out *out);

/**
 * SESSION_COMPOUND command handler
 *
 * @param in  - structure with compound header and array of operations
 * @param out - resulting structure to send to client
 *
 * @retval 1 - all operations processed successfully.
 * @retval 0 any error hit (bad command format, error hit in processing, or other)
 */
int c2_session_compound_svc(struct session_compound_arg *in,
			    struct session_compound_reply *out);

/**
 * ADJUST_SESSION command handler
 *
 * @param arg - incoming argument to adjusting session settings
 * @param out - result of adjusting
 *
 * @retval 0   success
 * @retval -ve failure, e.g., server don't connected
 */
int c2_session_adjust_svc(struct c2_session_adjust_in *arg,
			  struct c2_session_adjust_out *out);

#endif
