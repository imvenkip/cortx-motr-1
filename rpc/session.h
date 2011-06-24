/* -*- C -*- */
/*
 * COPYRIGHT 2011 XYRATEX TECHNOLOGY LIMITED
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF XYRATEX TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF XYRATEX TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF XYRATEX LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF XYRATEX'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A XYRATEX REPRESENTATIVE
 * http://www.xyratex.com/contact
 *
 * Original author: Alexey Lyashkov <Alexey_Lyashkov@xyratex.com>
 *		    Rohan Puri <Rohan_Puri@xyratex.com>
 *                  Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 04/09/2010
 */

#ifndef __COLIBRI_RPC_SESSION_H__

#define __COLIBRI_RPC_SESSION_H__

/**

@defgroup rpc_session RPC Sessions

@{

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

<b> Reply cache: </b><BR>

Receiver maintains reply cache. For each slot, "reply cache" caches reply item
of last executed item. Reply cache is persistent on receiver.

Session module implements a slot as a "special file". This allows to reuse
persistent data structure and code of cob infrastructure.

For each slot, session module will create a cob named
/sessions/$SENDER_ID/$SESSION_ID/$SLOT_ID:$GEN
Where $SENDER_ID, $SESSION_ID, $SLOT_ID and $GEN are place-holders for actual
identifier values.

Version number of slot is same as version number of cob that represents
the slot.

The cached item is stored in the fol to which the slot-cob refers through its
version number

<b> Ensuring FIFO and EOS: </b>
<BR>
A version number is a pair <lsn, version_count>. Each slot has associated
version number (slot.verno).
Before sending first item associated with a slot, following condition holds.
receiver.slot.verno.vc == sender.slot.verno.vc == 0

Each rpc item associated with the slot also contains a version
number (item.verno). Before sending an item, slot.verno is copied into
item.verno

When an item is received, following conditions are possible:
    @code
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
    @endcode

    @todo
	- Currently sender_id and session_id are chosen to be random().
	- How to get unique stob_id for session and slot cobs?
	- Default slot count is currently set to 4. Nothing special about 4.
	  Needs a proper value.
	- session recovery needs to be implemented.
	- slot table resize needs to be implemented.
	- Design protocol to dynamically adjust number of slots.
	- Integrate with ADDB
 */

#include "lib/list.h"
#include "lib/chan.h"
#include "lib/mutex.h"
#include "dtm/verno.h"

/* Imports */
struct c2_rpc_item;

/* Exports */
struct c2_rpc_session;
struct c2_rpc_conn;

enum {
	/** session_[create|terminate] items go on session 0 */
	SESSION_0 = 0,
	SESSION_ID_INVALID = ~0,
	/** conn_[create_terminate] fops are sent out of session.
	    Such items have session id as SESSION_ID_NOSESSION */
	SESSION_ID_NOSESSION = SESSION_ID_INVALID - 1,
	/** Range of valid session ids */
	SESSION_ID_MIN = SESSION_0 + 1,
	SESSION_ID_MAX = SESSION_ID_NOSESSION - 1,
	SENDER_ID_INVALID = ~0,
	SLOT_ID_INVALID = ~0,
};

struct c2_rpc_sender_uuid {
	uint64_t	su_uuid;
};
void c2_rpc_sender_uuid_generate(struct c2_rpc_sender_uuid *u);
int c2_rpc_sender_uuid_cmp(struct c2_rpc_sender_uuid *u1,
			   struct c2_rpc_sender_uuid *u2);

enum c2_rpc_conn_state {
	/**
	  All the fields of conn are initialised locally. But the connection
	  is not yet established.
	 */
	C2_RPC_CONN_INITIALISED = (1 << 0),

	/**
	   When sender is waiting for receiver reply to get its sender ID it is
	   in CREATING state.
	 */
	C2_RPC_CONN_CREATING = (1 << 1),

	/**
	   When initialization is successfull connection enters in ACTIVE state.
	   It stays in this state for until termination.
	 */
	C2_RPC_CONN_ACTIVE = (1 << 2),

	/**
	   If conn init or terminate fails or time-outs connection enters in
	   FAILED state. c2_rpc_conn::c_rc gives reason for failure.
	*/
	C2_RPC_CONN_FAILED = (1 << 3),

	/**
	   When sender calls c2_rpc_conn_terminate() on c2_rpc_conn object
	   a FOP is sent to the receiver side to terminate the rpc connection.
	   Until reply is received, c2_rpc_conn object stays in TERMINATING
	   state
	 */
	C2_RPC_CONN_TERMINATING = (1 << 4),

	/**
	   When sender receives reply for conn_terminate FOP and reply FOP
	   specifies the conn_terminate operation is successful then
	   the object of c2_rpc_conn enters in TERMINATED state
	 */
	C2_RPC_CONN_TERMINATED = (1 << 5),
};

/**
   RPC Connection flags
 */
enum {
	RCF_SENDER_END = 1,
	RCF_RECV_END = 1 << 1
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
   Receiver creates session 0 while creating the rpc connection itself.
   Session 0 is required to send other SESSION_CREATE/SESSION_TERMINATE requests
   to the receiver. As SESSION_CREATE and SESSION_TERMINATE operations are
   non-idempotent, they also need EOS and FIFO guarantees.

   <PRE>

   +-------------------------> unknown state
         allocated                  |
                                    |  c2_rpc_conn_init()
                                    V
                               INITIALISED
                                    |
                                    |  c2_rpc_conn_create()
                                    |
                                    V
         +---------------------- CREATING
         | time-out ||              |
         |     reply.rc != 0        | conn_create_reply_received() &&
         |                          |    reply.rc == 0
         V                          |
       FAILED                       |
         |  ^                       V
         |  |                    ACTIVE
         |  |                       |
         |  |                       | conn_terminate()
         |  | failed || timeout     |
         |  |                       V
         |  +-------------------TERMINATING
	 |                          |
         |                          | conn_terminate_reply_received() && rc== 0
	 |                          V
	 |			TERMINATED
	 |                          |
	 |			    |  fini()
	 |	fini()		    V
	 +--------------------> unknown state

</PRE>
  Concurrency:
  * c2_rpc_conn::c_mutex protects all but c_link fields of c2_rpc_conn.
  * Locking order: rpcmachine => c2_rpc_conn => c2_rpc_session
 */
struct c2_rpc_conn {
	struct c2_rpc_sender_uuid	 c_uuid;
	/** Every c2_rpc_conn is stored on a list
	    c2_rpcmachine::cr_rpc_conn_list
	    conn is in the list if c_state is not in {
	    CONN_INITIALISED, CONN_FAILED, CONN_TERMINATED} */
	struct c2_list_link              c_link;
	enum c2_rpc_conn_state		 c_state;
	uint64_t			 c_flags;
	struct c2_rpcmachine		*c_rpcmachine;
	/**
	    XXX Deprecated: c2_service_id
	    Id of the service with which this c2_rpc_conn is associated
	*/
	struct c2_service_id            *c_service_id;
	struct c2_net_end_point		*c_end_point;
	struct c2_cob			*c_cob;
	/** Sender ID (aka client ID) */
	uint64_t                         c_sender_id;
	/** List of all the sessions for this <sender,receiver> */
	struct c2_list                   c_sessions;
	/** Counts number of sessions (excluding session 0) */
	uint64_t                         c_nr_sessions;
	struct c2_chan                   c_chan;
	struct c2_mutex                  c_mutex;
	/** if c_state == C2_RPC_CONN_FAILED then c_rc contains error code */
	int32_t				 c_rc;
};

/**
   Initialise @conn object and associate it with @machine.
   No network communication is involved.

   Note: c2_rpc_conn_init() can fail with -ENOMEM, -EINVAL.
	 if c2_rpc_conn_init() fails, conn is left in undefined state.

   @post ergo(rc == 0, conn->c_state == C2_RPC_CONN_INITIALISED &&
			conn->c_machine == machine &&
			conn->c_sender_id == SENDER_ID_INVALID &&
			(conn->c_flags & RCF_SENDER_END) != 0)
 */
int c2_rpc_conn_init(struct c2_rpc_conn		*conn,
		     struct c2_rpcmachine	*machine);

/**
    Send handshake conn create fop to the remote end. The reply
    contains sender-id.

    @pre conn->c_state == C2_RPC_CONN_INITIALISED
    @post ergo(result == 0, conn->c_state == C2_RPC_CONN_CREATING &&
		c2_list_contains(&machine->cr_rpc_conn_list, &conn->c_link))
    @post ergo(result != 0, conn->c_state == C2_RPC_CONN_INITIALISED)
 */
int c2_rpc_conn_create(struct c2_rpc_conn	*conn,
		       struct c2_net_end_point	*ep);

/**
   Send "conn_terminate" FOP to receiver.
   @pre conn->c_state == C2_RPC_CONN_ACTIVE && conn->c_nr_sessions == 0
   @post ergo(result == 0, conn->c_state == C2_RPC_CONN_TERMINATING)
   @post if result != 0 then conn->c_state is left unchanged
 */
int c2_rpc_conn_terminate(struct c2_rpc_conn *conn);

/**
   Finalize c2_rpc_conn
   No network communication involved.
   @pre conn->c_state == C2_RPC_CONN_FAILED ||
	conn->c_state == C2_RPC_CONN_INITIALISED ||
	conn->c_state == C2_RPC_CONN_TERMINATED
 */
void c2_rpc_conn_fini(struct c2_rpc_conn *conn);

/**
    Wait until c2_rpc_conn state machine reached the desired state.

    @param state_flags can specify multiple states by ORing
    @param abs_timeout absolute time since Epoch (00:00:00, 1 January 1970)
    @return true if @conn reaches in one of the state(s) specified by
                @state_flags
    @return false if time out has occured before @conn reaches in desired
                state.
 */
bool c2_rpc_conn_timedwait(struct c2_rpc_conn	*conn,
			   uint64_t		state_flags,
			   const c2_time_t	abs_timeout);

/**
   checks internal consistency of c2_rpc_conn
 */
bool c2_rpc_conn_invariant(const struct c2_rpc_conn *conn);

/**
   Possible states of a session object
 */
enum c2_rpc_session_state {
	/**
	   all lists, mutex and channels of session are initialised.
	   No actual session is established with any end point
	 */
	C2_RPC_SESSION_INITIALISED = 1,
	/**
	   When sender sends a SESSION_CREATE FOP to reciever it
	   is in CREATING state
	 */
	C2_RPC_SESSION_CREATING = (1 << 1),
	/**
	   A session is IDLE if both of following is true
		- for each slot S in session
			for each item I in S->item_list
				// I has got reply
				I->state is in {PAST_COMMITTED, PAST_VOLATILE}
		- session->unbound_items list is empty
	   A session can be terminated only if it is IDLE.
	 */
	C2_RPC_SESSION_IDLE = (1 << 2),
	/**
	   A session is busy if any of following is true
		- Any of slots has item to be sent (FUTURE items)
		- Any of slots has item for which reply is not received
			(IN_PROGRESS items)
		- unbound_items list is not empty
	 */
	C2_RPC_SESSION_BUSY = (1 << 3),
	/**
	   Creation/termination of session failed
	 */
	C2_RPC_SESSION_FAILED = (1 << 4),
	/**
	   When sender sends SESSION_DESTROY fop to receiver and is waiting
	   for reply, then it is in state TERMINATING.
	*/
	C2_RPC_SESSION_TERMINATING = (1 << 5),
	/**
	   When sender gets reply to session_terminate fop and reply informs
	   the session termination is successful then the session enters in
	   TERMINATED state
	 */
	C2_RPC_SESSION_TERMINATED = (1 << 6)
};

/**
   Session object at the sender side.
   It is opaque for the client like c2t1fs.
<PRE>

            +------------------> some unknown state
                 allocated            |
				      |  c2_rpc_session_init()
				      V
				  INITIALISED
				      |
				      | c2_rpc_session_create()
				      |
		timed-out	      V
          +-------------------------CREATING
	  |   create_failed           | create successful/n = 0
	  V       		      |
	FAILED <------+               |   n == 0 && list_empty(unbound_items)
	  |           |               +-----------------+
	  |           |               |                 | +-----+
	  |           |failed         |	                | |     | item add/n++
	  |           |               V  item add/n++   | V     | reply rcvd/n--
	  |           |             IDLE--------------->BUSY----+
	  |           |               |
	  | fini      |               | session_terminate
	  |	      |               V
	  |           +----------TERMINATING
	  |			      |
	  |			      |
	  |			      |
	  |		              |session_terminated
	  |		              V
	  |		         TERMINATED
	  |		              |
	  |			      | fini()
	  |			      V
	  +----------------------->unknown state

</PRE>
 */
struct c2_rpc_session {
	/** linkage into list of all sessions within a c2_rpc_conn */
	struct c2_list_link		 s_link;
	enum c2_rpc_session_state	 s_state;
	/** identifies a particular session. It is not globally unique */
	uint64_t			 s_session_id;
	struct c2_cob			*s_cob;
	/** rpc connection on which this session is created */
	struct c2_rpc_conn	 	*s_conn;
	struct c2_chan			 s_chan;
	/** lock protecting this session and slot table */
	struct c2_mutex 		 s_mutex;
	/** Number of items that needs to sent or their reply is
	    not yet received */
	int32_t				 s_nr_active_items;
	/** list of items that can be sent through any available slot.
	    items are placed using c2_rpc_item::ri_unbound_link */
	struct c2_list			 s_unbound_items;
	/** Number of active slots in the table */
	uint32_t 			 s_nr_slots;
	/** Capacity of slot table */
	uint32_t			 s_slot_table_capacity;
	/** highest slot id for which the sender has the outstanding request
	    XXX currently unused */
	uint32_t 			 s_highest_used_slot_id;
	/** if s_state == C2_RPC_SESSION_FAILED then s_rc contains error code
		denoting cause of failure */
	int32_t				 s_rc;
	/** Array of pointers to slots */
	struct c2_rpc_slot 		**s_slot_table;
};

/**
   Initialises all fields of session. Allocates and initialises
   nr_slots number of slots.
   No network communication is involved.

   @post ergo(rc == 0, session->s_state == C2_RPC_SESSION_INITIALISED &&
	session->s_conn == conn && session->s_session_id == SESSION_ID_INVALID)
 */
int c2_rpc_session_init(struct c2_rpc_session *session,
			struct c2_rpc_conn    *conn,
			uint32_t	      nr_slots);

/**
    Sends a SESSION_CREATE fop across pre-defined 0-session in the c2_rpc_conn.

    @pre session->s_state == C2_RPC_SESSION_INITIALISED
    @pre session->s_conn->c_state == C2_RPC_CONN_ACTIVE
    @post ergo(result == 0, session->s_state == C2_RPC_SESSION_CREATING &&
			c2_list_contains(conn->c_sessions, &session->s_link))
 */
int c2_rpc_session_create(struct c2_rpc_session	*session);

/**
   Send terminate session message to receiver.

   @pre session->s_state == C2_RPC_SESSION_IDLE
   @post ergo(result == 0, session->s_state == C2_RPC_SESSION_TERMINATING)
 */
int c2_rpc_session_terminate(struct c2_rpc_session *session);

/**
    Wait until desired state is reached.

    @param state_flags can specify multiple states by ORing
    @param abs_timeout absolute time since Epoch (00:00:00, 1 January 1970)
    @return true if session reaches in one of the state(s) specified by
		@state_flags
    @return false if time out has occured before session reaches in desired
		state.
 */
bool c2_rpc_session_timedwait(struct c2_rpc_session	*session,
			      uint64_t 			state_flags,
			      const c2_time_t		abs_timeout);

/**
   Finalize session object

   @pre session->s_state == C2_RPC_SESSION_TERMINATED ||
	session->s_state == C2_RPC_SESSION_FAILED ||
	session->s_state == C2_RPC_SESSION_INITIALISED
 */
void c2_rpc_session_fini(struct c2_rpc_session *session);

/**
   checks internal consistency of session
 */
bool c2_rpc_session_invariant(const struct c2_rpc_session *session);

enum {
	SLOT_DEFAULT_MAX_IN_FLIGHT = 1
};

struct c2_rpc_slot_ops {
	void (*so_consume_item)(struct c2_rpc_item *i);
	void (*so_consume_reply)(struct c2_rpc_item	*req,
				 struct c2_rpc_item	*reply);
	void (*so_slot_idle)(struct c2_rpc_slot *slot);
};
struct c2_rpc_slot {
	/** Session to which this slot belongs */
	struct c2_rpc_session		*sl_session;
	/** identifier of slot, unique within the session */
	uint32_t			sl_slot_id;
	struct c2_cob			*sl_cob;
	/** list anchor to put in c2_rpcmachine::ready_slots */
	struct c2_list_link		sl_link;
	/** Current version number of slot */
	struct c2_verno			sl_verno;
	/** slot generation */
	uint64_t			sl_slot_gen;
	/** a monotonically increasing counter, copied in each item
	    sent through this slot */
	uint64_t			sl_xid;
	/** List of items, starting from oldest */
	struct c2_list			sl_item_list;
	/** earliest item that the receiver possibly have seen */
	struct c2_rpc_item		*sl_last_sent;
	/** item that is most recently persistent on receiver */
	struct c2_rpc_item		*sl_last_persistent;
	/** Number of items in flight */
	uint32_t			sl_in_flight;
	/** Maximum number of items that can be in flight on this slot.
	    @see SLOT_DEFAULT_MAX_IN_FLIGHT */
	uint32_t			sl_max_in_flight;
	/** List of items ready to put in rpc */
	struct c2_list			sl_ready_list;
	struct c2_mutex			sl_mutex;
	const struct c2_rpc_slot_ops	*sl_ops;
};

int c2_rpc_slot_init(struct c2_rpc_slot			*slot,
		     const struct c2_rpc_slot_ops	*ops);

void c2_rpc_slot_item_add(struct c2_rpc_slot	*slot,
			  struct c2_rpc_item	*item);

int c2_rpc_slot_item_apply(struct c2_rpc_slot	*slot,
			  struct c2_rpc_item	*item);

void c2_rpc_slot_reply_received(struct c2_rpc_slot	*slot,
				struct c2_rpc_item	*reply,
				struct c2_rpc_item	**req_out);

void c2_rpc_slot_persistence(struct c2_rpc_slot	*slot,
			     struct c2_verno	last_persistent);

void c2_rpc_slot_reset(struct c2_rpc_slot	*slot,
		       struct c2_verno		last_seen);

bool c2_rpc_slot_invariant(struct c2_rpc_slot	*slot);

void c2_rpc_slot_fini(struct c2_rpc_slot	*slot);

/** XXX temporary */
void c2_rpc_form_slot_idle(struct c2_rpc_slot	*slot);

/**
   Iterate over all the rpc connections present in rpcmachine
 */
#define c2_rpc_for_each_outgoing_conn(machine, conn)	\
	c2_list_for_each_entry(&(machine)->cr_outgoing_conns, (conn), \
		struct c2_rpc_conn, c_link)

/**
   Iterate over all the sessions in rpc connection
 */
#define c2_rpc_for_each_session(conn, session)	\
	c2_list_for_each_entry(&(conn)->c_sessions, (session),	\
		struct c2_rpc_session, s_link)

/** @} end of session group */	

#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */

