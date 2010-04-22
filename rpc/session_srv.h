/* -*- C -*- */
#ifndef _RPC_CLI_SESSION_H_

#define _RPC_CLI_SESSION_H_

#include "lib/cdefs.h"
#include "lib/refs.h"

#include "rpc/rpc_types.h"

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
