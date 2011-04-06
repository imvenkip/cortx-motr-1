/* -*- C -*- */

#include "rpc/session.h"

/**
   Change size of slot table in 'session' to 'nr_slots'.

   If nr_slots > current capacity of slot table then
        it reallocates the slot table.
   else
        it just marks slots above nr_slots as 'dont use'
 */
int c2_rpc_session_slot_table_resize(struct c2_rpc_session *session,
					uint32_t nr_slots);

/**
   Fill all the session related fields of c2_rpc_item.

   If item is unbound, assign session and slot id.
   If item is bound, then no need to assign session and slot as it is already
   there in the item.

   Copy verno of slot into item.verno. And mark slot as 'waiting_for_reply'

   rpc-core can call this routine whenever it finds it appropriate to
   assign slot and session info to an item.

   Assumption: c2_rpc_item has a field giving service_id of
                destination service.
 */
int c2_rpc_session_item_prepare(struct c2_rpc_item *);

/**
   Inform session module that a reply item is received.

   rpc-core can call this function when it receives an item. session module
   can then mark corresponding slot "unbusy", move the item to replay list etc.
 */
void c2_rpc_session_reply_item_received(struct c2_rpc_item *);

/**
   Start session recovery.

   @pre c2_rpc_session->s_state == SESSION_ALIVE
   @post c2_rpc_session->s_state == SESSION_RECOVERING
   
 */
int c2_rpc_session_recovery_start(struct c2_rpc_session *);

/**
   All session specific parameters except slot table
   should go here

   All instances of c2_rpc_session_params will be stored
   in db5 in memory table with <sender_id, session_id> as key.
 */
struct c2_rpc_session_params {
        uint32_t        sp_nr_slots;
        uint32_t        sp_target_highest_slot_id;
        uint32_t        sp_enforced_highest_slot_id;
};

int c2_rpc_session_params_get(uint64_t sender_id, uint64_t session_id,
                                struct c2_rpc_session_params **out);

int c2_rpc_session_params_set(uint64_t sender_id, uint64_t session_id,
                                struct c2_rpc_session_params *param);

/**
    Key into c2_rpc_in_core_slot_table.
    Receiver side.

    session_id is not unique on receiver.
    For each sender_id, receiver has session 0 associated with it.
    Hence snd_id (sender_id) is also a part of key.
 */
struct c2_rpc_slot_table_key {
        uint64_t        stk_snd_id;
        uint64_t        stk_session_id;
        uint32_t        stk_slot_id;
        uint64_t        stk_generation;
};

/**
   In core slot table stores attributes of slots which
   are not needed to be persistent.
   Key is same as c2_rpc_slot_table_key.
   Value is modified in transaction. So no explicit lock required.
 */
struct c2_rpc_in_core_slot_table_value {
	/** A request is being executed on this slot */
	bool		ics_busy;
};

/**
   Insert a reply item in reply cache and advance slot version.
   
   In the absence of stable transaction APIs, we're using
   db5 transaction apis as place holder
    
   @note that for certain fop types eg. READ, reply cache does not
   contain the whole reply state, because it is too large. Instead
   the cache contains the pointer (block address) to the location
   of the data.
 */
int c2_rpc_reply_cache_insert(struct c2_rpc_item *, struct c2_db_tx *);

enum c2_rpc_session_seq_check_result {
	/** item is valid in sequence. accept it */
	RSSC_ACCEPT_ITEM,
	/** item is duplicate of request whose reply is cached in reply cache*/
	RSSC_RESEND_REPLY,
	/** Already received this item and its processing is in progress */
	RSSC_IGNORE_ITEM,
	/** Item is not in seq. send err msg to sender */
	RSSC_SEND_ERROR_MISORDERED,
	/** Invalid session or slot */
	RSSC_SESSION_INVALID
};

/**
   Checks whether received item is correct in sequence or not and suggests
   action to be taken.
   'reply_out' is valid only if return value is RESEND_REPLY.
 */
enum c2_rpc_session_seq_check_result c2_rpc_session_item_received(
		struct c2_rpc_item *, struct c2_rpc_item **reply_out);

/**
   Receiver side SESSION_CREATE handler
 */
int c2_rpc_session_create_handler(struct c2_fom *);

/**
   Destroys all the information associated with the session on the receiver
   including reply cache entries.
 */
int c2_rpc_session_destroy_handler(struct c2_fom *);

int c2_rpc_session_create_rep_handler(struct c2_fom *);

int c2_rpc_session_destroy_rep_handler(struct c2_fom *);

int c2_rpc_conn_create_handler(struct c2_fom *);

int c2_rpc_conn_create_rep_handler(struct c2_fom *);

int c2_rpc_conn_terminate_handler(struct c2_fom *);

int c2_rpc_conn_terminate_rep_handler(struct c2_fom *);

/** @} end of session group */

