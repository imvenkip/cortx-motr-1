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

/* Declarations of functions that are private to rpc-layer */

#pragma once

#ifndef __COLIBRI_RPC_SESSION_INT_H__
#define __COLIBRI_RPC_SESSION_INT_H__

#include "cob/cob.h"
#include "dtm/verno.h"
#include "rpc/session.h"
#include "rpc/rpc_onwire.h" /* c2_rpc_onwire_slot_ref */

/**
   @addtogroup rpc_session

   @{
 */

struct c2_rpc_item_ops;
struct c2_fop;

/**
   Initialises all the session related fop types
 */
int c2_rpc_session_module_init(void);

/**
   Finalises all session realted fop types
 */
void c2_rpc_session_module_fini(void);

enum {
	SESSION_COB_MAX_NAME_LEN = 40
};

/**
   checks internal consistency of session
 */
bool c2_rpc_session_invariant(const struct c2_rpc_session *session);

/**
   Searches in conn->c_sessions list, a session object whose session id
   matches with given @session_id.

   Caller is expected to decide whether conn will be locked or not
   The function is also called from session_foms.c, that's why is not static.

   @return pointer to session if found, NULL otherwise
   @post ergo(result != NULL, result->s_session_id == session_id)
 */
struct c2_rpc_session *c2_rpc_session_search(const struct c2_rpc_conn *conn,
					     uint64_t session_id);

/**
   Searches and returns session with session_id 0.
   Each rpc connection always has exactly one instance of session with
   SESSION_ID_0 in its c_sessions list.

   @post result != NULL && result->s_session_id == SESSION_ID_0
 */
struct c2_rpc_session *c2_rpc_conn_session0(const struct c2_rpc_conn *conn);

void c2_rpc_conn_fini_locked(struct c2_rpc_conn *conn);

int c2_rpc_session_init_locked(struct c2_rpc_session *session,
			       struct c2_rpc_conn    *conn,
			       uint32_t               nr_slots);
void c2_rpc_session_fini_locked(struct c2_rpc_session *session);

/**
   Generates UUID
 */
void c2_rpc_sender_uuid_generate(struct c2_rpc_sender_uuid *u);

/**
   3WAY comparison function for UUID
 */
int c2_rpc_sender_uuid_cmp(const struct c2_rpc_sender_uuid *u1,
			   const struct c2_rpc_sender_uuid *u2);

/**
   Initialises in memory slot.

   @post ergo(result == 0, slot->sl_verno.vn_vc == 0 &&
			   slot->sl_xid == 1 &&
			   !c2_list_is_empty(slot->sl_item_list) &&
			   slot->sl_ops == ops)
 */
int c2_rpc_slot_init(struct c2_rpc_slot           *slot,
		     const struct c2_rpc_slot_ops *ops);

/**
   If verno of item matches with verno of slot, then adds the item
   to the slot->sl_item_list. If item is update opeation, verno of
   slot is advanced. if item is already present in slot->sl_item_list
   its reply is immediately consumed.
 */
int c2_rpc_slot_item_apply(struct c2_rpc_slot *slot,
			   struct c2_rpc_item *item);

/**
   Called when a reply item is received for an item which was sent on this slot.

   Searches request item for which @reply is received. Marks the request item
   to be in state PAST_VOLATILE and calls slot->sl_ops->so_reply_consume()
   callback.
   req_out will contain pointer to original item for which this is reply.
   Takes care of duplicate replies. Sets *req_out to NULL if @reply is
   duplicate or unexpected.
 */
int c2_rpc_slot_reply_received(struct c2_rpc_slot  *slot,
			       struct c2_rpc_item  *reply,
			       struct c2_rpc_item **req_out);

/**
   Reports slot that effects of item with verno <= @last_pesistent, are
   persistent on receiver.

   @post c2_verno_cmp(&slot->sl_last_persistent->ri_slot_refs[0].sr_verno,
		      &last_persistent) >= 0
 */
void c2_rpc_slot_persistence(struct c2_rpc_slot *slot,
			     struct c2_verno     last_persistent);

void c2_rpc_slot_misordered_item_received(struct c2_rpc_slot *slot,
                                         struct c2_rpc_item *item);

/**
   Resets version number of slot to @last_seen
   @post c2_verno_cmp(&slot->sl_last_sent->ri_slot_refs[0].sr_verno,
		      &last_seen) == 0
 */
void c2_rpc_slot_reset(struct c2_rpc_slot *slot,
		       struct c2_verno     last_seen);

/**
   Finalises slot
 */
void c2_rpc_slot_fini(struct c2_rpc_slot *slot);

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

int c2_rpc_cob_create_helper(struct c2_cob_domain *dom,
			     struct c2_cob        *pcob,
			     const char           *name,
			     struct c2_cob       **out,
			     struct c2_db_tx      *tx);

/**
   Lookup a cob named 'name' in parent cob @pcob. If found store reference
   in @out. If not found set *out to NULL. To lookup root cob, pcob can be
   set to NULL
 */
int c2_rpc_cob_lookup_helper(struct c2_cob_domain *dom,
			     struct c2_cob        *pcob,
			     const char           *name,
			     struct c2_cob       **out,
			     struct c2_db_tx      *tx);

/**
  Lookup /SESSIONS entry in cob namespace
 */
int c2_rpc_root_session_cob_get(struct c2_cob_domain *dom,
				 struct c2_cob      **out,
				 struct c2_db_tx     *tx);

/**
  Creates /SESSIONS entry in cob namespace
 */
int c2_rpc_root_session_cob_create(struct c2_cob_domain *dom,
				   struct c2_db_tx      *tx);

/**
   Lookup for a cob that represents rpc connection with given @sender_id.

   Searches for /SESSIONS/SENDER_$sender_id
 */
int c2_rpc_conn_cob_lookup(struct c2_cob_domain *dom,
			   uint64_t              sender_id,
			   struct c2_cob       **out,
			   struct c2_db_tx      *tx);

/**
   Creates a cob that represents rpc connection with given @sender_id

   Creates a cob /SESSIONS/SENDER_$sender_id
 */
int c2_rpc_conn_cob_create(struct c2_cob_domain *dom,
			   uint64_t              sender_id,
			   struct c2_cob       **out,
			   struct c2_db_tx      *tx);

/**
   Lookup for a cob named "SESSION_$session_id" that represents rpc session
   within a given @conn_cob (cob that identifies rpc connection)
 */
int c2_rpc_session_cob_lookup(struct c2_cob   *conn_cob,
			      uint64_t         session_id,
			      struct c2_cob  **session_cob,
			      struct c2_db_tx *tx);

/**
   Creates a cob named "SESSION_$session_id" that represents rpc session
   within a given @conn_cob (cob that identifies rpc connection)
 */
int c2_rpc_session_cob_create(struct c2_cob   *conn_cob,
			      uint64_t         session_id,
			      struct c2_cob  **session_cob,
			      struct c2_db_tx *tx);

/**
   Lookup for a cob named "SLOT_$slot_id:$slot_generation" in @session_cob
 */
int c2_rpc_slot_cob_lookup(struct c2_cob   *session_cob,
			   uint32_t         slot_id,
			   uint64_t         slot_generation,
			   struct c2_cob  **slot_cob,
			   struct c2_db_tx *tx);

/**
   Creates a cob named "SLOT_$slot_id:$slot_generation" in @session_cob
 */
int c2_rpc_slot_cob_create(struct c2_cob   *session_cob,
			   uint32_t         slot_id,
			   uint64_t         slot_generation,
			   struct c2_cob  **slot_cob,
			   struct c2_db_tx *tx);

/**
   Initalises receiver end of conn object.

   @post ergo(result == 0, conn_state(conn) == C2_RPC_CONN_INITIALISED &&
			   conn->c_rpc_machine == machine &&
			   conn->c_sender_id == SENDER_ID_INVALID &&
			   (conn->c_flags & RCF_RECV_END) != 0)
 */
int c2_rpc_rcv_conn_init(struct c2_rpc_conn              *conn,
			 struct c2_net_end_point         *ep,
			 struct c2_rpc_machine           *machine,
			 const struct c2_rpc_sender_uuid *uuid);
/**
   Creates a receiver end of conn.

   @pre conn_state(conn) == C2_RPC_CONN_INITIALISED
   @post ergo(result == 0, conn_state(conn) == C2_RPC_CONN_ACTIVE &&
			   conn->c_sender_id != SENDER_ID_INVALID &&
			   c2_list_contains(&machine->rm_incoming_conns,
					    &conn->c_link)
   @post ergo(result != 0, conn_state(conn) == C2_RPC_CONN_FAILED)
   @post ergo(result == 0, conn_state(conn) == C2_RPC_CONN_ACTIVE)
 */
int c2_rpc_rcv_conn_establish(struct c2_rpc_conn *conn);

/**
   Creates receiver end of session object.

   @pre session->s_state == C2_RPC_SESSION_INITIALISED &&
	session->s_conn != NULL
   @post ergo(result == 0, session->s_state == C2_RPC_SESSION_ALIVE)
   @post ergo(result != 0, session->s_state == C2_RPC_SESSION_FAILED)
 */
int c2_rpc_rcv_session_establish(struct c2_rpc_session *session);

/**
   Terminates receiver end of session.

   @pre session->s_state == C2_RPC_SESSION_IDLE
   @post ergo(result == 0, session->s_state == C2_RPC_SESSION_TERMINATED)
   @post ergo(result != 0 && session->s_rc != 0, session->s_state ==
	      C2_RPC_SESSION_FAILED)
 */
int c2_rpc_rcv_session_terminate(struct c2_rpc_session *session);

/**
   Terminates receiver end of rpc connection.

   @pre conn_state(conn) == C2_RPC_CONN_ACTIVE && conn->c_nr_sessions == 0
   @post ergo(result == 0, conn_state(conn) == C2_RPC_CONN_TERMINATING)
   @post ergo(result != 0 && result != -EBUSY,
		conn_state(conn) == C2_RPC_CONN_FAILED)
 */
int c2_rpc_rcv_conn_terminate(struct c2_rpc_conn *conn);

/**
   Cleans up in memory state of rpc connection.

   XXX Right now this function is not called from anywhere. There
   should be ->item_sent() callback in item->ri_ops, where this
   function can be hooked.

   The conn_terminate FOM cannot free in-memory state of rpc connection.
   Because it needs to send conn_terminate_reply fop, by using session-0 and
   slot-0 of the rpc connection being terminated. Hence we cleanup in memory
   state of the conn when conn_terminate_reply has been sent.

   @pre conn_state(conn) == C2_RPC_CONN_TERMINATING
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
	struct c2_rpc_slot           *sr_slot;

	struct c2_rpc_item           *sr_item;

	struct c2_rpc_onwire_slot_ref sr_ow;

	/** Anchor to put item on c2_rpc_slot::sl_item_list
	    List descriptor: slot_item
	 */
	struct c2_tlink               sr_link;
};

/**
   Called for each received item.
   If item is request then
	APPLY the item to proper slot
   else
	report REPLY_RECEIVED to appropriate slot
 */
int c2_rpc_item_received(struct c2_rpc_item    *item,
			 struct c2_rpc_machine *machine);

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
void c2_rpc_conn_establish_reply_received(struct c2_rpc_item *req);

/**
   Callback routine called through item->ri_ops->rio_replied().

   The routine is executed when reply to conn terminate fop is received
 */
void c2_rpc_conn_terminate_reply_received(struct c2_rpc_item *req);

/**
   Callback routine called through item->ri_ops->rio_replied().

   The routine is executed when reply to session create fop is received
 */
void c2_rpc_session_establish_reply_received(struct c2_rpc_item *req);

/**
   Callback routine called through item->ri_ops->rio_replied().

   The routine is executed when reply to session terminate fop is received
 */
void c2_rpc_session_terminate_reply_received(struct c2_rpc_item *req);
/**
  A callback called when conn terminate reply has been put on network.
  Finalizes and frees conn.

  @pre conn_state(conn) == C2_RPC_CONN_TERMINATING
 */
void c2_rpc_conn_terminate_reply_sent(struct c2_rpc_conn *conn);

enum {
	/**
	   window size for a sliding-window of slot
	 */
	SLOT_DEFAULT_MAX_IN_FLIGHT = 1
};

/**
   Call-backs for events that a slot can trigger.
 */
struct c2_rpc_slot_ops {
	/** Item i is ready to be consumed */
	void (*so_item_consume)(struct c2_rpc_item *i);
	/** A @reply for a request item @req is received and is
	    ready to be consumed */
	void (*so_reply_consume)(struct c2_rpc_item *req,
				 struct c2_rpc_item *reply);
	/** Slot has no items to send and hence is idle. Formation
	    can use such slot to send unbound items. */
	void (*so_slot_idle)(struct c2_rpc_slot *slot);
};

/**
   Returns true iff given rpc item is conn_establish.
 */
bool c2_rpc_item_is_conn_establish(const struct c2_rpc_item *item);

/**
   Returns true iff given rpc item is conn_terminate.
 */
bool c2_rpc_item_is_conn_terminate(const struct c2_rpc_item *item);

/**
   @see c2_rpc_fop_conn_establish_ctx for more information.
 */
void c2_rpc_fop_conn_establish_ctx_init(struct c2_rpc_item      *item,
					struct c2_net_end_point *ep,
					struct c2_rpc_machine   *machine);

/**
   Helper routine, internal to rpc module.
   Sets up and posts rpc-item representing @fop.
 */
int c2_rpc__fop_post(struct c2_fop                *fop,
		     struct c2_rpc_session        *session,
		     const struct c2_rpc_item_ops *ops);

/**
   Return true iff @conn is sender side object of rpc-connection.
 */
bool c2_rpc_conn_is_snd(const struct c2_rpc_conn *conn);

/**
   Return true iff @conn is receiver side object of rpc-connection.
 */
bool c2_rpc_conn_is_rcv(const struct c2_rpc_conn *conn);

/**
   Temporary routine to place fop in a global queue, from where it can be
   selected for execution.
 */
void c2_rpc_item_dispatch(struct c2_rpc_item *item);

/**
   Returns true iff, it is okay to add item internally (i.e.
   c2_rpc_item_add_internal()). This is required, so that formation can add
   unbound items to the slot until slot->sl_max_in_flight limit is reached.

   @pre c2_mutex_is_locked(&slot->sl_mutex)
 */
bool c2_rpc_slot_can_item_add_internal(const struct c2_rpc_slot *slot);

/**
   For all slots belonging to @session,
     if slot is in c2_rpc_machine::rm_ready_slots list,
     then remove it from the list.
 */
void c2_rpc_session_del_slots_from_ready_list(struct c2_rpc_session *session);

void c2_rpc_conn_add_session(struct c2_rpc_conn    *conn,
                             struct c2_rpc_session *session);
void c2_rpc_conn_remove_session(struct c2_rpc_session *session);

bool c2_rpc_item_is_control_msg(const struct c2_rpc_item *item);

bool c2_rpc_session_is_idle(const struct c2_rpc_session *session);

bool c2_rpc_session_bind_item(struct c2_rpc_item *item);

void c2_rpc_session_item_failed(struct c2_rpc_item *item);

void c2_rpc_session_mod_nr_active_items(struct c2_rpc_session *session,
					int delta);

void c2_rpc_slot_process_reply(struct c2_rpc_item *req);

#ifndef __KERNEL__
int c2_rpc_slot_item_list_print(struct c2_rpc_slot *slot, bool only_active,
				int count);
#endif

static inline struct c2_rpc_machine *
session_machine(const struct c2_rpc_session *s)
{
	return s->s_conn->c_rpc_machine;
}

int __slot_reply_received(struct c2_rpc_slot *slot,
			  struct c2_rpc_item *req,
			  struct c2_rpc_item *reply);

C2_TL_DESCR_DECLARE(rpc_session, extern);
C2_TL_DECLARE(rpc_session, extern, struct c2_rpc_session);

C2_TL_DESCR_DECLARE(ready_slot, extern);
C2_TL_DECLARE(ready_slot, extern, struct c2_rpc_slot);

C2_TL_DESCR_DECLARE(slot_item, extern);
C2_TL_DECLARE(slot_item, extern, struct c2_rpc_item);

/** Helper macro to iterate over every item in a slot */
#define for_each_item_in_slot(item, slot) \
	        c2_tl_for(slot_item, &slot->sl_item_list, item)
#define end_for_each_item_in_slot c2_tl_endfor

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
