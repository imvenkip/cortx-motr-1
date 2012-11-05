/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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

#pragma once

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
- rpc machine @see c2_rpc_machine
- rpc item @see c2_rpc_item.

<B> Relationships among objects: </B>
rpc_machine has two lists of rpc connections.
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
its reply is retrieved from reply cache and returned to the sender.
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

 XXX Current implementation does not have way to define dependencies.
     Hence no more than 1 items are allowed to be in-flight for a particular
     slot.

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

 <B> Using two identifiers for session and conn </B>
 @todo
 currently, receiver assigns identifiers to connections and sessions and
 these identifiers are used by both parties. What we can do, is to allow
 sender to assign identifiers to sessions (this identifier is sent in
 SESSION_ESTABLISH). Then, whenever receiver uses the session to send a
 reply, it uses this identifier (instead of receiver assigned session-id).
 The advantage of this, is that sender can use an identifier that allows
 quick lookup (e.g., an index in some session array or simply a pointer).
 Similarly for connections (i.e., another sender generated identifier in
 addition to uuid, that is not guaranteed to be globally unique)

    @todo
	- stats
	- Generate ADDB data points for important session events
	- UUID generation
	- store replies in FOL
	- Support more than one items in flight for a slot
	- Optimization: Cache misordered items at receiver, rather than
	  discarding them.
	- How to get unique stob_id for session and slot cobs?
	- slot table resize needs to be implemented.
	- Design protocol to dynamically adjust number of slots.
	- Session level timeout
	- session can be terminated only if all items are pruned from all
		slot->sl_item_list

 */

#include "lib/list.h"
#include "lib/tlist.h"
#include "lib/time.h"
#include "sm/sm.h"       /* c2_sm */
#include "rpc/rpc_onwire.h"

/* Imports */
struct c2_rpc_conn;
struct c2_cob;
struct c2_rpc_slot;

/* Exports */
struct c2_rpc_session;

enum {
	/** [conn|session]_[create|terminate] items go on session 0 */
	SESSION_ID_0       = 0,
	SESSION_ID_INVALID = UINT64_MAX,
	/** Range of valid session ids */
	SESSION_ID_MIN     = SESSION_ID_0 + 1,
	SESSION_ID_MAX     = SESSION_ID_INVALID - 1,
	SLOT_ID_INVALID    = UINT32_MAX,
};

/**
   Possible states of a session object
 */
enum c2_rpc_session_state {
	/**
	   all lists, mutex and channels of session are initialised.
	   No actual session is established with any end point
	 */
	C2_RPC_SESSION_INITIALISED,
	/**
	   When sender sends a SESSION_ESTABLISH FOP to reciever it
	   is in CREATING state
	 */
	C2_RPC_SESSION_ESTABLISHING,
	/**
	   A session is IDLE if both of following is true
		- for each slot S in session
			for each item I in S->item_list
				// I has got reply
				I->state is in {PAST_COMMITTED, PAST_VOLATILE}
		- session->unbound_items list is empty
	   A session can be terminated only if it is IDLE.
	 */
	C2_RPC_SESSION_IDLE,
	/**
	   A session is busy if any of following is true
		- Any of slots has item to be sent (FUTURE items)
		- Any of slots has item for which reply is not received
			(IN_PROGRESS items)
		- unbound_items list is not empty
	 */
	C2_RPC_SESSION_BUSY,
	/**
	   Creation/termination of session failed
	 */
	C2_RPC_SESSION_FAILED,
	/**
	   When sender sends SESSION_TERMINATE fop to receiver and is waiting
	   for reply, then it is in state TERMINATING.
	*/
	C2_RPC_SESSION_TERMINATING,
	/**
	   When sender gets reply to session_terminate fop and reply informs
	   the session termination is successful then the session enters in
	   TERMINATED state
	 */
	C2_RPC_SESSION_TERMINATED,
	/** After c2_rpc_session_fini() the RPC session instance is moved to
	    FINALISED state.
	 */
	C2_RPC_SESSION_FINALISED
};

/**
   Rpc connection can be shared by multiple entities (e.g. users) by
   creating their own "session" on the connection.
   A session can be used to maintain authentication information or QoS
   parameters. But currently it is just a container for slots.

   <B> Liveness: </B>

   On sender side, allocation and deallocation of c2_rpc_session is entirely
   managed by user except for SESSION 0. SESSION 0 is allocated and deallocated
   by rpc-layer internally along with c2_rpc_conn.
   @see c2_rpc_conn for more information on creation and use of SESSION 0.

   On receiver side, c2_rpc_session object will be allocated and deallocated
   by rpc-layer internally, in response to session create and session terminate
   requests respectively.

   <B> Concurrency:</B>

   Users of rpc-layer are never expected to take lock on session. Rpc layer
   will internally synchronise access to c2_rpc_session.

   c2_rpc_session::s_mutex protects all fields except s_link. s_link is
   protected by session->s_conn->c_mutex.

   There is no need to take session->s_mutex while posting item on the session.`
   When session is in one of INITIALISED, TERMINATED, FINALISED and
   FAILED state, user is expected to serialise access to the session object.
   (It is assumed that session in one of {INITIALISED, TERMINATED, FAILED,
    FINALISED} state, very likely does not have concurrent users).

   Locking order:
    - slot->sl_mutex
    - session->s_mutex
    - conn->c_mutex
    - rpc_machine->rm_session_mutex, rpc_machine->rm_ready_slots_mutex (As of
      now, there is no case where these two mutex are held together. If such
      need arises then ordering of these two mutex should be decided.)

   @verbatim
                                      |
                                      |c2_rpc_session_init()
  c2_rpc_session_establish() != 0     V
          +----------------------INITIALISED
          |                           |
          |                           | c2_rpc_session_establish()
          |                           |
          |     timed-out             V
          +-----------------------ESTABLISHING
	  |   create_failed           | create successful/n = 0
	  V                           |
	FAILED <------+               |   n == 0
	  |           |               +-----------------+
	  |           |               |                 | +-----+
	  |           |failed         |                 | |     | item add/n++
	  |           |               V  item add/n++   | V     | reply rcvd/n--
	  |           +-------------IDLE--------------->BUSY----+
	  |           |               |
	  | fini()    |               | c2_rpc_session_terminate()
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
	  +----------------------> FINALISED

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
   C2_ASSERT(ergo(rc == 0, session_state(session) ==
                           C2_RPC_SESSION_INITIALISED));

   // ESTABLISH SESSION

   rc = c2_rpc_session_establish(session);

   rc = c2_rpc_session_timedwait(session, C2_BITS(C2_RPC_SESSION_IDLE,
					          C2_RPC_SESSION_FAILED),
				 timeout);

   if (rc == 0 && session_state(session) == C2_RPC_SESSION_IDLE) {
	// Session is successfully established
   } else {
	// timeout has happened or session establish failed
   }

   // Assuming session is successfully established.
   // post unbound items using c2_rpc_post(item)

   item->ri_session = session;
   item->ri_prio = C2_RPC_ITEM_PRIO_MAX;
   item->ri_deadline = absolute_time;
   item->ri_ops = item_ops;   // item_ops contains ->replied() callback which
			      // will be called when reply to this item is
			      // received. DO NOT FREE THIS ITEM.

   c2_rpc_post(item);

   // TERMINATING SESSION
   // Wait until all the items that were posted on this session, are sent and
   // for all those items either reply is received or reply_timeout has
   // triggered.
   c2_rpc_session_timedwait(session, C2_BITS(C2_RPC_SESSION_IDLE), timeout);
   C2_ASSERT(session_state == C2_RPC_SESSION_IDLE);
   rc = c2_rpc_session_terminate(session);
   if (rc == 0) {
	// Wait until session is terminated.
	rc = c2_rpc_session_timedwait(session,
				      C2_BITS(C2_RPC_SESSION_TERMINATED,
					      C2_RPC_SESSION_FAILED),
				      timeout);
   }

   // FINALISE SESSION

   c2_rpc_session_fini(session);
   c2_free(session);
   @endcode

   Receiver is not expected to call any of these APIs. Receiver side session
   structures will be set-up while handling fops
   c2_rpc_fop_[conn|session]_[establish|terminate].

   When receiver needs to post reply, it uses c2_rpc_reply_post().

   @code
   c2_rpc_reply_post(request_item, reply_item);
   @endcode

   c2_rpc_reply_post() will copy all the session related information from
   request item to reply item and process reply item.

   Note: rpc connection is a two-way communication channel. There are requests
   and corresponding reply items, on the same connection. Receiver NEED NOT
   have to establish other separate connection with sender, to be able to
   send replies.
 */
struct c2_rpc_session {
	/** identifies a particular session. Unique in all sessions belonging
	    to same c2_rpc_conn
	 */
	uint64_t                  s_session_id;

	/** rpc connection on which this session is created */
	struct c2_rpc_conn       *s_conn;

	/** Link in RPC conn. c2_rpc_conn::c_sessions
	    List descriptor: session
	 */
	struct c2_tlink		  s_link;

	/** Cob representing this session in persistent state */
	struct c2_cob            *s_cob;

	/** Number of items that needs to be sent or their reply is
	    not yet received. i.e. count of items in {FUTURE, IN_PROGRESS}
	    state in all slots belonging to this session.
	 */
	int32_t                   s_nr_active_items;

	/** list of items that can be sent through any available slot.
	    items are placed using c2_rpc_item::ri_unbound_link
	 */
	struct c2_list            s_unbound_items;

	/** Capacity of slot table */
	uint32_t                  s_slot_table_capacity;

	/** Only [0, s_nr_slots) slots from the s_slot_table can be used to
            bind items. s_nr_slots <= s_slot_table_capacity
	 */
	uint32_t                  s_nr_slots;

	/** Array of pointers to slots */
	struct c2_rpc_slot      **s_slot_table;

	/** if > 0, then session is in BUSY state */
	uint32_t                  s_hold_cnt;

	/** List of slots, which can be associated with an unbound item.
	    Link: c2_rpc_slot::sl_link
	 */
	struct c2_tl              s_ready_slots;

	/** RPC session state machine
	    @see c2_rpc_session_state, session_conf
	 */
	struct c2_sm		  s_sm;

	/** C2_RPC_SESSION_MAGIC */
	uint64_t		  s_magic;
};

/**
   Initialises all fields of session. Allocates and initialises
   nr_slots number of slots.
   No network communication is involved.

   @param session session being initialised
   @param conn rpc connection with which this session is associated
   @param nr_slots number of slots in the session

   @post ergo(rc == 0, session_state(session) == C2_RPC_SESSION_INITIALISED &&
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

    @pre session_state(session) == C2_RPC_SESSION_INITIALISED
    @pre conn_state(session->s_conn) == C2_RPC_CONN_ACTIVE
    @post ergo(result != 0, session_state(session) == C2_RPC_SESSION_FAILED)
 */
int c2_rpc_session_establish(struct c2_rpc_session *session);

/**
 * Same as c2_rpc_session_establish(), but in addition uses
 * c2_rpc_session_timedwait() to ensure that session is in idle state after
 * c2_rpc_session_establish() call.
 *
 * @param session     A session object to operate on.
 * @param timeout_sec How much time in seconds to wait for session to become idle.
 *
 * @pre  session_state(session) == C2_RPC_SESSION_INITIALISED
 * @pre  conn_state(session->s_conn) == C2_RPC_CONN_ACTIVE
 * @post session_state(session) == C2_RPC_SESSION_IDLE
 */
int c2_rpc_session_establish_sync(struct c2_rpc_session *session,
				  uint32_t timeout_sec);

/**
 * A combination of c2_rpc_session_init() and c2_rpc_session_establish_sync() in
 * a single routine - initialize session object, establish a session and wait
 * until it become idle.
 */
int c2_rpc_session_create(struct c2_rpc_session *session,
			  struct c2_rpc_conn    *conn,
			  uint32_t               nr_slots,
			  uint32_t               timeout_sec);

/**
   Sends terminate session fop to receiver.
   Acts as no-op if session is already in TERMINATING state.
   c2_rpc_session_terminate_reply_received() is called when reply to
   CONN_TERMINATE fop is received.

   @pre C2_IN(session_state(session), (C2_RPC_SESSION_IDLE,
				       C2_RPC_SESSION_TERMINATING))
   @post ergo(rc != 0, session_state(session) == C2_RPC_SESSION_FAILED)
 */
int c2_rpc_session_terminate(struct c2_rpc_session *session);

/**
 * Same as c2_rpc_session_terminate(), but in addition uses
 * c2_rpc_session_timedwait() to ensure that session is in terminated state
 * after c2_rpc_session_terminate() call.
 *
 * @param session     A session object to operate on.
 * @param timeout_sec How much time in seconds to wait for session to become
 *                    terminated.
 *
 * @pre C2_IN(session_state(session), (C2_RPC_SESSION_IDLE,
 *				       C2_RPC_SESSION_TERMINATING))
 * @post C2_IN(session_state(session), (C2_RPC_SESSION_TERMINATED,
 *					C2_RPC_SESSION_FAILED))
 */
int c2_rpc_session_terminate_sync(struct c2_rpc_session *session,
				  uint32_t timeout_sec);

/**
    Waits until @session object reaches in one of states given by @state_flags.

    @param state_flags can specify multiple states by ORing
    @param abs_timeout thread does not sleep past abs_timeout waiting for conn
		to reach in desired state.
    @return 0 if session reaches in one of the state(s) specified by
		@state_flags
            -ETIMEDOUT if time out has occured before session reaches in desired
                state.
 */
int c2_rpc_session_timedwait(struct c2_rpc_session *session,
			     uint64_t               state_flags,
			     const c2_time_t        abs_timeout);

/**
   Finalises session object

   @pre C2_IN(session_state(session), (C2_RPC_SESSION_TERMINATED,
				       C2_RPC_SESSION_FAILED,
				       C2_RPC_SESSION_INITIALISED))
 */
void c2_rpc_session_fini(struct c2_rpc_session *session);

/**
 * A combination of c2_rpc_session_terminate_sync() and c2_rpc_session_fini() in
 * a single routine - terminate the session, wait until it switched to
 * terminated state and finalize session object.
 */
int c2_rpc_session_destroy(struct c2_rpc_session *session,
			   uint32_t               timeout_sec);

/**
   Returns maximum size of an RPC item allowed on this session.
 */
c2_bcount_t
c2_rpc_session_get_max_item_size(const struct c2_rpc_session *session);

/** Returns maximum possible size of RPC item payload. */
c2_bcount_t
c2_rpc_session_get_max_item_payload_size(const struct c2_rpc_session *session);

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
