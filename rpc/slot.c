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
 * Original author: Rohan Puri <Rohan_Puri@xyratex.com>
 *                  Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 08/24/2011
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "rpc/session.h"
#include "lib/bitstring.h"
#include "cob/cob.h"
#include "fop/fop.h"
#include "lib/arith.h"
#include "rpc/session_xc.h"
#include "rpc/session_internal.h"
#include "db/db.h"
#include "dtm/verno.h"
#include "rpc/session_fops.h"
#include "rpc/rpc2.h"

/**
   @addtogroup rpc_session

   @{

   This file contains implementation of rpc slots.

 */

void item_exit_stats_set(struct c2_rpc_item    *item,
			 enum c2_rpc_item_path  path);

void rpc_item_replied(struct c2_rpc_item *item, struct c2_rpc_item *reply,
                      uint32_t rc);
void c2_rpc_slot_process_reply(struct c2_rpc_item *req);
void c2_rpc_item_set_stage(struct c2_rpc_item     *item,
			   enum c2_rpc_item_stage  stage);
void c2_rpc_slot_postpone_reply_processing(struct c2_rpc_item *req,
					   struct c2_rpc_item *reply);
int c2_rpc_slot_item_received(struct c2_rpc_item *item);

static struct c2_rpc_machine *
slot_get_rpc_machine(const struct c2_rpc_slot *slot)
{
	return slot->sl_session->s_conn->c_rpc_machine;
}

bool c2_rpc_slot_invariant(const struct c2_rpc_slot *slot)
{
	struct c2_rpc_item *item1 = NULL;  /* init to NULL, required */
	struct c2_rpc_item *item2;
	struct c2_verno    *v1;
	struct c2_verno    *v2;
	bool                ok;

	ok = slot != NULL &&
	     slot->sl_in_flight <= slot->sl_max_in_flight &&
	     c2_list_invariant(&slot->sl_item_list);

	if (!ok)
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
		ok = ergo(C2_IN(item2->ri_stage,
				(RPC_ITEM_STAGE_PAST_VOLATILE,
				 RPC_ITEM_STAGE_PAST_COMMITTED)),
			  item2->ri_reply != NULL);
		if (!ok)
			return false;

		ok = (item1->ri_stage <= item2->ri_stage);
		if (!ok)
			return false;

		v1 = &item1->ri_slot_refs[0].sr_verno;
		v2 = &item2->ri_slot_refs[0].sr_verno;

		/*
		 * AFTER an "update" item is applied on a slot
		 * the version number of slot is advanced
		 */
		ok = c2_rpc_item_is_update(item1) ?
			v1->vn_vc + 1 == v2->vn_vc :
			v1->vn_vc == v2->vn_vc;
		if (!ok)
			return false;

		ok = (item1->ri_slot_refs[0].sr_xid + 1 ==
		      item2->ri_slot_refs[0].sr_xid);
		if (!ok)
			return false;

		item1 = item2;
	}
	return true;
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
		if (reply != NULL)
			c2_rpc_item_free(reply);
		item->ri_reply = NULL;
		c2_list_del(&item->ri_slot_refs[0].sr_link);
		c2_rpc_item_free(item);
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

	C2_PRE(slot != NULL);
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

	C2_PRE(c2_rpc_slot_invariant(slot));
	C2_PRE(c2_rpc_machine_is_locked(slot_get_rpc_machine(slot)));

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
			c2_rpc_item_set_stage(item, RPC_ITEM_STAGE_IN_PROGRESS);

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
	struct c2_rpc_machine  *machine;

	C2_PRE(item != NULL);
	C2_PRE(c2_rpc_slot_invariant(slot));
	C2_PRE(slot->sl_session == item->ri_session);
	C2_PRE(slot->sl_session != NULL);

	session = slot->sl_session;
	machine = session->s_conn->c_rpc_machine;
	C2_PRE(c2_rpc_machine_is_locked(machine));

	sref                = &item->ri_slot_refs[0];
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
	item->ri_stage = RPC_ITEM_STAGE_FUTURE;
	if (session != NULL)
		c2_rpc_session_inc_nr_active_items(session);

	__slot_balance(slot, allow_events);
}

void c2_rpc_slot_item_add_internal(struct c2_rpc_slot *slot,
				   struct c2_rpc_item *item)
{
	C2_PRE(c2_rpc_slot_invariant(slot) && item != NULL);
	C2_PRE(c2_rpc_machine_is_locked(slot_get_rpc_machine(slot)));
	C2_PRE(slot->sl_session == item->ri_session);

	__slot_item_add(slot, item,
			false);  /* slot is not allowed to trigger events */
}

void c2_rpc_slot_misordered_item_received(struct c2_rpc_slot *slot,
					  struct c2_rpc_item *item)
{
	struct c2_rpc_item *reply;
	struct c2_fop      *fop;

	/*
	 * Send a dummy NOOP fop as reply to report misordered item
	 * XXX We should've a special fop type to report session error
	 */
	fop = c2_fop_alloc(&c2_rpc_fop_noop_fopt, NULL);
	if (fop != NULL) {
		reply = &fop->f_item;
		reply->ri_session = item->ri_session;
		reply->ri_error = -EBADR;
		reply->ri_slot_refs[0] = item->ri_slot_refs[0];
		c2_list_link_init(&reply->ri_slot_refs[0].sr_link);
		c2_list_link_init(&reply->ri_slot_refs[0].sr_ready_link);

		slot->sl_ops->so_reply_consume(item, reply);
	}
}

int c2_rpc_slot_item_apply(struct c2_rpc_slot *slot,
			   struct c2_rpc_item *item)
{
	struct c2_rpc_item *req;
	int                 redoable;
	int                 rc = -EPROTO;

	C2_ASSERT(item != NULL);
	C2_ASSERT(c2_rpc_slot_invariant(slot));
	C2_PRE(c2_rpc_machine_is_locked(slot_get_rpc_machine(slot)));

	redoable = c2_verno_is_redoable(&slot->sl_verno,
					&item->ri_slot_refs[0].sr_verno,
					false);
	switch (redoable) {
	case 0:
		__slot_item_add(slot, item, true);
		rc = 0;
		break;
	case -EALREADY:
		/* item is a duplicate request. Find originial. */
		req = item_find(slot, item->ri_slot_refs[0].sr_xid);
		if (req == NULL) {
			c2_rpc_slot_misordered_item_received(slot, item);
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
			slot->sl_ops->so_reply_consume(req, req->ri_reply);
			break;
		case RPC_ITEM_STAGE_IN_PROGRESS:
		case RPC_ITEM_STAGE_FUTURE:
			/* item is already present but is not
			   processed yet. Ignore it*/
			/* do nothing */;
			break;
		case RPC_ITEM_STAGE_UNKNOWN:
		case RPC_ITEM_STAGE_FAILED:
			C2_IMPOSSIBLE("Original req in UNKNOWN/FAILED stage");
		}
		/*
		 * Irrespective of any of above cases, we're going to
		 * ignore this _duplicate_ item.
		 */
		break;

	case -EAGAIN:
		c2_rpc_slot_misordered_item_received(slot, item);
		break;
	}
	C2_ASSERT(c2_rpc_slot_invariant(slot));
	return rc;
}

int c2_rpc_slot_reply_received(struct c2_rpc_slot  *slot,
				struct c2_rpc_item  *reply,
				struct c2_rpc_item **req_out)
{
	struct c2_rpc_item     *req;
	struct c2_rpc_slot_ref *sref;
	struct c2_rpc_machine  *machine;
	uint64_t                req_state;
	int                     rc = -EPROTO;

	C2_PRE(slot != NULL && reply != NULL && req_out != NULL);

	machine = slot_get_rpc_machine(slot);
	C2_PRE(c2_rpc_machine_is_locked(machine));
	C2_ASSERT(c2_rpc_slot_invariant(slot));

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
		return rc;
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
	} else if (C2_IN(req->ri_stage, (RPC_ITEM_STAGE_PAST_COMMITTED,
					 RPC_ITEM_STAGE_PAST_VOLATILE))) {
		/*
		 * Got a reply to an item for which the reply was already
		 * received in the past. Compare with the original reply.
		 * XXX find out how to compare two rpc items to be same
		 */
		/* Do nothing */;
	} else if (req->ri_stage == RPC_ITEM_STAGE_UNKNOWN) {
		/*
		 * The reply is valid but too late. The req has already
		 * timedout. Return without setting *req_out.
		 */
		/* Do nothing */
	} else {
		/*
		 * This is valid reply case.
		 */
		C2_ASSERT(req->ri_stage == RPC_ITEM_STAGE_IN_PROGRESS);
		C2_ASSERT(slot->sl_in_flight > 0);

		req_state = req->ri_sm.sm_state;
		if (C2_IN(req_state,(C2_RPC_ITEM_ACCEPTED,
				     C2_RPC_ITEM_WAITING_FOR_REPLY))) {
			req->ri_reply = reply;
			c2_rpc_slot_process_reply(req);
		} else if (req_state == C2_RPC_ITEM_SENDING) {
			/* buffer sent callback is still pending */
			c2_rpc_slot_postpone_reply_processing(req, reply);
		} else {
			/* XXX Remove this assert */
			C2_ASSERT(false);
		}
		*req_out = req;
		rc = 0;
	}
	return rc;
}

void c2_rpc_slot_process_reply(struct c2_rpc_item *req)
{
	struct c2_rpc_slot    *slot;

	C2_PRE(req != NULL && req->ri_reply != NULL);
	C2_PRE(c2_rpc_item_is_request(req));
	C2_PRE(C2_IN(req->ri_sm.sm_state, (C2_RPC_ITEM_WAITING_FOR_REPLY,
					   C2_RPC_ITEM_ACCEPTED)));
	c2_rpc_item_set_stage(req, RPC_ITEM_STAGE_PAST_VOLATILE);
	slot = req->ri_slot_refs[0].sr_slot;
	slot->sl_in_flight--;
	slot_balance(slot);
	rpc_item_replied(req, req->ri_reply, 0);
	/*
	 * On receiver, ->so_reply_consume(req, reply) will hand over
	 * @reply to formation, to send it back to sender.
	 * see: rcv_reply_consume(), snd_reply_consume()
	 */
	slot->sl_ops->so_reply_consume(req, req->ri_reply);
}

void c2_rpc_slot_postpone_reply_processing(struct c2_rpc_item *req,
					   struct c2_rpc_item *reply)
{
	C2_PRE(req != NULL && req->ri_sm.sm_state == C2_RPC_ITEM_SENDING);

	req->ri_reply         = reply;
	req->ri_reply_pending = true;
}

void c2_rpc_slot_persistence(struct c2_rpc_slot *slot,
			     struct c2_verno     last_persistent)
{
	struct c2_rpc_item     *item;
	struct c2_list_link    *link;

	C2_PRE(c2_rpc_slot_invariant(slot));
	C2_PRE(c2_rpc_machine_is_locked(slot_get_rpc_machine(slot)));

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

			C2_ASSERT(C2_IN(item->ri_stage,
					(RPC_ITEM_STAGE_PAST_COMMITTED,
					 RPC_ITEM_STAGE_PAST_VOLATILE)));

			c2_rpc_item_set_stage(item,
					      RPC_ITEM_STAGE_PAST_COMMITTED);
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

	C2_PRE(c2_rpc_slot_invariant(slot));
	C2_PRE(c2_rpc_machine_is_locked(slot_get_rpc_machine(slot)));
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

static struct c2_rpc_conn *
find_conn(const struct c2_rpc_machine *machine,
	  const struct c2_rpc_item    *item)
{
	const struct c2_list         *conn_list;
	const struct c2_rpc_slot_ref *sref;
	struct c2_rpc_conn           *conn;
	bool                          use_uuid;

	conn_list = c2_rpc_item_is_request(item) ?
			&machine->rm_incoming_conns :
			&machine->rm_outgoing_conns;

	sref = &item->ri_slot_refs[0];
	use_uuid = (sref->sr_sender_id == SENDER_ID_INVALID);

	c2_list_for_each_entry(conn_list, conn, struct c2_rpc_conn, c_link) {
		if (use_uuid) {
			if (c2_rpc_sender_uuid_cmp(&conn->c_uuid,
						   &sref->sr_uuid) == 0)
				return conn;
		} else {
			if (conn->c_sender_id == sref->sr_sender_id)
				return conn;
		}
	}
	return NULL;
}

static int associate_session_and_slot(struct c2_rpc_item    *item,
				      struct c2_rpc_machine *machine)
{
	struct c2_rpc_conn     *conn;
	struct c2_rpc_session  *session;
	struct c2_rpc_slot     *slot;
	struct c2_rpc_slot_ref *sref;

	C2_PRE(c2_rpc_machine_is_locked(machine));

	sref = &item->ri_slot_refs[0];
	if (sref->sr_session_id > SESSION_ID_MAX)
		return -EINVAL;

	conn = find_conn(machine, item);
	if (conn == NULL)
		return -ENOENT;

	session = c2_rpc_session_search(conn, sref->sr_session_id);
	if (session == NULL || sref->sr_slot_id >= session->s_nr_slots)
		return -ENOENT;

	slot = session->s_slot_table[sref->sr_slot_id];
	/* XXX Check generation of slot */
	C2_ASSERT(slot != NULL);
	sref->sr_slot    = slot;
	item->ri_session = session;
	C2_POST(item->ri_session != NULL &&
		item->ri_slot_refs[0].sr_slot != NULL);

	return 0;
}

int c2_rpc_item_received(struct c2_rpc_item    *item,
			 struct c2_rpc_machine *machine)
{
	int rc;

	C2_ASSERT(item != NULL);
	C2_PRE(c2_rpc_machine_is_locked(machine));

	/** @todo XXX This code path assumes item is of kind request or reply.
		      Add handling for one-way items.
	 */
	rc = associate_session_and_slot(item, machine);
	if (rc == 0) {
		item_exit_stats_set(item, C2_RPC_PATH_INCOMING);
		rc = c2_rpc_slot_item_received(item);
	} else if (c2_rpc_item_is_conn_establish(item)) {
		c2_rpc_item_dispatch(item);
		rc = 0;
	} else {
		/*
		 * If we cannot associate the item with its slot
		 * then there is nothing that we can do with this
		 * item except to discard it.
		 * XXX generate ADDB record
		 */
	}
	return rc;
}

int c2_rpc_slot_item_received(struct c2_rpc_item *item)
{
	struct c2_rpc_item *req;
	struct c2_rpc_slot *slot;
	int                 rc = 0;

	slot = item->ri_slot_refs[0].sr_slot;
	C2_ASSERT(slot != NULL);

	if (c2_rpc_item_is_request(item))
		rc = c2_rpc_slot_item_apply(slot, item);
	else if (c2_rpc_item_is_reply(item))
		rc = c2_rpc_slot_reply_received(slot, item, &req);

	return rc;
}

/**
 * @TODO XXX ->replied() callback should be triggered
 * iff item is in WAITING_FOR_REPLY state.
 */
void rpc_item_replied(struct c2_rpc_item *item, struct c2_rpc_item *reply,
                      uint32_t rc)
{
	item->ri_error = rc;
	item->ri_reply = reply;

	c2_rpc_item_change_state(item, C2_RPC_ITEM_REPLIED);
	if (item->ri_ops != NULL && item->ri_ops->rio_replied != NULL)
			item->ri_ops->rio_replied(item);
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
int c2_rpc_slot_item_list_print(struct c2_rpc_slot *slot, bool only_active, int count)
{
	struct c2_rpc_item *item;
	bool                first = true;
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
		/* Skip dummy item */
		if (first) {
			first = false;
			continue;
		}
		if (ergo(only_active,
			 C2_IN(item->ri_stage,
			       (RPC_ITEM_STAGE_IN_PROGRESS,
				RPC_ITEM_STAGE_FUTURE)))) {

			printf("%d: item %p <%u, %lu>  state %s\n",
					++count,
					item,
					slot->sl_slot_id,
					item->ri_slot_refs[0].sr_xid,
					str_stage[item->ri_stage]);
		}
	}
	return count;
}
#endif
bool c2_rpc_slot_can_item_add_internal(const struct c2_rpc_slot *slot)
{
	return slot->sl_in_flight < slot->sl_max_in_flight;
}
