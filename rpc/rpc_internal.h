/* -*- C -*- */
#ifndef _RPC_INTERNAL_

#define _RPC_INTERNAL_
/**
 find empty slot to send request.
 function scan slot table until first slot without busy flag is found and return
 that slot with busy flag set.

 @param sess - full initialized session

 @retval NULL failure, e.g., not have free slots to send
 @retval !NULL pointer to client slot info
 */
struct cli_slot *c2_find_unused_slot(struct cli_session *sess);

/**
 request ordering enumeration
 */
enum c2_request_order {
	/**
	 request out of order
	 */
	REQ_ORD_BAD,
	/**
	 normal request in expected order,
	 sequence id is greater last used by one.
	 */
	REQ_ORD_NORMAL,
	/**
	 resend of already handled request.
	 request sequence is same prevoisly hanlded
	 */
	REQ_ORD_RESEND,
};

/**
 * validate a request via sequnce ordering
 * @param sess - server side session info
 * @param cli_seq - client provided info in sequence operation.
 *
 * @return enum request code to describe request status.
 */
enum c2_request_order c2_check_request(const struct c2_srv_session *sess,
					const struct c2_session_sequence_args *cli_seq);

#endif

