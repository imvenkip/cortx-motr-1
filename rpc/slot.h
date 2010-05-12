/* -*- C -*- */
#ifndef __COLIBRY_RPC_SLOT_H_

#define __COLIBRY_RPC_SLOT_H_
/**
	@page rpc-slot RPC Slot

	Slot is dynamically created object. Slot created by a client request and
attached to active client session. It function is to maintain the order of
requests execution. On server, a slot contains a sequence ID and the cached
reply corresponding to the request sent with that sequence ID.

	Slot is internal structure and do not visible outside of rpc layer.
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
	c2_seq_t 	sl_seq;
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
	struct c2_cli_slot	sltbl_slots[0];
};

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

