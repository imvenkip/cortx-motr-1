/* -*- C -*- */
#ifndef _RPC_SESSION_TYPES_H_

#define _RCP_SESSION_TYPES_H_

/**
 sequence in a slot
 */
typedef uint32_t c2_seq_t;


enum c2_session_cmd {
	/**
	 Create new session on server
	 */
	C2_SESSION_CREATE = 1,
	/**
	 Destroy session on server
	 */
	C2_SESSION_DESTROY,
	/**
	 Adjust session related paramters
	 currently only max slot id supported
	 */
	C2_SESSION_ADJUST,
	/**
	 send compound request over session
	 */
	C2_SESSION_COMPOUND
};

/**
 C2_SESSION_CREATE command
 */

/**
 parameters to the C2_SESSION_CREATE command
 */
struct session_create_arg {
	/**
         * client requested a new session
         */
	struct c2_node_id	sca_client;
        /**
         * server to accept connection
         */
	struct c2_node_id	sca_server;
        /**
         * maximal slot count handled by client
         */
	uint32_t		sca_high_slot_id;
        /**
         * maximal rpc size can be handled by client
         */
	uint32_t		sca_max_rpc_size;
};

/**
 * server reply to SESSION_CREATE command.
 */
struct session_create_out {
        /**
         * server assigned session identifier
         */
	struct c2_session_id sco_session_id;
        /**
         * maximal slot id (slot's count) assigned to the client
         */
	uint32_t sco_high_slot_id;
        /**
         * maximal rpc size can be handle by client
         */
	uint32_t sco_max_rpc_size;
};

struct session_create_ret {
	/**
	 status of operation.
	 if operation failed - not need a decoding reply
	*/
	int32_t errno;
	/**
	 real reply to C2_SESSION_CREATE command
	*/
	struct session_create_out reply;
};


/**
  C2_SESSION_DESTROY command
 */

/**
 argument to server side procedure
 */
struct session_destroy_arg {
	struct c2_session_id da_session_id;
};

struct session_destroy_ret {
	int32_t sda_errno;
};

/**
*/
struct c2_session_adjust_in {
	struct session_id sr_session_id;
	uint32_t sr_new_high_slot_id;
};

struct c2_session_adjust_rep {
	uint32_t sr_new_high_slot_id;
};

struct c2_session_adjust_out {
	int32_t errno;
	struct c2_session_adjust_rep s_reply;
};


#endif
