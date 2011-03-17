#ifndef __COLIBRI_RPC_SESSION_H__

#define __COLIBRI_RPC_SESSION_H__

/**
@page rpc-session rpc sessions Detailed Level Design Specification

@section Overview

Session is a dynamically created, long-lived object created by
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
@ref http://tools.ietf.org/html/rfc5661#section-2.10

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
	
	- FIFO ordering of fops within a stream (achieved through resend).

	- exactly once semantics (EOS) of fop execution (achieved through replay).

	- negotiable degree of concurrency of sender-receiver interaction (achieved 
	    through having multiple slots within a session, plus a run-time protocol
	    to adjust number of slots).


	Some definitions : -

	- Sender ID		: - sender ID is a 64-bit quantity used as a unique 
				    reference to the sender. NFSv4.1 specification 
				    states that it is supplied by the receiver.
	
	- Session ID		: - Its an identifier used to identify a particular 
				    session. 

	- Slot ID		: - Its the index into the slot table. Its 32-bit 
				    quantity.
	
	- Update Stream	: - It is an ADT associated with <session, slot>
				    and used to send rpc items with FIFO and EOS
				    constraints.

@section sessionfunct Functional specification

@ref session

@section sessionlogspec Logical specification


Session exists between sender and receiver (each session is uniquely identified 
by the session identifier).

There are multiple slots in the session. Each session is associated with a 
particular incarnation (instance of the sender). Sender instance is identified by the 
sender id.

Each session has a slot table associated with it. Slot ID is the index into this 
slot table. Each slot ID has a state associated with it whether that slot is reply_pending
or not. If set then it indicates that there is an outstanding request 
(means sender is waiting for the reply rpc item for the sent rpc item) else 
there is no pending reply rpc item for the last sent rpc item on that slot.


On the receiver following things takes place when session_create request is received: -

     - Receiver creates session by allocating space for the session reply cache 
		(it is persistent cache do db made use of).
    
     - For each slotID in reply cache receiver sets the last_seen_xid to zero (0) 
	and associates a NULL reply rpc item with the error RPC_ITEM_REQ_MISORDERED.

     - If sender sends the request rpc item with last_sent_xid zero then this 
        above error is returned.

     - After these initializations session state is successfully created and 
	associated with the senderID.

Receiver compares xid of received rpc item with the last seen xid for that 
particular slot.

With each slot two counters are associated:
last_executed_xid: xid of request whose execution is complete and its transaction
 			has been commited in memory and its reply is cached.
			This counter is in persistent slot table.
			This counter is updated in same transaction context
			as in which the request is executed.
last_seen_xid: xid of request which we accepted most recently. This counter is part
			of in core slot table.

At any time, one of following to conditions is true:
1. last_seen_xid == last_executed_xid
2. last_seen_xid == last_executed_xid + 1

For the duration between, the time when we accepted a request AND the time when
this request completes its execution, last_seen_xid == last_executed_xid + 1.

These two counters are required to resolve a schenarious where a duplicate 
request is executed when its execution was in progress.

Following conditions are possible : -

    - New request:
	If xid of received request is 1 more than last_executed_xid AND
			Not equal to last_seen_xid
	then 
	 - receiver concludes that it is a valid request in seq.
	 - set last_seen_xid to xid of new request
         - And forwards the request to upper layer for execution.

	@note that for certain fop types eg. READ, reply cache does not
	contain the whole reply state, because it is too large. Instead
	the cache contains the pointer (block address) to the location
	of the data.
 
    - Retransmitted request:
	If xid of received request == last_seen_xid
	then
		if last_seen_xid == last_executed_xid + 1
		then
			ignore the request. Its execution is already in progress.
		else
			obtain reply from reply cache.
			And send this reply to sender.
		end
	end

    - Misordered retry:
	In this case the xid of the rpc item is less 
	than the last_seen_xid receiver returns RPC_ITEM_SEQ_MISORDERED. 

    - Misordered new request: 
	where xid is greater than last_seen_xid 
	on the server for the same update stream by two or more. 
	The receiver will return RPC_ITEM_SEQ_REORDERED error.

	@note Future Optimization : - We can put misordered new
	rpc item request in some (memory-only) queue with the timeout
	hoping that the preceding rpc item will arrive soon.

    @note TODO : -
	- Design protocol to generate sender id.(similar to EXCHANGE_ID protocol in NFSv4.1)
	- Design protocol to dynamically adjust the no. of slots.

@defgroup session RPC SESSIONS 

@{

*/

/* Imports */
struct c2_rpc_item;
struct c2_service_id;

/* Exports */
struct c2_rpc_session;

/* Internal: required for declaration of c2_rpc_session */
struct c2_rpc_snd_slot_table;
struct c2_rpc_snd_slot;

enum c2_rpc_session_state {
	SESSION_UNINITIALIZED,
	SESSION_CREATING,
	SESSION_ALIVE,
	SESSION_RECOVERING,
	SESSION_TIMED_OUT,
	SESSION_TERMINATING,
	SESSION_DEAD
};

/**
   Session object at the sender side.
   It is opaque for the client like c2t1fs.

   c2_rpc_session state transition:
   Starting state: UNINITIALIZED

   Current state		Event/action			Next state
   UNINITIALIZED		session_create			CREATING
   CREATING			create successful		ALIVE
	"			time out			TIMED_OUT
   ALIVE			receiver/nw failure		RECOVERING
	"			terminate			TERMINATING
   RECOVERING			recovery successful		ALIVE
   TERMINATING			terminate successful		DEAD
	"			time out/retry			TERMINATING
   TIMED_OUT			free				DEAD

 */
struct c2_rpc_session {
	/** linkage into list of all sessions */
	struct c2_list_link		s_link;
	enum c2_rpc_session_state	s_state;
	/** session_id valid only if s_state is in {ALIVE,
		RECOVERING} */
	uint64_t			s_session_id;
	struct c2_rpc_snd_slot_table	*s_slot_table;			
	/** Sender state associated with this session */
	struct c2_rpc_session_group 	*s_sg;
	struct c2_service_id		s_svc_id;
	/** Connection with receiver */
	struct c2_net_conn 		*ss_conn;
	struct c2_chan			ss_chan;
	/** lock protecting this session and slot table */
	struct c2_mutex 		sst_lock;
};

/** 
   Creates a new session and associates it with connection conn
   As session is independent of the connection itself, the
   SESSION_CREATE API does not establishes a connection internally.
   Instead it takes a reference to existing connection object.

   Steps:
   - Get service id of remote service from c2_net_conn
   - Find session_group object associated with the service id
   - If NOT present
	-- Instantiate new session_group
	-- Communicate with remote service to obtain sender_id
	-- store sender_id in session_group object
	-- Intialize a new session object with session_id = 0 
		and only one slot. Don't inform
		anything about this session to the remote service
	-- Store the session 0 in list present in session_group.
	-- Insert the session_group object in a global list of 
		session_group.
     end if
   - create a SESSION_CREATE fop
   - Find session with session id = 0 in session_group
   - Associate session create request fop with <session_id=0, slot=0>
   - Send the request to remote service over conn
   - Get session id from reply and store it in session object.
   - Initialize all the slots of session object.
   - Insert the session in session_group->s_sessions list
   - return
 */
int c2_rpc_session_create(struct c2_net_conn *conn,
				struct c2_rpc_session *out);

/**
   Wake-up call will be in the handler of SESSION_CREATE reply
   Waits for SESSION_CREATE completion.
 */
int c2_rpc_session_create_wait(struct c2_chan *);

/**
   If layer above rpc wants reference to any open session to a particular
   service then it can use this routine
*/
int c2_rpc_session_find(struct c2_service_id *svc_id, struct c2_rpc_session **out);

/**
   Bind a connection to the session. This is required in case
   existing connection of session is some-how got terminated and
   a new connection is to be associated with session
*/
void c2_rpc_session_bind_conn(struct c2_rpc_session *, 
					struct c2_net_conn *);

/**
   Send destroy session message to receiver and remove all the
   state associated with this session on sender.
*/
int c2_rpc_session_destroy(struct c2_rpc_session *);

/**
   Waits for SESSION_DESTROY completion.
 */

int c2_rpc_session_destroy_wait(struct c2_chan *);


	
/** 
   This structure represents the slot table information
 */
struct c2_rpc_snd_slot_table {
	/** sequence id and used/unused property per slot */
	struct c2_rpc_snd_slot 		*sst_slots;
	/** Number of slots in the table */
	uint32_t 			sst_nr_slots;
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
	uint64_t		ss_last_persistent_xid;
	uint64_t		ss_generation;
	/** reference to the last sent item for which the
	reply is not received (In case need to resend) */ 
	struct c2_rpc_item 	*ss_sent_item;
	/** list of items for which we've received reply from receiver but
	their effects not persistent on receiver */
	struct c2_queue		*ss_replay_queue;
	/** receiver can ask sender to reduce number of slots.
	we shouldn't destroy this slot unless and untill all the reference
	to this slot are released */
	struct c2_ref		ss_ref;
};

/**
  Get a non-busy slot
*/
int c2_rpc_slot_get(struct c2_rpc_session *session, struct c2_rpc_snd_slot **out);

int c2_rpc_slot_put(struct c2_rpc_snd_slot *);

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
    These are reply handlers
 */
int c2_rpc_session_create_rep_handler(struct c2_fop *, struct c2_fop_ctx *);

int c2_rpc_session_destroy_rep_handler(struct c2_fop *, struct c2_fop_ctx *);

/** @} end of rpc-sessions group */	

#endif
