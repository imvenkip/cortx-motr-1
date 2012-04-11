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
 * Original author: Rohan Puri <Rohan_Puri@xyratex.com>
 *                  Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 08/24/2011
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "rpc/session.h"
#include "lib/bitstring.h"
#include "cob/cob.h"
#include "fop/fop.h"
#include "fop/fop_format_def.h"
#include "lib/arith.h"

#ifdef __KERNEL__
#include "rpc/session_k.h"
#else
#include "rpc/session_u.h"
#endif

#include "rpc/session_internal.h"
#include "db/db.h"
#include "dtm/verno.h"
#include "rpc/session_fops.h"
#include "rpc/rpc2.h"
#include "rpc/formation.h"

/**
   @addtogroup rpc_session

   @{

   This file contains implementation of rpc slots.

 */

void item_exit_stats_set(struct c2_rpc_item    *item,
			 enum c2_rpc_item_path  path);

void frm_item_reply_received(struct c2_rpc_item *reply_item,
			     struct c2_rpc_item *req_item);

void rpc_item_replied(struct c2_rpc_item *item, struct c2_rpc_item *reply,
                      uint32_t rc);

bool c2_rpc_slot_invariant(const struct c2_rpc_slot *slot)
{
	struct c2_rpc_item *item1 = NULL;  /* init to NULL, required */
	struct c2_rpc_item *item2;
	struct c2_verno    *v1;
	struct c2_verno    *v2;
	bool                ret = true;   /* init to true, required */

	if (slot == NULL ||
	      slot->sl_in_flight > slot->sl_max_in_flight ||
	      !c2_list_invariant(&slot->sl_item_list))
		return false;

	/*
	 * Traverse slot->sl_item_list using item2 ptr
	 * item1 will be previous item of item2 i.e.
	 * next(item1) == item2
	 */
	c2_list_for_each_entry(&slot->sl_item_list, item2, struct c2_rpc_item,
				ri_slot_refs[0].sr_link) {

		if (item1 == NULL) {
			/*
			 * First element is dummy item. So no need to check.
			 */
			item1 = item2;
			continue;
		}
		ret = ergo(item2->ri_stage == RPC_ITEM_STAGE_PAST_VOLATILE ||
			   item2->ri_stage == RPC_ITEM_STAGE_PAST_COMMITTED,
			   item2->ri_reply != NULL);
		if (!ret)
			break;

		ret = (item1->ri_stage <= item2->ri_stage);
		if (!ret)
			break;

		v1 = &item1->ri_slot_refs[0].sr_verno;
		v2 = &item2->ri_slot_refs[0].sr_verno;

		/*
		 * AFTER an "update" item is applied on a slot
		 * the version number of slot is advanced
		 */
		ret = c2_rpc_item_is_update(item1) ?
			v1->vn_vc + 1 == v2->vn_vc :
			v1->vn_vc == v2->vn_vc;
		if (!ret)
			break;

		ret = (item1->ri_slot_refs[0].sr_xid + 1 ==
			item2->ri_slot_refs[0].sr_xid);
		if (!ret)
			break;

		item1 = item2;
	}
	return ret;
}

int c2_rpc_slot_init(struct c2_rpc_slot           *slot,
		     const struct c2_rpc_slot_ops *ops)
{
	struct c2_fop          *fop;
	struct c2_rpc_item     *dummy_item;
	struct c2_rpc_slot_ref *sref;

	/*
	 * Allocate dummy item.
	 * The dummy item is used to avoid special cases
	 * i.e. last_sent == NULL, last_persistent == NULL
	 */
	fop = c2_fop_alloc(&c2_rpc_fop_noop_fopt, NULL);
	if (fop == NULL)
		return -ENOMEM;

	c2_list_link_init(&slot->sl_link);
	/*
	 * XXX temporary value for lsn. This will be set to some proper value
	 * when sessions will be integrated with FOL
	 */
	slot->sl_verno.vn_lsn  = C2_LSN_RESERVED_NR + 2;
	slot->sl_verno.vn_vc   = 0;
	slot->sl_slot_gen      = 0;
	slot->sl_xid           = 1; /* xid 0 will be taken by dummy item */
	slot->sl_in_flight     = 0;
	slot->sl_max_in_flight = SLOT_DEFAULT_MAX_IN_FLIGHT;
	slot->sl_cob           = NULL;
	slot->sl_ops           = ops;

	c2_list_init(&slot->sl_item_list);
	c2_list_init(&slot->sl_ready_list);
	c2_mutex_init(&slot->sl_mutex);

	/*
	 * Add a dummy item with very low verno in item_list
	 */

	dummy_item = &fop->f_item;

	dummy_item->ri_stage     = RPC_ITEM_STAGE_PAST_COMMITTED;
	/* set ri_reply to some value. Doesn't matter what */
	dummy_item->ri_reply     = dummy_item;
	slot->sl_last_sent       = dummy_item;
	slot->sl_last_persistent = dummy_item;

	sref                  = &dummy_item->ri_slot_refs[0];
	sref->sr_slot         = slot;
	sref->sr_item         = dummy_item;
	sref->sr_xid          = 0;
	/*
	 * XXX lsn will be assigned to some proper value once sessions code
	 * will be integrated with FOL
	 */
	sref->sr_verno.vn_lsn = C2_LSN_DUMMY_ITEM;
	sref->sr_verno.vn_vc  = 0;
	sref->sr_slot_gen     = slot->sl_slot_gen;

	c2_list_link_init(&sref->sr_link);
	c2_list_add(&slot->sl_item_list, &sref->sr_link);
	return 0;
}

/**
  Frees all the items from slot->sl_item_list except dummy_item.

  XXX This is temporary. This routine will be scraped entirely.
  When slots will be integrated with FOL, there will be some pruning mechanism
  that will evict items from slot's item_list. But for now, we need to be able
  to fini() slot for testing purpose. That's why freeing the items explicitly.
 */
static void slot_item_list_prune(struct c2_rpc_slot *slot)
{
	struct c2_rpc_item  *item;
	struct c2_rpc_item  *reply;
	struct c2_rpc_item  *next;
	struct c2_rpc_item  *dummy_item;
	struct c2_list_link *link;
	int                  count = 0;
	bool                 first_item = true;

	/*
	 * XXX See comments above function prototype
	 */
	C2_ASSERT(slot != NULL);

	c2_list_for_each_entry_safe(&slot->sl_item_list, item, next,
			struct c2_rpc_item, ri_slot_refs[0].sr_link) {

		if (first_item) {
			/*
			 * Don't delete dummy item
			 */
			first_item = false;
			continue;
		}
		reply = item->ri_reply;
		if (reply != NULL) {
			C2_ASSERT(reply->ri_ops != NULL &&
					reply->ri_ops->rio_free != NULL);
			reply->ri_ops->rio_free(reply);
		}
		item->ri_reply = NULL;

		c2_list_del(&item->ri_slot_refs[0].sr_link);

		C2_ASSERT(item->ri_ops != NULL &&
				item->ri_ops->rio_free != NULL);
		item->ri_ops->rio_free(item);
		count++;
	}
        C2_ASSERT(c2_list_length(&slot->sl_item_list) == 1);

        link = c2_list_first(&slot->sl_item_list);
        C2_ASSERT(link != NULL);

        dummy_item = c2_list_entry(link, struct c2_rpc_item,
                                   ri_slot_refs[0].sr_link);
        C2_ASSERT(c2_list_link_is_in(&dummy_item->ri_slot_refs[0].sr_link));

	slot->sl_last_sent = dummy_item;
	slot->sl_last_persistent = dummy_item;
}

void c2_rpc_slot_fini(struct c2_rpc_slot *slot)
{
	struct c2_rpc_item  *dummy_item;
	struct c2_fop       *fop;
	struct c2_list_link *link;

	slot_item_list_prune(slot);
	c2_list_link_fini(&slot->sl_link);
	c2_list_fini(&slot->sl_ready_list);

	/*
	 * Remove the dummy item from the list
	 */
	C2_ASSERT(c2_list_length(&slot->sl_item_list) == 1);

	link = c2_list_first(&slot->sl_item_list);
	C2_ASSERT(link != NULL);

	dummy_item = c2_list_entry(link, struct c2_rpc_item,
				ri_slot_refs[0].sr_link);
	C2_ASSERT(c2_list_link_is_in(&dummy_item->ri_slot_refs[0].sr_link));

	c2_list_del(&dummy_item->ri_slot_refs[0].sr_link);
	C2_ASSERT(dummy_item->ri_slot_refs[0].sr_xid == 0);

	fop = c2_rpc_item_to_fop(dummy_item);
	c2_fop_free(fop);

	c2_list_fini(&slot->sl_item_list);
	if (slot->sl_cob != NULL) {
		c2_cob_put(slot->sl_cob);
	}
	C2_SET0(slot);
}

/**
   Searches slot->sl_item_list to find item with matching @xid.
   @return item if found, NULL otherwise.
 */
static struct c2_rpc_item* item_find(const struct c2_rpc_slot *slot,
				     uint64_t                  xid)
{
	struct c2_rpc_item *item;

	C2_PRE(slot != NULL && c2_mutex_is_locked(&slot->sl_mutex));
	c2_list_for_each_entry(&slot->sl_item_list, item, struct c2_rpc_item,
				ri_slot_refs[0].sr_link) {

		if (item->ri_slot_refs[0].sr_xid == xid)
			return item;
	}
	return NULL;
}

uint32_t c2_rpc_slot_items_possible_inflight(struct c2_rpc_slot *slot)
{
	C2_PRE(slot != NULL);

	return slot->sl_max_in_flight - slot->sl_in_flight;
}

/**
   If slot->sl_item_list has item(s) in state FUTURE then
	call slot->sl_ops->so_item_consume() for upto slot->sl_max_in_flight
	  number of (FUTURE)items. (On sender, each "consumed" item will be
	  given to formation for transmission. On receiver, "consumed" item is
	  "dispatched" to request handler for execution)
   else
	Notify that the slot is idle (i.e. call slot->sl_ops->so_slot_idle()

   if allow_events is false then items are not consumed.
   This is required when formation wants to add item to slot->sl_item_list
   but do not want item to be consumed.
 */
static void __slot_balance(struct c2_rpc_slot *slot,
			   bool                allow_events)
{
	struct c2_rpc_item  *item;
	struct c2_list_link *link;

	C2_PRE(slot != NULL);
	C2_PRE(c2_mutex_is_locked(&slot->sl_mutex));
	C2_PRE(c2_rpc_slot_invariant(slot));

	while (slot->sl_in_flight < slot->sl_max_in_flight) {
		/* Is slot->item_list is empty? */
		link = &slot->sl_last_sent->ri_slot_refs[0].sr_link;
		if (c2_list_link_is_last(link, &slot->sl_item_list)) {
			if (allow_events)
				slot->sl_ops->so_slot_idle(slot);
			break;
		}
		/* Take slot->last_sent->next item for sending */
		item = c2_list_entry(link->ll_next, struct c2_rpc_item,
				     ri_slot_refs[0].sr_link);

		if (item->ri_stage == RPC_ITEM_STAGE_FUTURE)
			item->ri_stage = RPC_ITEM_STAGE_IN_PROGRESS;

		if (item->ri_reply != NULL && !c2_rpc_item_is_update(item)) {
			/*
			 * Don't send read only queries for which answer is
			 * already known
			 */
			continue;
		}
		slot->sl_last_sent = item;
		slot->sl_in_flight++;
		/*
		 * Tell formation module that an item is ready to be put in rpc
		 */
		if (allow_events)
			slot->sl_ops->so_item_consume(item);
	}
	C2_POST(c2_rpc_slot_invariant(slot));
}

/**
   For more information see __slot_balance()
   @see __slot_balance()
 */
static void slot_balance(struct c2_rpc_slot *slot)
{
	__slot_balance(slot, true);
}

/**
   @see c2_rpc_slot_item_add_internal()
 */
static void __slot_item_add(struct c2_rpc_slot *slot,
			    struct c2_rpc_item *item,
			    bool                allow_events)
{
	struct c2_rpc_slot_ref *sref;
	struct c2_rpc_session  *session;

	C2_PRE(slot != NULL && item != NULL && slot->sl_session != NULL);
	C2_PRE(c2_mutex_is_locked(&slot->sl_mutex));
	C2_PRE(c2_mutex_is_locked(&slot->sl_session->s_mutex));
	C2_PRE(slot->sl_session == item->ri_session);
	C2_PRE(c2_rpc_slot_invariant(slot));

	session = slot->sl_session;

	sref                = &item->ri_slot_refs[0];
	item->ri_stage      = RPC_ITEM_STAGE_FUTURE;
	sref->sr_session_id = session->s_session_id;
	sref->sr_sender_id  = session->s_conn->c_sender_id;
	sref->sr_uuid       = session->s_conn->c_uuid;

	/*
	 * c2_rpc_slot_item_apply() will provide an item
	 * which already has verno initialised. Yet, following
	 * assignment should not be any problem because slot_item_apply()
	 * will call this routine only if verno of slot and item
	 * matches
	 */
	sref->sr_slot_id    = slot->sl_slot_id;
	sref->sr_verno      = slot->sl_verno;
	sref->sr_xid        = slot->sl_xid;

	slot->sl_xid++;
	if (c2_rpc_item_is_update(item)) {
		/*
		 * When integrated with lsn,
		 * use c2_fol_lsn_allocate() to allocate new lsn and
		 * use c2_verno_inc() to advance vn_vc.
		 */
		slot->sl_verno.vn_lsn++;
		slot->sl_verno.vn_vc++;
	}

	sref->sr_slot_gen = slot->sl_slot_gen;
	sref->sr_slot     = slot;
	sref->sr_item     = item;
	c2_list_link_init(&sref->sr_link);
	c2_list_add_tail(&slot->sl_item_list, &sref->sr_link);
	if (session != NULL) {
		/*
		 * we're already under the cover of session->s_mutex
		 */
		session->s_nr_active_items++;
		if (session->s_state == C2_RPC_SESSION_IDLE) {
			/*
			 * XXX When formation adds an item to
			 * c2_rpc_session::s_unbound_items it should
			 * set session->s_state as BUSY
			 */
			session->s_state = C2_RPC_SESSION_BUSY;
			c2_cond_broadcast(&session->s_state_changed,
					  &session->s_mutex);
		}
	}

	__slot_balance(slot, allow_events);
}

void c2_rpc_slot_item_add_internal(struct c2_rpc_slot *slot,
				   struct c2_rpc_item *item)
{
	C2_PRE(slot != NULL && item != NULL);
	C2_PRE(c2_mutex_is_locked(&slot->sl_mutex));
	C2_PRE(c2_mutex_is_locked(&slot->sl_session->s_mutex));
	C2_PRE(slot->sl_session == item->ri_session);

	__slot_item_add(slot, item,
			false);  /* slot is not allowed to trigger events */
}

int c2_rpc_slot_misordered_item_received(struct c2_rpc_slot *slot,
					 struct c2_rpc_item *item)
{
	struct c2_rpc_item *reply;
	struct c2_fop      *fop;

	/*
	 * Send a dummy NOOP fop as reply to report misordered item
	 * XXX We should've a special fop type to report session error
	 */
	fop = c2_fop_alloc(&c2_rpc_fop_noop_fopt, NULL);
	if (fop == NULL)
		return -ENOMEM;

	reply = &fop->f_item;

	reply->ri_session = item->ri_session;
	reply->ri_error = -EBADR;

	reply->ri_slot_refs[0] = item->ri_slot_refs[0];
	c2_list_link_init(&reply->ri_slot_refs[0].sr_link);
	c2_list_link_init(&reply->ri_slot_refs[0].sr_ready_link);

	slot->sl_ops->so_reply_consume(item, reply);
	return 0;
}

int c2_rpc_slot_item_apply(struct c2_rpc_slot *slot,
			   struct c2_rpc_item *item)
{
	struct c2_rpc_item *req;
	int                 redoable;
	int                 rc = 0;   /* init to 0, required */

	C2_ASSERT(slot != NULL && item != NULL);
	C2_ASSERT(c2_mutex_is_locked(&slot->sl_mutex));
	C2_ASSERT(c2_mutex_is_locked(&slot->sl_session->s_mutex));
	C2_ASSERT(c2_rpc_slot_invariant(slot));

	redoable = c2_verno_is_redoable(&slot->sl_verno,
					&item->ri_slot_refs[0].sr_verno,
					false);
	switch (redoable) {
	case 0:
		__slot_item_add(slot, item, true);
		break;
	case -EALREADY:
		req = item_find(slot, item->ri_slot_refs[0].sr_xid);
		if (req == NULL) {
			rc = c2_rpc_slot_misordered_item_received(slot,
								 item);
			break;
		}
		/*
		 * XXX At this point req->ri_slot_refs[0].sr_verno and
		 * item->ri_slot_refs[0].sr_verno MUST be same. If they are
		 * not same then generate ADDB record.
		 * For now, assert this condition for testing purpose.
		 */
		C2_ASSERT(c2_verno_cmp(&req->ri_slot_refs[0].sr_verno,
				&item->ri_slot_refs[0].sr_verno) == 0);

		switch (req->ri_stage) {
		case RPC_ITEM_STAGE_PAST_VOLATILE:
		case RPC_ITEM_STAGE_PAST_COMMITTED:
			/*
			 * @item is duplicate and corresponding original is
			 * already consumed (i.e. executed if item is FOP).
			 * Consume cached reply. (on receiver, this means
			 * resend cached reply)
			 */
			C2_ASSERT(req->ri_reply != NULL);
			slot->sl_ops->so_reply_consume(req,
						req->ri_reply);
			break;
		case RPC_ITEM_STAGE_IN_PROGRESS:
		case RPC_ITEM_STAGE_FUTURE:
			/* item is already present but is not
			   processed yet. Ignore it*/
			/* do nothing */;
		}
		break;
	case -EAGAIN:
		rc = c2_rpc_slot_misordered_item_received(slot, item);
		break;
	}
	C2_ASSERT(c2_rpc_slot_invariant(slot));
	return rc;
}

void c2_rpc_slot_reply_received(struct c2_rpc_slot  *slot,
				struct c2_rpc_item  *reply,
				struct c2_rpc_item **req_out)
{
	struct c2_rpc_item     *req;
	struct c2_rpc_slot_ref *sref;
	struct c2_rpc_session  *session;

	C2_PRE(slot != NULL && reply != NULL && req_out != NULL);
	C2_PRE(c2_mutex_is_locked(&slot->sl_mutex));

	*req_out = NULL;

	sref = &reply->ri_slot_refs[0];
	C2_ASSERT(slot == sref->sr_slot);

	req = item_find(slot, reply->ri_slot_refs[0].sr_xid);
	if (req == NULL) {
		/*
		 * Either it is a duplicate reply and its corresponding request
		 * item is pruned from the item list, or it is a corrupted
		 * reply
		 */
		return;
	}
	/*
	 * XXX At this point req->ri_slot_refs[0].sr_verno and
	 * reply->ri_slot_refs[0].sr_verno MUST be same. If they are not,
	 * then generate ADDB record.
	 * For now, assert this condition for testing purpose.
	 */
	C2_ASSERT(c2_verno_cmp(&req->ri_slot_refs[0].sr_verno,
			&reply->ri_slot_refs[0].sr_verno) == 0);

	if (c2_verno_cmp(&req->ri_slot_refs[0].sr_verno,
		&slot->sl_last_sent->ri_slot_refs[0].sr_verno) > 0) {
		/*
		 * Received a reply to an item that wasn't sent. This is
		 * possible if the receiver failed and forget about some
		 * items. The sender moved last_seen to the past, but then a
		 * late reply to one of items unreplied before the failure
		 * arrived.
		 *
		 * Such reply must be ignored
		 */
		;
	} else if (req->ri_stage == RPC_ITEM_STAGE_PAST_COMMITTED ||
			req->ri_stage == RPC_ITEM_STAGE_PAST_VOLATILE) {
		/*
		 * Got a reply to an item for which the reply was already
		 * received in the past. Compare with the original reply.
		 * XXX find out how to compare two rpc items to be same
		 */
	} else {
		/*
		 * This is valid reply case.
		 */
		C2_ASSERT(req->ri_stage == RPC_ITEM_STAGE_IN_PROGRESS);
		C2_ASSERT(slot->sl_in_flight > 0);

		session = slot->sl_session;
		C2_ASSERT(session != NULL);

		c2_mutex_lock(&session->s_mutex);
		C2_ASSERT(c2_rpc_session_invariant(session));
		C2_ASSERT(session->s_state == C2_RPC_SESSION_BUSY);
		C2_ASSERT(session->s_nr_active_items > 0);

		req->ri_stage = RPC_ITEM_STAGE_PAST_VOLATILE;
		req->ri_reply = reply;
		*req_out = req;
		slot->sl_in_flight--;

		session->s_nr_active_items--;
		/*
		 * Setting of req->ri_stage to PAST_VOLATILE and reducing
		 * session->s_nr_active_items must be in same critical
		 * region protected by session->s_mutex.
		 */
		if (session->s_nr_active_items == 0 &&
			c2_list_is_empty(&session->s_unbound_items)) {
			session->s_state = C2_RPC_SESSION_IDLE;
			/*
			 * ->s_state_change is broadcast after slot_balance()
			 * call in this function.
			 * Cannot broadcast on session->s_state_changed here.
			 * Doing so introduces a race condition:
			 *
			 * - User thread might be waiting for session to be
			 *   IDLE, so that it can call
			 *   c2_rpc_session_terminate().
			 * - current thread broadcasts on s_state_changed
			 * - the user thread will come out of wait, and issue
			 *   c2_rpc_session_terminate(), which removes all
			 *   slots of the IDLE session, from ready_slots list.
			 * - current thread continues execution and calls
			 *   slot_balance(), which will trigger
			 *   slot->sl_ops->so_slot_idle() event, resulting in
			 *   slot placed again on ready_slots list.
			 *   A TERMINATING session must not have any of its
			 *   slots on ready_slots list.
			 * - session invariant catches this, in
			 *   c2_rpc_session_terminate_reply_received()
			 */
		}
		C2_ASSERT(c2_rpc_session_invariant(session));
		c2_mutex_unlock(&session->s_mutex);

		slot_balance(slot);

		c2_mutex_lock(&session->s_mutex);
		C2_ASSERT(c2_rpc_session_invariant(session));

		if (session->s_state == C2_RPC_SESSION_IDLE) {
			c2_cond_broadcast(&session->s_state_changed,
					&session->s_mutex);
		}
		c2_mutex_unlock(&session->s_mutex);

		/*
		 * On receiver, ->so_reply_consume(req, reply) will hand over
		 * @reply to formation, to send it back to sender.
		 * see: rcv_reply_consume(), snd_reply_consume()
		 */
		slot->sl_ops->so_reply_consume(req, reply);
		/*
		 * XXX On receiver, there is a potential race, from this point
		 * to slot mutex unlock in c2_rpc_reply_post().
		 * - Context switch happens at this point. slot->sl_mutex is
		 *   yet to be unlocked.
		 * - @reply reaches back to sender. This might make sender side
		 *   session IDLE.
		 * - sender sends SESSION_TERMINATE.
		 * - SESSION_TERMINATE fop gets submitted to reqh and completes
		 *    its execution. As part of session termination it frees all
		 *    session and slot objects.
		 * - execution of this thread resumes from this point.
		 * - control returns to c2_rpc_reply_post() which tries to
		 *   unlock the mutex, but the mutex is embeded in slot and
		 *   slot is already freed during session termination. BOOM!!!
		 * - Holding session mutex across c2_rpc_slot_reply_received()
		 *   might sound obvious solution, but formation tries to
		 *   aquire same session mutex while processing reply item,
		 *   leading to self deadlock.
		 */
	}
}

void c2_rpc_slot_persistence(struct c2_rpc_slot *slot,
			     struct c2_verno     last_persistent)
{
	struct c2_rpc_item     *item;
	struct c2_list_link    *link;

	C2_PRE(slot != NULL && c2_mutex_is_locked(&slot->sl_mutex));

	/*
	 * From last persistent item to end of slot->item_list,
	 *    if item->verno <= @last_persistent
	 *       Mark item as PAST_COMMITTED
	 *    else
	 *       break
	 */
	link = &slot->sl_last_persistent->ri_slot_refs[0].sr_link;
	for (; link != (void *)&slot->sl_item_list; link = link->ll_next) {

		item = c2_list_entry(link, struct c2_rpc_item,
					ri_slot_refs[0].sr_link);

		if (c2_verno_cmp(&item->ri_slot_refs[0].sr_verno,
				&last_persistent) <= 0) {

			C2_ASSERT(
			   item->ri_stage == RPC_ITEM_STAGE_PAST_COMMITTED ||
			   item->ri_stage == RPC_ITEM_STAGE_PAST_VOLATILE);

			item->ri_stage = RPC_ITEM_STAGE_PAST_COMMITTED;
			slot->sl_last_persistent = item;
		} else {
			break;
		}
	}
	C2_POST(
	   c2_verno_cmp(&slot->sl_last_persistent->ri_slot_refs[0].sr_verno,
			&last_persistent) >= 0);
}

void c2_rpc_slot_reset(struct c2_rpc_slot *slot,
		       struct c2_verno     last_seen)
{
	struct c2_rpc_item     *item;
	struct c2_rpc_slot_ref *sref;

	C2_PRE(slot != NULL && c2_mutex_is_locked(&slot->sl_mutex));
	C2_PRE(c2_verno_cmp(&slot->sl_verno, &last_seen) >= 0);

	c2_list_for_each_entry(&slot->sl_item_list, item, struct c2_rpc_item,
				ri_slot_refs[0].sr_link) {

		sref = &item->ri_slot_refs[0];
		if (c2_verno_cmp(&sref->sr_verno, &last_seen) == 0) {
			C2_ASSERT(item->ri_stage != RPC_ITEM_STAGE_FUTURE);
			slot->sl_last_sent = item;
			break;
		}

	}
	C2_ASSERT(c2_verno_cmp(&slot->sl_last_sent->ri_slot_refs[0].sr_verno,
				&last_seen) == 0);
	slot_balance(slot);
}

static int associate_session_and_slot(struct c2_rpc_item    *item,
				      struct c2_rpc_machine *machine)
{
	struct c2_list         *conn_list;
	struct c2_rpc_conn     *conn;
	struct c2_rpc_session  *session;
	struct c2_rpc_slot     *slot;
	struct c2_rpc_slot_ref *sref;
	bool                    found;
	bool                    use_uuid;

	sref = &item->ri_slot_refs[0];
	if (sref->sr_session_id > SESSION_ID_MAX)
		return -EINVAL;

	conn_list = c2_rpc_item_is_request(item) ?
			&machine->rm_incoming_conns :
			&machine->rm_outgoing_conns;

	use_uuid = (sref->sr_sender_id == SENDER_ID_INVALID);

	c2_mutex_lock(&machine->rm_session_mutex);
	found = false;
	c2_list_for_each_entry(conn_list, conn, struct c2_rpc_conn, c_link) {

		found = use_uuid ?
		   c2_rpc_sender_uuid_cmp(&conn->c_uuid, &sref->sr_uuid) == 0 :
		   conn->c_sender_id == sref->sr_sender_id;
		if (found)
			break;

	}
	c2_mutex_unlock(&machine->rm_session_mutex);
	if (!found)
		return -ENOENT;

	c2_mutex_lock(&conn->c_mutex);
	session = c2_rpc_session_search(conn, sref->sr_session_id);
	c2_mutex_unlock(&conn->c_mutex);
	if (session == NULL)
		return -ENOENT;

	c2_mutex_lock(&session->s_mutex);
	if (sref->sr_slot_id >= session->s_nr_slots) {
		c2_mutex_unlock(&session->s_mutex);
		return -ENOENT;
	}
	slot = session->s_slot_table[sref->sr_slot_id];
	/* XXX Check generation of slot */
	C2_ASSERT(slot != NULL);
	sref->sr_slot = slot;
	item->ri_session = session;
	C2_POST(item->ri_session != NULL &&
		item->ri_slot_refs[0].sr_slot != NULL);
	c2_mutex_unlock(&session->s_mutex);

	return 0;
}

int c2_rpc_item_received(struct c2_rpc_item    *item,
			 struct c2_rpc_machine *machine)
{
	struct c2_rpc_item *req;
	struct c2_rpc_slot *slot;
	int                 rc;

	C2_ASSERT(item != NULL && machine != NULL);

	rc = associate_session_and_slot(item, machine);
	if (rc != 0) {
		/*
		 * stats for conn establish item are updated in its
		 * fom's state() method.
		 */
		if (c2_rpc_item_is_conn_establish(item)) {
			c2_rpc_item_dispatch(item);
			return 0;
		}
		/*
		 * If we cannot associate the item with its slot
		 * then there is nothing that we can do with this
		 * item except to discard it.
		 * XXX generate ADDB record
		 */
		return rc;
	}
	C2_ASSERT(item->ri_session != NULL &&
		  item->ri_slot_refs[0].sr_slot != NULL);

	item_exit_stats_set(item, C2_RPC_PATH_INCOMING);

	slot = item->ri_slot_refs[0].sr_slot;
	if (c2_rpc_item_is_request(item)) {
		c2_mutex_lock(&slot->sl_mutex);
		c2_mutex_lock(&slot->sl_session->s_mutex);

		c2_rpc_slot_item_apply(slot, item);

		c2_mutex_unlock(&slot->sl_session->s_mutex);
		c2_mutex_unlock(&slot->sl_mutex);
	} else {
		c2_mutex_lock(&slot->sl_mutex);
		c2_rpc_slot_reply_received(slot, item, &req);
		c2_mutex_unlock(&slot->sl_mutex);

		/*
		 * In case the reply is duplicate/unwanted then
		 * c2_rpc_slot_reply_received() sets req to NULL.
		 */
		if (req != NULL) {
			/* Send reply received event to formation component.*/
			frm_item_reply_received(item, req);
			/*
			 * informing upper layer that reply is received should
			 * be the done after all the reply processing has been
			 * done by rpc-layer.
			 */
			rpc_item_replied(req, item, 0);
		}
	}
	return 0;
}

void rpc_item_replied(struct c2_rpc_item *item, struct c2_rpc_item *reply,
                      uint32_t rc)
{
	bool broadcast = true;

	item->ri_error = rc;
	item->ri_reply = reply;

	if (c2_rpc_item_is_conn_terminate(item))
		broadcast = false;

	if (item->ri_ops != NULL && item->ri_ops->rio_replied != NULL)
		item->ri_ops->rio_replied(item);

	/*
	 * If item is of type conn terminate reply,
	 * then req and item (including any of its associated
	 * rpc layer structures e.g. session, frm_sm etc.)
	 * should not be accessed from this point onwards.
	 */
	if (broadcast)
		c2_chan_broadcast(&item->ri_chan);
}

int c2_rpc_slot_cob_lookup(struct c2_cob   *session_cob,
			   uint32_t         slot_id,
			   uint64_t         slot_generation,
			   struct c2_cob  **slot_cob,
			   struct c2_db_tx *tx)
{
	struct c2_cob *cob;
	char           name[SESSION_COB_MAX_NAME_LEN];
	int            rc;

	C2_PRE(session_cob != NULL && slot_cob != NULL);

	*slot_cob = NULL;
	sprintf(name, "SLOT_%u:%lu", slot_id, (unsigned long)slot_generation);

	rc = c2_rpc_cob_lookup_helper(session_cob->co_dom, session_cob, name,
					&cob, tx);
	C2_ASSERT(ergo(rc != 0, cob == NULL));
	*slot_cob = cob;
	return rc;
}

int c2_rpc_slot_cob_create(struct c2_cob   *session_cob,
			   uint32_t         slot_id,
			   uint64_t         slot_generation,
			   struct c2_cob  **slot_cob,
			   struct c2_db_tx *tx)
{
	struct c2_cob *cob;
	char           name[SESSION_COB_MAX_NAME_LEN];
	int            rc;

	C2_PRE(session_cob != NULL && slot_cob != NULL);

	*slot_cob = NULL;
	sprintf(name, "SLOT_%u:%lu", slot_id, (unsigned long)slot_generation);

	rc = c2_rpc_cob_create_helper(session_cob->co_dom, session_cob, name,
					&cob, tx);
	C2_ASSERT(ergo(rc != 0, cob == NULL));
	*slot_cob = cob;
	return rc;
}

/**
   Just for debugging purpose.
 */
#ifndef __KERNEL__
void c2_rpc_slot_item_list_print(struct c2_rpc_slot *slot)
{
	struct c2_rpc_item *item;
	char                str_stage[][20] = {
				"INVALID",
				"PAST_COMMITTED",
				"PAST_VOLATILE",
				"IN_PROGRESS",
				"FUTURE"
			     };

	c2_list_for_each_entry(&slot->sl_item_list, item,
				struct c2_rpc_item,
				ri_slot_refs[0].sr_link) {
		printf("item %p xid %lu state %s\n", item,
				item->ri_slot_refs[0].sr_xid,
				str_stage[item->ri_stage]);
	}
}
#endif
bool c2_rpc_slot_can_item_add_internal(const struct c2_rpc_slot *slot)
{
	C2_PRE(c2_mutex_is_locked(&slot->sl_mutex));

	return slot->sl_in_flight < slot->sl_max_in_flight;
}
