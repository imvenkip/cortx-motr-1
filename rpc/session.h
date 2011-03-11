#ifndef __COLIBRI_RPC_SESSION_H__

#define __COLIBRI_RPC_SESSION_H__

/**
@page rpc-session rpc sessions Detailed Level Design Specification

@section Overview

Session is a dynamically created, long-lived receiver object created by
sender and used over time from one or more transport connections. Its function
is to maintain the receiver state relative to the connection belonging to a 
sender instance. This state is independent of the connection itself. Session
is shared by sender and receiver.

In other terms, one can also say that the session serves as an object 
representing a means of access by a sender to associated sender state on 
receiver independent of the physical means of access to that state.

A session is established between two end points. An end-point is 
dynamically bound to a service. There can be multiple active sessions in 
between same <sender, receiver> pair. Objective of session infrastructure 
is to provide FIFO and EOS (exactly once semantic) guarantees for fop 
execution. 

The sessions mechanism in colibri is similar to sessions in NFSv4.1.
@ref http://tools.ietf.org/html//rfc5661#page-40

A fop is a rpc item and so is ADDB record. rpc is a container for one or more
rpc items.

A session has multiple slots. Number of slots in the session decides the
degree of concurrency. Number of slots can vary dynamically. A slot represents 
an update stream. An Update stream is a sequential (FIFO) stream of rpc items 
with exactly once semantics.

Every rpc item being sent is associated with an update stream (in turn to a slot). 

A sender may want to send an item over a specific update stream or any available 
update stream.

@section def Definitions and Requirements

	Here is the brief list of requirements : - (ref. Networking 1-pager).
	
	@li FIFO ordering of fops within a stream (achieved through resend).

	@li exactly once semantics (EOS) of fop execution (achieved through replay).

	@li negotiable degree of concurrency of sender-receiver interaction (achieved 
	    through having multiple slots within a session, plus a run-time protocol
	    to adjust number of slots).


	Some definitions : -

	@li Sender		: - Sender represents client.
	
	@li Receiver		: - Receiver represents server.
	
	@li sender ID		: - sender ID is a 64-bit quantity used as a unique 
				    reference to the sender. NFSv4.1 specification 
				    states that it is supplied by the receiver.
	
	@li Session ID		: - Its an identifier used to identify a particular 
				    session. 

	@li Slot ID		: - Its the index into the slot table. Its 32-bit 
				    quantity.
	
	@li Update Stream	: - It is an ADT associated with <session, slot>
				    and used to send rpc items with FIFO and EOS
				    constraints.

@section sessionfunct Functional specification

@ref session

@section sessionlogspec Logical specification


Session exists between sender and receiver (each session is uniquely identified 
by the session identifier).

There are multiple slots in the session. Each session is associated with a 
particular incarnation (instance of the sender). Sender is identified by the 
sender id.

Each session has a slot table associated with it. Slot ID is the index into this 
slot table. Each slot ID has a state associated with it whether that slot is reply_pending
or not. If set then it indicates that there is an outstanding request 
(means sender is waiting for the reply rpc item for the sent rpc item) else 
there is no pending reply rpc item for the last sent rpc item on that slot.


On the receiver following things takes place when session_create request is received: -

     @li Receiver creates session by allocating space for the session reply cache 
		(it is persistent cache do db made use of).
    
     @li For each slotID in reply cache receiver sets the last_seen_xid to zero (0) 
	and associates a NULL reply rpc item with the error RPC_ITEM_REQ_MISORDERED.

     @li If sender sends the request rpc item with last_sent_xid zero then this 
        above error is returned.

     @li After these initializations session state is successfully created and 
	associated with the senderID.

Each rpc item must contain the associated update stream. When it is sent the 
sequence id (xid) is also to be assigned to the request.

Receiver compares xid of received rpc item with the last seen xid for that 
particular slot.

Following conditions are possible : -

    i) New request ->   xid is greater by one compared to last_seen_xid on 
			the receiver for that slot then the receiver consumes 
			the rpc item and constructs the rpc item reply message 
			and increments last_seen_xid by one and sends the rpc 
			item reply message to the sender.
                        (executing rpc item requests serially for a particular 
			update stream).
			@note that for certain fop types eg. READ, reply cache does not
			contain the whole reply state, because it is too large. Instead
			the cache contains the pointer (block address) to the location
			of the data.
 
    ii) Retransmitted request -> in this case the rpc item's xid is equal to 
				the last_seen_xid on the server for that particular 
				slot. In this case the server constructs the reply 
				 rpc item from the persistent reply cache and sends 
				to the sender.

    iii) Misordered retry -> In this case the xid of the rpc item is less 
				than the last_seen_xid receiver returns 
				RPC_ITEM_SEQ_MISORDERED. 

    iv) Misordered new request -> where xid is greater than last_seen_xid 
				on the server for the same update stream by two or more. 
				The receiver will return RPC_ITEM_SEQ_REORDERED error.

				@note Future Optimization : - We can put misordered new
				rpc item request in some (memory-only) queue with the timeout
				hoping that the preceding rpc item will arrive soon.

    @note TODO : -
	@li Design protocol to generate sender id.(similar to EXCHANGE_ID protocol in NFSv4.1)
	@li Design protocol to dynamically adjust the no. of slots.

@defgroup session RPC SESSIONS 

@{

*/

struct c2_rpc_item;
struct c2_service_id;
struct c2_rpc_session;
struct c2_update_stream;
struct c2_rpc_snd_rcv;
struct c2_rpc_snd_slot_table;
struct c2_rpc_snd_slot;

/**
   Session object at the sender side.
   It is opaque for the client like c2t1fs.
 */

struct c2_rpc_session {
	/** linkage into list of all sessions */
	struct c2_list_link		s_link;
	uint64_t			s_session_id;
	struct c2_rpc_snd_slot_table	*s_slot_table;			
	/** Sender state associated with this session */
	struct c2_rpc_snd_rcv 		*s_snd_rcv;
	/** Connection with receiver */
	struct c2_net_conn 		*ss_conn;
};

/** 
   Creates a new session and associates it with connection conn
   As session is independent of the connection itself, the
   SESSION_CREATE API does not establishes a connection internally.
   Instead it takes a reference to existing connection object.
 */

int c2_rpc_session_create(struct c2_net_conn *conn,
				struct c2_rpc_session **out);

/**
   Bind a connection to the session. This is required in case
   existing connection of session is some-how got terminated and
   a new connection is to be associated with session
*/

int c2_rpc_session_bind_conn(struct c2_rpc_session *, 
					struct c2_net_conn *);

/**
   Send destroy session message to receiver and remove all the
   state associated with this session on sender.
*/

int c2_rpc_session_destroy(struct c2_rpc_session *);

/**
   FIFO property is not ensured for items across update stream.
*/ 

struct c2_rpc_update_stream {
	uint64_t	us_session_id;
	uint32_t	us_slot_id;
};

/**
   Get an unused update stream from given session.
   When a client (e.g. c2t1fs) gets an update stream, it is NOT
   the exclusive user of the stream. i.e. To ensure FIFO property
   for a set of rpc-items it is necessary that they should be
   sent on same stream but exclusive access to the stream is not required.
 */

int c2_rpc_update_stream_get(struct c2_rpc_session *,
				struct c2_rpc_update_stream **);

/* ===================================================================== */

/**
   Gets the update stream associated with the rpc item, else returns
   NULL in 'out' parameter
 */

int c2_rpc_item_update_stream_get(struct c2_rpc_item *item, 
					struct c2_rpc_update_stream **out);
int c2_rpc_item_update_stream_set(struct c2_rpc_item *item, 
					struct c2_rpc_update_stream *in);

/**
   Obtain sender ID for communicating with the receiver at the
   other end of conn. 
   XXX The protocol is yet to be decided.
   This routine is used while creating first session with any service.
*/

int c2_rpc_snd_id_get(struct c2_net_conn *conn, uint64_t *out);

/**
   For every service to which sender wants to communicate there is one
   instance of c2_rpc_snd_rcv. All these instances of c2_rpc_snd_rcv are
   maintained in a global list. Instance of c2_rpc_snd_rcv stores a list of all
   sessions currently active with the service.
   Same sender has different sender_id to communicate with different service.
 */

struct c2_rpc_snd_rcv {
	/** Every snd_rcv is stored on a global list */ 
	struct c2_list_link	sr_link;
	struct c2_service_id	*sr_service_id;
	/** Sender ID (aka client ID) */
	uint64_t		sr_snd_id;
	/** List of all the sessions for this <sender,receiver> */
	struct c2_list		sr_sessions;
	/** Sequence no. to serialize SESSION_CREATE's */
	uint64_t 		sr_seq_id;
	/** At any time at max one SESSION_CREATE request can be 
		in progress with the service. */
	bool			sr_session_create_in_progress;
};

void c2_rpc_snd_rcv_init(struct c2_rpc_snd_rcv *snd_rcv, unint64_t snd_id,
				struct c2_service_id *svc_id);

int  c2_rpc_snd_rcv_fini(struct c2_rpc_snd_rcv *snd_rcv);

/** 
   This structure represents the slot table information
 */
 
struct c2_rpc_snd_slot_table {
	/** sequence id and used/unused property per slot */
	struct c2_rpc_snd_slot 		*sst_slots;
	/** Number of slots in the table */
	uint32_t 			sst_nr_slots;
	/** lock protecting this slot table */
	struct c2_mutex 		sst_lock;
	/** highest slot id for which the sender has the outstanding
	    request */
	uint32_t 			sst_highest_used_slot_id;
};

/** 
    structure giving information about used/unused, sequence id for the
    particular slot-id
 */

struct c2_rpc_snd_slot {
	bool 			ss_waiting_for_reply;
	/** sequence id for this particular slot */
	uint64_t 		ss_xid;
	/** reference to the last sent item for which the
	reply is not received (In case need to resend) */ 
	struct c2_rpc_item 	*ss_sent_item;
};

/** 
    Receiver side structures
 */

/** 
   All session specific parameters except slot table
   should go here 

   All instances of c2_rpc_session_params will be stored
   in db5 in memory table with session_id as key.
 */

struct c2_rpc_session_params {
	uint32_t	sp_nr_slots;
	uint32_t	sp_target_highest_slot_id;
	uint32_t	sp_enforced_highest_slot_id;
};

int c2_rpc_session_params_get(uint64_t session_id, 
				struct c2_rpc_session_params **out);

int c2_rpc_session_params_set(uint64_t session_id,
				struct c2_rpc_session_params *param);

/** 
   Receiver side SESSION_CREATE and SESSION_DESTROY handlers
 */

int c2_rpc_session_create_handler(struct c2_fop *, struct c2_fop_ctx *);

/**
   Destroys all the information associated with the session on the receiver 
   including reply cache entries.
 */

int c2_rpc_session_destroy_handler(struct c2_fop *, struct c2_fop_ctx *);

/**
    reply cache structures

    There is only one reply cached in reply-cache per <session, slot>.
 */

/**
    key =  <session-id, slot-id> 
 */

struct c2_rpc_slot_table_key {
        uint64_t 	stk_session_id;
        uint32_t 	stk_slot_id;
};

/** 
   We need reply rpc item specific methods
   to serialize and de-serialize the item to and from the 
   persistent reply cache store.
   for eg: In case of fop read reply, we need to store the
   block addresses for the data read.
 */

struct c2_rpc_slot_table_value {
	/** xid of request whose reply is cached */
	uint64_t	stv_xid;
	/** size of serialized reply in bytes */
	uint64_t	stv_size;
	/** Serialized reply */
	char		stv_data[0];
};

/**
    SESSION_CREATE is non-idempotent operation and hence requires EOS
    semantic.
    So we need to persistently store reply of most recent SESSION_CREATE 
    request.
 */
 
struct c2_rpc_session_reply_cache_key {
	uint64_t	srck_snd_id;
};

struct c2_rpc_session_reply_cache_value {
	/** seq id can be obtained from the reply itself */
	struct c2_rpc_session_create_rep	srcv_reply;
};

struct c2_reply_cache {
	struct c2_table		*rc_session_reply_cache;
	/** persistent store for slot tables of all the sessions */
	struct c2_table		*rc_slot_table;
};

int c2_rpc_reply_cache_init(struct c2_reply_cache *, struct c2_dbenv *);

int c2_rpc_reply_cache_fini(struct c2_reply_cache *);

/**
   Insert a reply in reply cache. reply itself is an rpc-item.
   Takes <session_id, slot_id> from c2_rpc_item fields.
   Inserts xid and reply in db using <session_id, slot_id> as key
   c2_rpc_encode_item_t is a function pointer, that serializes
   the rpc_item. 
   If there is existing record at <session_id, slot_id> then it
   updates the record with new value.
   In the absence of stable transaction APIs, we're using
   db5 transaction apis as place holder
   We need to give some more thought on serialization-deserialization
   of rpc_item to and from database. 
 */
 
int c2_rpc_reply_cache_insert(struct c2_rpc_item *, c2_rpc_encode_item_t,
				 struct c2_db_tx *);
 
/**
   Get information about last reply sent on a particular slot
 */

int c2_rpc_reply_cache_search(struct c2_reply_cache *, uint64_t session_id,
				uint32_t slot_id, c2_rpc_decode_item_t,
				struct c2_rpc_item **out_reply, 
				uint64_t *out_xid); 

int c2_rpc_session_reply_cache_insert(struct c2_rpc_session_create_rep *, 
					struct c2_db_tx *);

int c2_rpc_session_reply_cache_search(struct c2_reply_cache *, uint64_t snd_id,
					struct c2_rpc_session_create_rep **out);

/** @} end of rpc-sessions group */	

#endif
