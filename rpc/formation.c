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
 * Original author: Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 04/28/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "rpc/rpc2.h"
#include "lib/errno.h"
#include "lib/memory.h"
#include "lib/cdefs.h"
#include "lib/trace.h"
#include "lib/misc.h"

#include "rpc/rpc_onwire.h"

C2_TL_DESCR_DECLARE(rpcitem, extern);

/* ADDB Instrumentation for rpc formation. */
static const struct c2_addb_ctx_type frm_addb_ctx_type = {
        .act_name = "rpc-formation"
};

static const struct c2_addb_loc frm_addb_loc = {
        .al_name = "rpc-formation"
};

C2_ADDB_EV_DEFINE(formation_func_fail, "formation_func_fail",
		C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

void rpc_item_replied(struct c2_rpc_item *item, struct c2_rpc_item *reply,
		      uint32_t rc);

extern void item_exit_stats_set(struct c2_rpc_item *item,
				enum c2_rpc_item_path path);

extern void rpcobj_exit_stats_set(const struct c2_rpc         *rpcobj,
				  struct c2_rpc_machine       *mach,
		                  const enum c2_rpc_item_path  path);

extern void send_buffer_deallocate(struct c2_net_buffer *nb,
				   struct c2_net_domain *net_dom);

extern int send_buffer_allocate(struct c2_net_domain *net_dom,
		struct c2_net_buffer **nb, uint64_t rpc_size);

/* Forward declarations of local static functions. */
static void frm_item_remove(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item);

static void frm_item_add(struct c2_rpc_frm_sm *frm_sm,
			 struct c2_rpc_item *item);

static void frm_send_onwire(struct c2_rpc_frm_sm *frm_sm);

static void sm_updating_state(struct c2_rpc_frm_sm *frm_sm,
			      struct c2_rpc_item *item);

static void sm_forming_state(struct c2_rpc_frm_sm *frm_sm,
			    struct c2_rpc_item *item);

static struct c2_rpc_machine *
frm_sm_to_rpc_machine(struct c2_rpc_frm_sm *frm_sm)
{
	const struct c2_rpc_chan *chan;

	chan = container_of(frm_sm, struct c2_rpc_chan, rc_frmsm);

	return chan->rc_rpc_machine;
}

static bool frm_is_locked(struct c2_rpc_frm_sm *frm)
{
	return c2_rpc_machine_is_locked(frm_sm_to_rpc_machine(frm));
}

static void frm_item_rpc_stats_set(struct c2_rpc *rpc)
{
	struct c2_rpc_item *item;
	struct c2_rpc_item *first_item;

	C2_PRE(rpc != NULL);

	if (c2_list_is_empty(&rpc->r_items))
		return;
	first_item = c2_list_entry((c2_list_first(&rpc->r_items)),
				struct c2_rpc_item, ri_rpcobject_linkage);
	rpcobj_exit_stats_set(rpc,
			first_item->ri_session->s_conn->c_rpc_machine,
			C2_RPC_PATH_OUTGOING);

	c2_list_for_each_entry(&rpc->r_items, item,
			struct c2_rpc_item, ri_rpcobject_linkage)
		item_exit_stats_set(item, C2_RPC_PATH_OUTGOING);
}

/* Changes the state of each rpc item in rpc object to RPC_ITEM_SENT. */
static void frm_item_state_set(const struct c2_rpc *rpc, const enum
		c2_rpc_item_state state)
{
	struct c2_rpc_item *item;

	C2_PRE(rpc != NULL);

	c2_list_for_each_entry(&rpc->r_items, item,
			struct c2_rpc_item, ri_rpcobject_linkage) {
		C2_ASSERT(item->ri_state == RPC_ITEM_ADDED);
		item->ri_state = state;
	}
}

static void frm_item_state_failed(const struct c2_rpc *rpc, int error)
{
	struct c2_rpc_item *item;

	C2_PRE(rpc != NULL);

	c2_list_for_each_entry(&rpc->r_items, item,
			struct c2_rpc_item, ri_rpcobject_linkage) {
		C2_ASSERT(item->ri_state == RPC_ITEM_ADDED);
		item->ri_state = RPC_ITEM_SEND_FAILED;
		item->ri_error = error;
	}
}

void c2_rpcobj_init(struct c2_rpc *rpc)
{
	C2_PRE(rpc != NULL);

	c2_list_link_init(&rpc->r_linkage);
	c2_list_init(&rpc->r_items);
	rpc->r_session = NULL;
	rpc->r_fbuf.fb_magic = C2_RPC_FRM_BUFFER_MAGIC;
}

/* Invariant subroutine for struct c2_rpc_frm_buffer. */
static bool frm_buf_invariant(const struct c2_rpc_frm_buffer *fbuf)
{
	return fbuf != NULL && fbuf->fb_frm_sm != NULL &&
			fbuf->fb_magic == C2_RPC_FRM_BUFFER_MAGIC;
}

int c2_rpcobj_fbuf_init(struct c2_rpc_frm_buffer *fb,
		struct c2_rpc_frm_sm *frm_sm, struct c2_net_domain *net_dom,
		uint64_t rpc_size)
{
	int			 rc;
	struct c2_net_buffer	*nb;

	C2_PRE(fb != NULL);
	C2_PRE(frm_sm != NULL);
	C2_PRE(net_dom != NULL);
	C2_PRE(rpc_size != 0);

	fb->fb_frm_sm = frm_sm;
	nb = &fb->fb_buffer;
	rc = send_buffer_allocate(net_dom, &nb, rpc_size);
	if (rc != 0)
		return rc;
	C2_POST(frm_buf_invariant(fb));
	return rc;
}

static void c2_rpcobj_fbuf_fini(struct c2_rpc_frm_buffer *fb)
{
	C2_PRE(fb != NULL);
	C2_PRE(frm_buf_invariant(fb));

	/* Currently, the policy is to release the buffer on completion. */
	send_buffer_deallocate(&fb->fb_buffer, fb->fb_buffer.nb_dom);
}

void frm_sm_fini(struct c2_rpc_frm_sm *frm_sm)
{
	int cnt;

	C2_PRE(frm_sm != NULL);
	C2_PRE(frm_sm->fs_state == C2_RPC_FRM_STATE_WAITING);

	c2_list_fini(&frm_sm->fs_groups);
	c2_list_fini(&frm_sm->fs_rpcs);
	for (cnt = 0; cnt < ARRAY_SIZE(frm_sm->fs_unformed); ++cnt)
		c2_list_fini(&frm_sm->fs_unformed[cnt].pl_unformed_items);
	c2_addb_ctx_fini(&frm_sm->fs_rpc_form_addb);
}

static bool frm_sm_invariant(const struct c2_rpc_frm_sm *frm_sm)
{
	int			 cnt;
	bool			 item_present = false;
	struct c2_rpc_chan	*chan;

	if (frm_sm == NULL)
		return false;

	chan = container_of(frm_sm, struct c2_rpc_chan, rc_frmsm);
	if (chan == NULL)
		return false;

	/* Formation state machine should be in a valid state. */
	if (frm_sm->fs_state < C2_RPC_FRM_STATE_WAITING ||
	    frm_sm->fs_state > C2_RPC_FRM_STATE_FORMING)
		return false;

	/* The transfer machine associated with this formation state machine
	   should have been started already. */
	if (chan->rc_rpc_machine->rm_tm.ntm_state != C2_NET_TM_STARTED)
		return false;

	/* Number of rpcs in flight should always be less than max limit. */
	if (frm_sm->fs_curr_rpcs_in_flight > frm_sm->fs_max_rpcs_in_flight)
		return false;

	/* The invariant data in formation state machine according to different
	   states of state machine. */
	switch (frm_sm->fs_state) {
	case C2_RPC_FRM_STATE_WAITING:
		break;
	case C2_RPC_FRM_STATE_UPDATING:
		/* If any of these lists contain items, then item sizes
		   should be accounted in frm_sm->fs_cumulative_size. */
		for (cnt = 0; cnt < C2_RPC_ITEM_PRIO_NR; ++cnt)
			if (!c2_list_is_empty(&frm_sm->fs_unformed[cnt].
					      pl_unformed_items))
				item_present = true;
		if (item_present && frm_sm->fs_cumulative_size == 0)
			return false;

		/* If there is at least one complete rpc group present
		   in state machine, then frm_sm->fs_groups list should
		   not be empty. */
		if (frm_sm->fs_complete_groups_nr > 0 &&
		    c2_list_is_empty(&frm_sm->fs_groups))
			return false;
		break;
	case C2_RPC_FRM_STATE_FORMING:
		break;
	default:
		C2_IMPOSSIBLE("Invalid state of formation state machine.");
	};
	return true;
}

static struct c2_rpc_frm_sm *item_to_frm_sm(const struct c2_rpc_item *item)
{
	struct c2_rpc_frm_sm *frm_sm;

	C2_PRE(item != NULL);
	frm_sm = &item->ri_session->s_conn->c_rpcchan->rc_frmsm;
	C2_POST(frm_sm != NULL);
	return frm_sm;
}

void frm_sm_init(struct c2_rpc_frm_sm *frm_sm, uint64_t max_rpcs_in_flight)
{
	struct c2_rpc_chan   *chan;
	struct c2_net_domain *netdom;
	int                   i;

	C2_PRE(frm_sm != NULL);

	chan = container_of(frm_sm, struct c2_rpc_chan, rc_frmsm);
	C2_PRE(c2_rpc_machine_is_locked(chan->rc_rpc_machine));

	frm_sm->fs_sender_side           = false;
	frm_sm->fs_curr_rpcs_in_flight   = 0;
	frm_sm->fs_cumulative_size       = 0;
	frm_sm->fs_urgent_nogrp_items_nr = 0;
	frm_sm->fs_complete_groups_nr    = 0;
	frm_sm->fs_timedout_items_nr     = 0;
	frm_sm->fs_items_left            = 0;

	netdom = chan->rc_rpc_machine->rm_tm.ntm_dom;

	frm_sm->fs_max_msg_size = c2_net_domain_get_max_buffer_size(netdom);
	frm_sm->fs_max_frags = c2_net_domain_get_max_buffer_segments(netdom);
	frm_sm->fs_max_rpcs_in_flight = max_rpcs_in_flight;

	c2_list_init(&frm_sm->fs_groups);
	c2_list_init(&frm_sm->fs_rpcs);

	for (i = 0; i < ARRAY_SIZE(frm_sm->fs_unformed); i++) {
		struct c2_rpc_frm_prio_list *list;

		list = &frm_sm->fs_unformed[i];

		list->pl_prio = C2_RPC_ITEM_PRIO_NR - (i + 1);
		c2_list_init(&list->pl_unformed_items);
	}

        c2_addb_ctx_init(&frm_sm->fs_rpc_form_addb, &frm_addb_ctx_type,
			 &c2_addb_global_ctx);

	frm_sm->fs_state = C2_RPC_FRM_STATE_WAITING;
}

/* Callback function for addition of a bound rpc item. */
void frm_item_ready(struct c2_rpc_item *item)
{
	struct c2_rpc_slot	*slot;
	struct c2_rpc_machine	*machine;
	struct c2_rpc_frm_sm	*frm_sm;

	C2_PRE(item != NULL);

	slot = item->ri_slot_refs[0].sr_slot;
	C2_ASSERT(slot != NULL);

	machine = slot->sl_session->s_conn->c_rpc_machine;
	C2_ASSERT(c2_rpc_machine_is_locked(machine));

	c2_list_add(&slot->sl_ready_list, &item->ri_slot_refs[0].sr_ready_link);

	if (!c2_list_link_is_in(&slot->sl_link))
		c2_list_add(&machine->rm_ready_slots, &slot->sl_link);

	frm_sm = item_to_frm_sm(item);
	sm_updating_state(frm_sm, item);
}

/* Callback function for slot becoming idle. */
void frm_slot_idle(struct c2_rpc_slot *slot)
{
	struct c2_rpc_machine *machine;

	C2_PRE(slot != NULL);
	C2_PRE(slot->sl_session != NULL);
	C2_PRE(slot->sl_in_flight == 0);
	C2_PRE(!c2_list_link_is_in(&slot->sl_link));

	machine = slot->sl_session->s_conn->c_rpc_machine;

	C2_ASSERT(c2_rpc_machine_is_locked(machine));

	c2_list_add(&machine->rm_ready_slots, &slot->sl_link);
}

/* Callback function for addition of unbounded/unsolicited item. */
void frm_ubitem_added(struct c2_rpc_item *item)
{
	struct c2_rpc_frm_sm *frm_sm;

	C2_PRE(item != NULL);
	C2_PRE(!c2_rpc_item_is_bound(item));

	frm_sm = item_to_frm_sm(item);

	C2_ASSERT(frm_is_locked(frm_sm));

	sm_updating_state(frm_sm, item);
}

/* Callback function for <struct c2_net_buffer> which indicates that
  message has been sent out from the buffer. */
void frm_net_buffer_sent(const struct c2_net_buffer_event *ev)
{
	struct c2_rpc			*rpc;
	struct c2_net_buffer		*nb;
	struct c2_rpc_frm_buffer	*fb;
	struct c2_rpc_item		*item;
	struct c2_rpc_item		*item_next;
	struct c2_rpc_frm_sm		*frm_sm;
	struct c2_rpc_machine           *machine;

	C2_PRE(ev != NULL &&
	       ev->nbe_buffer != NULL &&
	       ev->nbe_buffer->nb_qtype == C2_NET_QT_MSG_SEND);

	nb = ev->nbe_buffer;
	fb = container_of(nb, struct c2_rpc_frm_buffer, fb_buffer);
	C2_PRE(frm_buf_invariant(fb));

	frm_sm = fb->fb_frm_sm;
	C2_PRE(frm_sm != NULL);

	rpc = container_of(fb, struct c2_rpc, r_fbuf);
	machine = frm_sm_to_rpc_machine(frm_sm);

	c2_rpc_machine_lock(machine);

	C2_ASSERT((nb->nb_flags & C2_NET_BUF_QUEUED) == 0);

	if (ev->nbe_status == 0) {

		C2_ADDB_ADD(&frm_sm->fs_rpc_form_addb, &frm_addb_loc,
			    c2_addb_trace, "Rpc sent on wire.");
		frm_item_rpc_stats_set(rpc);
		/** XXX @todo implement SENT callback */
		frm_item_state_set(rpc, RPC_ITEM_SENT);

	} else {

		C2_ADDB_ADD(&fb->fb_frm_sm->fs_rpc_form_addb,
			    &frm_addb_loc, formation_func_fail,
			    "net buf send failed", ev->nbe_status);
		/** XXX @todo implement FAILED callback */
		frm_item_state_failed(rpc, ev->nbe_status);
		C2_ASSERT("BUF_SEND_FAILED" == NULL);

	}

	c2_list_for_each_entry_safe(&rpc->r_items, item, item_next,
			struct c2_rpc_item, ri_rpcobject_linkage) {

		c2_list_del(&item->ri_rpcobject_linkage);
		/* session was held BUSY when the item was added to rpc */
		c2_rpc_session_release(item->ri_session);
	}

	c2_rpcobj_fbuf_fini(fb);
	c2_rpcobj_fini(rpc);

	c2_rpc_machine_unlock(machine);

	c2_free(rpc);
}

/* Callback function for deletion of an rpc item from the formation lists. */
int c2_rpc_frm_item_delete(struct c2_rpc_item *item)
{
	struct c2_rpc_frm_sm *frm_sm;

	C2_PRE(item != NULL);

	frm_sm = item_to_frm_sm(item);

	C2_PRE(frm_is_locked(frm_sm));

	frm_item_remove(frm_sm, item);
	return 0;
}

/* Callback function for setting priority of rpc item after it is
   submitted to rpc layer. */
void c2_rpc_frm_item_priority_set(struct c2_rpc_item *item,
		enum c2_rpc_item_priority prio)
{
	struct c2_rpc_frm_sm *frm_sm;

	C2_PRE(item != NULL);
	C2_PRE(prio < C2_RPC_ITEM_PRIO_NR);

	frm_sm = item_to_frm_sm(item);
	C2_PRE(frm_is_locked(frm_sm));

	frm_item_remove(frm_sm, item);
	item->ri_prio = prio;
	frm_item_add(frm_sm, item);
}

/* Callback function for setting deadline of rpc item after it is
   submitted to rpc layer. */
void c2_rpc_frm_item_deadline_set(struct c2_rpc_item *item, c2_time_t deadline)
{
	struct c2_rpc_frm_sm *frm_sm;

	C2_PRE(item != NULL);

	frm_sm = item_to_frm_sm(item);
	C2_PRE(frm_is_locked(frm_sm));

	frm_item_remove(frm_sm, item);
	item->ri_deadline = deadline;
	frm_item_add(frm_sm, item);
}

/* Callback function for changing group of rpc item after it is
   submitted to rpc layer. */
void c2_rpc_frm_item_group_set(struct c2_rpc_item *item,
			       struct c2_rpc_group *group)
{
	struct c2_rpc_frm_sm *frm_sm;

	C2_PRE(item != NULL);

	frm_sm = item_to_frm_sm(item);
	C2_PRE(frm_is_locked(frm_sm));

	frm_item_remove(frm_sm, item);
	item->ri_group = group;
	frm_item_add(frm_sm, item);
}

/* Callback function for reply received of an rpc item. */
void frm_item_reply_received(struct c2_rpc_item *reply_item,
			     struct c2_rpc_item *req_item)
{
	struct c2_rpc_frm_sm *frm_sm;

	C2_PRE(req_item != NULL);

	frm_sm = item_to_frm_sm(req_item);
	C2_PRE(frm_is_locked(frm_sm));

	sm_forming_state(frm_sm, req_item);
}

static void item_deadline_handle(struct c2_rpc_frm_sm *frm_sm,
				struct c2_rpc_item *item)
{
	struct c2_list		*list;
	struct c2_rpc_session	*session;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);
	C2_PRE(item->ri_state == RPC_ITEM_SUBMITTED);
	C2_PRE(frm_is_locked(frm_sm));

	/* Move the rpc item to first list in unformed item data structure
	   so that it is bundled first in the rpc being formed. */
	if (c2_rpc_item_is_bound(item)) {

		c2_list_del(&item->ri_unformed_linkage);
		list = &frm_sm->fs_unformed[C2_RPC_ITEM_PRIO_NR -
			(item->ri_prio + 1)].pl_unformed_items;
		c2_list_add(list, &item->ri_unformed_linkage);

	} else {

		session = item->ri_session;
		c2_list_move(&session->s_unbound_items, &item->ri_unbound_link);

	}
	++frm_sm->fs_timedout_items_nr;
}

/* Callback function for deadline expiry of an rpc item. */
void frm_item_deadline(struct c2_rpc_item *item)
{
	struct c2_rpc_frm_sm *frm_sm;
	struct c2_rpc_machine *machine;

	C2_PRE(item != NULL);

	frm_sm  = item_to_frm_sm(item);
	machine = frm_sm_to_rpc_machine(frm_sm);

	c2_rpc_machine_lock(machine);

	item_deadline_handle(frm_sm, item);
	sm_forming_state(frm_sm, item);

	c2_rpc_machine_unlock(machine);
}

/* Callback for timer expiry of an rpc item. */
static unsigned long item_timer_callback(unsigned long data)
{
	struct c2_rpc_item *item;

	item = (struct c2_rpc_item*)data;

	if (item->ri_state == RPC_ITEM_SUBMITTED)
		frm_item_deadline(item);
	/* Returning 0 since timer routine ignores return values. */
	return 0;
}

static struct c2_rpc_frm_group *frm_rpcgroup_locate(
		const struct c2_rpc_frm_sm *frm_sm,
		const struct c2_rpc_item *item)
{
	struct c2_rpc_frm_group *rg;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);
	C2_PRE(item->ri_group != NULL);

	c2_list_for_each_entry(&frm_sm->fs_groups, rg,
			struct c2_rpc_frm_group, frg_linkage) {
		if (rg->frg_group == item->ri_group)
			return rg;
	}
	return NULL;
}

static struct c2_rpc_frm_group *frm_rpcgroup_init(
		struct c2_rpc_frm_sm *frm_sm, const struct c2_rpc_item *item)
{
	struct c2_rpc_frm_group	*rg;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);
	C2_PRE(item->ri_group != NULL);

	C2_ALLOC_PTR_ADDB(rg, &frm_sm->fs_rpc_form_addb, &frm_addb_loc);
	if (rg == NULL)
		return NULL;
	c2_list_link_init(&rg->frg_linkage);
	c2_list_add(&frm_sm->fs_groups, &rg->frg_linkage);
	rg->frg_group = item->ri_group;
	return rg;
}

static void frm_rpcgroup_fini(struct c2_rpc_frm_group *rg)
{
	C2_PRE(rg != NULL);
	c2_list_del(&rg->frg_linkage);
	c2_free(rg);
}

/* Remove data of rpc item embedded within the formation state machine. */
static void frm_item_remove(struct c2_rpc_frm_sm *frm_sm,
			    struct c2_rpc_item *item)
{
	size_t			 item_size;
	struct c2_rpc_frm_group	*rg;

	C2_PRE(item != NULL);
	C2_PRE(frm_sm != NULL);
	C2_PRE(frm_is_locked(frm_sm));
	C2_PRE(item->ri_state == RPC_ITEM_SUBMITTED);

	/* Reduce the cumulative size of rpc items from formation
	   state machine. */
	item_size = item->ri_type->rit_ops->rito_item_size(item);
	frm_sm->fs_cumulative_size -= item_size;
	C2_ASSERT(frm_sm->fs_items_left > 0);
	--frm_sm->fs_items_left;

	/* If timer of rpc item is still running, change the deadline in
	   rpc item as per remaining time and stop and fini the timer. */
	if (item->ri_deadline != 0) {
		item->ri_deadline = 0;
		c2_timer_stop(&item->ri_timer);
		c2_timer_fini(&item->ri_timer);
	} else {
		C2_ASSERT(frm_sm->fs_timedout_items_nr > 0);
		--frm_sm->fs_timedout_items_nr;
		if (item->ri_group == NULL)
			frm_sm->fs_urgent_nogrp_items_nr--;
	}

	/* If item is bound, remove it from formation state machine data. */
	if (c2_list_link_is_in(&item->ri_unformed_linkage))
		c2_list_del(&item->ri_unformed_linkage);

	C2_ADDB_ADD(&frm_sm->fs_rpc_form_addb, &frm_addb_loc, c2_addb_trace,
		    "Item removed from formation module.");

	if (item->ri_group == NULL)
		return;

	rg = frm_rpcgroup_locate(frm_sm, item);
	if (rg == NULL)
		return;

	/* If the referred rpc group was complete earlier, then afer
	   removing this rpc item, the rpc group will be incomplete,
	   hence decrement the counter of complete groups. */
	if (rg->frg_items_nr == rg->frg_expected_items_nr)
		frm_sm->fs_complete_groups_nr--;

	/* Remove the data entered by this item in this rpc group.*/
	if (--rg->frg_items_nr == 0)
		frm_rpcgroup_fini(rg);
}

/* Update formation state machine data on addition of an rpc item. */
static void frm_item_add(struct c2_rpc_frm_sm *frm_sm,
			 struct c2_rpc_item *item)
{
	int				 rc;
	bool				 item_inserted = false;
	size_t				 item_size;
	struct c2_list			*list;
	struct c2_rpc_item		*rpc_item;
	struct c2_rpc_item		*rpc_item_next;
	struct c2_rpc_session		*session;
	struct c2_rpc_frm_group		*rg;
	struct c2_rpc_machine           *machine;

	C2_PRE(item != NULL);
	C2_PRE(frm_sm != NULL);
	C2_PRE(item->ri_state == RPC_ITEM_SUBMITTED);

	machine = frm_sm_to_rpc_machine(frm_sm);
	C2_PRE(c2_rpc_machine_is_locked(machine));

	item_size = item->ri_type->rit_ops->rito_item_size(item);
	frm_sm->fs_cumulative_size += item_size;
	++frm_sm->fs_items_left;

	/* Initialize the timer only when the deadline value is non-zero
	   i.e. dont initialize the timer for URGENT items */
	if (item->ri_deadline == 0) {
		++frm_sm->fs_timedout_items_nr;
		if (item->ri_group == NULL)
			++frm_sm->fs_urgent_nogrp_items_nr;
	} else {
		/* C2_TIMER_SOFT creates a different thread to handle the
		   callback. */
		c2_timer_init(&item->ri_timer, C2_TIMER_SOFT, item->ri_deadline,
				item_timer_callback, (unsigned long)item);
		rc = c2_timer_start(&item->ri_timer);
		if (rc != 0) {
			C2_ADDB_ADD(&frm_sm->fs_rpc_form_addb,
				&frm_addb_loc, formation_func_fail,
				"frm_item_add", rc);
			return;
		}
	}

	/*
	 * ADDB message is logged for both - bound and unbound items,
	 * although unbound items are not really added to formation
	 * state machine.
	 */
	C2_ADDB_ADD(&frm_sm->fs_rpc_form_addb, &frm_addb_loc, c2_addb_trace,
		    "Item added to formation module.");

	/* If item is unbound or unsolicited, don't add it to list
	   of formation state machine. */
	if (!c2_rpc_item_is_bound(item)) {
		/* Add the item to list of unbound items in its session. */
		session = item->ri_session;
		C2_ASSERT(c2_rpc_session_invariant(session));
		C2_ASSERT(C2_IN(session->s_state, (C2_RPC_SESSION_IDLE,
						   C2_RPC_SESSION_BUSY)));

		c2_list_add(&session->s_unbound_items, &item->ri_unbound_link);
		if (session->s_state == C2_RPC_SESSION_IDLE) {
			session->s_state = C2_RPC_SESSION_BUSY;
			c2_cond_broadcast(&session->s_state_changed,
					  &machine->rm_mutex);
		}
		C2_ASSERT(c2_rpc_session_invariant(session));
		return;
	}

	/* Index into the array to find out correct list as per
	   priority of current rpc item. */
	C2_ASSERT(item->ri_prio < C2_RPC_ITEM_PRIO_NR);
	list = &frm_sm->fs_unformed[C2_RPC_ITEM_PRIO_NR -
	       (item->ri_prio + 1)].pl_unformed_items;

	/* Insert the item into unformed list sorted according to deadline. */
	c2_list_for_each_entry_safe(list, rpc_item, rpc_item_next,
			struct c2_rpc_item, ri_unformed_linkage) {
		if (item->ri_deadline <= rpc_item->ri_deadline) {
			c2_list_add_before(&rpc_item->ri_unformed_linkage,
					&item->ri_unformed_linkage);
			item_inserted = true;
			break;
		}
	}
	if (!item_inserted)
		c2_list_add_after(&rpc_item->ri_unformed_linkage,
				&item->ri_unformed_linkage);

	/* If item does not belong to any rpc group, no rpc group
	   processing will be done for this item. */
	if (item->ri_group == NULL)
		return;

	/* Search for the group of rpc item in list of rpc groups in frm_sm. */
	rg = frm_rpcgroup_locate(frm_sm, item);
	if (rg == NULL)
		rg = frm_rpcgroup_init(frm_sm, item);

	rg->frg_expected_items_nr = item->ri_group->rg_expected;
	rg->frg_items_nr++;

	/* If number of items from this rpc group match the expected
	   items number, all items from this group are present with
	   current formation state machine, so the complete groups
	   counter is incremented. */
	if (rg->frg_items_nr == rg->frg_expected_items_nr)
		frm_sm->fs_complete_groups_nr++;
}

/* Decides if an optimal rpc can be prepared from the items submitted
   to this formation state machine. */
static bool frm_size_is_violated(struct c2_rpc_frm_sm *frm_sm,
				 uint64_t rpcobj_size, size_t item_size)
{
	C2_PRE(frm_sm != NULL);

	if (rpcobj_size + item_size >= frm_sm->fs_max_msg_size)
		return true;

	return false;
}

/* Policy function to dictate if an rpc should be formed or not. */
static bool frm_check_policies(struct c2_rpc_frm_sm *frm_sm)
{
	return
		/* If there are any rpc items whose deadline is expired,
		   trigger formation. */
		frm_sm->fs_timedout_items_nr > 0 ||
		/* Number of urgent items which do not belong to any rpc group
		   added to this state machine so far.
		   Any number > 0 will trigger formation. */
		frm_sm->fs_urgent_nogrp_items_nr > 0 ||
		/* Number of complete groups in the sense that this state
		   machine contains all rpc items from such rpc groups.
		   Any number > 0 will trigger formation. */
		frm_sm->fs_complete_groups_nr > 0;
}

/* Checks whether the items gathered so far in formation state machine
   are good enough to form an rpc object and proceed to forming state. */
static bool formation_qualify(const struct c2_rpc_frm_sm *frm_sm)
{
	C2_PRE(frm_sm != NULL);

	return
		/* If current rpcs in flight for this formation state machine
		   has reached the max rpcs limit, don't send any more rpcs
		   unless this number drops. */
		(!frm_sm->fs_sender_side ||
		 frm_sm->fs_curr_rpcs_in_flight <
		 frm_sm->fs_max_rpcs_in_flight) &&
		(frm_sm->fs_urgent_nogrp_items_nr > 0 ||
		 frm_sm->fs_cumulative_size >= frm_sm->fs_max_msg_size);
}

/* State function for UPDATING state. Formation state machine is updated
   with contents of incoming rpc item. */
static void sm_updating_state(struct c2_rpc_frm_sm *frm_sm,
			      struct c2_rpc_item *item)
{
	bool qualify;

	C2_PRE(item != NULL);
	C2_PRE(frm_is_locked(frm_sm));
	C2_PRE(frm_sm_invariant(frm_sm));

	frm_sm->fs_state = C2_RPC_FRM_STATE_UPDATING;

	if (c2_rpc_item_is_conn_establish(item))
		frm_sm->fs_sender_side = true;

	frm_item_add(frm_sm, item);

	C2_ASSERT(frm_sm_invariant(frm_sm));

	qualify = formation_qualify(frm_sm);
	if (qualify)
		sm_forming_state(frm_sm, item);
}

/* Add an rpc item to the formed list of an rpc object. */
static void frm_add_to_rpc(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc *rpc, struct c2_rpc_item *item,
		uint64_t *rpcobj_size)
{
	struct c2_rpc_slot *slot;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);
	C2_PRE(rpcobj_size != NULL);
	C2_PRE(item->ri_state != RPC_ITEM_ADDED);

	/* Update size of rpc object and current count of fragments. */
	c2_list_add(&rpc->r_items, &item->ri_rpcobject_linkage);
	*rpcobj_size += item->ri_type->rit_ops->rito_item_size(item);

	/* Remove the item data from c2_rpc_frm_sm structure. */
	frm_item_remove(frm_sm, item);
	item->ri_state = RPC_ITEM_ADDED;

	/* Remove item from slot->ready_items list AND if slot->ready_items
	   list is empty, remove slot from rpc_machine->ready_slots list.*/
	slot = item->ri_slot_refs[0].sr_slot;
	C2_ASSERT(slot != NULL);
	c2_list_del(&item->ri_slot_refs[0].sr_ready_link);
	if (c2_list_link_is_in(&slot->sl_link))
		c2_list_del(&slot->sl_link);
}

/* Invokes io coalescing op for io fops and stores the net buf descriptor
   into io fop as a result. */
static void io_coalesce(struct c2_rpc_item *item, struct c2_rpc_frm_sm *frm_sm,
			uint64_t size)
{
	struct c2_rpc_session		  *session;
	const struct c2_rpc_item_type_ops *ops;

	C2_PRE(item != NULL);
	C2_PRE(frm_sm != NULL);
	C2_PRE(frm_is_locked(frm_sm));

	session = item->ri_session;
	C2_PRE(session != NULL);

	if (frm_sm->fs_max_msg_size - size == 0)
		return;

	ops = item->ri_type->rit_ops;
	if (ops->rito_io_coalesce)
		ops->rito_io_coalesce(item, &session->s_unbound_items,
				      frm_sm->fs_max_msg_size - size);
}

/* Add bound items to rpc object. Rpc items are added until size gets
   optimal or any other policy of formation module has met. */
static void bound_items_add_to_rpc(struct c2_rpc_frm_sm *frm_sm,
				   struct c2_rpc *rpcobj, uint64_t *rpcobj_size)
{
	int			   i;
	bool			   size_exceeds;
	uint64_t		   rpc_size;
	uint64_t		   item_size;
	struct c2_list		  *list;
	struct c2_rpc_item	  *item;
	struct c2_rpc_item	  *item_next;
	struct c2_rpc_machine     *machine;
	struct c2_rpc_session     *session;

	C2_PRE(frm_sm != NULL);
	C2_PRE(rpcobj_size != NULL);
	C2_PRE(rpcobj != NULL);

	machine = frm_sm_to_rpc_machine(frm_sm);
	C2_PRE(c2_rpc_machine_is_locked(machine));

	/* Iterate over the priority bands and add items arranged in
	   increasing order of deadlines till rpc is optimal.
	   Algorithm skips the rpc items for which policies other than
	   size policy are not satisfied */
	for (i = 0; i < ARRAY_SIZE(frm_sm->fs_unformed); ++i) {

		list = &frm_sm->fs_unformed[i].pl_unformed_items;

		c2_list_for_each_entry_safe(list, item, item_next,
				struct c2_rpc_item, ri_unformed_linkage) {

			rpc_size  = *rpcobj_size;
			item_size = item->ri_type->rit_ops->rito_item_size(
								item);
			size_exceeds = frm_size_is_violated(frm_sm, rpc_size,
							    item_size);
			if (size_exceeds)
				goto done;

			io_coalesce(item, frm_sm, rpc_size);
			frm_add_to_rpc(frm_sm, rpcobj, item, rpcobj_size);

			session = item->ri_session;
			/* The hold on session will be released in
			   frm_net_buffer_sent() callback */
			c2_rpc_session_hold_busy(session);
		}
	}

done:
	rpc_size = *rpcobj_size;
	C2_POST(!frm_size_is_violated(frm_sm, rpc_size, 0));
}

/* Make an unbound item (which is not unsolicited) bound by calling
   item_add_internal(). Also change item type flags accordingly. */
static void frm_item_make_bound(struct c2_rpc_slot *slot,
				struct c2_rpc_item *item)
{
	C2_PRE(slot != NULL);
	C2_PRE(item != NULL);
	C2_PRE(!c2_rpc_item_is_bound(item));

	if (!c2_rpc_item_is_unsolicited(item)) {
		c2_rpc_slot_item_add_internal(slot, item);
		c2_list_add(&slot->sl_ready_list,
				&item->ri_slot_refs[0].sr_ready_link);
	}
}

/* Make unbound items bound first and then add items to rpc object
   until rpc becomes optimal size or other formation policies are met. */
static void unbound_items_add_to_rpc(struct c2_rpc_frm_sm *frm_sm,
				     struct c2_rpc *rpcobj,
				     uint64_t *rpcobj_size)
{
	bool				   sz_policy_violated = false;
	uint64_t			   rpc_size;
	struct c2_rpc_item		  *item;
	struct c2_rpc_item		  *item_next;
	struct c2_rpc_slot		  *slot;
	struct c2_rpc_slot		  *slot_next;
	struct c2_rpc_chan		  *chan;
	struct c2_rpc_machine		  *rpc_machine;
	struct c2_rpc_session		  *session;
	const struct c2_rpc_item_type_ops *rit_ops;

	C2_PRE(frm_sm != NULL);
	C2_PRE(rpcobj != NULL);
	C2_PRE(rpcobj_size != NULL);

	chan = container_of(frm_sm, struct c2_rpc_chan, rc_frmsm);
	C2_ASSERT(chan != NULL);

	rpc_machine = chan->rc_rpc_machine;
	C2_ASSERT(c2_rpc_machine_is_locked(rpc_machine));

	c2_list_for_each_entry_safe(&rpc_machine->rm_ready_slots, slot,
			slot_next, struct c2_rpc_slot, sl_link) {

		if (!c2_list_is_empty(&slot->sl_ready_list) ||
		    (slot->sl_session->s_conn->c_rpcchan != chan))
			continue;

		session = slot->sl_session;

		c2_list_for_each_entry_safe(&session->s_unbound_items,
				item, item_next, struct c2_rpc_item,
				ri_unbound_link) {

			if (!c2_rpc_slot_can_item_add_internal(slot))
				break;

			rpc_size = *rpcobj_size;
			rit_ops = item->ri_type->rit_ops;
			sz_policy_violated = frm_size_is_violated(frm_sm,
					rpc_size, rit_ops->rito_item_size(
						item));
			if (sz_policy_violated)
				goto done;

			frm_item_make_bound(slot, item);
			c2_list_del(&item->ri_unbound_link);
			io_coalesce(item, frm_sm, rpc_size);
			frm_add_to_rpc(frm_sm, rpcobj, item, rpcobj_size);

			/* The hold on session will be released when
			   buffer sent callback is generated for the buffer
			   that carries this rpc. See frm_net_buffer_sent()
			 */
			c2_rpc_session_hold_busy(session);
		}
	}

done:
	rpc_size = *rpcobj_size;
	C2_POST(!frm_size_is_violated(frm_sm, rpc_size, 0));
}

/* State function for FORMING state. Core of formation algorithm.
   It scans the lists of rpc items to form an RPC object by cooperation
   of multiple policies. Formation algorithm takes hints from rpc groups
   and will try to form rpc objects by keeping all group member rpc items
   together. Forming state will take care of coalescing of items.
   Coalescing Policy:
   A stream of unbound items will be coalesced together in a bound item
   if they happen to share the fid and intent of a read/write operation.
   The bound item will be formed into an rpc and the member unbound items
   will be hanging off the coalesced item data structure.
   *** Formation Algorithm ***
   1. Check all formation policies to see if an rpc can be formed.
   2. If rpc can be formed, go through all bound items and add them to rpc.
   3. If space permits, add unbound items to rpc. All unbound items are
      made bound before they are included in rpc.
   4. Step 2 and 3 include IO coalescing which happens between a bound
      item and a stream of unbound items. On successful coalescing, the
      resultant IO vector is associated with the bound item and it is
      included in the rpc while the unbound items are hanging off the
      coalesced bound item.
   5. Send the prepared rpc on wire. The rpc is encoded here and the
      resulting network buffer is sent to destination using a network
      transfer machine. */
static void sm_forming_state(struct c2_rpc_frm_sm *frm_sm,
			     struct c2_rpc_item *item)
{
	bool		 size_optimal;
	bool		 frm_policy;
	uint64_t	 rpcobj_size = 0;
	struct c2_rpc	*rpcobj;

	C2_PRE(item != NULL);
	C2_PRE(frm_is_locked(frm_sm));
	C2_PRE(frm_sm_invariant(frm_sm));

	frm_sm->fs_state = C2_RPC_FRM_STATE_FORMING;

	if (!c2_list_is_empty(&frm_sm->fs_rpcs))
		frm_send_onwire(frm_sm);

	size_optimal = frm_size_is_violated(frm_sm, frm_sm->fs_cumulative_size,
					    0);
	frm_policy = frm_check_policies(frm_sm);

	if (frm_policy || size_optimal) {

		C2_ALLOC_PTR_ADDB(rpcobj, &frm_sm->fs_rpc_form_addb,
				  &frm_addb_loc);

		if (rpcobj != NULL) {

			c2_rpcobj_init(rpcobj);

			bound_items_add_to_rpc(frm_sm, rpcobj, &rpcobj_size);
			unbound_items_add_to_rpc(frm_sm, rpcobj, &rpcobj_size);

			if (!c2_list_is_empty(&rpcobj->r_items)) {

				c2_list_add(&frm_sm->fs_rpcs,
					    &rpcobj->r_linkage);
				frm_send_onwire(frm_sm);

			} else {

				c2_rpcobj_fini(rpcobj);
				c2_free(rpcobj);
				rpcobj = NULL;

			}
			C2_ASSERT(frm_sm_invariant(frm_sm));
		}
	}
	frm_sm->fs_state = C2_RPC_FRM_STATE_WAITING;
}

uint64_t rpc_size_get(const struct c2_rpc *rpc)
{
	uint64_t		 rpc_size = 0;
	struct c2_rpc_item	*item;

	C2_PRE(rpc != NULL);

	c2_list_for_each_entry(&rpc->r_items, item,
			struct c2_rpc_item, ri_rpcobject_linkage)
		rpc_size += item->ri_type->rit_ops->rito_item_size(item);

	return rpc_size;
}

static void frm_send_onwire(struct c2_rpc_frm_sm *frm_sm)
{
	int				 rc;
	uint64_t			 rpc_size;
	struct c2_rpc			*rpc_obj;
	struct c2_rpc			*rpc_obj_next;
	struct c2_net_domain		*dom;
	struct c2_rpc_frm_buffer	*fb;
	struct c2_net_transfer_mc	*tm;
	struct c2_rpc_chan		*chan;
	struct c2_rpc_machine           *machine;

	C2_PRE(frm_sm != NULL);

	machine = frm_sm_to_rpc_machine(frm_sm);
	C2_PRE(c2_rpc_machine_is_locked(machine));

	chan = container_of(frm_sm, struct c2_rpc_chan, rc_frmsm);

	c2_list_for_each_entry_safe(&frm_sm->fs_rpcs, rpc_obj,
			rpc_obj_next, struct c2_rpc, r_linkage) {

		if (frm_sm->fs_sender_side &&
		    frm_sm->fs_curr_rpcs_in_flight >=
				frm_sm->fs_max_rpcs_in_flight) {

			rc = -EBUSY;
			break;

		}

		tm       = &chan->rc_rpc_machine->rm_tm;
		dom      = tm->ntm_dom;
		rpc_size = rpc_size_get(rpc_obj);
		fb       = &rpc_obj->r_fbuf;

		rc = c2_rpcobj_fbuf_init(fb, frm_sm, dom, rpc_size);
		if (rc < 0)
			continue;

		fb->fb_buffer.nb_ep     = chan->rc_destep;
		fb->fb_buffer.nb_length = rpc_size;

		/** @todo Allocate bulk i/o buffers before encoding. */
		/** @todo rpc_encode will encode the bulk i/o
		   buffer descriptors. */
		rc = c2_rpc_encode(rpc_obj, &fb->fb_buffer);
		if (rc < 0) {
			C2_ADDB_ADD(&frm_sm->fs_rpc_form_addb,
					&frm_addb_loc, formation_func_fail,
					"c2_rpc_encode failed.", rc);
			continue;
		}

		/* Add the buffer to transfer machine.*/
		rc = c2_net_buffer_add(&fb->fb_buffer, tm);
		if (rc < 0) {
			C2_ADDB_ADD(&frm_sm->fs_rpc_form_addb,
					&frm_addb_loc, formation_func_fail,
					"c2_net_buffer_add failed", rc);
			continue;
		}

		C2_ASSERT(fb->fb_buffer.nb_tm->ntm_dom == tm->ntm_dom);
		if (frm_sm->fs_sender_side) {

			frm_sm->fs_curr_rpcs_in_flight++;
			C2_ADDB_ADD(&frm_sm->fs_rpc_form_addb, &frm_addb_loc,
				    c2_addb_trace, "Rpc dispatched.");

		}
		c2_list_del(&rpc_obj->r_linkage);
	}
}

/* Decrement the current number of rpcs in flight from given rpc item. */
void frm_rpcs_inflight_dec(struct c2_rpc_frm_sm *frm_sm)
{
	C2_PRE(frm_sm != NULL);
	C2_PRE(frm_is_locked(frm_sm));

	if (frm_sm->fs_sender_side) {
		if (frm_sm->fs_curr_rpcs_in_flight > 0) {
			frm_sm->fs_curr_rpcs_in_flight--;
			C2_ADDB_ADD(&frm_sm->fs_rpc_form_addb, &frm_addb_loc,
				    c2_addb_trace, "Rpc received from wire.");
		}
	}
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
