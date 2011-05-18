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

Layers above rpc (e.g. c2t1fs, replicator) may or may not specify update stream
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

<b> Reply cache: </b>
------------
Receiver maintains reply cache. For each slot, "reply cache" caches reply item
of last executed item. Reply cache is persistent on receiver.

Session module implements a slot as a "special file". This allows to reuse
persistent data structure and code of cob infrastructure.

For each slot, session module will create a cob named
/sessions/$SENDER_ID/$SESSION_ID/$SLOT_ID:$GEN
Where $SENDER_ID, $SESSION_ID, $SLOT_ID and $GEN are place-holders for actual
identifier values.

"Contents" of this file will be "cached reply item". Every time a new reply
item is cached, it overwrites previous reply item and version number of
slot file is advanced. These modifications take place in the same transaction
in which the item is processed (e.g. the transaction in which fop is executed).

Version number of slot is same as version number of cob that represents
the slot.

<b> Ensuring FIFO and EOS: </b>
---------------------
A version number is a pair <lsn, version_count>. Each slot has associated
version number (slot.verno).
Before sending first item associated with a slot, following condition holds.
receiver.slot.verno.vc == sender.slot.verno.vc == 0

Each rpc item associated with the slot also contains a version
number (item.verno). Before sending an item, slot.verno is copied into
item.verno

When an item is received, following conditions are possible:

    - New item:
	if c2_verno_is_redoable(slot.verno, item.verno) == 0 and
			NOT slot.busy
	then
		accept item as new item
		slot.busy = true
	end if
    - Retransmitted item:
	if c2_verno_is_undoable(slot.verno, item.verno) == 0
	then
		item is retransmitted.
		obtain reply from reply cache and send to sender
	end if
    - Misordered retry:
	if c2_verno_is_redoable(slot.verno, item.verno) == -EALREADY
	then
		it is a misordered item.
		send error RPC_ITEM_MISORDERED
	end if
    - Misordered new item:
	if c2_verno_is_redoable(slot.verno, item.verno) == -EAGAIN
	then
		it is misordered new item.
		send error RPC_ITEM_MISORDERED
	end if

	@note Future Optimization : - We can put misordered new
	rpc item request in some (memory-only) queue with the timeout
	hoping that the preceding rpc item will arrive soon.


    @note TODO : -
	- Design protocol to generate sender id.(similar to EXCHANGE_ID
		protocol in NFSv4.1)
	- Design protocol to dynamically adjust the no. of slots.
	- Update Streams.

@defgroup session RPC SESSIONS

@{

*/
#include "lib/list.h"
#include "lib/chan.h"
#include "lib/mutex.h"
#include "dtm/verno.h"

/* Imports */
struct c2_rpc_item;
struct c2_service_id;

/* Exports */
struct c2_rpc_session;
struct c2_rpc_conn;

/* Internal: required for declaration of c2_rpc_session */
struct c2_rpc_snd_slot;
struct c2_rpc_session_ops;

enum {
	SESSION_0 = 0,
	SESSION_ID_INVALID = ~0,
	SESSION_ID_NOSESSION = ~0 - 1,
	SENDER_ID_INVALID = 0,
	/* CF_.* values for c2_rpc_conn->c_flags */
	CF_WAITING_FOR_CONN_CREATE_REPLY = 1,
	CF_WAITING_FOR_CONN_TERM_REPLY = (1 << 1)
};

enum c2_rpc_conn_state {
        /**
           A newly allocated c2_rpc_conn object is in
           UNINITIALIZED state.
         */
        CS_CONN_UNINITIALIZED = 0,
        /**
           When sender is waiting for receiver reply to get its sender ID it is
           in INITIALIZIG state.
         */
        CS_CONN_INITIALIZING = (1 << 0),
        /**
	   When initialization is successfull connection enters in ACTIVE state.
	   It stays in this state for until termination.
         */
        CS_CONN_ACTIVE = (1 << 1),
        /**
           If c2_rpc_conn is in INITIALIZING state and sender doesn't receive
           sender-id from receiver within a specific time OR if sender receives 
	   reply stating "error occured during conn create"
	   then c2_rpc_conn moves to INIT_FAILED state
        */
        CS_CONN_INIT_FAILED = (1 << 2),

	/**
	   When sender calls c2_rpc_conn_terminate() on c2_rpc_conn object
	   a FOP is sent to the receiver side to terminate the rpc connection.
	   Until reply is received, c2_rpc_conn object stays in TERMINATING
	   state
	 */
	CS_CONN_TERMINATING = (1 << 3),

	/**
	   When sender receives reply for conn_terminate FOP and reply FOP
	   specifies the conn_terminate operation is successful then
	   the object of c2_rpc_conn enters in TERMINATED state
	 */
	CS_CONN_TERMINATED = (1 << 4),
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
   When receiver receives first item on session 0 for any sender_id,
   it creates an entry of slot of session 0 in its slot table.
   Session 0 is required to send other SESSION_CREATE/SESSION_TERMINATE requests
   to the receiver. As SESSION_CREATE and SESSION_TERMINATE operations are
   non-idempotent, they also need EOS and FIFO guarantees.

XXX incorporate INIT_FAILED state in this diagram

   +-------------------------> UNINITIALIZED
                                    |
                                    |  c2_rpc_conn_init()
   +----------------------------+   |
   |                            |   |
   |                            V   V
   |     +-------------------- INITIALIZING
 R |     | time-out || rc != 0      |
 E |     |                          | init_successful
 T |     |                          |
 R |     V                          |
 Y +--- INIT_FAILED                 | 
         |                          V            
         |           +---------> ACTIVE
         |           |              |
         |           |failed        | conn_terminate()
         |           |              |          
         |           |              V
         |           +----------TERMINATING
	 |                          |
         |                          |  conn_terminate_reply_received() && rc == 0
	 |                          V
	 |			TERMINATED
	 |                          |
	 |			    |  fini()
	 |	fini()		    V  
	 +--------------------> UNINITIALIZED

 */
struct c2_rpc_conn {
        /** Every c2_rpc_conn is stored on a global list */
        struct c2_list_link              c_link;
        enum c2_rpc_conn_state		 c_state;
	uint64_t			 c_flags;
	struct c2_rpcmachine		*c_rpcmachine;
        /**
	    XXX Deprecated: c2_service_id 
	    Id of the service with which this c2_rpc_conn is associated
	*/
        struct c2_service_id            *c_service_id;
        /** Sender ID (aka client ID) */
        uint64_t                         c_sender_id;
        /** List of all the sessions for this <sender,receiver> */
        struct c2_list                   c_sessions;
        /** Counts number of sessions (excluding session 0) */
        uint64_t                         c_nr_sessions;
        struct c2_chan                   c_chan;
        struct c2_mutex                  c_mutex;
	/** stores conn_create fop pointer during initialization */
	void				*c_private;
};

/**
    Send handshake fop to the remote end. The reply contains sender-id.

    This function asynchronously sends an initial hand-shake fop to the other
    end of the connection. When reply is received, the c2_rpc_conn is
    moved into INITIALIZED state.

    @note c2_net_conn argument will be removed in future.

    @pre c2_rpc_conn->c_state == CONN_UNINITIALIZED
    @post c2_rpc_conn->c_state == CONN_INITIALIZING
 */
int c2_rpc_conn_init(struct c2_rpc_conn		*rpc_conn,
		     struct c2_service_id	*svc_id);

/**
   Destroy c2_rpc_conn object.
   No network communication involved.
   @pre c2_rpc_conn->c_state == CONN_INIT_FAILED ||
	c2_rpc_conn->c_state == CONN_TERMINATED
   @post c2_rpc_conn->c_state == CONN_UNINITIALIZED
 */
int  c2_rpc_conn_fini(struct c2_rpc_conn *);

/**
   Send "conn_terminate" FOP to receiver.
   @pre conn->c_state == CONN_ACTIVE && conn->c_nr_sessions == 0
   @post conn->c_state == TERMINATING
 */
int c2_rpc_conn_terminate(struct c2_rpc_conn *conn);

/**
    Wait until c2_rpc_conn state machine reached the desired state.
 */
void c2_rpc_conn_timedwait(struct c2_rpc_conn *, uint64_t,
                        const struct c2_time *);

/**
   checks internal consistency of c2_rpc_conn
 */
bool c2_rpc_conn_invariant(const struct c2_rpc_conn *session);

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
	   Creation of session failed
	 */
	SESSION_CREATE_FAILED = (1 << 2),
	/**
	   When recovery is in progress (i.e. replay or resend) the
	   session is in RECOVERING state. New requests coming on this
	   session are held in a queue until recovery is complete.
	 */
	SESSION_RECOVERING = (1 << 3),
	/**
	   If sender does not get reply for SESSION_CREATE within specific
	   time then session goes in TIMED_OUT state.
	 */
	SESSION_TIMED_OUT = (1 << 4),
	/**
	   When sender sends SESSION_DESTROY fop to receiver and is waiting
	   for reply, then it is in state TERMINATING.
	*/
	SESSION_TERMINATING = (1 << 5),
	/**
	   When sender gets reply to session_terminate fop and reply informs
	   the session termination is successful then the session enters in
	   TERMINATED state
	 */
	SESSION_TERMINATED = (1 << 6)
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
	CREATE_FAILED                 |
	  |      		      V		Recovery complete
	  |			    ALIVE <-----------------------------------+
	  |			      |	|				      |
	  |			      | | receiver or nw failure              |
	  |			      | +-------------------> RECOVERING -----+
	  |			      |
	  | fini 		      | session_terminate
	  |			      V
	  |		         TERMINATING <-------------+
	  |			      |  |		   |
	  |			      |  |                 | timed-out/retry
	  |			      |  +-----------------+
	  |		              |session_terminated
	  |		              V
	  |		         TERMINATED
	  |		              |
	  |			      | fini()
	  |			      V
	  +-----------------------> UNINITIALIZED 

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
	/** Array of pointers to slots */
	struct c2_rpc_snd_slot 		**s_slot_table;
	/** Session ops */
	const struct c2_rpc_session_ops	 *s_ops;
};

/**
   Session operation vector
 */
struct c2_rpc_session_ops {
	/**
	   Called after each state change of @session object.
	   Previous state of @session is given by @prev_state
	   New state of @session can be retrieved from @session->s_state
	 */
	void (*session_state_changed)(struct c2_rpc_session *session,
			enum c2_rpc_session_state prev_state);
};

/**
    Sends a SESSION_CREATE fop across pre-defined 0-session in the c2_rpc_conn.

    @pre c2_rpc_conn->c_state == CONN_ACTIVE
    @pre session->s_state == SESSION_UNINITIALIZED
    @post c2_rpc_conn->c_state == CONN_ACTIVE
    @post session->s_state == SESSION_CREATING
 */
int c2_rpc_session_create(struct c2_rpc_session	*session,
			  struct c2_rpc_conn	*conn);

/**
   Send terminate session message to receiver.

   @pre c2_rpc_session->s_state == SESSION_ALIVE && no slot is busy
   @post c2_rpc_session->s_state == SESSION_TERMINATING
 */
int c2_rpc_session_terminate(struct c2_rpc_session *);

/**
    Wait until desired state is reached.
 */
void c2_rpc_session_timedwait(struct c2_rpc_session *session,
		uint64_t, const struct c2_time *abs_timeout);

/**
   checks internal consistency of session
 */
bool c2_rpc_session_invariant(const struct c2_rpc_session *session);

/**
   flag bits in c2_rpc_snd_slot::ss_flags
 */
enum {
	/** An item associated with this slot has been sent.
	    Do not assign this slot until reply is received on this slot */
	SLOT_WAITING_FOR_REPLY = 1,
	/** A slot cannot be assigned to an item if SLOT_IN_USE flag
	    is NOT set */
	SLOT_IN_USE = (1 << 1)
};

/**
   Session slot

   @note
   * Pointer to a c2_rpc_snd_slot should never go out of session module.
     Rest of the rpc layer should always access it by slot-id because
     size of a slot table can change and hence it may need to relocated
     to some other location thus invalidating the pointer.
   * ss_slot_cob_id can be specified in c2_rpc_item instead of
	<session_id, slot_id>. This will avoid lookup of
	/sessions/$SENDER_ID/$SESSION_ID/$SLOT_ID:$GEN for every
	item received.
   * When sender side session state will be integrated with FOL,
	we will require stob_id of "slot file"
*/
struct c2_rpc_snd_slot {
	uint64_t		 ss_flags;
	struct c2_verno		 ss_verno;
	/** effects upto last_persistent_verno have reached persistent
		store */
	struct c2_verno		 ss_last_persistent_verno;
	uint64_t		 ss_generation;
	/** List of items queued for this slot. These are the items to which
	slot is assigned but verno is not filled */
	struct c2_list		 ss_ready_list;
	/** reference to the last sent item for which the
	reply is not received (In case need to resend) */
	struct c2_rpc_item 	*ss_sent_item;
	/** list of items for which we've received reply from receiver but
	their effects not persistent on receiver */
	struct c2_list		 ss_replay_list;
};

/** @} end of session group */	

#endif
