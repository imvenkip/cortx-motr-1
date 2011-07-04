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

A fop is a rpc item and so is ADDB record. rpc is a container for one or more
rpc items.

A rpc connection identfies a sender to the receiver. It acts as a parent
object within which sessions are created. rpc connection has two
identifiers.
UUID: Uniquely Identifies the sender end of the rpc connection
globally within the cluster. UUID is generated by sender.
SenderID: A sender id is assigned by receiver. Sender Id uniquely identifies
a rpc connection uniquely on receiver side.

A session has multiple slots. Number of slots in the session decides the
degree of concurrency. Number of slots can vary dynamically.

With a slot a list of items, ordered by verno is associated. An item on
the list is in one of the following states:

* past committed: the reply for the item was received and the receiver confirmed
  that the item is persistent. Items can linger for some time in this state,
  because distributed transaction recovery requires keeping items in memory
  after they become persistent on the receiver;

* past volatile: the reply was received, but persistence confirmation wasn't;

* unreplied: the item was sent (i.e., placed into an rpc) and no reply is
  received. We are usually talking about a situations where there is at
  most a single item in this state, but it is not, strictly speaking, necessary.
  More than a single item can be in flight per-slot;

* future: the item wasn't sent.

an item can be linked into multiple slots (similar to c2_fol_obj_ref).For each
slot the item has a separate verno and separate linkage into the slot's item
list. Item state is common for all slots;

An item, has a MUTABO flag, which is set when the item is an update (i.e.,
changes the file system state). When the item is an update then (for each
slot the item is in) its verno is greater than the verno of the previous
item on the slot's item list. Multiple consecutive non-MUTABO items (i.e.,
read-only queries can have the same verno);

With each item a (pointer to) reply is associated. This pointer is set
once the reply is received for the item;

a slot has a number of pointers into this list and other fields, described
below:

<li><b>last_sent</b>
pointer usually points to the latest unreplied request. When the
receiver fails and restarts, the last_sent pointer is shifted back to the
item from which the recovery must continue.
Note that last_sent might be moved all the way back to the oldest item;
<li><b>last_persistent</b>
last_persistent item points to item whose effects have reached to persistent
storage.

sender_slot_invariant() should check that:
* items on the slot list are ordered by verno and state;
* MUTABO items have increasing verno-s;
* an item has reply attached exactly when it is in the unreplied state.

A slot state machine reacts to the following events:
[ITEM ADD]: a new item is added to the future list;
[REPLY RECEIVED]: a reply is received for an item;
[PERSISTENCE]: the receiver notified the sender about the change in the
last persistent item;
[RESET]: last_sent is reset back due to the receiver restart.

The state of a slot is described by the following variables:
item list: the list of items, starting from the oldest;
last_sent: the earliest item that the receiver possibly have seen;
in_flight: the number of items currently on the network.

Note that slot.in_flight not necessary equals the number of unreplied items:
during recovery items from the past parts of the list are replayed.

Also note, that the item list might be empty. To avoid special cases with
last_sent pointer, let's prepend a special dummy item with an impossibly
low verno to the list.

For each slot, a configuration parameter slot.max_in_flight is defined.
This parameter is set to 1 to obtain a "standard" slot behaviour, where no
more than single item is in flight.

<b> Reply cache: </b><BR>

Reply cache caches replies for update operations. Replies of read-only
operations is not cached. When a duplicate update item is received which is
already executed on receiver, then instead of again processing the item,
its reply from reply cache is retrieved and returned to the sender.

Session module implements a slot as a "special file". This allows to reuse
persistent data structure and code of cob infrastructure.

For each slot, session module will create a cob named
/sessions/$SENDER_ID/$SESSION_ID/$SLOT_ID:$GEN
Where $SENDER_ID, $SESSION_ID, $SLOT_ID and $GEN are place-holders for actual
identifier values.

Version number of slot is same as version number of cob that represents
the slot.

A FOL (File Operation Log) can be used to cache replies. Current verno of slot
points to record of most recent update operation that updated the slot verno.


<B>Maintaining item dependancies when slot.max_in_flight > 1 </B> <BR>

 First, let's clarify that xid is used to uniquely identify items sent through
 a particular slot, whereas verno is used to identify the state of target
 objects (including slots) when the operation is applicable.

 We want to achieve the following goals:

   * sequence of operations sent through a slot can be exactly reproduced
     after the receiver or the network failed. "Reproduced" here means that
     after the operations are re-applied the same observable system state
     results.

   * Network level concurrency (multiple items in flight).

   * Server level concurrency (multiple operations executed on the receiver
     concurrently, where possible).

   * Operations might have dependencies.

 Let's put the following data into each item:
 @code
 struct c2_rpc_item {
       struct c2_verno     verno;
       uint64_t            xid;
       struct c2_rpc_item *dep;
 };
 @endcode

 ->verno and ->xid are used as usual. ->dep identifies an item, sent to
  the same receiver, which this item "depends on". item can be executed only
  after item->dep has been executed. If an item does not depend on any other
  item, then item->dep == NULL.

 When an item is submitted to the rpc layer, its ->dep is

   * either set by the caller (the rpc checks that the receiver is the same
     in a pre-condition);

   * set to NULL to indicate that there are no dependencies;

   * set to a special LAST constant value, to indicate that the item depends
     on the last item sent through the same slot. In this case, the rpc code
     assigns ->dep to the appropriate item:

     @code
       if (item->flags & MUTABO)
               item->dep = latest item on the slot list;
       else
               item->dep = latest MUTABO item on the slot list;
     @endcode

     For a freestanding item, this assignment is done lately, when the
     freestanding item is assigned a slot and xid.

 item->dep->xid is sent to the receiver along with other item attributes. On
 the receiver, the item is submitted to the request handler only when

   * item->dep item has been received and executed and

   * item->verno matches the slot verno (as usual).

 Typical scenarios are as following:

   * update operations are submitted, all with LAST dep, they are sent
     over the network concurrently (up to slot->max_in_flight) and executed
     serially by the receiver;

   * an update item I0 is submitted, then a set of read-only items is submitted,
     all with ->dep set to LAST. Read-only operations are sent concurrently and
     executed concurrently after I0 has been executed.

 Note that LAST can be nicely generalized (we don't need this now): instead of
 distinguishing MUTABO and !MUTABO items, we split items into "commutative" groups
 such that items in a group can be executed in any order.

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
struct c2_rpcmachine;
struct c2_rpc_chan;
struct c2_net_end_point;

/* Exports */
struct c2_rpc_session;
struct c2_rpc_conn;

enum {
	/** session_[create|terminate] items go on session 0 */
	SESSION_0 = 0,
	SESSION_ID_INVALID = ~0,
	/** Range of valid session ids */
	SESSION_ID_MIN = SESSION_0 + 1,
	SESSION_ID_MAX = SESSION_ID_INVALID - 1,
	SENDER_ID_INVALID = ~0,
	SLOT_ID_INVALID = ~0,
};

/**
   Requirements:
   * UUID must change whenever a storage-less client re-boots.
   * for a client with persistent state (e.g., a disk) uuid
     must survive reboots.
 */
struct c2_rpc_sender_uuid {
	/** XXX Temporary */
	uint64_t	su_uuid;
};
/**
   Generate UUID
 */
void c2_rpc_sender_uuid_generate(struct c2_rpc_sender_uuid *u);

/**
   3WAY comparison function for UUID
 */
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
enum c2_rpc_conn_flags {
	RCF_SENDER_END = 1 << 0,
	RCF_RECV_END = 1 << 1
};

/**
   For every service to which sender wants to communicate there is one
   instance of c2_rpc_conn. All instances of c2_rpc_conn are
   maintained in a list inside rpcmachine. Instance of c2_rpc_conn stores
   a list of all sessions currently active with the service.
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
	/** Globally unique ID of rpc connection */
	struct c2_rpc_sender_uuid	 c_uuid;
	/** Every c2_rpc_conn is stored on a list
	    c2_rpcmachine::cr_rpc_conn_list
	    conn is in the list if c_state is not in {
	    CONN_INITIALISED, CONN_FAILED, CONN_TERMINATED} */
	struct c2_rpcmachine		*c_rpcmachine;
	struct c2_list_link              c_link;
	enum c2_rpc_conn_state		 c_state;
	/** @see c2_rpc_conn_flags for list of flags */
	uint64_t			 c_flags;
	/* A c2_rpc_chan structure that will point to the transfer
	   machine used by this c2_rpc_conn. */
	struct c2_rpc_chan		*c_rpcchan;
	/**
	    XXX Deprecated: c2_service_id
	    Id of the service with which this c2_rpc_conn is associated
	*/
	struct c2_service_id            *c_service_id;
	/** Destination end point */
	struct c2_net_end_point		*c_end_point;
	/** cob representing the connection */
	struct c2_cob			*c_cob;
	/** Sender ID unique on receiver */
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
	C2_RPC_SESSION_INITIALISED = (1 << 0),
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
	   When sender sends SESSION_TERMINATE fop to receiver and is waiting
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
	  V                           |
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
	struct c2_rpc_conn		*s_conn;
	struct c2_chan			 s_chan;
	/** lock protecting this session and slot table */
	struct c2_mutex			 s_mutex;
	/** Number of items that needs to sent or their reply is
	    not yet received */
	int32_t				 s_nr_active_items;
	/** list of items that can be sent through any available slot.
	    items are placed using c2_rpc_item::ri_unbound_link */
	struct c2_list			 s_unbound_items;
	/** Number of active slots in the table */
	uint32_t			 s_nr_slots;
	/** Capacity of slot table */
	uint32_t			 s_slot_table_capacity;
	/** Array of pointers to slots */
	struct c2_rpc_slot		 **s_slot_table;
	/** if s_state == C2_RPC_SESSION_FAILED then s_rc contains error code
		denoting cause of failure */
	int32_t				 s_rc;
};

/**
   Initialises all fields of session. Allocates and initialises
   nr_slots number of slots.
   No network communication is involved.

   @post ergo(rc == 0, session->s_state == C2_RPC_SESSION_INITIALISED &&
		       session->s_conn == conn &&
		       session->s_session_id == SESSION_ID_INVALID)
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
			      uint64_t			state_flags,
			      const c2_time_t		abs_timeout);

/**
   Finalize session object

   @pre session->s_state == C2_RPC_SESSION_TERMINATED ||
	session->s_state == C2_RPC_SESSION_FAILED ||
	session->s_state == C2_RPC_SESSION_INITIALISED
 */
void c2_rpc_session_fini(struct c2_rpc_session *session);

enum {
	SLOT_DEFAULT_MAX_IN_FLIGHT = 1
};

struct c2_rpc_slot_ops {
	void (*so_consume_item)(struct c2_rpc_item *i);
	void (*so_consume_reply)(struct c2_rpc_item	*req,
				 struct c2_rpc_item	*reply);
	void (*so_slot_idle)(struct c2_rpc_slot *slot);
};
/**
  In memory slot object.

  A slot provides the FIFO and exactly once properties for item delivery.
  c2_rpc_slot and its corresponding methods are equally valid on sender and
  receiver side.

  One can think of a slot as a pipe. On sender side, application/formation is
  placing items at one end of this pipe. The item appears on the other end 
  of pipe. And formation takes the item packs in some RPC and sends it.

  On receiver side, when an item is received it is placed in one end of the
  pipe. When the item appears on other end of pipe it is sent for execution.

  When an update item (one that modifies file-system state) is added to the
  slot, it advances version number of slot.
  The verno.vn_vc is set to 0 at the time of slot create on both ends.

  A slot responds to following events:
  ITEM_APPLY
  REPLY_RECEIVED
  PERSISTENCE
  RESET
 */
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
