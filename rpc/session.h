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

#ifndef __MERO_RPC_SESSION_H__
#define __MERO_RPC_SESSION_H__

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
- rpc connection @see m0_rpc_conn
- rpc session @see m0_rpc_session
- slot @see m0_rpc_slot
- slot ref @see m0_rpc_slot_ref.

Out of these, m0_rpc_conn and m0_rpc_session are visible to user.
m0_rpc_slot and m0_rpc_slot_ref are internal to rpc layer and not visible to
users outside rpc layer.

Session module uses following types of objects:
- rpc machine @see m0_rpc_machine
- rpc item @see m0_rpc_item.

<B> Relationships among objects: </B>
rpc_machine has two lists of rpc connections.
- Outgoing connections: Contains m0_rpc_conn objects for which this node is
sender.
- Incoming connections: Contains m0_rpc_conn objects for which this node is
receiver.

Rpc connection has a list of rpc sessions, which are created on this
connection. A rpc connection cannot be terminated until all the sessions
created on the connection are terminated.

A session contains one or more slots. Number of slots in the session can
vary over the lifetime of session (In current implementation state, the number
of slots do not vary. The slot count is fixed for entire lifetime of session.
Slot count is specified at the time of session initialisation).

Each object of type [m0_rpc_conn|m0_rpc_session|m0_rpc_slot] on sender
has counterpart object of same type on receiver. (Note: same structure
definitions are used on both sender and receiver side)

<B> Bound and Unbound rpc items: </B>

Rpc layer can send an rpc item on network only if it is associated with some
slot within the session. User can specify the slot on which the item needs to
be sent. Or the user can just specify session and leave the task of choosing
any available slot to rpc layer.

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
guaranteed even in the face of receiver restart. Mero implements Reply
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
 struct m0_rpc_item {
       struct m0_verno     verno;
       uint64_t            xid;
       struct m0_rpc_item *dep;
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
	- Generate ADDB data points for important session events
	- store replies in FOL
	- Support more than one items in flight for a slot
	- Optimization: Cache misordered items at receiver, rather than
	  discarding them.
	- How to get unique stob_id for session and slot cobs?
	- slot table resize needs to be implemented.
	- Design protocol to dynamically adjust number of slots.
 */

#include "lib/list.h"
#include "lib/tlist.h"
#include "lib/time.h"
#include "sm/sm.h"                /* m0_sm */
#include "rpc/rpc_onwire.h"

/* Imports */
struct m0_rpc_conn;
struct m0_cob;
struct m0_rpc_slot;

/* Exports */
struct m0_rpc_session;

/**
   Possible states of a session object
 */
enum m0_rpc_session_state {
	/**
	   all lists, mutex and channels of session are initialised.
	   No actual session is established with any end point
	 */
	M0_RPC_SESSION_INITIALISED,
	/**
	   When sender sends a SESSION_ESTABLISH FOP to reciever it
	   is in ESTABLISHING state
	 */
	M0_RPC_SESSION_ESTABLISHING,
	/**
	   A session is IDLE if both of following is true
		- for each slot S in session
			for each item I in S->item_list
				// I has got reply
				I->state is in {PAST_COMMITTED, PAST_VOLATILE}
		- formation queue has no item associated with this session
	   A session can be terminated only if it is IDLE.
	 */
	M0_RPC_SESSION_IDLE,
	/**
	   A session is busy if any of following is true
		- Any of slots has item to be sent (FUTURE items)
		- Any of slots has item for which reply is not received
			(IN_PROGRESS items)
		- Formation queue has item associated with this session
	 */
	M0_RPC_SESSION_BUSY,
	/**
	   Creation/termination of session failed
	 */
	M0_RPC_SESSION_FAILED,
	/**
	   When sender sends SESSION_TERMINATE fop to receiver and is waiting
	   for reply, then it is in state TERMINATING.
	*/
	M0_RPC_SESSION_TERMINATING,
	/**
	   When sender gets reply to session_terminate fop and reply informs
	   the session termination is successful then the session enters in
	   TERMINATED state
	 */
	M0_RPC_SESSION_TERMINATED,
	/** After m0_rpc_session_fini() the RPC session instance is moved to
	    FINALISED state.
	 */
	M0_RPC_SESSION_FINALISED
};

/**
   Rpc connection can be shared by multiple entities (e.g. users) by
   creating their own "session" on the connection.
   A session can be used to maintain authentication information or QoS
   parameters. But currently it is just a container for slots.

   <B> Liveness: </B>

   On sender side, allocation and deallocation of m0_rpc_session is entirely
   managed by user except for SESSION 0. SESSION 0 is allocated and deallocated
   by rpc-layer internally along with m0_rpc_conn.
   @see m0_rpc_conn for more information on creation and use of SESSION 0.

   On receiver side, m0_rpc_session object will be allocated and deallocated
   by rpc-layer internally, in response to session create and session terminate
   requests respectively.

   <B> Concurrency:</B>

   Users of rpc-layer are never expected to take lock on session. Rpc layer
   will internally synchronise access to m0_rpc_session.

   All access to session are synchronized using
   session->s_conn->c_rpc_machine->rm_sm_grp.s_lock.

   When session is in one of INITIALISED, TERMINATED, FINALISED and
   FAILED state, user is expected to serialise access to the session object.
   (It is assumed that session, in one of {INITIALISED, TERMINATED, FAILED,
    FINALISED} states, does not have concurrent users).

   @verbatim
                                      |
                                      |m0_rpc_session_init()
  m0_rpc_session_establish() != 0     V
          +----------------------INITIALISED
          |                           |
          |                           | m0_rpc_session_establish()
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
	  | fini()    |               | m0_rpc_session_terminate()
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

   struct m0_rpc_session *session;
   M0_ALLOC_PTR(session);

   // INITIALISE SESSION

   nr_slots = 4;
   rc = m0_rpc_session_init(session, conn, nr_slots);
   M0_ASSERT(ergo(rc == 0, session_state(session) ==
                           M0_RPC_SESSION_INITIALISED));

   // ESTABLISH SESSION

   rc = m0_rpc_session_establish(session);

   rc = m0_rpc_session_timedwait(session, M0_BITS(M0_RPC_SESSION_IDLE,
					          M0_RPC_SESSION_FAILED),
				 timeout);

   if (rc == 0 && session_state(session) == M0_RPC_SESSION_IDLE) {
	// Session is successfully established
   } else {
	// timeout has happened or session establish failed
   }

   // Assuming session is successfully established.
   // post unbound items using m0_rpc_post(item)

   item->ri_session = session;
   item->ri_prio = M0_RPC_ITEM_PRIO_MAX;
   item->ri_deadline = absolute_time;
   item->ri_ops = item_ops;   // item_ops contains ->replied() callback which
			      // will be called when reply to this item is
			      // received. DO NOT FREE THIS ITEM.

   rc = m0_rpc_post(item);

   // TERMINATING SESSION
   // Wait until all the items that were posted on this session, are sent and
   // for all those items either reply is received or reply_timeout has
   // triggered.
   m0_rpc_session_timedwait(session, M0_BITS(M0_RPC_SESSION_IDLE), timeout);
   M0_ASSERT(session_state == M0_RPC_SESSION_IDLE);
   rc = m0_rpc_session_terminate(session);
   if (rc == 0) {
	// Wait until session is terminated.
	rc = m0_rpc_session_timedwait(session,
				      M0_BITS(M0_RPC_SESSION_TERMINATED,
					      M0_RPC_SESSION_FAILED),
				      timeout);
   }

   // FINALISE SESSION

   m0_rpc_session_fini(session);
   m0_free(session);
   @endcode

   Receiver is not expected to call any of these APIs. Receiver side session
   structures will be set-up while handling fops
   m0_rpc_fop_[conn|session]_[establish|terminate].

   When receiver needs to post reply, it uses m0_rpc_reply_post().

   @code
   m0_rpc_reply_post(request_item, reply_item);
   @endcode

   m0_rpc_reply_post() will copy all the session related information from
   request item to reply item and process reply item.

   Note: rpc connection is a two-way communication channel. There are requests
   and corresponding reply items, on the same connection. Receiver NEED NOT
   have to establish other separate connection with sender, to be able to
   send replies.
 */
struct m0_rpc_session {
	/** identifies a particular session. Unique in all sessions belonging
	    to same m0_rpc_conn
	 */
	uint64_t                  s_session_id;

	/** rpc connection on which this session is created */
	struct m0_rpc_conn       *s_conn;

	/** Link in RPC conn. m0_rpc_conn::c_sessions
	    List descriptor: session
	 */
	struct m0_tlink		  s_link;

	/** Cob representing this session in persistent state */
	struct m0_cob            *s_cob;

	/** Number of items that needs to be sent or their reply is
	    not yet received. i.e. count of items in {FUTURE, IN_PROGRESS}
	    state in all slots belonging to this session.

	    XXX FIXME The value can't be negative. Change the type to uint32_t.
	 */
	int32_t                   s_nr_active_items;

	/** list of items that can be sent through any available slot.
	    items are placed using m0_rpc_item::ri_unbound_link
	    @deprecated XXX
	 */
	struct m0_list            s_unbound_items;

	/** Capacity of slot table */
	uint32_t                  s_slot_table_capacity;

	/**
	 * Only [0, s_nr_slots) slots from the s_slot_table can be used to bind
	 * items.  s_nr_slots <= s_slot_table_capacity
	 *
	 * Each slot is serial: the next fop is sent only once the reply to the
	 * previous one has been received.  Hence, ->s_nr_slots should be equal
	 * to the expected concurrency level.
	 */
	uint32_t                  s_nr_slots;

	/** Array of pointers to slots */
	struct m0_rpc_slot      **s_slot_table;

	/** if > 0, then session is in BUSY state */
	uint32_t                  s_hold_cnt;

	/** List of slots, which can be associated with an unbound item.
	    Link: m0_rpc_slot::sl_link
	 */
	struct m0_tl              s_ready_slots;

	/** RPC session state machine
	    @see m0_rpc_session_state, session_conf
	 */
	struct m0_sm		  s_sm;

	/** M0_RPC_SESSION_MAGIC */
	uint64_t		  s_magic;
};

/**
   Initialises all fields of session. Allocates and initialises
   nr_slots number of slots.
   No network communication is involved.

   @param session session being initialised
   @param conn rpc connection with which this session is associated
   @param nr_slots number of slots in the session

   @post ergo(rc == 0, session_state(session) == M0_RPC_SESSION_INITIALISED &&
		       session->s_conn == conn &&
		       session->s_session_id == SESSION_ID_INVALID)
 */
M0_INTERNAL int m0_rpc_session_init(struct m0_rpc_session *session,
				    struct m0_rpc_conn *conn,
				    uint32_t nr_slots);

/**
    Sends a SESSION_ESTABLISH fop across pre-defined session-0 in
    session->s_conn. Use m0_rpc_session_timedwait() to wait
    until session reaches IDLE or FAILED state.

    @pre session_state(session) == M0_RPC_SESSION_INITIALISED
    @pre conn_state(session->s_conn) == M0_RPC_CONN_ACTIVE
    @post ergo(result != 0, session_state(session) == M0_RPC_SESSION_FAILED)
 */
M0_INTERNAL int m0_rpc_session_establish(struct m0_rpc_session *session,
					 m0_time_t abs_timeout);

/**
 * Same as m0_rpc_session_establish(), but in addition uses
 * m0_rpc_session_timedwait() to ensure that session is in idle state after
 * m0_rpc_session_establish() call.
 *
 * @param session     A session object to operate on.
 * @param abs_timeout Absolute time after which session establish operation
 *                    is aborted and session is moved to FAILED state.
 *
 * @pre  session_state(session) == M0_RPC_SESSION_INITIALISED
 * @pre  conn_state(session->s_conn) == M0_RPC_CONN_ACTIVE
 * @post session_state(session) == M0_RPC_SESSION_IDLE
 */
M0_INTERNAL int m0_rpc_session_establish_sync(struct m0_rpc_session *session,
					      m0_time_t abs_timeout);

/**
 * A combination of m0_rpc_session_init() and m0_rpc_session_establish_sync() in
 * a single routine - initialize session object, establish a session and wait
 * until it become idle.
 *
 * @see m0_rpc_session::s_nr_slots
 */
M0_INTERNAL int m0_rpc_session_create(struct m0_rpc_session *session,
				      struct m0_rpc_conn *conn,
				      uint32_t nr_slots, m0_time_t abs_timeout);

/**
   Sends terminate session fop to receiver.
   Acts as no-op if session is already in TERMINATING state.
   Does not wait for reply. Use m0_rpc_session_timedwait() to wait
   until session reaches TERMINATED or FAILED state.

   @pre M0_IN(session_state(session), (M0_RPC_SESSION_IDLE,
				       M0_RPC_SESSION_TERMINATING))
   @post ergo(rc != 0, session_state(session) == M0_RPC_SESSION_FAILED)
 */
M0_INTERNAL int m0_rpc_session_terminate(struct m0_rpc_session *session,
					 m0_time_t abs_timeout);

/**
 * Same as m0_rpc_session_terminate(), but in addition uses
 * m0_rpc_session_timedwait() to ensure that session is in terminated state
 * after m0_rpc_session_terminate() call.
 *
 * @param session     A session object to operate on.
 * @param abs_timeout Absolute time after which session terminate operation
 *                    is considered as failed and session is moved to
 *                    FAILED state.
 *
 * @pre M0_IN(session_state(session), (M0_RPC_SESSION_IDLE,
 *				       M0_RPC_SESSION_BUSY,
 *				       M0_RPC_SESSION_TERMINATING))
 * @post M0_IN(session_state(session), (M0_RPC_SESSION_TERMINATED,
 *					M0_RPC_SESSION_FAILED))
 */
M0_INTERNAL int m0_rpc_session_terminate_sync(struct m0_rpc_session *session,
					      m0_time_t abs_timeout);

/**
    Waits until @session object reaches in one of states given by @state_flags.

    @param states can specify multiple states by using M0_BITS()
    @param abs_timeout thread does not sleep past abs_timeout waiting for conn
		to reach in desired state.
    @return 0 if session reaches in one of the state(s) specified by
		@state_flags
            -ETIMEDOUT if time out has occured before session reaches in
                desired state.
 */
M0_INTERNAL int m0_rpc_session_timedwait(struct m0_rpc_session *session,
					 uint64_t states,
					 const m0_time_t abs_timeout);

/**
   Finalises session object

   @pre M0_IN(session_state(session), (M0_RPC_SESSION_TERMINATED,
				       M0_RPC_SESSION_FAILED,
				       M0_RPC_SESSION_INITIALISED))
 */
M0_INTERNAL void m0_rpc_session_fini(struct m0_rpc_session *session);

/**
 * A combination of m0_rpc_session_terminate_sync() and m0_rpc_session_fini()
 * in a single routine - terminate the session, wait until it switched to
 * terminated state and finalize session object.
 */
int m0_rpc_session_destroy(struct m0_rpc_session *session,
			   m0_time_t abs_timeout);

/**
   Returns maximum size of an RPC item allowed on this session.
 */
M0_INTERNAL m0_bcount_t
m0_rpc_session_get_max_item_size(const struct m0_rpc_session *session);

/** Returns maximum possible size of RPC item payload. */
M0_INTERNAL m0_bcount_t
m0_rpc_session_get_max_item_payload_size(const struct m0_rpc_session *session);

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
