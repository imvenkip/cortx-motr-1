/* -*- C -*- */
#ifndef _COMPOUND_TYPES_H_

#define _COMPOUND_TYPES_H_

#include "rpc/rpc_types.h"

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
	struct c2_session_id sco_session_id;
	uint32_t sco_high_slot_id;
	uint32_t sco_max_rpc_size;
};

struct session_create_ret {
	int32_t errno;
	struct session_create_out reply;
};


/**
  C2_SESSION_DESTROY command
 */

/**
 argument to server side procedure
 */
struct session_destroy_arg {
	struct session_id da_session_id;
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

/**

 */
enum c2_session_compound_op {
	session_sequence_op = 1,
	session_null_op,
};

struct session_sequence_args {
	uint32_t ssa_slot_id;
	uint32_t ssa_sequence_id;
};

struct compound_op_arg {
	c2_session_compound_op c2op;
	union {
		struct session_sequence_args sess_args;
	} compound_op_arg_u;
};

struct compound_args {
	session_id ca_session_id;
	struct {
		u_int ca_oparray_len;
		struct compound_op_arg *ca_oparray_val;
	} ca_oparray;
};

struct session_sequence_reply {
	int32_t errno;
};

struct c2_session_resop {
	c2_session_compound_op c2op;
	union {
		struct c2_sequence_reply c2seq_reply;
	} c2_session_resop_u;
};

struct c2_compound_reply {
	uint32_t status;
	struct {
		u_int resarray_len;
		struct c2_session_resop *resarray_val;
	} resarray;
};

#endif
