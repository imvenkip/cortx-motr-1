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
 * Original author: Amit_Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 05/02/2011
 */

#pragma once

#ifndef __COLIBRI_RPC_SESSION_INT_H__
#define __COLIBRI_RPC_SESSION_INT_H__

#include "rpc/session.h"

/**
   @addtogroup rpc_session

   @{
 */

/* Imports */
struct c2_db_tx;
struct c2_rpc_item;

enum {
	SESSION_COB_MAX_NAME_LEN = 40
};

/**
   checks internal consistency of session
 */
C2_INTERNAL bool c2_rpc_session_invariant(const struct c2_rpc_session *session);

/**
   Holds a session in BUSY state.
   Every call to c2_rpc_session_hold_busy() must accompany
   call to c2_rpc_session_release()

   @pre C2_IN(session_state(session), (C2_RPC_SESSION_IDLE,
				       C2_RPC_SESSION_BUSY))
   @pre c2_rpc_machine_is_locked(session_machine(session))
   @post session_state(session) == C2_RPC_SESSION_BUSY
 */
C2_INTERNAL void c2_rpc_session_hold_busy(struct c2_rpc_session *session);

/**
   Decrements hold count. Moves session to IDLE state if it becomes idle.

   @pre session_state(session) == C2_RPC_SESSION_BUSY
   @pre session->s_hold_cnt > 0
   @pre c2_rpc_machine_is_locked(session_machine(session))
   @post ergo(c2_rpc_session_is_idle(session),
	      session_state(session) == C2_RPC_SESSION_IDLE)
 */
C2_INTERNAL void c2_rpc_session_release(struct c2_rpc_session *session);

C2_INTERNAL void session_state_set(struct c2_rpc_session *session, int state);
C2_INTERNAL int session_state(const struct c2_rpc_session *session);

C2_INTERNAL int c2_rpc_session_init_locked(struct c2_rpc_session *session,
					   struct c2_rpc_conn *conn,
					   uint32_t nr_slots);
C2_INTERNAL void c2_rpc_session_fini_locked(struct c2_rpc_session *session);

/**
   Generates UUID
 */
C2_INTERNAL uint64_t uuid_generate(void);

/**
   Lookup for a cob named "SESSION_$session_id" that represents rpc session
   within a given @conn_cob (cob that identifies rpc connection)
 */
C2_INTERNAL int c2_rpc_session_cob_lookup(struct c2_cob *conn_cob,
					  uint64_t session_id,
					  struct c2_cob **session_cob,
					  struct c2_db_tx *tx);

/**
   Creates a cob named "SESSION_$session_id" that represents rpc session
   within a given @conn_cob (cob that identifies rpc connection)
 */
C2_INTERNAL int c2_rpc_session_cob_create(struct c2_cob *conn_cob,
					  uint64_t session_id,
					  struct c2_cob **session_cob,
					  struct c2_db_tx *tx);

/**
   Creates receiver end of session object.

   @pre session->s_state == C2_RPC_SESSION_INITIALISED &&
	session->s_conn != NULL
   @post ergo(result == 0, session->s_state == C2_RPC_SESSION_ALIVE)
   @post ergo(result != 0, session->s_state == C2_RPC_SESSION_FAILED)
 */
C2_INTERNAL int c2_rpc_rcv_session_establish(struct c2_rpc_session *session);

/**
   Terminates receiver end of session.

   @pre session->s_state == C2_RPC_SESSION_IDLE
   @post ergo(result == 0, session->s_state == C2_RPC_SESSION_TERMINATED)
   @post ergo(result != 0 && session->s_rc != 0, session->s_state ==
	      C2_RPC_SESSION_FAILED)
 */
C2_INTERNAL int c2_rpc_rcv_session_terminate(struct c2_rpc_session *session);

/**
   Callback routine called through item->ri_ops->rio_replied().

   The routine is executed when reply to session create fop is received
 */
C2_INTERNAL void c2_rpc_session_establish_reply_received(struct c2_rpc_item
							 *req);

/**
   Callback routine called through item->ri_ops->rio_replied().

   The routine is executed when reply to session terminate fop is received
 */
C2_INTERNAL void c2_rpc_session_terminate_reply_received(struct c2_rpc_item
							 *req);

/**
   For all slots belonging to @session,
     if slot is in c2_rpc_machine::rm_ready_slots list,
     then remove it from the list.
 */
C2_INTERNAL void c2_rpc_session_del_slots_from_ready_list(struct c2_rpc_session
							  *session);

C2_INTERNAL bool c2_rpc_session_is_idle(const struct c2_rpc_session *session);

C2_INTERNAL bool c2_rpc_session_bind_item(struct c2_rpc_item *item);

C2_INTERNAL void c2_rpc_session_item_failed(struct c2_rpc_item *item);

C2_INTERNAL void c2_rpc_session_mod_nr_active_items(struct c2_rpc_session
						    *session, int delta);

C2_INTERNAL struct c2_rpc_machine *session_machine(const struct c2_rpc_session
						   *s);

C2_TL_DESCR_DECLARE(rpc_session, C2_EXTERN);
C2_TL_DECLARE(rpc_session, C2_INTERNAL, struct c2_rpc_session);

/** @}  End of rpc_session group */
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
