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
 * Original creation date: 05/12/2010
 */

#pragma once

#ifndef __COLIBRI_RPC_SLOT_INT_H__
#define __COLIBRI_RPC_SLOT_INT_H__

#include "lib/tlist.h"
#include "dtm/verno.h"

/* Imports */
struct c2_rpc_item;
struct c2_rpc_session;
struct c2_cob;
struct c2_db_tx;

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
  sl_link is protected by c2_rpc_machine::rm_ready_slots_mutex.

  Locking order:
    - slot->sl_mutex
    - session->s_mutex
    - conn->c_mutex
    - rpc_machine->rm_session_mutex, rpc_machine->rm_ready_slots_mutex (As of
      now, there is no case where these two mutex are held together. If such
      need arises then ordering of these two mutex should be decided.)
 */
struct c2_rpc_slot {
	/** Session to which this slot belongs */
	struct c2_rpc_session        *sl_session;

	/** identifier of slot, unique within the session */
	uint32_t                      sl_slot_id;

	/** Cob representing this slot in persistent state */
	struct c2_cob                *sl_cob;

	/** list anchor to put in c2_rpc_session::s_ready_slots */
	struct c2_tlink		      sl_link;

	/** Current version number of slot */
	struct c2_verno               sl_verno;

	/** slot generation */
	uint64_t                      sl_slot_gen;

	/** A monotonically increasing sequence counter, assigned to an item
	    when it is bound to the slot
	 */
	uint64_t                      sl_xid;

	/** List of items, starting from oldest. Items are placed using
	    c2_rpc_item::ri_slot_refs[0].sr_link
	    List descriptor: slot_item
	 */
	struct c2_tl                  sl_item_list;

	/** earliest item that the receiver possibly have seen */
	struct c2_rpc_item           *sl_last_sent;

	/** item that is most recently persistent on receiver */
	struct c2_rpc_item           *sl_last_persistent;

	/** On sender: Number of items in flight
	    On receiver: Number of items submitted for execution but whose
	    reply is not yet received.
	 */
	uint32_t                      sl_in_flight;

	/** Maximum number of items that can be in flight on this slot.
	    @see SLOT_DEFAULT_MAX_IN_FLIGHT */
	uint32_t                      sl_max_in_flight;

	const struct c2_rpc_slot_ops *sl_ops;

	/** C2_RPC_SLOT_MAGIC */
	uint64_t		      sl_magic;
};

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
   Adds an item to slot->sl_item_list, without triggering
   any slot related events i.e. slot->ops->consume_item()
 */
void c2_rpc_slot_item_add_internal(struct c2_rpc_slot *slot,
				   struct c2_rpc_item *item);

void c2_rpc_slot_process_reply(struct c2_rpc_item *req);

#ifndef __KERNEL__
int c2_rpc_slot_item_list_print(struct c2_rpc_slot *slot, bool only_active,
				int count);
#endif

int __slot_reply_received(struct c2_rpc_slot *slot,
			  struct c2_rpc_item *req,
			  struct c2_rpc_item *reply);

C2_TL_DESCR_DECLARE(ready_slot, extern);
C2_TL_DECLARE(ready_slot, extern, struct c2_rpc_slot);

#endif /* __COLIBRI_RPC_SLOT_INT_H__ */

