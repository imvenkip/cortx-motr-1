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

#define M0_TRACE_SUBSYSTEM M0_TRACE_SUBSYS_RPC
#include "lib/trace.h"

#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/misc.h"
#include "lib/bitstring.h"
#include "lib/finject.h"
#include "fop/fop.h"
#include "lib/arith.h"
#include "db/db.h"
#include "dtm/verno.h"
#include "mero/magic.h"
#include "rpc/rpc_internal.h"

/**
   @addtogroup rpc_session

   @{

   This file contains implementation of rpc slots.

 */

M0_INTERNAL void frm_item_reply_received(struct m0_rpc_item *reply_item,
					 struct m0_rpc_item *req_item);
M0_INTERNAL void m0_rpc_item_set_stage(struct m0_rpc_item *item,
				       enum m0_rpc_item_stage stage);
M0_INTERNAL int m0_rpc_slot_item_received(struct m0_rpc_item *item);

static void duplicate_item_received(struct m0_rpc_slot *slot,
				    struct m0_rpc_item *item);
static void misordered_item_received(struct m0_rpc_slot *slot,
				     struct m0_rpc_item *item);

M0_TL_DESCR_DEFINE(slot_item, "slot-ref-item-list", M0_INTERNAL,
		   struct m0_rpc_item, ri_slot_refs[0].sr_link, ri_magic,
		   M0_RPC_ITEM_MAGIC, M0_RPC_SLOT_REF_HEAD_MAGIC);
M0_TL_DEFINE(slot_item, M0_INTERNAL, struct m0_rpc_item);

static struct m0_rpc_machine *
slot_get_rpc_machine(const struct m0_rpc_slot *slot)
{
	return slot->sl_session->s_conn->c_rpc_machine;
}

M0_INTERNAL bool m0_rpc_slot_invariant(const struct m0_rpc_slot *slot)
{
	struct m0_rpc_item *item1 = NULL;  /* init to NULL, required */
	struct m0_rpc_item *item2;
	struct m0_verno    *v1;
	struct m0_verno    *v2;
	bool                ok;

	ok = _0C(slot != NULL) &&
	     _0C(slot->sl_in_flight <= slot->sl_max_in_flight) &&
	     M0_CHECK_EX(m0_tlist_invariant(&slot_item_tl,
					    &slot->sl_item_list));
	if (!ok)
		return false;

	/*
	 * Traverse slot->sl_item_list using item2 ptr
	 * item1 will be previous item of item2 i.e.
	 * next(item1) == item2
	 */
	for_each_item_in_slot(item2, slot) {

		if (item1 == NULL) {
			/*
			 * First element is dummy item. So no need to check.
			 */
			item1 = item2;
			continue;
		}
		ok = _0C(ergo(M0_IN(item2->ri_stage,
				(RPC_ITEM_STAGE_PAST_VOLATILE,
				 RPC_ITEM_STAGE_PAST_COMMITTED)),
			  item2->ri_reply != NULL));
		if (!ok)
			return false;

		ok = _0C(item1->ri_stage <= item2->ri_stage);
		if (!ok)
			return false;

		v1 = item_verno(item1, 0);
		v2 = item_verno(item2, 0);

		/*
		 * AFTER an "update" item is applied on a slot
		 * the version number of slot is advanced
		 */
		ok = m0_rpc_item_is_update(item1) ?
			_0C(v1->vn_vc < v2->vn_vc) :
			_0C(v1->vn_vc <= v2->vn_vc);
		if (!ok)
			return false;

		ok = _0C(item_xid(item1, 0) < item_xid(item2, 0));
		if (!ok)
			return false;

		item1 = item2;
	} end_for_each_item_in_slot;
	return true;
}

M0_INTERNAL int m0_rpc_slot_init(struct m0_rpc_slot *slot,
				 const struct m0_rpc_slot_ops *ops)
{
	struct m0_fop          *fop;
	struct m0_rpc_item     *dummy_item;
	struct m0_rpc_slot_ref *sref;

	M0_ENTRY("slot: %p", slot);

	/*
	 * Allocate dummy item.
	 * The dummy item is used to avoid special cases
	 * i.e. last_sent == NULL, last_persistent == NULL
	 */
	fop = m0_fop_alloc(&m0_rpc_fop_noop_fopt, NULL);
	if (fop == NULL)
		M0_RETURN(-ENOMEM);

	/*
	 * Add a dummy item with very low verno in item_list
	 */
	dummy_item = &fop->f_item;
	dummy_item->ri_stage     = RPC_ITEM_STAGE_PAST_COMMITTED;
	dummy_item->ri_reply     = NULL;

	/*
	 * XXX temporary value for lsn. This will be set to some proper value
	 * when sessions will be integrated with FOL
	 */
	*slot = (struct m0_rpc_slot){
		.sl_verno = {
			.vn_lsn = M0_LSN_RESERVED_NR + 2,
			.vn_vc  = 0,
		},
		.sl_slot_gen        = 0,
		.sl_xid             = 1, /* xid 0 will be taken by dummy item */
		.sl_in_flight       = 0,
		.sl_max_in_flight   = SLOT_DEFAULT_MAX_IN_FLIGHT,
		.sl_ops             = ops,
		.sl_last_sent       = dummy_item,
		.sl_last_persistent = dummy_item,
	};

	ready_slot_tlink_init(slot);
	slot_item_tlist_init(&slot->sl_item_list);

	sref = &dummy_item->ri_slot_refs[0];
	*sref = (struct m0_rpc_slot_ref){
		.sr_slot = slot,
		.sr_item = dummy_item,
		.sr_ow   = {
			.osr_xid      = 0,
			/*
			 * XXX lsn will be assigned to some proper value once
			 * sessions code will be integrated with FOL
			 */
			.osr_verno    = {
				.vn_lsn = M0_LSN_DUMMY_ITEM,
				.vn_vc  = 0,
			},
			.osr_slot_gen = slot->sl_slot_gen,
		},
	};

	slot_item_tlink_init_at(dummy_item, &slot->sl_item_list);
	M0_RETURN(0);
}

static void reply_item_put(struct m0_rpc_item *item)
{
	struct m0_rpc_item *reply = item->ri_reply;

	if (reply != NULL)
		m0_rpc_item_put(reply);
	item->ri_reply = NULL;
}

/**
  Frees all the items from slot->sl_item_list except dummy_item.

  XXX This is temporary. This routine will be scraped entirely.
  When slots will be integrated with FOL, there will be some pruning mechanism
  that will evict items from slot's item_list. But for now, we need to be able
  to fini() slot for testing purpose. That's why freeing the items explicitly.
 */
static void slot_item_list_prune(struct m0_rpc_slot *slot)
{
	struct m0_rpc_item  *item;
	struct m0_rpc_item  *dummy_item;
	bool                 first_item = true;

	M0_ENTRY("slot: %p", slot);
	/*
	 * XXX See comments above function prototype
	 */
	M0_ASSERT(slot != NULL);

	for_each_item_in_slot(item, slot) {

		if (first_item) {
			/*
			 * Don't delete dummy item
			 */
			first_item = false;
			continue;
		}
		slot_item_tlist_del(item);
		reply_item_put(item);
		m0_rpc_item_put(item);
	} end_for_each_item_in_slot;
        M0_ASSERT(slot_item_tlist_length(&slot->sl_item_list) == 1);

        dummy_item = slot_item_tlist_head(&slot->sl_item_list);
        M0_ASSERT(slot_item_tlink_is_in(dummy_item));

	slot->sl_last_sent = dummy_item;
	slot->sl_last_persistent = dummy_item;
	M0_LEAVE();
}

M0_INTERNAL void m0_rpc_slot_fini(struct m0_rpc_slot *slot)
{
	struct m0_rpc_item  *dummy_item;
	struct m0_fop       *fop;

	M0_ENTRY("slot: %p", slot);

	slot_item_list_prune(slot);
	ready_slot_tlink_fini(slot);

	/*
	 * Remove the dummy item from the list
	 */
        M0_ASSERT(slot_item_tlist_length(&slot->sl_item_list) == 1);

        dummy_item = slot_item_tlist_pop(&slot->sl_item_list);
	M0_ASSERT(item_xid(dummy_item, 0) == 0);

	fop = m0_rpc_item_to_fop(dummy_item);
	m0_fop_put(fop);

	slot_item_tlist_fini(&slot->sl_item_list);
	M0_SET0(slot);
	M0_LEAVE();
}

/**
   Searches slot->sl_item_list to find item with matching @xid.
   @return item if found, NULL otherwise.
 */
static struct m0_rpc_item* item_find(const struct m0_rpc_slot *slot,
				     uint64_t                  xid)
{
	struct m0_rpc_item *item;

	M0_PRE(slot != NULL);
	for_each_item_in_slot(item, slot) {

		if (item_xid(item, 0) == xid)
			return item;
	} end_for_each_item_in_slot;
	return NULL;
}

M0_INTERNAL uint32_t m0_rpc_slot_items_possible_inflight(struct m0_rpc_slot
							 *slot)
{
	M0_PRE(slot != NULL);

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
static void __slot_balance(struct m0_rpc_slot *slot,
			   bool                allow_events)
{
	struct m0_rpc_item *item;
	int                 rc;

	M0_ENTRY("slot: %p", slot);
	M0_PRE(m0_rpc_slot_invariant(slot));
	M0_PRE(m0_rpc_machine_is_locked(slot_get_rpc_machine(slot)));

	while (slot->sl_in_flight < slot->sl_max_in_flight) {
		item = slot_item_tlist_next(&slot->sl_item_list,
					    slot->sl_last_sent);
		if (item == NULL) {
			if (allow_events)
				slot->sl_ops->so_slot_idle(slot);
			break;
		} else {
			if (allow_events)
				slot->sl_ops->so_slot_busy(slot);
		}

		if (item->ri_stage == RPC_ITEM_STAGE_FUTURE)
			m0_rpc_item_set_stage(item, RPC_ITEM_STAGE_IN_PROGRESS);

		if (item->ri_reply != NULL && !m0_rpc_item_is_update(item)) {
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
		if (allow_events) {
			rc = slot->sl_ops->so_item_consume(item);
			if (rc != 0)
				m0_rpc_item_failed(item, rc);
		}
	}
	M0_POST(m0_rpc_slot_invariant(slot));
	M0_LEAVE();
}

/**
   For more information see __slot_balance()
   @see __slot_balance()
 */
static void slot_balance(struct m0_rpc_slot *slot)
{
	__slot_balance(slot, true);
}

/**
   @see m0_rpc_slot_item_add_internal()
 */
static void __slot_item_add(struct m0_rpc_slot *slot,
			    struct m0_rpc_item *item)
{
	struct m0_rpc_session  *session;
	struct m0_rpc_machine  *machine;

	M0_ENTRY("slot: %p, item: %p", slot, item);
	M0_PRE(item != NULL);
	M0_PRE(m0_rpc_slot_invariant(slot));
	M0_PRE(m0_rpc_item_is_request(item));
	M0_PRE(slot->sl_session == item->ri_session);
	M0_PRE(slot->sl_session != NULL);

	session = slot->sl_session;
	machine = session_machine(session);
	M0_PRE(m0_rpc_machine_is_locked(machine));

	item->ri_slot_refs[0] = (struct m0_rpc_slot_ref){
		.sr_ow = {
			.osr_session_id = session->s_session_id,
			.osr_sender_id  = session->s_conn->c_sender_id,
			.osr_uuid       = session->s_conn->c_uuid,
			/*
			 * m0_rpc_slot_item_apply() will provide an item
			 * which already has verno initialised. Yet, following
			 * assignment should not be any problem because
			 * slot_item_apply() will call this routine only if
			 * verno of slot and item matches
			 */
			.osr_slot_id    = slot->sl_slot_id,
			.osr_verno      = slot->sl_verno,
			.osr_xid        = slot->sl_xid,
			.osr_slot_gen   = slot->sl_slot_gen,
		},
		.sr_slot = slot,
		.sr_item = item,
	};

	slot->sl_xid++;
	if (m0_rpc_item_is_update(item)) {
		slot->sl_verno.vn_lsn++;
		slot->sl_verno.vn_vc++;
	}

	m0_rpc_item_get(item);
	slot_item_tlink_init_at_tail(item, &slot->sl_item_list);
	item->ri_stage = RPC_ITEM_STAGE_FUTURE;
	m0_rpc_session_mod_nr_active_items(session, 1);
	M0_LEAVE();
}

M0_INTERNAL void m0_rpc_slot_item_add_internal(struct m0_rpc_slot *slot,
					       struct m0_rpc_item *item)
{
	M0_ENTRY("slot: %p, item: %p", slot, item);
	__slot_item_add(slot, item);
	__slot_balance(slot, false); /* not allowed to trigger events */
	M0_LEAVE();
}

M0_INTERNAL void m0_rpc_slot_item_add(struct m0_rpc_slot *slot,
				      struct m0_rpc_item *item)
{
	M0_ENTRY("slot: %p, item: %p", slot, item);
	__slot_item_add(slot, item);
	if (m0_rpc_conn_is_snd(slot->sl_session->s_conn))
		m0_rpc_item_change_state(item, M0_RPC_ITEM_WAITING_IN_STREAM);
	slot_balance(slot);
	M0_LEAVE();
}

static void misordered_item_received(struct m0_rpc_slot *slot,
				     struct m0_rpc_item *item)
{
	struct m0_rpc_item *reply;
	struct m0_fop      *fop;

	M0_ENTRY("slot: %p, item: %p", slot, item);
	/*
	 * Send a dummy NOOP fop as reply to report misordered item
	 * XXX We should've a special fop type to report session error
	 */
	fop = m0_fop_alloc(&m0_rpc_fop_noop_fopt, NULL);
	if (fop != NULL) {
		reply = &fop->f_item;
		reply->ri_session = item->ri_session;
		reply->ri_rmachine = item->ri_rmachine;
		reply->ri_error   = -EBADR;
		reply->ri_slot_refs[0] = item->ri_slot_refs[0];
		slot_item_tlink_init(reply);
		m0_rpc_item_sm_init(reply, M0_RPC_ITEM_OUTGOING);

		slot->sl_ops->so_reply_consume(item, reply);
	}
	m0_fop_put(fop);
}

M0_INTERNAL int m0_rpc_slot_item_apply(struct m0_rpc_slot *slot,
				       struct m0_rpc_item *item)
{
	int rc = -EPROTO;

	M0_ENTRY("slot: %p, item: %p", slot, item);
	M0_ASSERT(item != NULL);
	M0_ASSERT(m0_rpc_slot_invariant(slot));
	M0_PRE(m0_rpc_machine_is_locked(slot_get_rpc_machine(slot)));

	M0_LOG(M0_DEBUG, "item_xid=%llx slot_xid=%llx",
		(long long unsigned)item_xid(item, 0),
		(long long unsigned)slot->sl_xid);
	if (M0_FI_ENABLED("misorder_item")) {
		misordered_item_received(slot, item);
	} else if (item_xid(item, 0) == slot->sl_xid) {
		/* valid in sequence */
		__slot_item_add(slot, item);
		slot_balance(slot);
		rc = 0;
	} else if (item_xid(item, 0) < slot->sl_xid) {
		duplicate_item_received(slot, item);
	} else {
		/* neither duplicate nor in seq */
		misordered_item_received(slot, item);
	}
	M0_POST(m0_rpc_slot_invariant(slot));
	M0_RETURN(rc);
}

static void duplicate_item_received(struct m0_rpc_slot *slot,
				    struct m0_rpc_item *item)
{
	struct m0_rpc_item *req;

	M0_ENTRY("slot: %p item: %p", slot, item);
	/* item is a duplicate request. Find original. */
	req = item_find(slot, item_xid(item, 0));
	if (req == NULL) {
		misordered_item_received(slot, item);
		M0_LEAVE();
		return;
	}
	/*
	 * XXX At this point req->ri_slot_refs[0].sr_verno and
	 * item->ri_slot_refs[0].sr_verno MUST be same. If they are
	 * not same then generate ADDB record.
	 * For now, assert this condition for testing purpose.
	 */
	M0_ASSERT(m0_verno_cmp(item_verno(req, 0),
			       item_verno(item, 0)) == 0);

	M0_LOG(M0_DEBUG, "req_stage=%d", req->ri_stage);
	switch (req->ri_stage) {
	case RPC_ITEM_STAGE_PAST_VOLATILE:
	case RPC_ITEM_STAGE_PAST_COMMITTED:
		/*
		 * @item is duplicate and corresponding original is
		 * already consumed (i.e. executed if item is FOP).
		 * Consume cached reply. (on receiver, this means
		 * resend cached reply)
		 */
		M0_ASSERT(req->ri_reply != NULL);
		slot->sl_ops->so_reply_consume(req, req->ri_reply);
		break;
	case RPC_ITEM_STAGE_IN_PROGRESS:
	case RPC_ITEM_STAGE_FUTURE:
		/* item is already present but is not
		   processed yet. Ignore it*/
		/* do nothing */;
		break;
	case RPC_ITEM_STAGE_FAILED:
		/* The request is failed, but receiver could not send reply.
		   e.g. consider fom allocation failed during
			m0_reqh_fop_handle()
		   Ignore the request. Sender side request should TIMEOUT.
		 */
		M0_LOG(M0_INFO, "Duplicate request of FAILED item rcvd");
		break;
	case RPC_ITEM_STAGE_TIMEDOUT:
		M0_IMPOSSIBLE("Original req in TIMEDOUT/FAILED stage");
		break;
	default:
		M0_IMPOSSIBLE("Invalid value of m0_rpc_item::ri_stage");
	}
	/*
	 * Irrespective of any of above cases, we're going to
	 * ignore this _duplicate_ item.
	 */
	M0_LEAVE();
}

M0_INTERNAL int m0_rpc_slot_reply_received(struct m0_rpc_slot *slot,
					   struct m0_rpc_item *reply,
					   struct m0_rpc_item **req_out)
{
	struct m0_rpc_item     *req;
	struct m0_rpc_slot_ref *sref;
	struct m0_rpc_machine  *machine;
	int                     rc;

	M0_ENTRY("slot: %p, item_reply: %p", slot, reply);
	M0_PRE(slot != NULL && reply != NULL && req_out != NULL);

	machine = slot_get_rpc_machine(slot);
	M0_PRE(m0_rpc_machine_is_locked(machine));
	M0_ASSERT(m0_rpc_slot_invariant(slot));

	*req_out = NULL;

	sref = &reply->ri_slot_refs[0];
	M0_ASSERT(slot == sref->sr_slot);

	req = item_find(slot, item_xid(reply, 0));
	if (req == NULL) {
		/*
		 * Either it is a duplicate reply and its corresponding request
		 * item is pruned from the item list, or it is a corrupted
		 * reply
		 * XXX This situation is not expected to arise during testing.
		 *     When control reaches this point during testing it might
		 *     be because of a possible bug. So assert.
		 */
		M0_RETURN(-EPROTO);
	}
	rc = __slot_reply_received(slot, req, reply);
	if (rc == 0)
		*req_out = req;

	return rc;
}

M0_INTERNAL int __slot_reply_received(struct m0_rpc_slot *slot,
				      struct m0_rpc_item *req,
				      struct m0_rpc_item *reply)
{
	int rc;

	M0_PRE(slot != NULL && req != NULL && reply != NULL);

	/*
	 * At this point req->ri_slot_refs[0].sr_verno and
	 * reply->ri_slot_refs[0].sr_verno MUST be same. If they are not,
	 * then generate ADDB record.
	 * For now, assert this condition for testing purpose.
	 */
	M0_ASSERT(m0_verno_cmp(item_verno(req, 0), item_verno(reply, 0)) == 0);

	rc = -EPROTO;
	if (m0_verno_cmp(item_verno(req, 0),
			 item_verno(slot->sl_last_sent, 0)) > 0) {
		/*
		 * Received a reply to an item that wasn't sent. This is
		 * possible if the receiver failed and forget about some
		 * items. The sender moved last_seen to the past, but then a
		 * late reply to one of items unreplied before the failure
		 * arrived.
		 *
		 * Such reply must be ignored
		 */
		/* Do nothing. */;
	} else if (M0_IN(req->ri_stage, (RPC_ITEM_STAGE_TIMEDOUT,
					 RPC_ITEM_STAGE_FAILED))) {
		/*
		 * TIMEDOUT:
		 * The reply is valid but too late. The req has already
		 * timedout. Return without setting *req_out.
		 * FAILED:
		 * FAILED items are not supposed to receive replies, but
		 * this might be a result of corruption
		 */
		/* Do nothing */
		M0_LOG(M0_DEBUG, "rply rcvd, timedout/failed req %p [%s/%u]",
			req, item_kind(req), req->ri_type->rit_opcode);
	} else {
		/*
		 * This is valid reply case.
		 */
		M0_PRE(M0_IN(req->ri_stage, (RPC_ITEM_STAGE_PAST_COMMITTED,
					     RPC_ITEM_STAGE_PAST_VOLATILE,
					     RPC_ITEM_STAGE_IN_PROGRESS)));
		M0_ASSERT(ergo(req->ri_stage == RPC_ITEM_STAGE_IN_PROGRESS,
			       slot->sl_in_flight > 0));

		m0_rpc_item_get(reply);

		switch (req->ri_sm.sm_state) {
		case M0_RPC_ITEM_ENQUEUED:
		case M0_RPC_ITEM_URGENT:
			m0_rpc_frm_remove_item(
				&req->ri_session->s_conn->c_rpcchan->rc_frm,
				req);
			m0_rpc_slot_process_reply(req, reply);
			m0_rpc_session_release(req->ri_session);
			break;

		case M0_RPC_ITEM_SENDING:
			/*
			 * Buffer sent callback is still pending;
			 * postpone reply processing.
			 * item_sent() will process the reply.
			 */
			M0_LOG(M0_DEBUG, "req: %p rply: %p rply postponed",
			       req, reply);
			req->ri_pending_reply = reply;
			break;

		case M0_RPC_ITEM_ACCEPTED:
		case M0_RPC_ITEM_WAITING_FOR_REPLY:
			m0_rpc_slot_process_reply(req, reply);
			break;

		case M0_RPC_ITEM_REPLIED:
			/* Duplicate reply. Drop it. */
			req->ri_rmachine->rm_stats.rs_nr_dropped_items++;
			m0_rpc_item_put(reply);
			break;

		default:
			M0_ASSERT(false);
		}
		rc = 0;
	}
	return rc;
}

M0_INTERNAL void m0_rpc_slot_process_reply(struct m0_rpc_item *req,
					   struct m0_rpc_item *reply)
{
	struct m0_rpc_slot *slot;

	M0_ENTRY("req: %p", req);

	M0_PRE(req != NULL && reply != NULL);
	M0_PRE(m0_rpc_item_is_request(req));
	M0_PRE(M0_IN(req->ri_sm.sm_state, (M0_RPC_ITEM_WAITING_FOR_REPLY,
					   M0_RPC_ITEM_ACCEPTED,
					   M0_RPC_ITEM_ENQUEUED,
					   M0_RPC_ITEM_URGENT)));
	M0_PRE(M0_IN(req->ri_stage, (RPC_ITEM_STAGE_PAST_COMMITTED,
				     RPC_ITEM_STAGE_PAST_VOLATILE,
				     RPC_ITEM_STAGE_IN_PROGRESS)));

	m0_rpc_item_stop_timer(req);
	if (req->ri_stage == RPC_ITEM_STAGE_IN_PROGRESS) {
		req->ri_reply = reply;
		if (req->ri_ops != NULL && req->ri_ops->rio_replied != NULL &&
		    reply->ri_type != &m0_rpc_fop_noop_fopt.ft_rpc_item_type)
			req->ri_ops->rio_replied(req);
		m0_rpc_item_set_stage(req, RPC_ITEM_STAGE_PAST_VOLATILE);
	} else {
		/*
		 * Got a reply to an item for which the reply was already
		 * received in the past. Compare with the original reply.
		 * XXX find out how to compare two rpc items to be same
		 */
		M0_ASSERT(req->ri_reply != NULL);
		req->ri_rmachine->rm_stats.rs_nr_dropped_items++;
		m0_rpc_item_put(reply);
	}
	m0_rpc_item_change_state(req, M0_RPC_ITEM_REPLIED);

	slot = req->ri_slot_refs[0].sr_slot;
	if (slot->sl_last_sent == req && slot->sl_in_flight == 1) {
		M0_ASSERT(slot->sl_max_in_flight == 1);
		slot->sl_in_flight--;
		slot_balance(slot);
	}
	/*
	 * On receiver, ->so_reply_consume(req, reply) will hand over
	 * @reply to formation, to send it back to sender.
	 * see: rcv_reply_consume(), snd_reply_consume()
	 */
	slot->sl_ops->so_reply_consume(req, req->ri_reply);
	M0_LEAVE();
}

M0_INTERNAL void m0_rpc_slot_persistence(struct m0_rpc_slot *slot,
					 struct m0_verno last_persistent)
{
	struct m0_rpc_item     *item;

	M0_ENTRY("slot: %p, lsn_of_last_persistent: %llu", slot,
		 (unsigned long long)last_persistent.vn_lsn);
	M0_PRE(m0_rpc_slot_invariant(slot));
	M0_PRE(m0_rpc_machine_is_locked(slot_get_rpc_machine(slot)));

	for (item = slot->sl_last_persistent; item != NULL &&
	     m0_verno_cmp(item_verno(item, 0), &last_persistent) <= 0;
	     item = slot_item_tlist_next(&slot->sl_item_list, item)) {

		M0_ASSERT(M0_IN(item->ri_stage,
				(RPC_ITEM_STAGE_PAST_COMMITTED,
				 RPC_ITEM_STAGE_PAST_VOLATILE)));

		m0_rpc_item_set_stage(item,
				      RPC_ITEM_STAGE_PAST_COMMITTED);
		slot->sl_last_persistent = item;
	}

	M0_POST(m0_verno_cmp(item_verno(slot->sl_last_persistent, 0),
			     &last_persistent) >= 0);
	M0_LEAVE();
}

M0_INTERNAL void m0_rpc_slot_reset(struct m0_rpc_slot *slot,
				   struct m0_verno last_seen)
{
	struct m0_rpc_item     *item;
	struct m0_rpc_slot_ref *sref;

	M0_ENTRY("slot: %p, lsn_last_seen: %llu", slot,
		 (unsigned long long)last_seen.vn_lsn);
	M0_PRE(m0_rpc_slot_invariant(slot));
	M0_PRE(m0_rpc_machine_is_locked(slot_get_rpc_machine(slot)));
	M0_PRE(m0_verno_cmp(&slot->sl_verno, &last_seen) >= 0);

	for_each_item_in_slot(item, slot) {

		sref = &item->ri_slot_refs[0];
		if (m0_verno_cmp(&sref->sr_ow.osr_verno, &last_seen) == 0) {
			M0_ASSERT(item->ri_stage != RPC_ITEM_STAGE_FUTURE);
			slot->sl_last_sent = item;
			break;
		}

	} end_for_each_item_in_slot;
	M0_ASSERT(m0_verno_cmp(item_verno(slot->sl_last_sent, 0),
			       &last_seen) == 0);
	slot_balance(slot);
	M0_LEAVE();
}

M0_INTERNAL struct m0_rpc_conn *
m0_rpc_machine_find_conn(const struct m0_rpc_machine *machine,
			 const struct m0_rpc_item    *item)
{
	const struct m0_rpc_onwire_slot_ref *sref;
	const struct m0_tl                  *conn_list;
	struct m0_rpc_conn                  *conn;
	bool                                 use_uuid;

	M0_ENTRY("machine: %p, item: %p", machine, item);

	sref = &item->ri_slot_refs[0].sr_ow;
	use_uuid = (sref->osr_sender_id == SENDER_ID_INVALID);
	conn_list = m0_rpc_item_is_request(item) ?
			&machine->rm_incoming_conns :
			&machine->rm_outgoing_conns;

	m0_tl_for(rpc_conn, conn_list, conn) {
		if (use_uuid) {
			if (m0_uint128_cmp(&conn->c_uuid,
					   &sref->osr_uuid) == 0)
				break;
		} else if (conn->c_sender_id == sref->osr_sender_id) {
			break;
		}
	} m0_tl_endfor;

	M0_LEAVE("conn: %p", conn);
	return conn;
}

static int associate_session_and_slot(struct m0_rpc_item    *item,
				      struct m0_rpc_machine *machine)
{
	struct m0_rpc_conn     *conn;
	struct m0_rpc_session  *session;
	struct m0_rpc_slot     *slot;
	struct m0_rpc_slot_ref *sref;

	M0_ENTRY("item: %p, machine: %p", item, machine);
	M0_PRE(m0_rpc_machine_is_locked(machine));

	sref = &item->ri_slot_refs[0];
	if (sref->sr_ow.osr_session_id > SESSION_ID_MAX)
		M0_RETERR(-EINVAL, "rpc_session_id");

	conn = m0_rpc_machine_find_conn(machine, item);
	if (conn == NULL)
		M0_RETURN(-ENOENT);

	session = m0_rpc_session_search(conn, sref->sr_ow.osr_session_id);
	if (session == NULL || sref->sr_ow.osr_slot_id >= session->s_nr_slots)
		M0_RETURN(-ENOENT);

	slot = session->s_slot_table[sref->sr_ow.osr_slot_id];
	/* XXX Check generation of slot */
	M0_ASSERT(slot != NULL);
	sref->sr_slot    = slot;
	item->ri_session = session;

	M0_POST(item->ri_session != NULL &&
		item->ri_slot_refs[0].sr_slot != NULL);
	M0_RETURN(0);
}

M0_INTERNAL int m0_rpc_item_received(struct m0_rpc_item *item,
				     struct m0_rpc_machine *machine)
{
	int rc;

	M0_ENTRY("item: %p, machine: %p", item, machine);
	M0_PRE(item != NULL);
	M0_PRE(m0_rpc_machine_is_locked(machine));

	m0_addb_counter_update(&machine->rm_cntr_rcvd_item_sizes,
			       (uint64_t)m0_rpc_item_size(item));
	++machine->rm_stats.rs_nr_rcvd_items;

	if (m0_rpc_item_is_oneway(item)) {
		m0_rpc_item_dispatch(item);
		M0_RETURN(0);
	}

	M0_ASSERT(m0_rpc_item_is_request(item) || m0_rpc_item_is_reply(item));
	rc = associate_session_and_slot(item, machine);
	if (rc == 0)
		M0_RETURN(m0_rpc_slot_item_received(item));

	if (m0_rpc_item_is_conn_establish(item)) {
		m0_rpc_item_dispatch(item);
		M0_RETURN(0);
	}

	/*
	 * We cannot associate the item with its slot. The only thing
	 * that we can do with this item is to discard it.
	 *
	 * XXX TODO: Generate ADDB record.
	 *
	 * At this point the item has only 1 reference on it. This
	 * reference will be dropped in packet_received(), resulting
	 * in the item getting deallocated.
	 */
	M0_RETURN(rc);
}

M0_INTERNAL int m0_rpc_slot_item_received(struct m0_rpc_item *item)
{
	struct m0_rpc_item *req;
	struct m0_rpc_slot *slot;
	int                 rc = 0;

	slot = item->ri_slot_refs[0].sr_slot;
	M0_ASSERT(slot != NULL);

	if (m0_rpc_item_is_request(item))
		rc = m0_rpc_slot_item_apply(slot, item);
	else if (m0_rpc_item_is_reply(item))
		rc = m0_rpc_slot_reply_received(slot, item, &req);

	return rc;
}

/**
   Just for debugging purpose.
 */
#ifndef __KERNEL__
int m0_rpc_slot_item_list_print(struct m0_rpc_slot *slot,
				bool                only_active,
				int                 count)
{
	struct m0_rpc_item *item;
	bool                first = true;
	char                str_stage[][20] = {
				"INVALID",
				"PAST_COMMITTED",
				"PAST_VOLATILE",
				"IN_PROGRESS",
				"FUTURE"
			     };

	for_each_item_in_slot(item, slot) {
		/* Skip dummy item */
		if (first) {
			first = false;
			continue;
		}
		if (ergo(only_active,
			 M0_IN(item->ri_stage,
			       (RPC_ITEM_STAGE_IN_PROGRESS,
				RPC_ITEM_STAGE_FUTURE)))) {

			printf("%d: item %p <%u, %lu>  state %s\n",
			       ++count,
			       item,
			       slot->sl_slot_id,
			       item_xid(item, 0),
			       str_stage[item->ri_stage]);
		}
	} end_for_each_item_in_slot;
	return count;
}
#endif

/** @} */
#undef M0_TRACE_SUBSYSTEM

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
