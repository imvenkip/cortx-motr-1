/* -*- C -*- */
#ifndef _RPC_INTERNAL_

#define _RPC_INTERNAL_
/**
 * find empty slot to send request
 *
 * @param sess - full initialized session
 *
 * @retval NULL failure, e.g., not have free slots to send
 * @retval !NULL pointer to client slot info
 */
struct cli_slot *c2_find_empty_slot(struct cli_session *sess);

/**
 * request type enum
 */
enum request_type {
	/**
	 * request out of order
	 */
	BAD_ORDER,
	/**
	 * normal request in expected order
	 */
	NORMAL_REQ,
	/**
	 * resend of already handled request
	 */
	RESEND_REQ,
	/**
	 * crash recovery - replay request
	 */
	REPLAY_REQ,
};

/**
 * validate a request via sequnce ordering
 * @param sess - server side session info
 * @param cli_seq - client provided info in sequence operation.
 *
 * @return enum request code to describe request status.
 */
enum request_type c2_check_request(struct srv_session *sess,
				   struct session_sequence_args *cli_seq);

#endif
