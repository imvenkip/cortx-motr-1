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
 * Original author: Amit Jambure <amit_jambure@xyratex.com>
 * Original creation date: 10/31/2012
 */

#pragma once

#ifndef __MERO_RPC_CONN_H__
#define __MERO_RPC_CONN_H__

#include "lib/tlist.h"
#include "lib/time.h"          /* m0_time_t */
#include "sm/sm.h"
#include "rpc/rpc_onwire.h"    /* m0_rpc_sender_uuid */

/* Imports */
struct m0_rpc_machine;
struct m0_rpc_service;
struct m0_rpc_chan;
struct m0_net_end_point;

/* Exports */
struct m0_rpc_conn;

/**
   @addtogroup rpc_session

   @{
 */

enum m0_rpc_conn_state {
	/**
	  All the fields of conn are initialised locally. But the connection
	  is not yet established.
	 */
	M0_RPC_CONN_INITIALISED,

	/**
	   Connection establish request is sent to receiver but its reply is
	   not yet received.
	 */
	M0_RPC_CONN_CONNECTING,

	/**
	   Receiver replied with a successful connection establish reply.
	   Connection is established and ready to be used.
	 */
	M0_RPC_CONN_ACTIVE,

	/**
	   If conn init or terminate fails or time-outs connection enters in
	   FAILED state. m0_rpc_conn::c_sm::sm_rc contains reason for failure.
	*/
	M0_RPC_CONN_FAILED,

	/**
	   When sender calls m0_rpc_conn_terminate() on m0_rpc_conn object
	   a FOP is sent to the receiver side to terminate the rpc connection.
	   Until reply is received, m0_rpc_conn object stays in TERMINATING
	   state
	 */
	M0_RPC_CONN_TERMINATING,

	/**
	   When sender receives reply for conn_terminate FOP and reply FOP
	   specifies the conn_terminate operation is successful then
	   the object of m0_rpc_conn enters in TERMINATED state
	 */
	M0_RPC_CONN_TERMINATED,

	/** After m0_rpc_conn_fini() the RPC connection instance is moved to
	    FINALISED state.
	 */
	M0_RPC_CONN_FINALISED,
};

/**
   RPC Connection flags @see m0_rpc_conn::c_flags
 */
enum m0_rpc_conn_flags {
	RCF_SENDER_END = 1 << 0,
	RCF_RECV_END   = 1 << 1
};

/**
   A rpc connection identifies a sender to the receiver. It acts as a parent
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

   m0_rpc_machine maintains two lists of m0_rpc_conn
   - rm_outgoing_conns: list of m0_rpc_conn objects for which this node is
     sender
   - rm_incoming_conns: list of m0_rpc_conn object for which this node is
     receiver

   Instance of m0_rpc_conn stores a list of all sessions currently active with
   the service.

   At the time of creation of a m0_rpc_conn, a "special" session with
   SESSION_ID_0 is also created. It is special in the sense that it is
   "hand-made" and there is no need to communicate to receiver in order to
   create this session. Receiver assumes that there always exists a session 0
   for each rpc connection.
   Receiver creates session 0 while creating the rpc connection itself.
   Session 0 is required to send special fops like
   - conn_establish or conn_terminate FOP
   - session_establish or session_terminate FOP.

   <B> State transition diagram: </B>

   @verbatim
                                    | m0_rpc_conn_init()
   m0_rpc_conn_establish() != 0     V
         +---------------------INITIALISED
         |                          |
         |                          |  m0_rpc_conn_establish()
         |                          |
         |                          V
         +---------------------- CONNECTING
         | time-out ||              |
         |     reply.rc != 0        | m0_rpc_conn_establish_reply_received() &&
         |                          |    reply.rc == 0
         V                          |
       FAILED                       |
         |  ^                       V
         |  |                    ACTIVE
         |  |                       |
         |  |                       | m0_rpc_conn_terminate()
         |  | failed || timeout     |
         |  |                       V
         |  +-------------------TERMINATING
	 |                          |
         |                          | m0_rpc_conn_terminate_reply_received() &&
         |                          |              rc== 0
	 |                          V
	 |			TERMINATED
	 |                          |
	 |m0_rpc_conn_fini()        V  m0_rpc_conn_fini()
	 +--------------------->FINALISED

  @endverbatim

  <B> Liveness and Concurrency: </B>
  - Sender side allocation and deallocation of m0_rpc_conn object is
    entirely handled by user (m0_rpc_conn object is not reference counted).
  - On receiver side, user is not expected to allocate or deallocate
    m0_rpc_conn objects explicitly.
  - Receiver side m0_rpc_conn object will be instantiated in response to
    rpc connection establish request and is deallocated while terminating the
    rpc connection.
  - Access to m0_rpc_conn is synchronized with
    conn->c_rpc_machine->rm_sm_grp.s_lock.

  <B> Typical sequence of API execution </B>
  Note: error checking is omitted.

  @code
  // ALLOCATE CONN
  struct m0_rpc_conn *conn;
  M0_ALLOC_PTR(conn);

  // INITIALISE CONN
  rc = m0_rpc_conn_init(conn, tgt_end_point, rpc_machine);
  M0_ASSERT(ergo(rc == 0, conn_state(conn) == M0_RPC_CONN_INITIALISED));

  // ESTABLISH RPC CONNECTION
  rc = m0_rpc_conn_establish(conn);

  if (rc != 0) {
	// some error occured. Cannot establish connection.
        // handle the situation and return
  }
  // WAIT UNTIL CONNECTION IS ESTABLISHED
  rc = m0_rpc_conn_timedwait(conn, M0_BITS(M0_RPC_CONN_ACTIVE,
					   M0_RPC_CONN_FAILED),
			     absolute_timeout);
  if (rc == 0) {
	if (conn_state(conn) == M0_RPC_CONN_ACTIVE)
		// connection is established and is ready to be used
	else
		// connection establishing failed
  } else {
	// timeout
  }
  // Assuming connection is established.
  // Create one or more sessions using this connection. @see m0_rpc_session

  // TERMINATING CONNECTION
  // Make sure that all the sessions that were created on this connection are
  // terminated
  M0_ASSERT(conn->c_nr_sessions == 0);

  rc = m0_rpc_conn_terminate(conn);

  // WAIT UNTIL CONNECTION IS TERMINATED
  rc = m0_rpc_conn_timedwait(conn, M0_BITS(M0_RPC_CONN_TERMINATED,
					   M0_RPC_CONN_FAILED),
			     absolute_timeout);
  if (rc == 0) {
	if (conn_state(conn) == M0_RPC_CONN_TERMINATED)
		// conn is successfully terminated
	else
		// conn terminate has failed
  } else {
	// timeout
  }
  // assuming conn is terminated
  m0_rpc_conn_fini(conn);
  m0_free(conn);

  @endcode

  On receiver side, user is not expected to call any of these APIs.
  Receiver side rpc-layer will internally allocate/deallocate and manage
  all the state transitions of conn internally.
 */
struct m0_rpc_conn {
	/** Sender ID, unique on receiver */
	uint64_t                  c_sender_id;

	/** Globally unique ID of rpc connection */
	struct m0_uint128         c_uuid;

	/** @see m0_rpc_conn_flags for list of flags */
	uint64_t                  c_flags;

	/** rpc_machine with which this conn is associated */
	struct m0_rpc_machine    *c_rpc_machine;

	/** list_link to put m0_rpc_conn in either
	    m0_rpc_machine::rm_incoming_conns or
	    m0_rpc_machine::rm_outgoing_conns.
	    List descriptor: rpc_conn
	 */
	struct m0_tlink           c_link;

	/** Counts number of sessions (excluding session 0) */
	uint64_t                  c_nr_sessions;

	/** List of all the sessions created under this rpc connection.
	    m0_rpc_session objects are placed in this list using
	    m0_rpc_session::s_link.
	    List descriptor: session
	 */
	struct m0_tl              c_sessions;

	/** List of m0_rpc_item_source instances.
	    List link: m0_rpc_item_source::ris_tlink
	    List descriptor: item_source
	 */
	struct m0_tl              c_item_sources;

	/** Identifies destination of this connection. */
	struct m0_rpc_chan       *c_rpcchan;

	/** RPC connection state machine
	    @see m0_rpc_conn_state, conn_conf
	 */
	struct m0_sm		  c_sm;

	/**
	   Finalises and frees m0_rpc_conn when conn terminate
	   reply is sent over network.
	   Posted in: conn_terminate_reply_sent_cb()
	   Invokes: conn_cleanup_ast() callback
	 */
	struct m0_sm_ast          c_ast;

	/** M0_RPC_CONN_MAGIC */
	uint64_t		  c_magic;
};

/**
   Initialises @conn object and associates it with @machine.
   No network communication is involved.

   Note: m0_rpc_conn_init() can fail with -ENOMEM, -EINVAL.
	 if m0_rpc_conn_init() fails, conn is left in undefined state.

   @pre conn != NULL && ep != NULL && machine != NULL
   @post ergo(rc == 0, conn_state(conn) == M0_RPC_CONN_INITIALISED &&
			conn->c_machine == machine &&
			conn->c_sender_id == SENDER_ID_INVALID &&
			(conn->c_flags & RCF_SENDER_END) != 0)
 */
M0_INTERNAL int m0_rpc_conn_init(struct m0_rpc_conn *conn,
				 struct m0_net_end_point *ep,
				 struct m0_rpc_machine *machine,
				 uint64_t max_rpcs_in_flight);

/**
    Sends handshake CONN_ESTABLISH fop to the remote end.

    Use m0_rpc_conn_timedwait() to wait until conn moves to
    ESTABLISHED or FAILED state.

    @pre conn_state(conn) == M0_RPC_CONN_INITIALISED
    @post ergo(result != 0, conn_state(conn) == M0_RPC_CONN_FAILED)
 */
M0_INTERNAL int m0_rpc_conn_establish(struct m0_rpc_conn *conn,
				      m0_time_t abs_timeout);

/**
 * Same as m0_rpc_conn_establish(), but in addition uses m0_rpc_conn_timedwait()
 * to ensure that connection is in active state after m0_rpc_conn_establish()
 * call.
 *
 * @param conn        A connection object to operate on.
 * @param abs_timeout Absolute timeout after which connection establishing is
 *                    given up and conn is moved to FAILED state.
 *
 * @pre  conn_state(conn) == M0_RPC_CONN_INITIALISED
 * @post conn_state(conn) == M0_RPC_CONN_ACTIVE
 */
M0_INTERNAL int m0_rpc_conn_establish_sync(struct m0_rpc_conn *conn,
					   m0_time_t abs_timeout);

/**
 * A combination of m0_rpc_conn_init() and m0_rpc_conn_establish_sync() in a
 * single routine - initialize connection object, establish a connection and
 * wait until it become active.
 */
M0_INTERNAL int m0_rpc_conn_create(struct m0_rpc_conn *conn,
				   struct m0_net_end_point *ep,
				   struct m0_rpc_machine *rpc_machine,
				   uint64_t max_rpcs_in_flight,
				   m0_time_t abs_timeout);

/**
   Sends "conn_terminate" FOP to receiver.
   m0_rpc_conn_terminate() is a no-op if @conn is already in TERMINATING
   state.

   Use m0_rpc_conn_timedwait() to wait until conn is moved to TERMINATED
   or FAILED state.

   @pre (conn_state(conn) == M0_RPC_CONN_ACTIVE && conn->c_nr_sessions == 0 &&
	 conn->c_service == NULL) ||
	 conn_state(conn) == M0_RPC_CONN_TERMINATING
   @post ergo(rc != 0, conn_state(conn) == M0_RPC_CONN_FAILED)
 */
M0_INTERNAL int m0_rpc_conn_terminate(struct m0_rpc_conn *conn,
				      m0_time_t abs_timeout);

/**
 * Same as m0_rpc_conn_terminate(), but in addition uses m0_rpc_conn_timedwait()
 * to ensure that connection is in terminated state after m0_rpc_conn_terminate()
 * call.
 *
 * @param conn        A connection object to operate on.
 * @param abs_timeout Absolute time after which conn-terminate operation
 *                    considered as failed and conn is moved to FAILED state.
 *
 * @pre (conn_state(conn) == M0_RPC_CONN_ACTIVE && conn->c_nr_sessions == 0) ||
 *       conn_state(conn) == M0_RPC_CONN_TERMINATING
 * @post conn_state(conn) == M0_RPC_CONN_TERMINATED
 */
M0_INTERNAL int m0_rpc_conn_terminate_sync(struct m0_rpc_conn *conn,
					   m0_time_t abs_timeout);

/**
   Finalises m0_rpc_conn.
   No network communication involved.
   @pre conn_state(conn) == M0_RPC_CONN_FAILED ||
	conn_state(conn) == M0_RPC_CONN_INITIALISED ||
	conn_state(conn) == M0_RPC_CONN_TERMINATED
 */
M0_INTERNAL void m0_rpc_conn_fini(struct m0_rpc_conn *conn);

/**
 * A combination of m0_rpc_conn_terminate_sync() and m0_rpc_conn_fini() in a
 * single routine - terminate the connection, wait until it switched to
 * terminated state and finalize connection object.
 */
int m0_rpc_conn_destroy(struct m0_rpc_conn *conn, m0_time_t abs_timeout);

/**
    Waits until @conn reaches in any one of states specified by @states.

    @param state_flags can specify multiple states by using M0_BITS().

    @param abs_timeout should not sleep past abs_timeout waiting for conn
		to reach in desired state.
    @return 0 if @conn reaches in one of the state(s) specified by
                @state_flags
            -ETIMEDOUT if time out has occured before @conn reaches in desired
                state.
 */
M0_INTERNAL int m0_rpc_conn_timedwait(struct m0_rpc_conn *conn,
				      uint64_t states,
				      const m0_time_t abs_timeout);

/**
   Just for debugging purpose. Useful in gdb.

   dir = 1, to print incoming conn list
   dir = 0, to print outgoing conn list
 */
M0_INTERNAL int m0_rpc_machine_conn_list_dump(struct m0_rpc_machine *machine,
					      int dir);

M0_INTERNAL const char *m0_rpc_conn_addr(const struct m0_rpc_conn *conn);

/** @}  End of rpc_session group */
#endif /* __MERO_RPC_CONN_H__ */
