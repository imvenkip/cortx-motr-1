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
 * Original author: Amit_Jambure <Amit_Jambure@xyratex.com> 
 * Original creation date: 05/02/2011
 */

/* Declarations of functions that are private to rpc-layer */

#ifndef _COLIBRI_RPC_SESSION_INT_H
#define _COLIBRI_RPC_SESSION_INT_H

#include "cob/cob.h"
#include "rpc/session.h"
#include "dtm/verno.h"

/**
   @addtogroup rpc_session

   @{
 */
enum {
	/* XXX need to set some proper value to this constant */
	DEFAULT_SLOT_COUNT = 4
};

int c2_rpc_session_module_init(void);
void c2_rpc_session_module_fini(void);

int c2_rpc_cob_create_helper(struct c2_cob_domain	*dom,
			     struct c2_cob		*pcob,
			     char			*name,
			     struct c2_cob		**out,
			     struct c2_db_tx		*tx);

#define COB_GET_PFID_HI(cob)    (cob)->co_nsrec.cnr_stobid.si_bits.u_hi
#define COB_GET_PFID_LO(cob)    (cob)->co_nsrec.cnr_stobid.si_bits.u_lo

int c2_rpc_cob_lookup_helper(struct c2_cob_domain	*dom,
			     struct c2_cob		*pcob,
			     char			*name,
			     struct c2_cob		**out,
			     struct c2_db_tx		*tx);

int c2_rpc_root_session_cob_get(struct c2_cob_domain	*dom,
				 struct c2_cob		**out,
				 struct c2_db_tx	*tx);

int c2_rpc_root_session_cob_create(struct c2_cob_domain	*dom,
				   struct c2_cob	**out,
				   struct c2_db_tx	*tx);

int c2_rpc_conn_cob_lookup(struct c2_cob_domain	*dom,
			   uint64_t		sender_id,
			   struct c2_cob	**out,
			   struct c2_db_tx	*tx);

int c2_rpc_conn_cob_create(struct c2_cob_domain	*dom,
			   uint64_t		sender_id,
			   struct c2_cob	**out,
			   struct c2_db_tx	*tx);

int c2_rpc_session_cob_lookup(struct c2_cob	*conn_cob,
			      uint64_t		session_id,
			      struct c2_cob	**session_cob,
			      struct c2_db_tx	*tx);

int c2_rpc_session_cob_create(struct c2_cob	*conn_cob,
			      uint64_t		session_id,
			      struct c2_cob	**session_cob,
			      struct c2_db_tx	*tx);

int c2_rpc_slot_cob_lookup(struct c2_cob	*session_cob,
			   uint32_t		slot_id,
			   uint64_t		slot_generation,
			   struct c2_cob	**slot_cob,
			   struct c2_db_tx	*tx);

int c2_rpc_slot_cob_create(struct c2_cob	*session_cob,
			   uint32_t		slot_id,
			   uint64_t		slot_generation,
			   struct c2_cob	**slot_cob,
			   struct c2_db_tx	*tx);

int conn_persistent_state_create(struct c2_cob_domain   *dom,
				 uint64_t               sender_id,
				 struct c2_cob          **conn_cob_out,
				 struct c2_cob          **session0_cob_out,
				 struct c2_cob          **slot0_cob_out,
				 struct c2_db_tx        *tx);

int conn_persistent_state_attach(struct c2_rpc_conn	*conn,
				 uint64_t		sender_id,
				 struct c2_db_tx	*tx);

int session_persistent_state_create(struct c2_cob	*conn_cob,
				    uint64_t		session_id,
				    struct c2_cob	**session_cob_out,
				    struct c2_cob	**slot_cob_array_out,
				    uint32_t		nr_slots,
				    struct c2_db_tx	*tx);

int session_persistent_state_attach(struct c2_rpc_session	*session,
				   uint64_t			session_id,
				   struct c2_db_tx		*tx);

int session_persistent_state_destroy(struct c2_rpc_session	*session,
				     struct c2_db_tx		*tx);

/**
   Initalise receiver end of conn object.
   @pre conn->c_state == C2_RPC_CONN_UNINITIALISED
   @post ergo(result == 0, conn->c_state == C2_RPC_CONN_INITIALISED &&
			   conn->c_rpcmachine == machine &&
			   conn->c_sender_id == SENDER_ID_INVALID &&
			   (conn->c_flags & RCF_RECV_END) != 0)
 */
int c2_rpc_rcv_conn_init(struct c2_rpc_conn	   *conn,
			 struct c2_rpcmachine	   *machine,
			 struct c2_rpc_sender_uuid *uuid);
/**
   Creates a receiver end of conn.

   @arg ep for receiver side conn, ep is end point of sender.
   @pre conn->c_state == C2_RPC_CONN_INITIALISED
   @post ergo(result == 0, conn->c_state == C2_RPC_CONN_ACTIVE &&
			   conn->c_sender_id != SENDER_ID_INVALID &&
			   c2_list_contains(&machine->cr_incoming_conns,
					    &conn->c_link)
 */
int c2_rpc_rcv_conn_create(struct c2_rpc_conn		*conn,
			   struct c2_net_end_point	*ep);
/**
   @pre session->s_state == C2_RPC_SESSION_INITIALISED &&
	session->s_conn != NULL
   @post ergo(result == 0, session->s_state == C2_RPC_SESSION_ALIVE)
 */
int c2_rpc_rcv_session_create(struct c2_rpc_session	*session);

/**
   Terminate receiver end of session

   @pre session->s_state == C2_RPC_SESSION_IDLE
   @post ergo(result == 0, session->s_state == C2_RPC_SESSION_TERMINATED)
   @post ergo(result != 0 && session->s_rc != 0, session->s_state ==
	      C2_RPC_SESSION_FAILED)
 */
int c2_rpc_rcv_session_terminate(struct c2_rpc_session	*session);

/**
   Terminate receiver end of rpc connection

   @pre conn->c_state == C2_RPC_CONN_ACTIVE && conn->c_nr_sessions == 0
   @post ergo(result == 0, conn->c_state == C2_RPC_CONN_TERMINATING
 */
int c2_rpc_rcv_conn_terminate(struct c2_rpc_conn *conn);

/**
   Clean up in memory state of rpc connection

   @pre conn->c_state == C2_RPC_CONN_TERMINATING
 */
void conn_terminate_reply_sent(struct c2_rpc_conn *conn);

uint64_t c2_rpc_sender_id_get(void);
uint64_t c2_rpc_session_id_get(void);

struct c2_rpc_slot_ref {
	uint32_t		sr_slot_id;
	struct c2_verno		sr_verno;
	struct c2_verno		sr_last_persistent_verno;
	struct c2_verno		sr_last_seen_verno;
	uint64_t		sr_xid;
	uint64_t		sr_slot_gen;
	struct c2_rpc_slot	*sr_slot;
	struct c2_rpc_item	*sr_item;
	/** Anchor to put item on c2_rpc_slot::sl_item_list */
	struct c2_list_link	sr_link;
	/** Anchor to put item on c2_rpc_slot::sl_ready_list */
	struct c2_list_link	sr_ready_link;
};

int __conn_init(struct c2_rpc_conn	*conn,
		struct c2_rpcmachine	*machine);

void session_search(const struct c2_rpc_conn	*conn,
		    uint64_t		  	session_id,
		    struct c2_rpc_session 	**out);

/**
   Returns true if item is carrying CONN_CREATE fop.
 */
bool item_is_conn_create(struct c2_rpc_item  *item);
void dispatch_item_for_execution(struct c2_rpc_item *item);

/**
   Called for each received item.
   If item is request then
	APPLY the item to proper slot
   else
	report REPLY_RECEIVED to appropriate slot
 */
int c2_rpc_item_received(struct c2_rpc_item *item);

void c2_rpc_slot_item_add_internal(struct c2_rpc_slot *slot,
				   struct c2_rpc_item *item);

void c2_rpc_conn_create_reply_received(struct c2_rpc_item *req,
				       struct c2_rpc_item *reply,
				       int		   rc);

void c2_rpc_conn_terminate_reply_received(struct c2_rpc_item *req,
					  struct c2_rpc_item *reply,
					  int		      rc);

void c2_rpc_session_create_reply_received(struct c2_rpc_item *req,
					  struct c2_rpc_item *reply,
					  int		      rc);

void c2_rpc_session_terminate_reply_received(struct c2_rpc_item	*req,
					     struct c2_rpc_item	*reply,
					     int		 rc);
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

