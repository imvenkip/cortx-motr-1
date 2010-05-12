/* -*- C -*- */
#ifndef _RPC_INTERNAL_

#define _RPC_INTERNAL_
/**
 internal structures and functions
 */

/**
 find empty slot to send request.
 function scan slot table until first slot without busy flag is found and return
 that slot with busy flag set, to indicate that slot sequence is busy.

 @param sess - full initialized session

 @retval NULL failure, e.g., not have free slots to send
 @retval !NULL pointer to client slot info
 */
struct c2_cli_slot *c2_slot_find_unused(struct c2_cli_session *sess);

/**
 release busy flag after finished processing reply.
 */
struct c2_slot_unbusy(struct c2_cli_slot *slot);

#endif

