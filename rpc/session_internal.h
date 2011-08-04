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

/**
   Initialise all the session related fop types
 */
int c2_rpc_session_module_init(void);

/**
   Fini all session realted fop types
 */
void c2_rpc_session_module_fini(struct c2_rpcmachine *machine);

enum {
	SESSION_COB_MAX_NAME_LEN = 40
};

/**
   checks internal consistency of session
 */
bool c2_rpc_session_invariant(const struct c2_rpc_session *session);

/**
   Search for a session with given @session_id in rpc connection conn.

   If found *out contains pointer to session object else *out is set to NULL
 */
void c2_rpc_session_search(const struct c2_rpc_conn	*conn,
			   uint64_t			session_id,
			   struct c2_rpc_session	**out);

/**
   Generate UUID
 */
void c2_rpc_sender_uuid_generate(struct c2_rpc_sender_uuid *u);

/**
   3WAY comparison function for UUID
 */
int c2_rpc_sender_uuid_cmp(const struct c2_rpc_sender_uuid *u1,
			   const struct c2_rpc_sender_uuid *u2);

/**
   Initialise in memory slot.

   @post ergo(result == 0, slot->sl_verno.vn_vc == 0 &&
			   slot->sl_xid == 1 &&
			   !c2_list_is_empty(slot->sl_item_list) &&
			   slot->sl_ops == ops)
 */
int c2_rpc_slot_init(struct c2_rpc_slot			*slot,
		     const struct c2_rpc_slot_ops	*ops);

/**
   If verno of item matches with verno of slot, then adds the item
   to the slot->sl_item_list. If item is update opeation, verno of
   slot is advanced. if item is already present in slot->sl_item_list
   its reply is immediately consumed.
 */
int c2_rpc_slot_item_apply(struct c2_rpc_slot	*slot,
			   struct c2_rpc_item	*item);

/**
   Called when a reply for an item which was sent on this slot.
   req_out will contain pointer to original item for which this is reply
 */
void c2_rpc_slot_reply_received(struct c2_rpc_slot	*slot,
				struct c2_rpc_item	*reply,
				struct c2_rpc_item	**req_out);

/**
   Effects of item with verno less than or equal to @last_pesistent, have
   been made persistent

   @post c2_verno_cmp(&slot->sl_last_persistent->ri_slot_refs[0].sr_verno,
		      &last_persistent) == 0
 */
void c2_rpc_slot_persistence(struct c2_rpc_slot	*slot,
			     struct c2_verno	last_persistent);

int c2_rpc_slot_misordered_item_received(struct c2_rpc_slot     *slot,
                                         struct c2_rpc_item     *item);

/**
   Reset the slot to verno @last_seen
   @post c2_verno_cmp(&slot->sl_last_sent->ri_slot_refs[0].sr_verno,
		      &last_seen) = 0
 */
void c2_rpc_slot_reset(struct c2_rpc_slot	*slot,
		       struct c2_verno		last_seen);

void c2_rpc_slot_fini(struct c2_rpc_slot	*slot);

bool c2_rpc_slot_invariant(const struct c2_rpc_slot *slot);

/**
   Helper to create cob

   @param dom cob domain in which cob should be created.
   @param pcob parent cob in which new cob is to be created
   @param name name of cob
   @param out newly created cob
   @param tx transaction context

   @return 0 on success. *out != NULL
 */

int c2_rpc_cob_create_helper(struct c2_cob_domain	*dom,
			     struct c2_cob		*pcob,
			     const char			*name,
			     struct c2_cob		**out,
			     struct c2_db_tx		*tx);

#define COB_GET_PFID_HI(cob)    (cob)->co_nsrec.cnr_stobid.si_bits.u_hi
#define COB_GET_PFID_LO(cob)    (cob)->co_nsrec.cnr_stobid.si_bits.u_lo

/**
   Lookup a cob named @name in parent cob @pcob. If found store reference
   in @out. If not found set *out to NULL. To lookup root cob, pcob can be
   set to NULL
 */
int c2_rpc_cob_lookup_helper(struct c2_cob_domain	*dom,
			     struct c2_cob		*pcob,
			     const char			*name,
			     struct c2_cob		**out,
			     struct c2_db_tx		*tx);

/**
  Lookup /SESSIONS entry in cob namespace
 */
int c2_rpc_root_session_cob_get(struct c2_cob_domain	*dom,
				 struct c2_cob		**out,
				 struct c2_db_tx	*tx);

/**
  Create /SESSIONS entry in cob namespace
 */
int c2_rpc_root_session_cob_create(struct c2_cob_domain	*dom,
				   struct c2_cob	**out,
				   struct c2_db_tx	*tx);

/**
   Lookup for a cob that represents rpc connection with given @sender_id.

   Searches for /SESSIONS/SENDER_$sender_id
 */
int c2_rpc_conn_cob_lookup(struct c2_cob_domain	*dom,
			   uint64_t		sender_id,
			   struct c2_cob	**out,
			   struct c2_db_tx	*tx);

/**
   Create a cob that represents rpc connection with given @sender_id

   Create a cob /SESSIONS/SENDER_$sender_id
 */
int c2_rpc_conn_cob_create(struct c2_cob_domain	*dom,
			   uint64_t		sender_id,
			   struct c2_cob	**out,
			   struct c2_db_tx	*tx);

/**
   Lookup for a cob named "SESSION_$session_id" that represents rpc session
   within a given @conn_cob (cob that identifies rpc connection)
 */
int c2_rpc_session_cob_lookup(struct c2_cob	*conn_cob,
			      uint64_t		session_id,
			      struct c2_cob	**session_cob,
			      struct c2_db_tx	*tx);

/**
   Create a cob named "SESSION_$session_id" that represents rpc session
   within a given @conn_cob (cob that identifies rpc connection)
 */
int c2_rpc_session_cob_create(struct c2_cob	*conn_cob,
			      uint64_t		session_id,
			      struct c2_cob	**session_cob,
			      struct c2_db_tx	*tx);

/**
   Lookup for a cob named "SLOT_$slot_id:$slot_generation" in @session_cob
 */
int c2_rpc_slot_cob_lookup(struct c2_cob	*session_cob,
			   uint32_t		slot_id,
			   uint64_t		slot_generation,
			   struct c2_cob	**slot_cob,
			   struct c2_db_tx	*tx);

/**
   Create a cob named "SLOT_$slot_id:$slot_generation" in @session_cob
 */
int c2_rpc_slot_cob_create(struct c2_cob	*session_cob,
			   uint32_t		slot_id,
			   uint64_t		slot_generation,
			   struct c2_cob	**slot_cob,
			   struct c2_db_tx	*tx);

/**
   Initalise receiver end of conn object.
   @pre conn->c_state == C2_RPC_CONN_UNINITIALISED
   @post ergo(result == 0, conn->c_state == C2_RPC_CONN_INITIALISED &&
			   conn->c_rpcmachine == machine &&
			   conn->c_sender_id == SENDER_ID_INVALID &&
			   (conn->c_flags & RCF_RECV_END) != 0)
 */
int c2_rpc_rcv_conn_init(struct c2_rpc_conn		 *conn,
			 const struct c2_rpcmachine	 *machine,
			 const struct c2_rpc_sender_uuid *uuid);
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

   Right now this function is not called from anywhere. There
   should be ->item_sent() callback in item->ri_ops, where this
   function can be hooked.

   @pre conn->c_state == C2_RPC_CONN_TERMINATING
 */
void conn_terminate_reply_sent(struct c2_rpc_conn *conn);

/**
   slot_ref object establishes association between c2_rpc_item and
   c2_rpc_slot. Upto MAX_SLOT_REF number of c2_rpc_slot_ref objects are
   embeded with c2_rpc_item.
   At the time item is associated with a slot, values of few slot fields are
   copied into slot_ref.
 */
struct c2_rpc_slot_ref {
	/** sr_slot and sr_item identify two ends of association */
	struct c2_rpc_slot		*sr_slot;
	struct c2_rpc_item		*sr_item;
	struct c2_rpc_sender_uuid	 sr_uuid;
	uint64_t			 sr_sender_id;
	uint64_t			 sr_session_id;
	/** Numeric id of slot. Used when encoding and decoding rpc item to
	    and from wire-format */
	uint32_t			 sr_slot_id;
	/** If slot has verno matching sr_verno, then only the item can be
	    APPLIED to the slot */
	struct c2_verno			 sr_verno;
	/** In each reply item, receiver reports to sender, verno of item
	    whose effects have reached persistent storage, using this field */
	struct c2_verno			 sr_last_persistent_verno;
	/** Inform the sender about current slot version */
	struct c2_verno			 sr_last_seen_verno;
	/** An identifier that uniquely identifies item within
	    slot->item_list.
	    XXX should we rename it to something like "item_id"?
		(somehow the name "xid" gives illusion that it is related to
		 some transaction identifier)
        */
	uint64_t			 sr_xid;
	/** Generation number of slot */
	uint64_t			 sr_slot_gen;
	/** Anchor to put item on c2_rpc_slot::sl_item_list */
	struct c2_list_link		 sr_link;
	/** Anchor to put item on c2_rpc_slot::sl_ready_list */
	struct c2_list_link		 sr_ready_link;
};

/**
   Called for each received item.
   If item is request then
	APPLY the item to proper slot
   else
	report REPLY_RECEIVED to appropriate slot
 */
int c2_rpc_item_received(struct c2_rpc_item *item);

/**
   Adds an item to slot->sl_item_list, without triggering
   any slot related events i.e. slot->ops->consume_item()
 */
void c2_rpc_slot_item_add_internal(struct c2_rpc_slot *slot,
				   struct c2_rpc_item *item);

/**
   Callback routine called through item->ri_ops->rio_replied().

   The routine is executed when reply to conn create fop is received
 */
void c2_rpc_conn_create_reply_received(struct c2_rpc_item *req,
				       struct c2_rpc_item *reply,
				       int		   rc);

/**
   Callback routine called through item->ri_ops->rio_replied().

   The routine is executed when reply to conn terminate fop is received
 */
void c2_rpc_conn_terminate_reply_received(struct c2_rpc_item *req,
					  struct c2_rpc_item *reply,
					  int		      rc);

/**
   Callback routine called through item->ri_ops->rio_replied().

   The routine is executed when reply to session create fop is received
 */
void c2_rpc_session_create_reply_received(struct c2_rpc_item *req,
					  struct c2_rpc_item *reply,
					  int		      rc);

/**
   Callback routine called through item->ri_ops->rio_replied().

   The routine is executed when reply to session terminate fop is received
 */
void c2_rpc_session_terminate_reply_received(struct c2_rpc_item	*req,
					     struct c2_rpc_item	*reply,
					     int		 rc);
/**
  A callback called when conn terminate reply has been put on network.
  Finalizes and frees conn.

  @pre conn->c_state == C2_RPC_CONN_TERMINATING
 */
void c2_rpc_conn_terminate_reply_sent(struct c2_rpc_conn *conn);


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

