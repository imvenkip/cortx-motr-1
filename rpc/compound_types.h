/* -*- C -*- */
#ifndef __COLIBRI_RPC_COMPOUND_TYPES_H__

#define __COLIBRI_RPC_COMPOUND_TYPES_H__

#include "rpc/rpc_types.h"

/**
 Operations inside compound request
 */
enum c2_compound_op {
	C2_COMP_NULL = 0,
	C2_COMP_SEQUENCE,
	C2_COMP_MAX_OP
};

/**
 parameters to sequence operation
 */
struct c2_session_sequence_args {
	/**
	 slot indetifyer to sequence protection
	 */
	uint32_t ssa_slot_id;
	/**
	 new sequence in the slot
	 */
	c2_seq_id ssa_sequence_id;
};
/**
 reply to sequence operation
 */
struct c2_session_sequence_reply {
	/**
	 status of operation
	 */
	int32_t error;
};


/**
 body of one operation
 */
struct c2_compound_op_arg {
	enum c2_session_compound_op c2op;
	/**
	 all arguments of operation should be listed here and xdr function need
	 to be updated
	*/
	union {
		struct c2_session_sequence_args sess_args;
	} compound_op_arg_u;
};

/**
 C2_COMPOUND_COMMAND body
 send many operations inside single command.
 */
struct c2_compound_args {
	/**
	 service to have addressed this request
	*/
	struct c2_service_id	ca_node;
	/**
	 session associated with that request (if exist)
	*/
	struct session_id	ca_session;
	/**
	 number operations inside compound
	*/
	uint32_t		ca_oparray_len;
	/**
	 array with operations
	*/
	struct c2_compound_op_arg *ca_oparray_val;
};


/**
 one reply structure
*/
struct c2_session_resop {
	/**
	 operation to get a reply
	 */
	enum c2_session_compound_op c2op;
	/**
	 all reply's should be listed here and xdr function need
	 to be updated
	*/
	union {
		struct c2_sequence_reply c2seq_reply;
	} c2_session_resop_u;
};

struct c2_compound_reply {
	/**
	 status of last operation
	 */
	uint32_t status;
	/**
	 number of reply's in answers
	 */
	uint32_t resarray_len;
	/**
	 array wiyh reply's
	 */
	struct c2_session_resop *resarray_val;
};

#endif
