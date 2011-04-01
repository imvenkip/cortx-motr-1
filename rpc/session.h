#ifndef __COLIBRI_RPC_SESSION_H__

#define __COLIBRI_RPC_SESSION_H__

/**
@page rpc-session rpc sessions Detailed Level Design Specification

@section Overview

A session is established between two end points. An end-point is 
dynamically bound to a service. There can be multiple active sessions in 
between same <sender, receiver> pair. Objective of session infrastructure 
is to provide FIFO and EOS (exactly once semantic) guarantees for fop 
execution. Session is shared by sender and receiver. i.e. for a particular 
session, both sender and receiver maintain some piece of information. 
Session state is independent of connection itself.

The sessions mechanism in colibri is similar to sessions in NFSv4.1.
@ref http://tools.ietf.org/html/rfc5661#section-2.10

A fop is a rpc item and so is ADDB record. rpc is a container for one or more
rpc items.

A session has multiple slots. Number of slots in the session decides the
degree of concurrency. Number of slots can vary dynamically. A slot represents 
an update stream. An Update stream is a sequential (FIFO) stream of rpc items 
with exactly once semantics.

Layers above rpc (e.g. c2t1fs, replicator) may or may not specify update_stream
while submitting an item to rpc layer. 

@section def Definitions and Requirements

Brief list of requirements: 
   - FIFO ordering of fops within a stream (achieved through resend).
   - exactly once semantics (EOS) of fop execution (achieved through replay).
   - negotiable degree of concurrency of sender-receiver interaction (achieved 
     through having multiple slots within a session, plus a run-time protocol
     to adjust number of slots).

@ref https://docs.google.com/a/xyratex.com/document/d/1KjdyLMcIuBdbscXZJf25IlA-eJ-JCwhU9Xm5syhtQgc/edit?hl=en#

Some definitions : -

- Sender ID:
  Sender-id identifies a sender _incarnation_. Incarnation of a sender changes
  when sender or receiver loses its persistent state. Same sender can have
  different sender-id to communicate with different receiver. Sender-id is 
  given by receiver.

- Session ID:
  session-id identifies a particular session on sender and receiver. It
  is not globally unique.  

- Slot ID:
  Its the index into the slot table. Its 32-bit quantity.
	
- Update Stream:
  It is an ADT associated with <session, slot> and used to send rpc items with 
  FIFO and EOS constraints.

@section sessionfunct Functional specification

@ref session

@section sessionlogspec Logical specification

Ensuring FIFO and EOS:
---------------------
A version number is a pair <lsn, version_count>. Each slot has associated version 
number (slot.verno).
Before sending first item associated with a slot, following condition holds.
receiver.slot.verno.vc == sender.slot.verno.vc == 0

Each rpc item associated with the slot also contains a version
number (item.verno). Before sending an item, slot.verno is copied into 
item.verno

When an item is received, following conditions are possible : -

    - New item:
	if c2_rpc_is_redoable(slot.verno, item.before_verno) == 0 and NOT slot.busy
	then 
		accept item as new item
		slot.busy = true
	end if
    - Retransmitted item:
	if c2_rpc_is_undoable(slot.verno, item.before_verno) == 0
	then
		item is retransmitted.
		obtain reply from reply cache and send to sender
	end if
    - Misordered retry:
	if c2_rpc_is_redoable(slot.verno, item.before_verno) == -EALREADY
	then
		it is a misordered item.
		send error RPC_ITEM_MISORDERED
	end if
    - Misordered new item:
	if c2_rpc_is_redoable(slot.verno, item.before_verno) == -EAGAIN
	then
		it is misordered new item.
		send error RPC_ITEM_MISORDERED
	end if

	@note Future Optimization : - We can put misordered new
	rpc item request in some (memory-only) queue with the timeout
	hoping that the preceding rpc item will arrive soon.

    @note TODO : -
	- Design protocol to generate sender id.(similar to EXCHANGE_ID protocol in NFSv4.1)
	- Design protocol to dynamically adjust the no. of slots.
	- Update Streams. 
@defgroup session RPC SESSIONS 

@{

*/

/* Imports */
struct c2_rpc_item;
struct c2_service_id;

/* Exports */
struct c2_rpc_session;

/* Internal: required for declaration of c2_rpc_session */
struct c2_rpc_conn;
struct c2_rpc_snd_slot;

enum {
	SESSION_0 = 0,
	SESSION_ID_INVALID = ~0,
	SENDER_ID_INVALID = 0
};

enum c2_rpc_conn_state {
        /**
           A newly allocated c2_rpc_conn object is in
           UNINITIALIZED state.
         */
        CONN_UNINITIALIZED = 0,
        /**
           When sender is waiting for receiver reply to get its sender ID it is
           in INITIALIZIG state.
         */
        CONN_INITIALIZING = (1 << 0),
        /**
           Once sender gets sender ID from receiver it is in 
           INITIALIZED state
         */
        CONN_INITIALIZED = (1 << 1),
        /**
           When first session (with session-id != 0) is added to c2_rpc_conn
           its state changes to IN_USE
         */
        CONN_IN_USE = (1 << 2),
        /**
           If c2_rpc_conn is in INITIALIZING state and sender doesn't receive
           sender-id from receiver within a specific time then c2_rpc_conn
           moves to TIMED_OUT state
        */
        CONN_TIMED_OUT = (1 << 3),
	/**
           When all the sessions belonging to a c2_rpc_conn are terminated
           the c2_rpc_conn is also freed. Then it moves to FREED state.
        */
        CONN_FREED = (1 << 4),
        CONN_STATE_MASK = 0x1F
};

/**
   For every service to which sender wants to communicate there is one
   instance of c2_rpc_conn. All instances of c2_rpc_conn are
   maintained in a global list. Instance of c2_rpc_conn stores a list of all
   sessions currently active with the service.
   Same sender has different sender_id to communicate with different service.

   At the time of creation of a c2_rpc_conn, a "special" session with SESSION_0
   is also created. It is special in the sense that it is "hand-made" and there
   is no need to communicate to receiver in order to create this session.
   Receiver assumes that there always exists a session 0 for each sender_id.
   Session 0 always have exactly 1 slot within it.
   When receiver receives first item on session 0 for any sender_id, it creates an
   entry of slot of session 0 in its slot table.
   Session 0 is required to send other SESSION_CREATE/SESSION_TERMINATE requests 
   to the receiver. As SESSION_CREATE and SESSION_TERMINATE operations are
   non-idempotent, they also need EOS and FIFO guarantees.

   +-------------------------> UNINITIALIZED
                                    |
                                    |  c2_rpc_conn_init()
   +----------------------------+   |
   |                            |   |
   |                            V   V
   |     +-------------------- INITIALIZING
 R |     | time-out                 |
 E |     |                          | init_successful
 T |     |                          |
 R |     V                          V
 Y +--- TIMED_OUT               INITIALIZED
         |                          |
         |                          | session_created/ref=1
         |                          |   
         |              +--------+  | +----------+
         |              |        |  | |          | session_created/ref++
         |              |        V  V V          |
         |              +--------IN-USE -------- +
         |      session_destroyed/  |            
         |              ref--       |
         |                          | ref == 0
         |      free                V
         +-----------------------> FREED

 */
struct c2_rpc_conn { 
        /** Every c2_rpc_conn is stored on a global list */  
        struct c2_list_link              c_link;
        enum c2_rpc_conn_state		 c_state;
        /** Id of the service with which this c2_rpc_conn is associated */
        struct c2_service_id            *c_service_id;
        /** Sender ID (aka client ID) */
        uint64_t                         c_snd_id;
        /** List of all the sessions for this <sender,receiver> */
        struct c2_list                   c_sessions;
        /** Counts number of sessions (excluding session 0) */
        uint64_t                         c_nr_sessions;
        /** Deprecated: connection with receiver. 
		All sessions share this connection */
        struct c2_net_conn              *c_conn;
        struct c2_chan                   c_chan;
        struct c2_mutex                  c_mutex;
};

/**
   Possible states of a session object
 */
enum c2_rpc_session_state {
	/**
	   When a session object is newly instantiated it is in 
	   UNINITIALIZED state.
	 */
	SESSION_UNINITIALIZED = 0,
	/**
	   When sender sends a SESSION_CREATE FOP to reciever it
	   is in CREATING state
	 */
	SESSION_CREATING = (1 << 0),
	/**
	   When sender gets "SESSION_CREATE operation successful" reply 
	   from receiver, session transitions to state ALIVE. This is the
	   normal working state of session.
	 */
	SESSION_ALIVE = (1 << 1),
	/**
	   When recovery is in progress (i.e. replay or resend) the
	   session is in RECOVERING state. New requests coming on this
	   session are held in a queue until recovery is complete.
	 */
	SESSION_RECOVERING = (1 << 2),
	/**
	   If sender does not get reply for SESSION_CREATE within specific
	   time then session goes in TIMED_OUT state.
	 */
	SESSION_TIMED_OUT = (1 << 3),
	/**
	   When sender sends SESSION_DESTROY fop to receiver and is waiting
	   for reply, then it is in state TERMINATING.
	*/
	SESSION_TERMINATING = (1 << 4),
	/**
	   Once sender gets reply for successful SESSION_DESTROY it moves
	   session in DEAD state.
	   Sender cannot use a DEAD session.
	 */
	SESSION_DEAD = (1 << 5),
	SESSION_STATE_MASK = 0x3F
};

/**
   Session object at the sender side.
   It is opaque for the client like c2t1fs.

            +------------------> UNINITIALIZED
				      |
				      | c2_rpc_session_create()
				      |
		timed-out	      V
          +-------------------------CREATING
	  |            		      |
	  V       		      | create successful
	TIMED_OUT		      |
	  |      		      V		Recovery complete
	  |			    ALIVE <-----------------------------------+
	  |			      |	|				      |
	  |			      | | receiver or nw failure              |
	  |			      | +-------------------> RECOVERING -----+
	  |			      |
	  | free 		      | session_terminate
	  |			      V
	  |		         TERMINATING <-------------+    
	  |			      |  |		   |			
	  |			      |  |                 | timed-out/retry
	  |			      |  +-----------------+
	  |		              | 
	  |			      | Session_terminated
	  |			      V
	  +----------------------->  DEAD

 */
struct c2_rpc_session {
	/** linkage into list of all sessions within a c2_rpc_conn */
	struct c2_list_link		 s_link;
	enum c2_rpc_session_state	 s_state;
	/** identifies a particular session. It is not globally unique */
	uint64_t			 s_session_id;
	/** rpc connection on which this session is created */
	struct c2_rpc_conn	 	*s_conn;
	struct c2_chan			 s_chan;
	/** lock protecting this session and slot table */
	struct c2_mutex 		 s_mutex;
	/** Number of active slots in the table */
	uint32_t 			 s_nr_slots;
	/** Capacity of slot table */
	uint32_t			 s_slot_table_capacity;
	/** highest slot id for which the sender has the outstanding request */
	uint32_t 			 s_highest_used_slot_id;
	/** pointer to array of slots */
	struct c2_rpc_snd_slot 		*s_slot_table;
};

/**
    Sends a SESSION_CREATE fop across pre-defined 0-session in the c2_rpc_conn.

    @pre c2_rpc_conn->c_state == SG_IN_USE || c2_rpc_conn->c_state == SG_INITIALIZED
    @pre out->s_state == SESSION_UNINITIALIZED
    @post c2_rpc_conn->c_state == SG_IN_USE
    @post out->s_state == SESSION_CREATING
 */
int c2_rpc_session_create(c2_rpc_conn *conn, c2_rpc_session *out);

/**
   Send destroy session message to receiver.
 */
int c2_rpc_session_destroy(struct c2_rpc_session *);

/**
    Wait until desired state is reached.
 */
void c2_rpc_session_timedwait(c2_rpc_session *session, enum c2_rpc_session_state state,
             const struct c2_time *abs_timeout);

/**
   checks internal consistency of session
 */
bool c2_rpc_session_invariant(const struct c2_rpc_session *session);

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
   Session slot
 */
struct c2_rpc_snd_slot {
	bool 			 ss_waiting_for_reply;
	/** If true, do not associate any item with this slot */
	bool			 ss_dont_use;
	struct c2_verno		 ss_verno;
	struct c2_verno		 ss_last_persistent_verno;
	uint64_t		 ss_generation;
	/** reference to the last sent item for which the
	reply is not received (In case need to resend) */ 
	struct c2_rpc_item 	*ss_sent_item;
	/** list of items for which we've received reply from receiver but
	their effects not persistent on receiver */
	struct c2_list		*ss_replay_queue;
};

/**
   Fill all the session related fields of c2_rpc_item.

   For unbound items, assign session and slot id.
   Fill verno field of item. And mark slot as 'waiting_for_reply'

   Assumption: c2_rpc_item has a field giving service_id of
		destination service.
 */
int c2_rpc_session_prepare_item_for_sending(struct c2_rpc_item *);

/**
   Inform session module that a reply item is received.
 */
void c2_rpc_session_reply_item_received(struct c2_rpc_item *);

/** 
   Receiver side SESSION_CREATE and SESSION_DESTROY handlers
 */
int c2_rpc_session_create_handler(struct c2_fom *);

/**
   Destroys all the information associated with the session on the receiver 
   including reply cache entries.
 */
int c2_rpc_session_destroy_handler(struct c2_fom *);

/**
    These are reply handlers
 */
int c2_rpc_session_create_rep_handler(struct c2_fom *);

int c2_rpc_session_destroy_rep_handler(struct c2_fom *);

/** @} end of session group */	

#endif
