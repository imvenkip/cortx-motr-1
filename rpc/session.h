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
 *                  Rohan Puri <Rohan_Puri@xyratex.com>
 *                  Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 04/09/2010
 */

#ifndef __COLIBRI_RPC_SESSION_H__
#define __COLIBRI_RPC_SESSION_H__

/**

@defgroup rpc_session RPC Sessions

@{

@section Overview

Session module of rpc layer has two objectives:

- If some rpc-items need to be delivered to receiver in same sequence in
  which sender has submitted them, then it is the session module which
  ensures that the items are delivered in FIFO sequence.

- To provide exactly once semantics.
  see http://www.cs.unc.edu/~dewan/242/s06/notes/ipc/node30.html

Aproach taken by session module to achive these two objectives, is similar
to session-slot implementation in NFSv4.1

See section 2.10.6 of rfc 5661 NFSv4.1
http://tools.ietf.org/html/rfc5661#section-2.10.6

Session module defines following types of objects:
- rpc connection @see c2_rpc_conn
- rpc session @see c2_rpc_session
- slot @see c2_rpc_slot
- slot ref @see c2_rpc_slot_ref.

Out of these, c2_rpc_conn and c2_rpc_session are visible to user.
c2_rpc_slot and c2_rpc_slot_ref are internal to rpc layer and not visible to
users outside rpc layer.

Session module uses following types of objects defined by rpc-core:
- rpc machine @see c2_rpcmachine
- rpc item @see c2_rpc_item.

<B> Relationships among objects: </B>
rpcmachine has two lists of rpc connections.
- Outgoing connections: Contains c2_rpc_conn objects for which this node is
sender.
- Incoming connections: Contains c2_rpc_conn objects for which this node is
receiver.

Rpc connection has a list of rpc sessions, which are created on this
connection. A rpc connection cannot be terminated until all the sessions
created on the connection are terminated.

A session contains one or more slots. Number of slots in the session can
vary over the lifetime of session (In current implementation state, the number
of slots do not vary. The slot count is fixed for entire lifetime of session.
Slot count is specified at the time of session initialisation).

Each object of type [c2_rpc_conn|c2_rpc_session|c2_rpc_slot] on sender
has counterpart object of same type on receiver. (Note: same structure
definitions are used on both sender and receiver side)

<B> Bound and Unbound rpc items: </B>

Rpc layer can send an rpc item on network only if it is associated with some
slot within the session. User of rpc layer can indirectly specify the slot (via
"update stream") on which the item needs to be sent. Or the user can just
specify session and leave the task of choosing any available slot to
rpc layer.

With respect to session and slots, an rpc item is said to be "bound" if
the item is associated with slot. An item is called as "unbound"/"freestanding"
if it is not associated with any particular slot yet. An unbound item will be
eventually associated with slot, and hence become bound item.

<b> Reply cache: </b>

Reply cache caches replies of update operations. Reply of read-only
operation is not cached. When a duplicate update item is received which is
already executed on receiver, then instead of again processing the item,
its reply from reply cache is retrieved and returned to the sender.
By preventing multiple executions of same item (or FOP), reply cache provides
"exactly once" semantics. If reply cache is persistent, then EOS can be
guaranteed even in the face of receiver restart. Colibri implements Reply
Cache via FOL (File Operation Log). See section "Slot as a cob" for more
details on this.

<B> Slot as a "cob": </B>
Session module implements a slot as a "special file". This allows to reuse
persistent data structure and code of cob infrastructure.

For each slot, session module will create a cob named $SLOT_ID:$GEN inside
directory /sessions/$SENDER_ID/$SESSION_ID/.
Where $SENDER_ID, $SESSION_ID, $SLOT_ID and $GEN are place-holders for actual
identifier values.

Version number of slot is same as version number of cob that represents
the slot. Each item also contains a version number. If an item is received,
whose version number matches with version number of slot, then the item is
accepted as valid item in the sequence and sent for execution. If item is an
"update operation" (i.e. it modifies file-system state) then the version
number of slot is advanced.

FOL (File Operation Log) contains record for each update operation executed.
The FOL record contains all the information required to undo or redo that
specific operation. If along with the operation details, reply of operation
is also stored in the FOL record, the FOL itself acts as a "Reply Cache".
Given fid of cob that represents the slot and version number of item, it is
possible to determine whether the item is duplicate or not. If it is duplicate
item then version number within the item will be less than version number of
slot. And there will be a record already present in the FOL representing the
same operation. Then the reply can be extracted from the FOL record and resent
back to sender.

<B>Ensuring FIFO when slot.max_in_flight > 1 </B>

 First, let's clarify that xid is used to uniquely identify items sent through
 a particular slot, whereas verno is used to identify the state of target
 objects (including slots) when the operation is applicable.

 We want to achieve the following goals:

   - sequence of operations sent through a slot can be exactly reproduced
     after the receiver or the network failed. "Reproduced" here means that
     after the operations are re-applied the same observable system state
     results.

   - Network level concurrency (multiple items in flight).

   - Server level concurrency (multiple operations executed on the receiver
     concurrently, where possible).

   - Operations might have dependencies.

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

   - either set by the caller (the rpc checks that the receiver is the same
     in a pre-condition);

   - set to NULL to indicate that there are no dependencies;

   - set to a special LAST constant value, to indicate that the item depends
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

   - item->dep item has been received and executed and

   - item->verno matches the slot verno (as usual).

 Typical scenarios are as following:

   - update operations are submitted, all with LAST dep, they are sent
     over the network concurrently (up to slot->max_in_flight) and executed
     serially by the receiver;

   - an update item I0 is submitted, then a set of read-only items is submitted,
     all with ->dep set to LAST. Read-only operations are sent concurrently and
     executed concurrently after I0 has been executed.

 Note that LAST can be nicely generalized (we don't need this now): instead of
 distinguishing MUTABO and !MUTABO items, we split items into "commutative"
 groups such that items in a group can be executed in any order.

 XXX Current implementation does not have way to define dependancies.
     Hence no more than 1 items are allowed to be in-flight for a particular
     slot.

    @todo
	- kernel mode support
	- stats
	- Generate ADDB data points for important session events
	- UUID generation
	- store replies in FOL
	- Support more than one items in flight for a slot
	- Optimization: Cache misordered items at receiver, rather than
	  discarding them.
	- How to get unique stob_id for session and slot cobs?
	- session recovery needs to be implemented.
	- slot table resize needs to be implemented.
	- can there be ACTIVE to FAILED transition for a c2_rpc_conn?
	- can there be BUSY to FAILED transition for c2_rpc_session?
	- Design protocol to dynamically adjust number of slots.
 */

#include "lib/list.h"
#include "lib/chan.h"
#include "lib/cond.h"
#include "lib/mutex.h"
#include "dtm/verno.h"

/* Imports */
struct c2_rpc_item;
struct c2_rpcmachine;
struct c2_rpc_chan;

/* Exports */
struct c2_rpc_session;
struct c2_rpc_conn;

enum {
	/** [conn|session]_[create|terminate] items go on session 0 */
	SESSION_ID_0 = 0,
	SESSION_ID_INVALID = UINT64_MAX,
	/** Range of valid session ids */
	SESSION_ID_MIN = SESSION_ID_0 + 1,
	SESSION_ID_MAX = SESSION_ID_INVALID - 1,
	SENDER_ID_INVALID = UINT64_MAX,
	SLOT_ID_INVALID = UINT32_MAX,
};

/**
   Requirements:
   * UUID must change whenever a storage-less client re-boots.
   * for a client with persistent state (e.g., a disk) uuid
     must survive reboots.
 */
struct c2_rpc_sender_uuid {
	/** XXX Temporary */
	uint64_t su_uuid;
};

enum c2_rpc_conn_state {
	/**
	  All the fields of conn are initialised locally. But the connection
	  is not yet established.
	 */
	C2_RPC_CONN_INITIALISED = (1 << 0),

	/**
	   When sender is waiting for receiver reply to get its sender ID it is
	   in CONNECTING state.
	 */
	C2_RPC_CONN_CONNECTING = (1 << 1),

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
   A rpc connection identfies a sender to the receiver. It acts as a parent
   object within which sessions are created. rpc connection has two
   identifiers.

   - UUID: Uniquely Identifies of the rpc connection globally within the
           cluster. UUID is generated by sender.

   - SenderID: A sender id is assigned by receiver. Sender Id uniquely
               identifies a rpc connection on receiver side.
               Same sender has different sender_id to communicate with
               different receiver.

   UUID being larger in size compared to SenderID, it is efficient to use
   sender id to locate rpc connection object.

   c2_rpcmachine maintains two lists of c2_rpc_conn
   - cr_outgoing_conns: list of c2_rpc_conn objects for which this node is
     sender
   - cr_incoming_conns: list of c2_rpc_conn object for which this node is
     receiver

   Instance of c2_rpc_conn stores a list of all sessions currently active with
   the service.

   At the time of creation of a c2_rpc_conn, a "special" session with
   SESSION_ID_0 is also created. It is special in the sense that it is
   "hand-made" and there is no need to communicate to receiver in order to
   create this session. Receiver assumes that there always exists a session 0
   for each rpc connection.
   Session 0 always have exactly 1 slot within it.
   Receiver creates session 0 while creating the rpc connection itself.
   Session 0 is required to send special fops like
   - conn_establish or conn_terminate FOP
   - session_establish or session_terminate FOP.

   <B> State transition diagram: </B>

   Note: There is no state named as "UNINITIALISED", it is in the state
   diagram to specify "before initialisation" and "after finalisation" state,
   and the contents of object are irrelevant and "unknown".

   @verbatim
   +-------------------------> UNINITIALISED
         allocated               ^  |
               c2_rpc_conn_fini()|  |  c2_rpc_conn_init()
                                 |  V
                               INITIALISED
                                    |
                                    |  c2_rpc_conn_establish()
                                    |
                                    V
         +---------------------- CONNECTING
         | time-out ||              |
         |     reply.rc != 0        | conn_establish_reply_received() &&
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
	 |                      TERMINATED
	 |                          |
	 |                          |  c2_rpc_conn_fini()
	 | c2_rpc_conn_fini()       V
	 +--------------------> UNINITIALISED

  @endverbatim

  <B> Liveness and Concurrency: </B>
  * Allocation and deallocation of c2_rpc_conn object is entirely handled by
    user (c2_rpc_conn object is not reference counted).
  * c2_rpc_conn::c_mutex protects all but c_link fields of c2_rpc_conn.
  * Locking order: slot => session => conn => rpcmachine

  <B> Typical sequence of API execution </B>
  Note: error checking is omitted.

  @code
  // ALLOCATE CONN
  struct c2_rpc_conn *conn;
  C2_ALLOC_PTR(conn);

  // INITIALISE CONN
  rc = c2_rpc_conn_init(conn, tgt_end_point, rpcmachine);
  C2_ASSERT(ergo(rc == 0, conn->c_state == C2_RPC_CONN_INITIALISED));

  // ESTABLISH RPC CONNECTION
  rc = c2_rpc_conn_establish(conn);
  C2_ASSERT(ergo(rc == 0, conn->c_state == C2_RPC_CONN_CONNECTING));

  // WAIT UNTIL CONNECTION IS ESTABLISHED
  flag = c2_rpc_conn_timedwait(conn, C2_RPC_CONN_ACTIVE | C2_RPC_CONN_FAILED,
				absolute_timeout);
  if (flag) {
	if (conn->c_state == C2_RPC_CONN_ACTIVE)
		// connection is established and is ready to be used
	ele
		// connection establishing failed
  } else {
	// timeout
  }
  // Assuming connection is established.
  // Create one or more sessions using this connection. @see c2_rpc_session

  // TERMINATING CONNECTION
  // Make sure that all the sessions that were created on this connection are
  // terminated
  C2_ASSERT(conn->c_nr_sessions == 0);

  rc = c2_rpc_conn_terminate(conn);
  C2_ASSERT(ergo(rc == 0, conn->c_state == C2_RPC_CONN_TERMINATING));

  // WAIT UNTIL CONNECTION IS TERMINATED
  flag = c2_rpc_conn_timedwait(conn, C2_RPC_CONN_TERMINATED |
				C2_RPC_CONN_FAILED, absolute_timeout);
  if (flag) {
	if (conn->c_state == C2_RPC_CONN_TERMINATED)
		// conn is successfully terminated
	else
		// conn terminate has failed
  } else {
	// timeout
  }
  // assuming conn is terminated
  c2_rpc_conn_fini(conn);
  c2_free(conn);

  @endcode
 */
struct c2_rpc_conn {
	/** Globally unique ID of rpc connection */
	struct c2_rpc_sender_uuid c_uuid;
	/**
	   rpcmachine with which this conn is associated
	 */
	struct c2_rpcmachine     *c_rpcmachine;
	/**
	   list_link to put c2_rpc_conn in either
	   c2_rpcmachine::cr_incoming_conns or c2_rpcmachine::cr_outgoing_conns
	 */
	struct c2_list_link       c_link;
	enum c2_rpc_conn_state    c_state;
	/** @see c2_rpc_conn_flags for list of flags */
	uint64_t                  c_flags;
	/** A c2_rpc_chan structure that will point to the transfer
	    machine used by this c2_rpc_conn. */
	struct c2_rpc_chan       *c_rpcchan;
	/**
	    XXX Deprecated: c2_service_id
	    Id of the service with which this c2_rpc_conn is associated
	*/
	struct c2_service_id     *c_service_id;
	/** Destination end point */
	struct c2_net_end_point  *c_end_point;
	/** cob representing the connection */
	struct c2_cob            *c_cob;
	/** Sender ID unique on receiver */
	uint64_t                  c_sender_id;
	/** List of all the sessions created under this rpc connection.
	    c2_rpc_session objects are placed in this list using
	    c2_rpc_session::s_link */
	struct c2_list            c_sessions;
	/** Counts number of sessions (excluding session 0) */
	uint64_t                  c_nr_sessions;
	/** Conditional variable on which "connection state changed" signal
	    is broadcast */
	struct c2_cond            c_state_changed;
	struct c2_mutex           c_mutex;
	/** if c_state == C2_RPC_CONN_FAILED then c_rc contains error code */
	int32_t                   c_rc;
};

/**
   Initialises @conn object and associates it with @machine.
   No network communication is involved.

   Note: c2_rpc_conn_init() can fail with -ENOMEM, -EINVAL.
	 if c2_rpc_conn_init() fails, conn is left in undefined state.

   @pre conn != NULL && ep != NULL && machine != NULL
   @post ergo(rc == 0, conn->c_state == C2_RPC_CONN_INITIALISED &&
			conn->c_machine == machine &&
			conn->c_end_point == ep &&
			conn->c_sender_id == SENDER_ID_INVALID &&
			(conn->c_flags & RCF_SENDER_END) != 0)
 */
int c2_rpc_conn_init(struct c2_rpc_conn      *conn,
		     struct c2_net_end_point *ep,
		     struct c2_rpcmachine    *machine);

/**
    Sends handshake CONN_ESTABLISH fop to the remote end.
    When reply to CONN_ESTABLISH is received,
    c2_rpc_conn_establish_reply_received() is called.

    @pre conn->c_state == C2_RPC_CONN_INITIALISED
    @post ergo(result == 0, conn->c_state == C2_RPC_CONN_CONNECTING &&
		c2_list_contains(conn->c_rpcmachine->cr_rpc_conn_list,
				 &conn->c_link))
    @post if result != 0, conn can be in C2_RPC_CONN_FAILED state.
 */
int c2_rpc_conn_establish(struct c2_rpc_conn *conn);

/**
   Sends "conn_terminate" FOP to receiver.
   c2_rpc_conn_terminate() is a no-op if @conn is already in TERMINATING
   state.
   c2_rpc_conn_terminate_reply_received() is called when reply to
   CONN_TERMINATE is received.

   @pre (conn->c_state == C2_RPC_CONN_ACTIVE && conn->c_nr_sessions == 0) ||
		conn->c_state == C2_RPC_CONN_TERMINATING
   @post ergo(result == 0, conn->c_state == C2_RPC_CONN_TERMINATING)
   @post if result != 0, conn can be in C2_RPC_CONN_FAILED state
 */
int c2_rpc_conn_terminate(struct c2_rpc_conn *conn);

/**
   Finalises c2_rpc_conn.
   No network communication involved.
   @pre conn->c_state == C2_RPC_CONN_FAILED ||
	conn->c_state == C2_RPC_CONN_INITIALISED ||
	conn->c_state == C2_RPC_CONN_TERMINATED
 */
void c2_rpc_conn_fini(struct c2_rpc_conn *conn);

/**
    Waits until @conn reaches in any one of states specified by @state_flags.
    @param state_flags can specify multiple states by ORing

    @param abs_timeout should not sleep past abs_timeout waiting for conn
		to reach in desired state.
    @return true if @conn reaches in one of the state(s) specified by
                @state_flags
    @return false if time out has occured before @conn reaches in desired
                state.
 */
bool c2_rpc_conn_timedwait(struct c2_rpc_conn *conn,
			   uint64_t            state_flags,
			   const c2_time_t     abs_timeout);

/**
   checks internal consistency of @conn.
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
	   When sender sends a SESSION_ESTABLISH FOP to reciever it
	   is in CREATING state
	 */
	C2_RPC_SESSION_ESTABLISHING = (1 << 1),
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
   Rpc connection can be shared by multiple entities (e.g. users) by
   creating their own "session" on the connection.
   A session can be used to maintain authentication information or QoS
   parameters. But currently it is just a container for slots.

   <B> Liveness: </B>
   Allocation and deallocation of c2_rpc_session is entirely managed by user.

   <B> Concurrency:</B>
   c2_rpc_session::s_mutex protects all fields except s_link. s_link is
   protected by session->s_conn->c_mutex.
   When session is in UNINITIALISED or in C2_RPC_SESSION_TERMINATED state,
   user is expected to serialise access to the session object.
   There is no need to take session->s_mutex while posting item
   on the session. Users of rpc-layer are never expected to take lock on
   session.

   Locking hierarchy is slot => session => conn => rpcmachine.

   @verbatim

            +------------------> UNINITIALISED
                 allocated         ^   |
                            fini() |   |  c2_rpc_session_init()
				   |   V
				  INITIALISED
				      |
				      | c2_rpc_session_establish()
				      |
		timed-out             V
          +-------------------------CREATING
	  |   create_failed           | create successful/n = 0
	  V                           |
	FAILED <------+               |   n == 0 && list_empty(unbound_items)
	  |           |               +-----------------+
	  |           |               |                 | +-----+
	  |           |failed         |                 | |     | item add/n++
	  |           |               V  item add/n++   | V     | reply rcvd/n--
	  |           |             IDLE--------------->BUSY----+
	  |           |               |
	  | fini()    |               | session_terminate
	  |           |               V
	  |           +----------TERMINATING
	  |                           |
	  |                           |
	  |                           |
	  |                           |session_terminated
	  |                           V
	  |                       TERMINATED
	  |                           |
	  |                           | fini()
	  |                           V
	  +----------------------> UNINITIALISED

   @endverbatim

   Typical sequence of execution of APIs on sender side. Error checking is
   omitted.

   @code

   // ALLOCATE SESSION

   struct c2_rpc_session *session;
   C2_ALLOC_PTR(session);

   // INITIALISE SESSION

   nr_slots = 4;
   rc = c2_rpc_session_init(session, conn, nr_slots);
   C2_ASSERT(ergo(rc == 0, session->s_state == C2_RPC_SESSION_INITIALISED));

   // ESTABLISH SESSION

   rc = c2_rpc_session_establish(session);
   C2_ASSERT(ergo(rc == 0, session->s_state == C2_RPC_SESSION_ESTABLISHING));

   flag = c2_rpc_session_timedwait(session, C2_RPC_SESSION_IDLE |
					C2_RPC_SESSION_FAILED, timeout);

   if (flag && session->s_state == C2_RPC_SESSION_IDLE) {
	// Session is successfully established
   } else {
	// timeout has happend or session establish failed
   }

   // Assuming session is successfully established.
   // post unbound items using c2_rpc_post(item)

   item->ri_session = session;
   item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
   item->ri_deadline = absolute_time;
   item->ri_ops = item_ops;   // item_ops contains ->replied() callback which
			      // will be called when reply to this item is
			      // received. DO NOT FREE THIS ITEM.

   // TERMINATING SESSION
   // Wait until all the items that were posted on this session, are sent and
   // for all those items either reply is received or reply_timeout has
   // triggered.
   flag = c2_rpc_session_timedwait(session, C2_RPC_SESSION_IDLE, timeout);
   if (flag) {
	C2_ASSERT(session->s_state == C2_RPC_SESSION_IDLE);
	rc = c2_rpc_session_terminate(session);
	C2_ASSERT(ergo(rc == 0, session->s_state ==
			C2_RPC_SESSION_TERMINATING));

	// Wait until session is terminated.
	flag1 = c2_rpc_session_timedwait(session, C2_RPC_SESSION_TERMINATED |
					C2_RPC_SESSION_FAILED, timeout);
	C2_ASSERT(ergo(flag1, session->s_state == C2_RPC_SESSION_TERMINATED ||
			session->s_state == C2_RPC_SESSION_FAILED));
   }

   // FINALISE SESSION

   c2_rpc_session_fini(session);
   c2_free(session);
   @endcode
 */
struct c2_rpc_session {
	/** linkage into list of all sessions within a c2_rpc_conn */
	struct c2_list_link       s_link;
	enum c2_rpc_session_state s_state;
	/** identifies a particular session. It is not globally unique */
	uint64_t                  s_session_id;
	struct c2_cob            *s_cob;
	/** rpc connection on which this session is created */
	struct c2_rpc_conn       *s_conn;
	/** A condition variable on which broadcast is sent whenever state of
	    session is changed. Associated with s_mutex */
	struct c2_cond            s_state_changed;
	/** lock protecting this session and slot table */
	struct c2_mutex           s_mutex;
	/** Number of items that needs to be sent or their reply is
	    not yet received. i.e. count of items in {FUTURE, IN_PROGRESS}
	    state in all slots belonging to this session. */
	int32_t                   s_nr_active_items;
	/** list of items that can be sent through any available slot.
	    items are placed using c2_rpc_item::ri_unbound_link */
	struct c2_list            s_unbound_items;
	/** Capacity of slot table */
	uint32_t                  s_slot_table_capacity;
	/** Only [0, s_nr_slots) slots from the s_slot_table can be used to
            bind items. s_nr_slots <= s_slot_table_capacity */
	uint32_t                  s_nr_slots;
	/** Array of pointers to slots */
	struct c2_rpc_slot      **s_slot_table;
	/** if s_state == C2_RPC_SESSION_FAILED then s_rc contains error code
		denoting cause of failure */
	int32_t                   s_rc;
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
			uint32_t               nr_slots);

/**
    Sends a SESSION_ESTABLISH fop across pre-defined session-0 in
    session->s_conn.
    c2_rpc_session_establish_reply_received() is called when reply to
    SESSION_ESTABLISH fop is received.

    @pre session->s_state == C2_RPC_SESSION_INITIALISED
    @pre session->s_conn->c_state == C2_RPC_CONN_ACTIVE
    @post ergo(result == 0, session->s_state == C2_RPC_SESSION_ESTABLISHING &&
		c2_list_contains(session->s_conn->c_sessions, &session->s_link))
    @post if result != 0, session can be in C2_RPC_SESSION_FAILED state
 */
int c2_rpc_session_establish(struct c2_rpc_session *session);

/**
   Sends terminate session fop to receiver.
   Acts as no-op if session is already in TERMINATING state.
   c2_rpc_session_terminate_reply_received() is called when reply to
   CONN_TERMINATE fop is received.

   @pre session->s_state == C2_RPC_SESSION_IDLE ||
	session->s_state == C2_RPC_SESSION_TERMINATING
   @post ergo(result == 0, session->s_state == C2_RPC_SESSION_TERMINATING)
   @post if result != 0, session can be in C2_RPC_SESSION_FAILED state
 */
int c2_rpc_session_terminate(struct c2_rpc_session *session);

/**
    Waits until @session object reaches in one of states given by @state_flags.

    @param state_flags can specify multiple states by ORing
    @param abs_timeout thread does not sleep past abs_timeout waiting for conn
		to reach in desired state.
    @return true if session reaches in one of the state(s) specified by
		@state_flags
    @return false if time out has occured before session reaches in desired
		state.
 */
bool c2_rpc_session_timedwait(struct c2_rpc_session *session,
			      uint64_t               state_flags,
			      const c2_time_t        abs_timeout);

/**
   Finalises session object

   @pre session->s_state == C2_RPC_SESSION_TERMINATED ||
	session->s_state == C2_RPC_SESSION_FAILED ||
	session->s_state == C2_RPC_SESSION_INITIALISED
 */
void c2_rpc_session_fini(struct c2_rpc_session *session);

/**
   Returns the number of rpc items that can be bound to this slot
   staying within SLOT_DEFAULT_MAX_IN_FLIGHT limit.
   @param slot - Input c2_rpc_slot
   @retval - Number of items that can be bound to this slot.
   XXX Need a better name.
 */
uint32_t c2_rpc_slot_items_possible_inflight(struct c2_rpc_slot *slot);

/**
  In memory slot object.

  A slot provides the FIFO and exactly once semantics for item delivery.
  c2_rpc_slot and its corresponding methods are equally valid on sender and
  receiver side.

  One can think of a slot as a pipe. On sender side, application/formation is
  placing items at one end of this pipe. The item appears on the other end
  of pipe. And formation takes the item, packs in some RPC, and sends it.

  On receiver side, when an item is received it is placed in one end of the
  pipe. When the item appears on other end of pipe it is sent for execution.

  With a slot a list of items, ordered by verno is associated. An item on
  the list is in one of the following states:

  - past committed: the reply for the item was received and the receiver
    confirmed that the item is persistent. Items can linger for some time
    in this state, because distributed transaction recovery requires keeping
    items in memory after they become persistent on the receiver;

  - past volatile: the reply was received, but persistence confirmation wasn't;

  - unreplied: the item was sent (i.e., placed into an rpc) and no reply is
    received. We are usually talking about a situations where there is at
    most a single item in this state, but it is not, strictly speaking,
    necessary. More than a single item can be in flight per-slot.;

  - future: the item wasn't sent.

  An item can be linked into multiple slots (similar to c2_fol_obj_ref).
  For each slot the item has a separate verno and separate linkage into
  the slot's item list. Item state is common for all slots;

  An item, has a MUTABO flag, which is set when the item is an update
  (i.e., changes the file system state). When the item is an update then
  (for each slot the item is in) its verno is greater than the verno of
  the previous item on the slot's item list. Multiple consecutive non-MUTABO
  items (i.e., read-only queries) can have the same verno;

  With each item a (pointer to) reply is associated. This pointer is set
  once the reply is received for the item;

  A slot has a number of pointers into this list and other fields,
  described below:

  - last_sent
  pointer usually points to the latest unreplied request. When the
  receiver fails and restarts, the last_sent pointer is shifted back to the
  item from which the recovery must continue.
  Note that last_sent might be moved all the way back to the oldest item;

  - last_persistent
  last_persistent item points to item whose effects have reached to persistent
  storage.

  A slot state machine reacts to the following events:
  [ITEM ADD]: a new item is added to the future list;
  [REPLY RECEIVED]: a reply is received for an item;
  [PERSISTENCE]: the receiver notified the sender about the change in the
  last persistent item;
  [RESET]: last_sent is reset back due to the receiver restart.

  Note that slot.in_flight not necessary equals the number of unreplied items:
  during recovery items from the past parts of the list are replayed.

  Also note, that the item list might be empty. To avoid special cases with
  last_sent pointer, let's prepend a special dummy item with an impossibly
  low verno to the list.

  For each slot, a configuration parameter slot.max_in_flight is defined.
  This parameter is set to 1 to obtain a "standard" slot behaviour, where no
  more than single item is in flight.

  When an update item (one that modifies file-system state) is added to the
  slot, it advances version number of slot.
  The verno.vn_vc is set to 0 at the time of slot creation on both ends.

  <B> Liveness and concurreny </B>
  Slots are allocated at the time of session initialisation and freed at the
  time of session finalisation.
  c2_rpc_slot::sl_mutex protects all fields of slot except sl_link.
  Locking hierarchy is slot => session => conn => rpcmachine
 */
struct c2_rpc_slot {
	/** Session to which this slot belongs */
	struct c2_rpc_session        *sl_session;
	/** identifier of slot, unique within the session */
	uint32_t                      sl_slot_id;
	struct c2_cob                *sl_cob;
	/** list anchor to put in c2_rpcmachine::cr_ready_slots */
	struct c2_list_link           sl_link;
	/** Current version number of slot */
	struct c2_verno               sl_verno;
	/** slot generation */
	uint64_t                      sl_slot_gen;
	/** A monotonically increasing sequence counter, assigned to an item
	    when it is bound to the slot */
	uint64_t                      sl_xid;
	/** List of items, starting from oldest. Items are placed using
	    c2_rpc_item::ri_slot_refs[0].sr_link */
	struct c2_list                sl_item_list;
	/** earliest item that the receiver possibly have seen */
	struct c2_rpc_item           *sl_last_sent;
	/** item that is most recently persistent on receiver */
	struct c2_rpc_item           *sl_last_persistent;
	/** Number of items in flight */
	uint32_t                      sl_in_flight;
	/** Maximum number of items that can be in flight on this slot.
	    @see SLOT_DEFAULT_MAX_IN_FLIGHT */
	uint32_t                      sl_max_in_flight;
	/** List of items ready to put in rpc. Items are placed in this
	    list using c2_rpc_item::ri_slot_refs[0].sr_ready_link */
	struct c2_list                sl_ready_list;
	struct c2_mutex               sl_mutex;
	const struct c2_rpc_slot_ops *sl_ops;
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
