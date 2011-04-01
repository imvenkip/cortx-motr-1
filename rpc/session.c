/* -*- C -*- */

#include "rpc/session.h"

/**
    Send handshake fop to the remote end. The reply contains sender-id.

    This function asynchronously sends an initial hand-shake fop to the other
    end of the connection. When reply is received, the c2_rpc_conn is
    moved into INITIALIZED state.

    @pre c2_rpc_conn->c_state == CONN_UNINITIALIZED
    @post c2_rpc_conn->c_state == CONN_INITIALIZING 
	|| c2_rpc_conn->c_state == CONN_INITIALIZED 
	|| c2_rpc_conn->c_state == CONN_TIMEOUT
 */
void c2_rpc_conn_init(struct c2_rpc_conn *, 
                                        struct c2_net_conn *); 

/**
   Destroy c2_rpc_conn object.
   No network communication involved.
   @pre c2_rpc_conn->c_nr_sessions == 0
   @post c2_rpc_conn->c_state == CONN_FREED
 */
int  c2_rpc_conn_fini(struct c2_rpc_conn *); 

/**
    Wait until c2_rpc_conn state machine reached the desired state.
 */
void c2_rpc_conn_timedwait(c2_rpc_conn *, enum c2_rpc_conn_state, 
			const struct c2_time *);
 
/**
   checks internal consistency of c2_rpc_conn
 */
bool c2_rpc_conn_invariant(const struct c2_rpc_conn *session);

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
    reply cache structures 

    There is only one reply cached in reply-cache per <session, slot>.

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
   We need reply rpc item specific methods to serialize and de-serialize 
   the item to and from the persistent reply cache store.
   for eg: In case of fop read reply, we need to store the
   block addresses for the data read.
 */
struct c2_rpc_slot_table_value {
	struct c2_verno	stv_verno;
        /** size of serialized reply in bytes. */
        uint64_t        stv_reply_size;
        /** Serialized reply */
        char            stv_reply[0];
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
   Reply cache stores reply of last processed item for each slot.
 */
struct c2_reply_cache {
        /** persistent store for slot tables of all the sessions */
	struct c2_db_env	*rc_dbenv;
        struct c2_table         *rc_slot_table;
	struct c2_table		*rc_in_core_slot_table;
};

int c2_rpc_reply_cache_init(struct c2_reply_cache *, struct c2_dbenv *);

int c2_rpc_reply_cache_fini(struct c2_reply_cache *);

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
	ACCEPT_ITEM,
	/** item is duplicate of request whose reply is cached in reply cache*/
	RESEND_REPLY,
	/** Already received this item and its processing is in progress */
	IGNORE_ITEM,
	/** Item is not in seq. send err msg to sender */
	SEND_ERROR_MISORDERED
};

/**
   Checks whether received item is correct in sequence or not and suggests
   action to be taken.
   'reply_out' is valid only if return value is RESEND_REPLY.
 */
enum c2_rpc_session_seq_check_result c2_rpc_session_item_received(
		struct c2_rpc_item *, struct c2_rpc_item **reply_out);

/** @} end of session group */

