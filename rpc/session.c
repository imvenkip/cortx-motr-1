/* -*- C -*- */

#include "rpc/session.h"

enum c2_rpc_session_group_state { 
        SG_UNINITIALIZED, 
        SG_INITIALIZING, 
        SG_INITIALIZED, 
        SG_IN_USE, 
        SG_TIMED_OUT, 
        SG_FREED 
}; 

/**
   @addtogroup session
   @{
 */


/**
   For every service to which sender wants to communicate there is one
   instance of c2_rpc_session_group. All these instances of c2_rpc_session_group are
   maintained in a global list. Instance of c2_rpc_session_group stores a list of all
   sessions currently active with the service.
   Same sender has different sender_id to communicate with different service.

   At the time of creation of a session_group, a "special" session with session_id 0
   is also created. It is special in the sense that it is "hand-made" and there is no need
   to communicate to receiver in order to create this session.
   Receiver assumes that there always exists a session 0 for each sender_id.
   Session 0 always have exactly 1 slot within it.
   When receiver receives first item on session 0 for any sender_id, it creates an
   entry of slot of session 0 in its slot table.
   Session 0 is required to send other SESSION_CREATE/SESSION_TERMINATE requests 
   to the receiver. As SESSION_CREATE and SESSION_TERMINATE operations are
   non-idempotent, they also need EOS and FIFO guarantees.

   c2_rpc_session_group state transitions:
   Starting state: UNINITIALIZED

   Current state                Event/action                    Next State

   UNINITIALIZED                init                            INITIALIZING
   INITIALIZING                 init_successful                 INITIALIZED
        "                       init_timed_out                  TIMED_OUT
   INITIALIZED                  session_created/ref = 1         IN_USE
        "                       finalize                        FREED
   IN_USE                       session_created/ref++           IN_USE
        "                       session_destroyed/ref--         IN_USE
        "                       ref == 0                        FREED
        "                       finalize                        IN_USE
   TIMED_OUT                    free                            FREED
        "                       retry                           INITIALIZING
 */
struct c2_rpc_session_group { 
        /** Every session_group is stored on a global list */  
        struct c2_list_link             sg_link; 
        enum c2_rpc_session_group_state sg_state; 
        struct c2_chan                  sg_chan; 
        struct c2_service_id            *sg_service_id; 
        /** Sender ID (aka client ID) */ 
        uint64_t                        sg_snd_id; 
        /** List of all the sessions for this <sender,receiver> */ 
        struct c2_list                  sg_sessions;  
        /** Counts number of sessions (excluding session 0) */ 
        struct c2_ref                   sg_ref; 
}; 
 
/** 
   This also creates a session object with session id 0 without any kind of  
   network communication and inserts it in sg_sessions list. 
   Session 0 is used to send other SESSION_CREATE request to this service. 
*/ 
void c2_rpc_session_group_init(struct c2_rpc_session_group *sg, 
                                        struct c2_service_id *svc_id); 
 
int  c2_rpc_session_group_fini(struct c2_rpc_session_group *sg); 


/** 
   Obtain sender ID for communicating with the receiver at the 
   other end of conn.  
   XXX The protocol is yet to be decided. 
   This routine is used while creating first session with any service. 
*/ 
int c2_rpc_snd_id_get(struct c2_net_conn *conn, struct c2_rpc_session_group *sg); 
 
/**  
   Wait until we get sender ID from receiver. 
 */ 

int c2_rpc_snd_id_get_wait(struct c2_chan *); 
/** 
   All session specific parameters except slot table
   should go here 

   All instances of c2_rpc_session_params will be stored
   in db5 in memory table with session_id as key.
 */
struct c2_rpc_session_params {
        uint32_t        sp_nr_slots;
        uint32_t        sp_target_highest_slot_id;
        uint32_t        sp_enforced_highest_slot_id;
};

int c2_rpc_session_params_get(uint64_t session_id,
                                struct c2_rpc_session_params **out);

int c2_rpc_session_params_set(uint64_t session_id,
                                struct c2_rpc_session_params *param);

/**
   Get reference to an unused slot
*/
int c2_rpc_snd_slot_get(struct c2_rpc_session *session, struct c2_rpc_snd_slot **out);

/**
   Release a slot
*/
int c2_rpc_snd_slot_put(struct c2_rpc_snd_slot *slot);

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
};

/** 
   We need reply rpc item specific methods
   to serialize and de-serialize the item to and from the 
   persistent reply cache store.
   for eg: In case of fop read reply, we need to store the
   block addresses for the data read.
 */
struct c2_rpc_slot_table_value {
        struct c2_lsn_t stv_lsn;
        /** last executed xid */
        uint64_t        stv_xid;
        uint64_t        stv_generation;
        /** size of serialized reply in bytes. */
        uint64_t        stv_size;
        /** Serialized reply */
        char            stv_data[0];
};

/**
   In core slot table stores attributes of slots which 
   are not needed to be persistent e.g. last_seen_xid.
   Key is same as c2_rpc_slot_table_key.
 */
struct c2_rpc_in_core_slot_table_value {
	uint64_t	rie_last_seen_xid;
};

struct c2_reply_cache {
        /** persistent store for slot tables of all the sessions */
        struct c2_table         *rc_slot_table;
	struct c2_table		*rc_in_core_slot_table;
};

int c2_rpc_reply_cache_init(struct c2_reply_cache *, struct c2_dbenv *);

int c2_rpc_reply_cache_fini(struct c2_reply_cache *);

/**
   Insert a reply in reply cache. reply itself is an rpc-item.
   Takes <snd_id, session_id, slot_id> from c2_rpc_item fields.
   Inserts xid and reply in db using <snd_id, session_id, slot_id> as key
   If there is existing record at <snd_id, session_id, slot_id> then it
   updates the record with new value.
   In the absence of stable transaction APIs, we're using
   db5 transaction apis as place holder
 */
int c2_rpc_reply_cache_insert(struct c2_rpc_item *, struct c2_db_tx *);

/**
   Get information about last reply sent on a particular slot
 */
int c2_rpc_reply_cache_search(struct c2_rpc_reply_cache *, 
              struct c2_rpc_slot_table_key *key, struct c2_rpc_item **out_reply,
              uint64_t *out_xid);

int c2_rpc_last_seen_xid_get(struct c2_rpc_reply_cache *rc,
		struct c2_rpc_slot_table_key *key, uint64_t *out_xid);
int c2_rpc_last_seen_xid_set(struct c2_rpc_reply_cache *rc,
		struct c2_rpc_slot_table_key *key, uint64_t in_xid);

/** @} end of session group */

