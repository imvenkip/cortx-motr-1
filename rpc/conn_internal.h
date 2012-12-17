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

#ifndef __MERO_RPC_CONN_INT_H__
#define __MERO_RPC_CONN_INT_H__

#include "rpc/conn.h"

/* Imports */
struct m0_rpc_session;
struct m0_db_tx;
struct m0_rpc_item;
struct m0_cob_domain;

/**
   @addtogroup rpc_session

   @{
 */

enum {
	SENDER_ID_INVALID = UINT64_MAX,
};

M0_INTERNAL bool m0_rpc_conn_invariant(const struct m0_rpc_conn *conn);

static inline int conn_state(const struct m0_rpc_conn *conn)
{
	return conn->c_sm.sm_state;
}

/**
   Searches in conn->c_sessions list, a session object whose session id
   matches with given @session_id.

   Caller is expected to decide whether conn will be locked or not
   The function is also called from session_foms.c, that's why is not static.

   @return pointer to session if found, NULL otherwise
   @post ergo(result != NULL, result->s_session_id == session_id)
 */
M0_INTERNAL struct m0_rpc_session *m0_rpc_session_search(const struct
							 m0_rpc_conn *conn,
							 uint64_t session_id);

/**
   Searches and returns session with session_id 0.
   Each rpc connection always has exactly one instance of session with
   SESSION_ID_0 in its c_sessions list.

   @post result != NULL && result->s_session_id == SESSION_ID_0
 */
M0_INTERNAL struct m0_rpc_session *m0_rpc_conn_session0(const struct m0_rpc_conn
							*conn);

M0_INTERNAL void m0_rpc_conn_fini_locked(struct m0_rpc_conn *conn);

M0_INTERNAL void m0_rpc_sender_uuid_get(struct m0_rpc_sender_uuid *u);

/**
   3WAY comparison function for UUID
 */
M0_INTERNAL int m0_rpc_sender_uuid_cmp(const struct m0_rpc_sender_uuid *u1,
				       const struct m0_rpc_sender_uuid *u2);

/**
   Lookup for a cob that represents rpc connection with given @sender_id.

   Searches for /SESSIONS/SENDER_$sender_id
 */
M0_INTERNAL int m0_rpc_conn_cob_lookup(struct m0_cob_domain *dom,
				       uint64_t sender_id,
				       struct m0_cob **out,
				       struct m0_db_tx *tx);

/**
   Creates a cob that represents rpc connection with given @sender_id

   Creates a cob /SESSIONS/SENDER_$sender_id
 */
M0_INTERNAL int m0_rpc_conn_cob_create(struct m0_cob_domain *dom,
				       uint64_t sender_id,
				       struct m0_cob **out,
				       struct m0_db_tx *tx);

/**
   Initalises receiver end of conn object.

   @post ergo(result == 0, conn_state(conn) == M0_RPC_CONN_INITIALISED &&
			   conn->c_rpc_machine == machine &&
			   conn->c_sender_id == SENDER_ID_INVALID &&
			   (conn->c_flags & RCF_RECV_END) != 0)
 */
M0_INTERNAL int m0_rpc_rcv_conn_init(struct m0_rpc_conn *conn,
				     struct m0_net_end_point *ep,
				     struct m0_rpc_machine *machine,
				     const struct m0_rpc_sender_uuid *uuid);
/**
   Creates a receiver end of conn.

   @pre conn_state(conn) == M0_RPC_CONN_INITIALISED
   @post ergo(result == 0, conn_state(conn) == M0_RPC_CONN_ACTIVE &&
			   conn->c_sender_id != SENDER_ID_INVALID &&
			   m0_list_contains(&machine->rm_incoming_conns,
					    &conn->c_link)
   @post ergo(result != 0, conn_state(conn) == M0_RPC_CONN_FAILED)
   @post ergo(result == 0, conn_state(conn) == M0_RPC_CONN_ACTIVE)
 */
M0_INTERNAL int m0_rpc_rcv_conn_establish(struct m0_rpc_conn *conn);

/**
   Terminates receiver end of rpc connection.

   @pre conn_state(conn) == M0_RPC_CONN_ACTIVE && conn->c_nr_sessions == 0
   @post ergo(result == 0, conn_state(conn) == M0_RPC_CONN_TERMINATING)
   @post ergo(result != 0 && result != -EBUSY,
		conn_state(conn) == M0_RPC_CONN_FAILED)
 */
M0_INTERNAL int m0_rpc_rcv_conn_terminate(struct m0_rpc_conn *conn);

/**
   Callback routine called through item->ri_ops->rio_replied().

   The routine is executed when reply to conn create fop is received
 */
M0_INTERNAL void m0_rpc_conn_establish_reply_received(struct m0_rpc_item *req);

/**
   Cleans up in memory state of rpc connection.

   XXX Right now this function is not called from anywhere. There
   should be ->item_sent() callback in item->ri_ops, where this
   function can be hooked.

   The conn_terminate FOM cannot free in-memory state of rpc connection.
   Because it needs to send conn_terminate_reply fop, by using session-0 and
   slot-0 of the rpc connection being terminated. Hence we cleanup in memory
   state of the conn when conn_terminate_reply has been sent.

   @pre conn_state(conn) == M0_RPC_CONN_TERMINATING
 */
M0_INTERNAL void m0_rpc_conn_terminate_reply_sent(struct m0_rpc_conn *conn);

/**
   Callback routine called through item->ri_ops->rio_replied().

   The routine is executed when reply to conn terminate fop is received
 */
M0_INTERNAL void m0_rpc_conn_terminate_reply_received(struct m0_rpc_item *req);

/**
   Returns true iff given rpc item is conn_establish.
 */
M0_INTERNAL bool m0_rpc_item_is_conn_establish(const struct m0_rpc_item *item);

/**
   Returns true iff given rpc item is conn_terminate.
 */
M0_INTERNAL bool m0_rpc_item_is_conn_terminate(const struct m0_rpc_item *item);

/**
   @see m0_rpc_fop_conn_establish_ctx for more information.
 */
M0_INTERNAL void m0_rpc_fop_conn_establish_ctx_init(struct m0_rpc_item *item,
						  struct m0_net_end_point *ep);

/**
   Return true iff @conn is sender side object of rpc-connection.
 */
M0_INTERNAL bool m0_rpc_conn_is_snd(const struct m0_rpc_conn *conn);

/**
   Return true iff @conn is receiver side object of rpc-connection.
 */
M0_INTERNAL bool m0_rpc_conn_is_rcv(const struct m0_rpc_conn *conn);

M0_INTERNAL void m0_rpc_conn_add_session(struct m0_rpc_conn *conn,
					 struct m0_rpc_session *session);
M0_INTERNAL void m0_rpc_conn_remove_session(struct m0_rpc_session *session);

/** @}  End of rpc_session group */
#endif /* __MERO_RPC_CONN_INT_H__ */
