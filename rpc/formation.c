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

#include "rpc/rpccore.h"

#ifndef __KERNEL__
#include "rpc/rpc_onwire.h"
#endif

/* ADDB Instrumentation for rpc formation. */
static const struct c2_addb_ctx_type frm_addb_ctx_type = {
        .act_name = "rpc-formation"
};

static const struct c2_addb_loc frm_addb_loc = {
        .al_name = "rpc-formation"
};

C2_ADDB_EV_DEFINE(formation_func_fail, "formation_func_fail",
		C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

/**
   Type definition of a state function.
   @param frm_sm - given c2_rpc_frm_sm structure.
   @param item - incoming rpc item.
   @param event - triggered event.
   @param pvt - private data of rpc item.
 */
typedef enum c2_rpc_frm_evt_id (*statefunc_t)(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event);

/* Forward declarations of local static functions. */
static void frm_item_remove(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item);

static int frm_item_add(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item);

static int frm_send_onwire(struct c2_rpc_frm_sm *frm_sm);

static void coalesced_item_fini(struct c2_rpc_frm_item_coalesced *c_item);

static void coalesced_item_reply_post(struct c2_rpc_frm_item_coalesced *cs);

static enum c2_rpc_frm_evt_id sm_waiting_state(
		struct c2_rpc_frm_sm *frm_sm ,struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event);

static enum c2_rpc_frm_evt_id sm_updating_state(
		struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event);

static enum c2_rpc_frm_evt_id sm_forming_state(
		struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event);

/**
   Temporary threashold values. Will be moved to appropriate files
   once rpc integration is done.
 */
static uint64_t max_rpcs_in_flight;

/**
   Assign network specific thresholds on max size of a message and max
   number of fragments that can be carried in one network transfer.
   @param frm_sm - Input Formation state machine.
   @param max_bufsize - Max permitted buffer size for given net domain.
   @param max_segs_nr - Max permitted segments a message can contain
   for given net domain.
 */
void c2_rpc_frm_sm_net_limits_set(struct c2_rpc_frm_sm *frm_sm,
		c2_bcount_t max_bufsize, c2_bcount_t max_segs_nr)
{
	C2_PRE(frm_sm != NULL);
	C2_PRE(frm_sm->fs_state != C2_RPC_FRM_STATE_UNINITIALIZED);

	frm_sm->fs_max_msg_size = max_bufsize;
	frm_sm->fs_max_frags = max_segs_nr;
}

/**
  This routine will set the stats for each rpc item
  in the rpc object to RPC_ITEM_SENT.
  @param rpc - rpc object for which stats have to be calculated.
 */
static void frm_item_set_rpc_stats(const struct c2_rpc *rpc)
{
	struct c2_rpc_item *item;

	C2_PRE(rpc != NULL);

	c2_list_for_each_entry(&rpc->r_items, item,
			struct c2_rpc_item, ri_rpcobject_linkage)
		c2_rpc_item_exit_stats_set(item, C2_RPC_PATH_OUTGOING);
}

/**
  This routine will change the state of each rpc item
  in the rpc object to RPC_ITEM_SENT
  @param rpc - rpc object
  @param state - item state
 */
static void frm_item_set_state(const struct c2_rpc *rpc, const enum
		c2_rpc_item_state state)
{
	struct c2_rpc_item *item;

	C2_PRE(rpc != NULL);

	/* Change the state of each rpc item in the rpc object
	   to RPC_ITEM_SENT. */
	c2_list_for_each_entry(&rpc->r_items, item,
			struct c2_rpc_item, ri_rpcobject_linkage) {
		C2_ASSERT(item->ri_state == RPC_ITEM_ADDED);
		item->ri_state = state;
	}
}

/**
   Invariant subroutine for struct c2_rpc_frm_buffer.
   @param fbuf - c2_rpc_frm_buffer structure
   @retval - TRUE if necessary conditions pass, FALSE otherwise
 */
static bool frm_buf_invariant(const struct c2_rpc_frm_buffer *fbuf)
{
	return (fbuf != NULL && fbuf->fb_frm_sm != NULL && fbuf->fb_rpc != NULL
			&& fbuf->fb_magic == C2_RPC_FRM_BUFFER_MAGIC &&
			fbuf->fb_retry <= C2_RPC_FRM_BUFFER_RETRY);
}

/**
   Allocate a buffer of type struct c2_rpc_frm_buffer.
   The net buffer is allocated and registered with the net domain.
   @param fb - c2_rpc_frm_buffer to be allocated
   @param rpc - rpc object to be put inside the fb
   @param frm_sm - formation state machine
   @param net_domain - network domain structure
   @retval 0 (success) -errno (failure)
 */
static int frm_buffer_init(struct c2_rpc_frm_buffer **fb, struct c2_rpc *rpc,
		struct c2_rpc_frm_sm *frm_sm, struct c2_net_domain *net_dom,
		uint64_t rpc_size)
{
	int				 rc = 0;
	struct c2_net_buffer		*nb;
	struct c2_rpc_frm_buffer	*fbuf;

	C2_PRE(fb != NULL);
	C2_PRE(rpc != NULL);
	C2_PRE(frm_sm != NULL);
	C2_PRE(net_dom != NULL);
	C2_PRE(rpc_size != 0);

	C2_ALLOC_PTR_ADDB(fbuf, &frm_sm->fs_formation->rf_rpc_form_addb,
			  &frm_addb_loc);
	if (fbuf == NULL)
		return -ENOMEM;
	fbuf->fb_magic = C2_RPC_FRM_BUFFER_MAGIC;
	fbuf->fb_frm_sm = frm_sm;
	fbuf->fb_rpc = rpc;
	nb = &fbuf->fb_buffer;
	rc = c2_rpc_net_send_buffer_allocate(net_dom, &nb, rpc_size);
	if (rc != 0)
		return rc;
	fbuf->fb_retry = C2_RPC_FRM_BUFFER_RETRY;
	*fb = fbuf;
	C2_POST(frm_buf_invariant(fbuf));
	return rc;
}

/**
   Deallocate a buffer of type struct c2_rpc_frm_buffer. The
   c2_net_buffer is deregistered and deallocated.
   @param fb - c2_rpc_frm_buffer to be freed
 */
static void frm_buffer_fini(struct c2_rpc_frm_buffer *fb)
{
	int rc;

	C2_PRE(fb != NULL);
	C2_PRE(frm_buf_invariant(fb));

	/* Currently, our policy is to release the buffer on completion. */
	rc = c2_rpc_net_send_buffer_deallocate(&fb->fb_buffer,
			fb->fb_buffer.nb_dom);
	c2_free(fb);
}

/**
   A state table guiding resultant states on arrival of events
   on earlier states.
   internal_state = statetable[current_state][current_event]
   Internal state is used by default handler to find out the next state.
 */
static const statefunc_t c2_rpc_frm_statetable
[C2_RPC_FRM_STATES_NR][C2_RPC_FRM_INTEVT_NR - 1] = {

	[C2_RPC_FRM_STATE_UNINITIALIZED] = { NULL },

	[C2_RPC_FRM_STATE_WAITING] = {
		[C2_RPC_FRM_EXTEVT_RPCITEM_READY] = sm_updating_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED] = sm_forming_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT] = sm_forming_state,
		[C2_RPC_FRM_EXTEVT_SLOT_IDLE] = NULL,
		[C2_RPC_FRM_EXTEVT_UBRPCITEM_ADDED] = sm_updating_state,
		[C2_RPC_FRM_EXTEVT_USRPCITEM_ADDED] = sm_updating_state,
		[C2_RPC_FRM_INTEVT_STATE_SUCCEEDED] = sm_waiting_state,
		[C2_RPC_FRM_INTEVT_STATE_FAILED] = sm_waiting_state,
	},

	[C2_RPC_FRM_STATE_UPDATING] = {
		[C2_RPC_FRM_EXTEVT_RPCITEM_READY] = sm_updating_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED] = sm_forming_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT] = sm_forming_state,
		[C2_RPC_FRM_EXTEVT_SLOT_IDLE] = NULL,
		[C2_RPC_FRM_EXTEVT_UBRPCITEM_ADDED] = sm_updating_state,
		[C2_RPC_FRM_EXTEVT_USRPCITEM_ADDED] = sm_updating_state,
		[C2_RPC_FRM_INTEVT_STATE_SUCCEEDED] = sm_forming_state,
		[C2_RPC_FRM_INTEVT_STATE_FAILED] = sm_waiting_state,
	},

	[C2_RPC_FRM_STATE_FORMING] = {
		[C2_RPC_FRM_EXTEVT_RPCITEM_READY] = sm_updating_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED] = sm_forming_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT] = sm_forming_state,
		[C2_RPC_FRM_EXTEVT_SLOT_IDLE] = NULL,
		[C2_RPC_FRM_EXTEVT_UBRPCITEM_ADDED] = sm_updating_state,
		[C2_RPC_FRM_EXTEVT_USRPCITEM_ADDED] = sm_updating_state,
		[C2_RPC_FRM_INTEVT_STATE_SUCCEEDED] = sm_waiting_state,
		[C2_RPC_FRM_INTEVT_STATE_FAILED] = sm_waiting_state,
	},
};

/**
   Set thresholds for rpc formation. Currently used by UT code.
   @param max_rpcs - Max rpcs in flight
 */
void c2_rpc_frm_set_thresholds(uint64_t max_rpcs)
{
	max_rpcs_in_flight = max_rpcs;
}

/**
   Initialization for formation component in rpc. This will register
   necessary callbacks and initialize necessary data structures.
   @param frm - formation structure
   @retval 0 if init completed, -errno otherwise
 */
int c2_rpc_frm_init(struct c2_rpc_formation *frm)
{
	int rc = 0;

	C2_PRE(frm != NULL);

        c2_addb_ctx_init(&frm->rf_rpc_form_addb,
			&frm_addb_ctx_type, &c2_addb_global_ctx);
        c2_addb_choose_default_level(AEL_WARN);
	c2_rwlock_init(&frm->rf_sm_list_lock);
	c2_list_init(&frm->rf_frm_sm_list);
	frm->rf_client_side = false;
	return rc;
}

/**
   Delete the group info list from struct c2_rpc_frm_sm.
   Called once formation component is finied.
   @param list - List of c2_rpc_frm_group structures that will be finied here.
 */
static void frm_groups_list_fini(struct c2_list *list)
{
	struct c2_rpc_frm_group	*group;
	struct c2_rpc_frm_group	*group_next;

	C2_PRE(list != NULL);

	c2_list_for_each_entry_safe(list, group, group_next,
			struct c2_rpc_frm_group, frg_linkage) {
		c2_list_del(&group->frg_linkage);
		c2_free(group);
	}
	c2_list_fini(list);
}

/**
   Delete the coalesced items list from struct c2_rpc_frm_sm.
   @param list - coalesced items list
 */
static void coalesced_items_fini(struct c2_list *list)
{
	struct c2_rpc_item			*item;
	struct c2_rpc_item			*item_next;
	struct c2_rpc_frm_item_coalesced	*c_item;
	struct c2_rpc_frm_item_coalesced	*c_item_next;

	C2_PRE(list != NULL);

	c2_list_for_each_entry_safe(list, c_item, c_item_next,
			struct c2_rpc_frm_item_coalesced, ic_linkage) {
		c2_list_del(&c_item->ic_linkage);
		c2_list_for_each_entry_safe(&c_item->ic_member_list,
				item, item_next, struct c2_rpc_item,
				ri_coalesced_linkage) {
			c2_list_del(&item->ri_coalesced_linkage);
		}
		c2_free(c_item);
	}
	c2_list_fini(list);
}

/**
   Delete the rpcobj items list from struct c2_rpc_frm_sm.
   @param list - rpc object list
 */
static void rpcobj_list_fini(struct c2_list *list)
{
	struct c2_rpc *obj;
	struct c2_rpc *obj_next;

	C2_PRE(list != NULL);

	c2_list_for_each_entry_safe(list, obj, obj_next, struct c2_rpc,
			r_linkage) {
		c2_rpc_rpcobj_fini(obj);
		c2_free(obj);
	}
	c2_list_fini(list);
}

/**
   Delete the unformed items list from struct c2_rpc_frm_sm.
   @param frm_sm - formation state machine
 */
static void unformed_prio_fini(struct c2_rpc_frm_sm *frm_sm)
{
	int cnt;

	C2_PRE(frm_sm != NULL);

	for (cnt = 0; cnt < ARRAY_SIZE(frm_sm->fs_unformed_prio); ++cnt)
		c2_list_fini(&frm_sm->fs_unformed_prio[cnt].pl_unformed_items);
}

/**
   Finish method for formation component in rpc.
   This will deallocate all memory claimed by formation
   and do necessary cleanup.
   @param formation - c2_rpc_formation structure to be finied
 */
void c2_rpc_frm_fini(struct c2_rpc_formation *formation)
{
	C2_PRE(formation != NULL);

	c2_rwlock_fini(&formation->rf_sm_list_lock);
	c2_list_fini(&formation->rf_frm_sm_list);
	c2_addb_ctx_fini(&formation->rf_rpc_form_addb);
}

/**
   Exit path from a state machine. There are no more rpc items due
   in formation state machine and the associated c2_rpc_chan structure
   is about to be destroyed.
   @param frm_sm - formation state machine
 */
void c2_rpc_frm_sm_fini(struct c2_rpc_frm_sm *frm_sm)
{
	struct c2_rpc_formation	*formation;

	C2_PRE(frm_sm != NULL);
	C2_PRE(frm_sm->fs_state == C2_RPC_FRM_STATE_WAITING);

	formation = frm_sm->fs_formation;
	c2_rwlock_write_lock(&formation->rf_sm_list_lock);
	c2_list_del(&frm_sm->fs_linkage);
	c2_rwlock_write_unlock(&formation->rf_sm_list_lock);
	frm_groups_list_fini(&frm_sm->fs_groups);
	coalesced_items_fini(&frm_sm->fs_coalesced_items);
	rpcobj_list_fini(&frm_sm->fs_rpcs);
	unformed_prio_fini(frm_sm);
	c2_mutex_fini(&frm_sm->fs_lock);
}

/**
   Check if the state machine structure is empty.
   @param frm_sm - formation state machine
   @retval - TRUE if necessary conditions satisfy, false otherwise
 */
static bool frm_sm_invariant(const struct c2_rpc_frm_sm *frm_sm)
{
	bool item_present = false;

	int cnt;

	if (frm_sm == NULL)
		return false;

	if (frm_sm->fs_formation == NULL)
		return false;

	/* Performance Intensive! */
	if (!c2_list_contains(&frm_sm->fs_formation->rf_frm_sm_list,
				&frm_sm->fs_linkage))
		return false;

	if (frm_sm->fs_rpcchan == NULL)
		return false;

	/* Formation state machine should be in a valid state. */
	if (frm_sm->fs_state < C2_RPC_FRM_STATE_WAITING ||
	    frm_sm->fs_state > C2_RPC_FRM_STATE_FORMING)
		return false;

	/* The transfer machine associated with this formation state machine
	   should have been started already. */
	if (frm_sm->fs_rpcchan->rc_tm.ntm_state != C2_NET_TM_STARTED)
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
		for (cnt = 0; cnt < C2_RPC_ITEM_PRIO_NR+1; ++cnt)
			if (!c2_list_is_empty(&frm_sm->fs_unformed_prio[cnt].
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
		/* If list of coalesced items is not empty, there should
		   be an rpc made in forming state. */
		if (!c2_list_is_empty(&frm_sm->fs_coalesced_items) &&
		    c2_list_is_empty(&frm_sm->fs_rpcs))
			return false;
		break;
	default:
		C2_IMPOSSIBLE("Invalid state of formation state machine.");
	};
	return true;
}

/**
   Add a formation state machine structure when the first rpc item gets added
   for a given c2_rpc_conn structure.
   @param conn - c2_rpc_conn structure used for unique formation state machine
   @param formation - c2_rpc_formation structure
   @retval new formation state machine if success, NULL otherwise
 */
static void frm_sm_add(struct c2_rpc_chan *chan,
		      struct c2_rpc_formation *formation,
		      struct c2_rpc_frm_sm *frm_sm)
{
	uint64_t cnt;

	C2_PRE(chan != NULL);
	C2_PRE(formation != NULL);
	C2_PRE(frm_sm != NULL);

	frm_sm->fs_formation = formation;
	c2_mutex_init(&frm_sm->fs_lock);
	c2_list_add(&formation->rf_frm_sm_list, &frm_sm->fs_linkage);
	c2_list_init(&frm_sm->fs_groups);
	c2_list_init(&frm_sm->fs_coalesced_items);
	c2_list_init(&frm_sm->fs_rpcs);
	frm_sm->fs_rpcchan = chan;
	frm_sm->fs_state = C2_RPC_FRM_STATE_WAITING;
	frm_sm->fs_max_rpcs_in_flight = max_rpcs_in_flight;
	frm_sm->fs_curr_rpcs_in_flight = 0;
	for (cnt = 0; cnt < ARRAY_SIZE(frm_sm->fs_unformed_prio); ++cnt) {
		frm_sm->fs_unformed_prio[cnt].pl_prio = C2_RPC_ITEM_PRIO_NR -
			cnt;
		c2_list_init(&frm_sm->fs_unformed_prio[cnt].pl_unformed_items);
	}
	frm_sm->fs_cumulative_size = 0;
	frm_sm->fs_urgent_nogrp_items_nr = 0;
	frm_sm->fs_complete_groups_nr = 0;
}

/**
   Return the function pointer to next state function given the current state
   and current event as input.
   @param current_state - current state of state machine.
   @param current_event - current event posted to the state machine.
   @retval function pointer
 */
static statefunc_t frm_next_state(const int current_state,
		int current_event)
{
	C2_PRE(current_state < C2_RPC_FRM_STATES_NR);
	C2_PRE(current_event < C2_RPC_FRM_INTEVT_NR);

	return c2_rpc_frm_statetable[current_state][current_event];
}

/**
   Get the formation state machine given an rpc item. The
   formation state machine is tightly bound to a c2_rpc_chan
   which points to a destination network endpoint. There is
   one-to-one relationship of c2_rpc_chan with c2_rpc_frm_sm.
   @param item - Input rpc item
   @retval - Connected formation state machine.
 */
static struct c2_rpc_frm_sm *rpc_item_to_frm_sm(const struct c2_rpc_item *item)
{
	return &item->ri_session->s_conn->c_rpcchan->rc_frmsm;
}

/**
   Create a new formation state machine object.
   @param conn - c2_rpc_chan structure used for unique formation state machine
   @param formation - c2_rpc_formation structure
   @param frm_sm - Formation state machine object to be initialized.
 */
void c2_rpc_frm_sm_init(struct c2_rpc_chan *chan,
		       struct c2_rpc_formation *formation,
		       struct c2_rpc_frm_sm *frm_sm)
{
	C2_PRE(chan != NULL);
	C2_PRE(formation != NULL);
	C2_PRE(frm_sm != NULL);

	/* Add a new formation state machine. */
	c2_rwlock_write_lock(&formation->rf_sm_list_lock);
	frm_sm_add(chan, formation, frm_sm);
	c2_rwlock_write_unlock(&formation->rf_sm_list_lock);
}

/**
   A default handler function for invoking all state functions
   based on incoming event.
   1. Find out the formation state machine for given rpc item.
   2. Lock the c2_rpc_frm_sm data structure.
   3. Fetch the state for this state machine and find out the resulting state
   from the state table given this event.
   4. Call the respective state function for resulting state.
   5. Release the lock.
   6. Handle further events on basis of return value of
   recent state function.
   @param item - incoming rpc item needed for external events.
   @param frm_sm - formation state machine
   @param sm_state - state of formation state machine.
   @param sm_event - event posted to the state machine (gets modified when
   a state is executed).
   @retval 0 if success, -errno othewise
 */
static int sm_default_handler(struct c2_rpc_item *item,
		struct c2_rpc_frm_sm *frm_sm, int sm_state,
		struct c2_rpc_frm_sm_event *sm_event)
{
	enum c2_rpc_frm_state		 prev_state;
	struct c2_rpc_frm_sm		*sm;
	struct c2_rpc_formation		*formation;

	C2_PRE(item != NULL);
	C2_PRE(sm_event->se_event < C2_RPC_FRM_INTEVT_NR);
	C2_PRE(sm_state <= C2_RPC_FRM_STATES_NR);

	if (frm_sm == NULL) {
		formation = &item->ri_mach->cr_formation;
		sm = rpc_item_to_frm_sm(item);
		C2_ASSERT(sm != NULL);
		c2_mutex_lock(&sm->fs_lock);
		prev_state = sm->fs_state;
	} else {
		c2_mutex_lock(&frm_sm->fs_lock);
		prev_state = sm_state;
		sm = frm_sm;
	}

	while (sm_event->se_event != C2_RPC_FRM_INTEVT_DONE) {
		/* Transition to next state.*/
		sm_event->se_event = (frm_next_state(prev_state,
					sm_event->se_event))
			(sm, item, sm_event);

		/* The return value should be an internal event.
		   Assert if its not. */
		C2_ASSERT((sm_event->se_event >=
			   C2_RPC_FRM_INTEVT_STATE_SUCCEEDED) &&
			   (sm_event->se_event < C2_RPC_FRM_INTEVT_NR));
		/* Get latest state of state machine. */
		prev_state = sm->fs_state;
	}

	c2_mutex_unlock(&sm->fs_lock);
	return 0;
}

/**
   Callback function for addition of an rpc item to the list of
   its corresponding free slot.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
   @retval 0 (success) -errno (failure)
 */
int c2_rpc_frm_item_ready(struct c2_rpc_item *item)
{
	struct c2_rpc_slot		*slot;
	struct c2_rpcmachine		*rpcmachine;
	struct c2_rpc_frm_sm_event	 sm_event;
	static int                       count = 0;

	C2_PRE(item != NULL);

	sm_event.se_event = C2_RPC_FRM_EXTEVT_RPCITEM_READY;

	count++;
	printf("frm_item_ready: count %d\n", count);
	/* Add the item to ready list of its slot. */
	slot = item->ri_slot_refs[0].sr_slot;
	C2_ASSERT(slot != NULL);
	c2_list_add(&slot->sl_ready_list, &item->ri_slot_refs[0].sr_ready_link);

	/* Add the slot to list of ready slots in rpcmachine. */
	rpcmachine = slot->sl_session->s_conn->c_rpcmachine;
	C2_ASSERT(rpcmachine != NULL);
	c2_mutex_lock(&rpcmachine->cr_ready_slots_mutex);

	/* Add the slot to ready list of slots in rpcmachine, if
	   it is not in that list already.*/
	if (!c2_list_contains(&rpcmachine->cr_ready_slots, &slot->sl_link)) {
		c2_list_add(&rpcmachine->cr_ready_slots, &slot->sl_link);
	}
	c2_mutex_unlock(&rpcmachine->cr_ready_slots_mutex);

	/* Curent state is not known at the moment. */
	return sm_default_handler(item, NULL, C2_RPC_FRM_STATES_NR,
			&sm_event);
}

/**
   Callback function for slot becoming idle.
   Adds the slot to the list of ready slots in concerned rpcmachine.
   @param item - slot structure for the slot which has become idle.
 */
void c2_rpc_frm_slot_idle(struct c2_rpc_slot *slot)
{
	struct c2_rpcmachine *rpcmachine;

	C2_PRE(slot != NULL);
	C2_PRE(slot->sl_session != NULL);
	C2_PRE(slot->sl_in_flight == 0);

	/* Add the slot to list of ready slots in its rpcmachine. */
	rpcmachine = slot->sl_session->s_conn->c_rpcmachine;
	C2_ASSERT(rpcmachine != NULL);
	c2_mutex_lock(&rpcmachine->cr_ready_slots_mutex);
	c2_list_add(&rpcmachine->cr_ready_slots, &slot->sl_link);
	c2_mutex_unlock(&rpcmachine->cr_ready_slots_mutex);
}

/**
   Callback function for unbounded/unsolicited item getting added to session.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
   @retval 0 (success) -errno (failure)
 */
int c2_rpc_frm_ubitem_added(struct c2_rpc_item *item)
{
	struct c2_rpc_frm_sm_event	 sm_event;
	struct c2_rpc_session		*session;

	C2_PRE(item != NULL);
	C2_PRE(!c2_rpc_item_is_bound(item));

	if (c2_rpc_item_is_unbound(item))
		sm_event.se_event = C2_RPC_FRM_EXTEVT_UBRPCITEM_ADDED;
	else
		sm_event.se_event = C2_RPC_FRM_EXTEVT_USRPCITEM_ADDED;

	/* Add the item to list of unbound items in its session. */
	session = item->ri_session;
	C2_ASSERT(session != NULL);
	c2_mutex_lock(&session->s_mutex);
	c2_list_add(&session->s_unbound_items, &item->ri_unbound_link);
	c2_mutex_unlock(&session->s_mutex);

	/* Curent state is not known at the moment. */
	return sm_default_handler(item, NULL, C2_RPC_FRM_STATES_NR,
			&sm_event);
}

/**
  Callback function for <struct c2_net_buffer> which indicates that
  message has been sent out from the buffer. This callback function
  corresponds to the C2_NET_QT_MSG_SEND event
  @param item - net buffer event
 */
void c2_rpc_frm_net_buffer_sent(const struct c2_net_buffer_event *ev)
{
	struct c2_net_buffer		*nb;
	struct c2_net_domain		*dom;
	struct c2_net_transfer_mc	*tm;
	struct c2_rpc_frm_buffer	*fb;
	struct c2_rpc_item		*item;
	struct c2_rpc_item		*rpc_item;
	struct c2_rpc_item		*rpc_item_next;
	struct c2_rpc_frm_sm		*frm_sm;
	struct c2_rpc_formation		*formation;

	C2_PRE((ev != NULL) && (ev->nbe_buffer != NULL) &&
		(ev->nbe_buffer->nb_qtype == C2_NET_QT_MSG_SEND));

	nb = ev->nbe_buffer;
	dom = nb->nb_dom;
	tm = nb->nb_tm;
	fb = container_of(nb, struct c2_rpc_frm_buffer, fb_buffer);
	C2_PRE(frm_buf_invariant(fb));
	item = c2_list_entry(c2_list_first(&fb->fb_rpc->r_items),
			struct c2_rpc_item, ri_rpcobject_linkage);
	C2_ASSERT(item->ri_state == RPC_ITEM_ADDED);
	formation = &item->ri_mach->cr_formation;
	C2_ASSERT(formation != NULL);

	/* The buffer should have been dequeued by now. */
	C2_ASSERT((nb->nb_flags & C2_NET_BUF_QUEUED) == 0);

	if (ev->nbe_status == 0) {
		frm_item_set_rpc_stats(fb->fb_rpc);
		frm_item_set_state(fb->fb_rpc, RPC_ITEM_SENT);

		/* Detach all rpc items from this object */
		c2_list_for_each_entry_safe(&fb->fb_rpc->r_items, rpc_item,
				rpc_item_next, struct c2_rpc_item,
				ri_rpcobject_linkage)
			c2_list_del(&rpc_item->ri_rpcobject_linkage);

		c2_rpc_rpcobj_fini(fb->fb_rpc);
		c2_free(fb->fb_rpc);
		frm_buffer_fini(fb);
	} else {
		/* If the send event fails, add the rpc back to concerned
		   queue so that it will be processed next time.*/
		if (--fb->fb_retry != 0) {
			frm_sm = fb->fb_frm_sm;
			c2_mutex_lock(&frm_sm->fs_lock);
			C2_ADDB_ADD(&frm_sm->fs_formation->
					rf_rpc_form_addb, &frm_addb_loc,
					formation_func_fail,
					"send retry", ev->nbe_status);
			c2_list_add(&frm_sm->fs_rpcs, &fb->fb_rpc->r_linkage);
			frm_buffer_fini(fb);
			frm_send_onwire(frm_sm);
			c2_mutex_unlock(&frm_sm->fs_lock);
		} else {
			C2_ADDB_ADD(&fb->fb_frm_sm->fs_formation->
					rf_rpc_form_addb, &frm_addb_loc,
					formation_func_fail,
					"net buf send failed", ev->nbe_status);
			frm_item_set_state(fb->fb_rpc, RPC_ITEM_SEND_FAILED);
			c2_rpc_rpcobj_fini(fb->fb_rpc);
			c2_free(fb->fb_rpc);
			frm_buffer_fini(fb);
		}
	}

}

/**
   Callback function for deletion of an rpc item from the rpc items cache.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
   @retval 0 (success) -errno (failure)
 */
int c2_rpc_frm_item_delete(struct c2_rpc_item *item)
{
	int				 rc = 0;
	struct c2_rpc_frm_sm		*frm_sm;

	C2_PRE(item != NULL);

	frm_sm = rpc_item_to_frm_sm(item);
	if (frm_sm == NULL)
		return -EINVAL;
	c2_mutex_lock(&frm_sm->fs_lock);
	frm_item_remove(frm_sm, item);
	c2_mutex_unlock(&frm_sm->fs_lock);
	return rc;
}

int c2_rpc_frm_item_change_priority(struct c2_rpc_item *item,
		enum c2_rpc_item_priority prio)
{
	int		      rc;
	struct c2_rpc_frm_sm *frm_sm;

	C2_PRE(item != NULL);
	C2_PRE(prio < C2_RPC_ITEM_PRIO_NR);

	frm_sm = rpc_item_to_frm_sm(item);
	if (frm_sm == NULL)
		return -EINVAL;

	c2_mutex_lock(&frm_sm->fs_lock);
	frm_item_remove(frm_sm, item);
	item->ri_prio = prio;
	rc = frm_item_add(frm_sm, item);
	c2_mutex_unlock(&frm_sm->fs_lock);
	return rc;
}

int c2_rpc_frm_item_change_timeout(struct c2_rpc_item *item, c2_time_t deadline)
{
	int		      rc;
	struct c2_rpc_frm_sm *frm_sm;

	C2_PRE(item != NULL);

	frm_sm = rpc_item_to_frm_sm(item);
	if (frm_sm == NULL)
		return -EINVAL;

	c2_mutex_lock(&frm_sm->fs_lock);
	frm_item_remove(frm_sm, item);
	item->ri_deadline = deadline;
	rc = frm_item_add(frm_sm, item);
	c2_mutex_unlock(&frm_sm->fs_lock);
	return rc;
}

int c2_rpc_frm_item_change_rpcgroup(struct c2_rpc_item *item,
				    struct c2_rpc_group *group)
{
	int		      rc;
	struct c2_rpc_frm_sm *frm_sm;

	C2_PRE(item != NULL);

	frm_sm = rpc_item_to_frm_sm(item);
	if (frm_sm == NULL)
		return -EINVAL;

	c2_mutex_lock(&frm_sm->fs_lock);
	frm_item_remove(frm_sm, item);
	item->ri_group = group;
	rc = frm_item_add(frm_sm, item);
	c2_mutex_unlock(&frm_sm->fs_lock);
	return rc;
}

/**
   Process the rpc item for which reply has been received.
   @param frm_sm - formation state machine
   @param item - rpc item for which reply is received
 */
static void frm_reply_received(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item)
{
	struct c2_rpc_frm_item_coalesced *c_item;
	struct c2_rpc_frm_item_coalesced *c_item_next;

	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));
	C2_PRE(item != NULL);

	/* Do all the post processing for a coalesced item. */
	c2_list_for_each_entry_safe(&frm_sm->fs_coalesced_items,
			c_item, c_item_next, struct c2_rpc_frm_item_coalesced,
			ic_linkage) {
		if (c_item->ic_resultant_item == item)
			coalesced_item_reply_post(c_item);
		else
			item->ri_type->rit_ops->rito_replied(item, 0);
	}
}

/**
   Callback function for reply received of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param reply_item - reply item.
   @param req_item - request item
   @retval 0 (success) -errno (failure)
 */
int c2_rpc_frm_item_reply_received(struct c2_rpc_item *reply_item,
		struct c2_rpc_item *req_item)
{
	struct c2_rpc_frm_sm		*frm_sm;
	struct c2_rpc_frm_sm_event	 sm_event;
	enum c2_rpc_frm_state		 sm_state;

	C2_PRE(req_item != NULL);

	sm_event.se_event = C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED;

	frm_sm = rpc_item_to_frm_sm(req_item);
	if (frm_sm == NULL)
		return -EINVAL;

	c2_mutex_lock(&frm_sm->fs_lock);
	frm_reply_received(frm_sm, req_item);
	sm_state = frm_sm->fs_state;
	c2_mutex_unlock(&frm_sm->fs_lock);

	return sm_default_handler(req_item, frm_sm, sm_state, &sm_event);
}

/**
   Add the given rpc item to the list of urgent items in state machine.
   @param frm_sm - formation state machine
   @param item - item for which timeout has been triggered
 */
static void item_timeout_handle(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item)
{
	struct c2_list *list;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);
	C2_PRE(item->ri_state == RPC_ITEM_SUBMITTED);

	item->ri_deadline = 0;

	/* Move the rpc item to first list in unformed item data structure
	   so that it is bundled first in the rpc being formed. */
	c2_list_del(&item->ri_unformed_linkage);
	list = &frm_sm->fs_unformed_prio[0].pl_unformed_items;
	c2_list_add(list, &item->ri_unformed_linkage);
}

/**
   Callback function for deadline expiry of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - rpc item for which timeout has been triggered.
   @retval 0 (success) -errno (failure)
 */
int c2_rpc_frm_item_timeout(struct c2_rpc_item *item)
{
	struct c2_rpc_frm_sm_event	 sm_event;
	struct c2_rpc_frm_sm		*frm_sm;
	enum c2_rpc_frm_state		 sm_state;

	C2_PRE(item != NULL);

	sm_event.se_event = C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT;

	frm_sm = rpc_item_to_frm_sm(item);
	if (frm_sm == NULL)
		return -EINVAL;
	c2_mutex_lock(&frm_sm->fs_lock);
	item_timeout_handle(frm_sm, item);
	sm_state = frm_sm->fs_state;
	c2_mutex_unlock(&frm_sm->fs_lock);

	return sm_default_handler(item, frm_sm, sm_state, &sm_event);
}

/**
   Call the completion callbacks for member rpc items of
   a coalesced rpc item.
   @param cs - coalesced item
 */
static void coalesced_item_reply_post(struct c2_rpc_frm_item_coalesced *cs)
{
	struct c2_rpc_item *item;
	struct c2_rpc_item *item_next;

	C2_PRE(cs != NULL);

	/* For all member items of coalesced_item struct, call
	   their completion callbacks. */
	c2_list_for_each_entry_safe(&cs->ic_member_list, item, item_next,
			struct c2_rpc_item, ri_coalesced_linkage) {
		c2_list_del(&item->ri_coalesced_linkage);
		item->ri_type->rit_ops->rito_replied(item, 0);
		cs->ic_member_nr--;
	}
	C2_ASSERT(cs->ic_member_nr == 0);
	item = cs->ic_resultant_item;
	item->ri_type->rit_ops->rito_iovec_restore(item, cs->ic_bkpfop);
	item->ri_type->rit_ops->rito_replied(item, 0);
	coalesced_item_fini(cs);
}

/**
   State function for WAITING state.
   Formation is waiting for any event to trigger. This is an exit point
   for the formation state machine.
   @param frm_sm - Corresponding c2_rpc_frm_sm structure for given rpc item.
   @param item - input rpc item.
   @param event - Since WAITING state handles a lot of events,
   @retval internal event id
 */
static enum c2_rpc_frm_evt_id sm_waiting_state(
		struct c2_rpc_frm_sm *frm_sm ,struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event)
{
	C2_PRE(item != NULL);
	C2_PRE((event->se_event == C2_RPC_FRM_INTEVT_STATE_SUCCEEDED) ||
			(event->se_event == C2_RPC_FRM_INTEVT_STATE_FAILED));
	C2_PRE(frm_sm_invariant(frm_sm));
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));

	frm_sm->fs_state = C2_RPC_FRM_STATE_WAITING;

	C2_POST(frm_sm_invariant(frm_sm));
	return C2_RPC_FRM_INTEVT_DONE;
}

/**
   Callback used to trigger the "deadline expired" event
   for an rpc item.
   @param data - data for this timer
   @retval 0 if success, -errno otherwise
 */
static unsigned long item_timer_callback(unsigned long data)
{
	int			 rc = 0;
	struct c2_rpc_item	*item;

	item = (struct c2_rpc_item*)data;

	if (item->ri_state == RPC_ITEM_SUBMITTED)
		rc = c2_rpc_frm_item_timeout(item);
	/* Returning 0 since timer routine ignores return values. */
	return 0;
}

/**
   Locate a rpcgroup object from list of such objects in formation
   state machine.
   @param frm_sm - Concerned Formation state machine.
   @param item - Item from which rpc group has to be located.
   @retval - Valid struct c2_rpc_frm_group, NULL otherwise.
 */
static struct c2_rpc_frm_group *frm_rpcgroup_locate(
		const struct c2_rpc_frm_sm *frm_sm,
		const struct c2_rpc_item *item)
{
	struct c2_rpc_frm_group *rg;

	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));
	C2_PRE(item != NULL);
	C2_PRE(item->ri_group != NULL);

	c2_list_for_each_entry(&frm_sm->fs_groups, rg,
			struct c2_rpc_frm_group, frg_linkage) {
		if (rg->frg_group == item->ri_group)
			return rg;
	}
	return NULL;
}

/**
   Create a rpcgroup object and add it to list of such objects in formation
   state machine.
   @param frm_sm - Concerned Formation state machine.
   @param item - Item from which rpc group has to be created.
   @retval - Valid struct c2_rpc_frm_group, NULL otherwise.
 */
static struct c2_rpc_frm_group *frm_rpcgroup_init(
		struct c2_rpc_frm_sm *frm_sm, const struct c2_rpc_item *item)
{
	struct c2_rpc_formation *formation;
	struct c2_rpc_frm_group	*rg;

	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));
	C2_PRE(item != NULL);
	C2_PRE(item->ri_group != NULL);

	formation = &item->ri_mach->cr_formation;
	C2_ALLOC_PTR_ADDB(rg, &formation->rf_rpc_form_addb, &frm_addb_loc);
	if (rg == NULL)
		return NULL;
	c2_list_link_init(&rg->frg_linkage);
	c2_list_add(&frm_sm->fs_groups, &rg->frg_linkage);
	rg->frg_group = item->ri_group;
	return rg;
}

/**
   Destroy a rpcgroup object and remove it from list of such objects
   in formation state machine.
   @param rg - Given frm rpc group object which has to be destroyed.
 */
static void frm_rpcgroup_fini(struct c2_rpc_frm_group *rg)
{
	C2_PRE(rg != NULL);
	c2_list_del(&rg->frg_linkage);
	c2_free(rg);
}

/**
   Remove the data of an rpc item embedded within the endpoint unit
   structure.
   @param frm_sm - formation state machine
   @param item - rpc item to be removed
 */
static void frm_item_remove(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item)
{
	size_t			 item_size;
	struct c2_rpc_frm_group	*rg;

	C2_PRE(item != NULL);
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));
	C2_PRE(item->ri_state == RPC_ITEM_SUBMITTED);

	/* Reduce the cumulative size of rpc items from formation
	   state machine. */
	item_size = item->ri_type->rit_ops->rito_item_size(item);
	frm_sm->fs_cumulative_size -= item_size;

	/* If timer of rpc item is still running, change the deadline in
	   rpc item as per remaining time and stop and fini the timer. */
	if (item->ri_deadline != 0) {
		item->ri_deadline = 0;
		c2_timer_stop(&item->ri_timer);
		c2_timer_fini(&item->ri_timer);
	} else if (item->ri_group == NULL)
		frm_sm->fs_urgent_nogrp_items_nr--;

	/* If item is bound, remove it from formation state machine data. */
	if (c2_rpc_item_is_bound(item))
		c2_list_del(&item->ri_unformed_linkage);

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

/**
   Update the c2_rpc_frm_sm data structure on addition of an rpc item.
   @param frm_sm - formation state machine
   @param item - rpc item to be added to the internal data structure
   @retval 0 if successful, -errno otherwise
 */
static int frm_item_add(struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item)
{
	int				 res = 0;
	bool				 item_inserted = false;
	size_t				 item_size;
	struct c2_list			*list;
	struct c2_rpc_item		*rpc_item;
	struct c2_rpc_item		*rpc_item_next;
	struct c2_rpc_frm_group		*rg;
	struct c2_rpc_formation		*formation;

	C2_PRE(item != NULL);
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));

	if (item->ri_state != RPC_ITEM_SUBMITTED)
		return -EINVAL;

	item_size = item->ri_type->rit_ops->rito_item_size(item);
	frm_sm->fs_cumulative_size += item_size;

	/* Initialize the timer only when the deadline value is non-zero
	   i.e. dont initialize the timer for URGENT items */
	formation = &item->ri_mach->cr_formation;
	if (item->ri_deadline != 0) {
		/* C2_TIMER_SOFT creates a different thread to handle the
		   callback. */
		c2_timer_init(&item->ri_timer, C2_TIMER_SOFT, item->ri_deadline,
				1, item_timer_callback, (unsigned long)item);
		res = c2_timer_start(&item->ri_timer);
		if (res != 0) {
			C2_ADDB_ADD(&formation->rf_rpc_form_addb,
				&frm_addb_loc, formation_func_fail,
				"frm_item_add", res);
			return res;
		}
	} else if (item->ri_group == NULL)
		frm_sm->fs_urgent_nogrp_items_nr++;

	/* If item is unbound or unsolicited, don't add it to list
	   of formation state machine. */
	if (!c2_rpc_item_is_bound(item))
		return res;

	/* Index into the array to find out correct list as per
	   priority of current rpc item. */
	C2_ASSERT(item->ri_prio < C2_RPC_ITEM_PRIO_NR);
	list = &frm_sm->fs_unformed_prio[C2_RPC_ITEM_PRIO_NR - item->ri_prio].
		pl_unformed_items;

	/* Insert the item into unformed list sorted according to timeout. */
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
		return res;

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

	return res;
}

/**
   Decide if an optimal rpc can be prepared from the items submitted
   to this endpoint.
   @param frm_sm - the c2_rpc_frm_sm structure
   based on whose data, it will be found if an optimal rpc can be made.
   @param frm_sm - formation state machine
   @param rpcobj_size - check if given size of rpc object is optimal or not.
   @retval true if size is optimal, false otherwise
 */
static bool frm_size_is_violated(struct c2_rpc_frm_sm *frm_sm,
		uint64_t rpcobj_size)
{
	C2_PRE(frm_sm != NULL);

	if (rpcobj_size >= frm_sm->fs_max_msg_size)
		return true;

	return false;
}

/**
   Policy function to dictate if an rpc should be formed or not.
   @param frm_sm - Concerned formation state machine
   @retval - true if rpc can be formed, false otherwise.
 */
static bool frm_check_policies(struct c2_rpc_frm_sm *frm_sm)
{
	/* If there are any rpc items whose deadline is expired,
	   trigger formation. */
	if (!c2_list_is_empty(&frm_sm->fs_unformed_prio[0].pl_unformed_items))
		return true;

	/* Number of urgent items which do not belong to any rpc group
	   added to this state machine so far.
	   Any number > 0 will trigger formation. */
	if (frm_sm->fs_urgent_nogrp_items_nr > 0)
		return true;

	/* Number of complete groups in the sense that this state
	   machine contains all rpc items from such rpc groups.
	   Any number > 0 will trigger formation. */
	if (frm_sm->fs_complete_groups_nr > 0)
		return true;

	return false;
}

/**
   Checks whether the items gathered so far in formation state machine
   are good enough to form a potential rpc object and proceed to
   forming state.
   @param frm_sm - Formation state machine.
   @retval - TRUE if qualified, FALSE otherwise.
 */
static bool formation_qualify(const struct c2_rpc_frm_sm *frm_sm)
{
	C2_PRE(frm_sm != NULL);

	/* If current rpcs in flight for this formation state machine
	   has reached the max rpcs limit, don't send any more rpcs
	   unless this number drops. */
	if (frm_sm->fs_formation->rf_client_side &&
	    frm_sm->fs_curr_rpcs_in_flight ==
	    frm_sm->fs_max_rpcs_in_flight)
		return false;

	if (frm_sm->fs_urgent_nogrp_items_nr > 0 ||
	    frm_sm->fs_cumulative_size >=
	    frm_sm->fs_max_msg_size)
		return true;
	return false;
}

/**
   State function for UPDATING state.
   Formation is updating its internal data structure by taking necessary locks.
   @param frm_sm - Corresponding c2_rpc_frm_sm structure for given rpc item.
   @param item - input rpc item.
   @param event - Since UPDATING state handles a lot of events,
   it needs some way of identifying the events.
   @retval internal event id
 */
static enum c2_rpc_frm_evt_id sm_updating_state(
		struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event)
{
	int			res;
	enum c2_rpc_frm_evt_id	ret;

	C2_PRE(item != NULL);
	C2_PRE(event != NULL &&
	       (event->se_event == C2_RPC_FRM_EXTEVT_RPCITEM_READY ||
	       event->se_event == C2_RPC_FRM_EXTEVT_UBRPCITEM_ADDED ||
	       event->se_event == C2_RPC_FRM_EXTEVT_USRPCITEM_ADDED));
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));
	C2_PRE(frm_sm_invariant(frm_sm));

	frm_sm->fs_state = C2_RPC_FRM_STATE_UPDATING;

	/* Add the item to frm_sm and subsequently to corresponding
	   priority list. */
	res = frm_item_add(frm_sm, item);
	if (res != 0)
		return C2_RPC_FRM_INTEVT_STATE_FAILED;

	/* Move the thread to the checking state only if an optimal rpc
	   can be formed. */
	if (formation_qualify(frm_sm))
		ret = C2_RPC_FRM_INTEVT_STATE_SUCCEEDED;
	else
		ret = C2_RPC_FRM_INTEVT_STATE_FAILED;

	C2_POST(frm_sm_invariant(frm_sm));
	return ret;
}

/**
   Check if addition of current fragment count and number of fragments
   from current rpc item fit within max_fragments count from
   formation state machine.
   @param frm_sm - Formation state machine.
   @param item - Input rpc item.
   @param fragments_nr - Current count of fragments.
   @retval TRUE if current count of fragments fit within max value,
   FALSE otherwise.
 */
static bool frm_fragment_policy_in_bounds(const struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item, uint64_t *fragments_nr)
{
	uint64_t curr_fragments;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);
	C2_PRE(fragments_nr != NULL);

	/* Fragment count check. */
	if (item->ri_type->rit_ops->rito_get_io_fragment_count) {
		curr_fragments = item->ri_type->rit_ops->
			rito_get_io_fragment_count(item);
		if ((*fragments_nr + curr_fragments) > frm_sm->fs_max_frags)
			return false;
	}
	return true;
}

/**
   Add an rpc item to the formed list of an rpc object.
   @param frm_sm - formation state machine
   @param rpc - rpc object
   @item - rpc item
   @rpcobj_size - rpc object size
   @param fragments_nr - number of fragments
 */
static void frm_add_to_rpc(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc *rpc, struct c2_rpc_item *item,
		uint64_t *rpcobj_size, uint64_t *fragments_nr)
{
	struct c2_rpc_slot *slot;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);
	C2_PRE(rpcobj_size != NULL);
	C2_PRE(fragments_nr != NULL);
	C2_PRE(item->ri_state != RPC_ITEM_ADDED);

	/* Update size of rpc object and current count of fragments. */
	c2_list_add(&rpc->r_items, &item->ri_rpcobject_linkage);
	*rpcobj_size += item->ri_type->rit_ops->rito_item_size(item);
	if (item->ri_type->rit_ops->rito_get_io_fragment_count != NULL)
		*fragments_nr += item->ri_type->rit_ops->
			rito_get_io_fragment_count(item);

	/* Remove the item data from c2_rpc_frm_sm structure. */
	frm_item_remove(frm_sm, item);
	item->ri_state = RPC_ITEM_ADDED;

	/* Remove item from slot->ready_items list AND if slot->ready_items
	   list is empty, remove slot from rpcmachine->ready_slots list.*/
	slot = item->ri_slot_refs[0].sr_slot;
	C2_ASSERT(slot != NULL);
	c2_list_del(&item->ri_slot_refs[0].sr_ready_link);
	//if (c2_list_is_empty(&slot->sl_ready_list))
	if (c2_list_link_is_in(&slot->sl_link))
		c2_list_del(&slot->sl_link);
}

/**
   Create a new c2_rpc_frm_item_coalesced structure and populate it.
   @param frm_sm - Formation state machine.
   @param fid - Concerned c2_fid structure.
   @param intent - read/write intent of coalesced item.
   @retval - Valid c2_rpc_frm_item_coalesced structure if succeeded,
   NULL otherwise.
 */
static struct c2_rpc_frm_item_coalesced *coalesced_item_init(
		struct c2_rpc_frm_sm *frm_sm)
{
	struct c2_rpc_frm_item_coalesced *coalesced_item;

	C2_PRE(frm_sm != NULL);

	C2_ALLOC_PTR_ADDB(coalesced_item,
			&frm_sm->fs_formation->rf_rpc_form_addb, &frm_addb_loc);
	if (coalesced_item == NULL)
		return NULL;
	c2_list_link_init(&coalesced_item->ic_linkage);
	coalesced_item->ic_resultant_item = NULL;
	coalesced_item->ic_member_nr = 0;
	coalesced_item->ic_bkpfop = NULL;
	c2_list_init(&coalesced_item->ic_member_list);
	/* Add newly created coalesced_item into list of fs_coalesced_items
	   in formation state machine. */
	c2_list_add(&frm_sm->fs_coalesced_items, &coalesced_item->ic_linkage);

	return coalesced_item;
}

/**
   Destroy a c2_rpc_frm_item_coalesced structure.
   @param c_item - Coalesced item to be deleted.
 */
static void coalesced_item_fini(struct c2_rpc_frm_item_coalesced *c_item)
{
	C2_PRE(c_item != NULL);

	c2_list_fini(&c_item->ic_member_list);
	c2_list_del(&c_item->ic_linkage);
	c2_free(c_item);
}

/**
   Populate the member items from session's unbound items list
   for a given coalesced item.
   @param b_item - Given bound rpc item.
   @param c_item - Given coalesced_item structure.
 */
static void frm_coalesced_item_populate(struct c2_rpc_item *b_item,
		struct c2_rpc_frm_item_coalesced *c_item)
{
	bool			 item_equal;
	bool			 fid_equal;
	struct c2_rpc_item	*ub_item;
	struct c2_rpc_session	*session;

	C2_PRE(b_item != NULL);
	C2_PRE(c_item != NULL);

	session = b_item->ri_session;
	C2_ASSERT(session != NULL);
	C2_PRE(c2_mutex_is_locked(&session->s_mutex));

	/* If fid and intent(read/write) of any unbound rpc item
	   are same as that of bound rpc item, add the given
	   unbound item as a member of current coalesced item structure. */
	c2_list_for_each_entry(&session->s_unbound_items, ub_item,
			struct c2_rpc_item, ri_unbound_link) {
		if (!ub_item->ri_type->rit_ops->rito_io_coalesce)
			continue;

		fid_equal = ub_item->ri_type->rit_ops->rito_fid_equal
			    (b_item, ub_item);
		item_equal = b_item->ri_type->rit_ops->rito_items_equal
			    (b_item, ub_item);
		if (fid_equal && item_equal) {
			c_item->ic_member_nr++;
			c2_list_add(&c_item->ic_member_list,
					&ub_item->ri_coalesced_linkage);
		}
	}
}

/**
   Try to coalesce rpc items from the session->free list.
   @pre The session, given item belongs to should be locked. This needs
   to be done due to the locking order of sessions code.
   @param frm_sm - the c2_rpc_frm_sm structure in which these activities
   are taking place.
   @param frm_sm - formation state machine
   @param item - given bound rpc item.
   @param rpcobj_size - current size of rpc object.
   @retval 0 if success, -errno otherwise
 */
static int try_coalesce(struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item,
		uint64_t *rpcobj_size)
{
	int					 rc = 0;
	uint64_t				 old_size;
	struct c2_rpc_item			*ub_item;
	struct c2_rpc_session			*session;
	struct c2_rpc_frm_item_coalesced	*coalesced_item;

	C2_PRE(item != NULL);
	C2_PRE(frm_sm != NULL);

	session = item->ri_session;
	C2_ASSERT(session != NULL);
	C2_PRE(c2_mutex_is_locked(&session->s_mutex));

	/* If there are no unbound items to coalesce, return right away. */
	if (c2_list_is_empty(&session->s_unbound_items))
		return rc;

	/* Similarly, if given rpc item is not part of an IO request,
	   return right away. */
	if (!item->ri_type->rit_ops->rito_io_coalesce)
		return rc;

	old_size = item->ri_type->rit_ops->rito_item_size(item);

	coalesced_item = coalesced_item_init(frm_sm);
	if (coalesced_item == NULL) {
		C2_ADDB_ADD(&frm_sm->fs_formation->rf_rpc_form_addb,
				&frm_addb_loc, c2_addb_oom);
		return -ENOMEM;
	}

	frm_coalesced_item_populate(item, coalesced_item);
	if (c2_list_is_empty(&coalesced_item->ic_member_list)) {
		coalesced_item_fini(coalesced_item);
		return rc;
	}

	/* Add the bound rpc item to member list so that it's IO segments
	   will also be coalesced. */
	c2_list_add(&coalesced_item->ic_member_list,
			&item->ri_coalesced_linkage);
	coalesced_item->ic_member_nr++;

	/* Try to coalesce IO segments of all member items. */
	rc = item->ri_type->rit_ops->rito_io_coalesce(coalesced_item, item);

	/* Remove the bound item from list of member elements
	   from a coalesced_item struct.*/
	c2_list_del(&item->ri_coalesced_linkage);
	coalesced_item->ic_member_nr--;

	if (rc == 0) {
		*rpcobj_size -= old_size;
		*rpcobj_size += item->ri_type->rit_ops->rito_item_size(item);
		/* Delete all member items for which coalescing was
		   successful from session->unbound list. */
		c2_list_for_each_entry(&coalesced_item->ic_member_list,
				ub_item, struct c2_rpc_item,
				ri_coalesced_linkage)
			c2_list_del(&ub_item->ri_unbound_link);
	}

	return rc;
}

/**
   Add bound items to rpc object. Rpc items are added until size gets
   optimal or any other policy of formation module has met.
   @param frm_sm - Formation state machine.
   @param rpcobj - c2_rpc object.
   @param rpcobj_size - Current size of rpc object.
   @param fragments_nr - Current number of IO fragments.
 */
static void bound_items_add_to_rpc(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc *rpcobj, uint64_t *rpcobj_size,
		uint64_t *fragments_nr)
{
	int				 cnt;
	int				 rc;
	bool				 sz_policy_violated = false;
	bool				 fragments_policy_ok = false;
	uint64_t			 rpc_size;
	struct c2_list			*list;
	struct c2_rpc_item		*rpc_item;
	struct c2_rpc_item		*rpc_item_next;
	struct c2_rpcmachine		*rpcmachine;
	struct c2_rpc_session		*session;

	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));
	C2_PRE(rpcobj_size != NULL);
	C2_PRE(fragments_nr != NULL);
	C2_PRE(rpcobj != NULL);

	/* Iterate over the priority bands and add items arranged in
	   increasing order of timeouts till rpc is optimal.
	   Algorithm skips the rpc items for which policies other than
	   size policy are not satisfied */
	for (cnt = 0; cnt < ARRAY_SIZE(frm_sm->fs_unformed_prio) &&
			!sz_policy_violated; ++cnt) {
		list = &frm_sm->fs_unformed_prio[cnt].pl_unformed_items;
		c2_list_for_each_entry_safe(list, rpc_item, rpc_item_next,
				struct c2_rpc_item, ri_unformed_linkage) {
			rpc_size = *rpcobj_size;
			sz_policy_violated = frm_size_is_violated(frm_sm,
					rpc_size);
			rpcmachine = rpc_item->ri_mach;

			/* If size threshold is not reached or other formation
			   policies are met, add item to rpc object. */
			if (!sz_policy_violated || frm_check_policies(frm_sm)) {
				fragments_policy_ok =
					frm_fragment_policy_in_bounds(frm_sm,
							rpc_item, fragments_nr);
				if (fragments_policy_ok) {
					c2_mutex_lock(&rpcmachine->
							cr_ready_slots_mutex);
					frm_add_to_rpc(frm_sm, rpcobj,
							rpc_item, rpcobj_size,
							fragments_nr);
					c2_mutex_unlock(&rpcmachine->
							cr_ready_slots_mutex);
					/* Try to coalesce current bound
					   item with list of unbound items
					   in its rpc session. */
					session = rpc_item->ri_session;
					c2_mutex_lock(&session->s_mutex);
					rc = try_coalesce(frm_sm, rpc_item,
							rpcobj_size);
					c2_mutex_unlock(&session->s_mutex);
				}
			} else
				break;
		}
	}
	rpc_size = *rpcobj_size;
	C2_POST(!frm_size_is_violated(frm_sm, rpc_size));
}

/**
   Make an unbound item (which is not unsolicited) bound by calling
   item_add_internal(). Also change item type flags accordingly.
   @param slot - Slot to which item has to be bound.
   @param item - Input rpc item.
 */
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

/**
   Make unbound items bound first and then add items to rpc object
   until rpc becomes optimal size or other formation policies are met.
   @param frm_sm - Formation state machine.
   @param rpcobj - c2_rpc object.
   @param rpcobj_size - Current size of rpc object.
   @param fragments_nr - Current number of IO fragments.
 */
static void unbound_items_add_to_rpc(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc *rpcobj, uint64_t *rpcobj_size,
		uint64_t *fragments_nr)
{
	int			 rc;
	bool			 sz_policy_violated = false;
	bool			 fragments_policy_ok = false;
	uint64_t		 rpc_size;
	struct c2_rpc_item	*item;
	struct c2_rpc_item	*item_next;
	struct c2_rpc_slot	*slot;
	struct c2_rpc_slot	*slot_next;
	struct c2_rpc_chan	*chan;
	struct c2_rpcmachine	*rpcmachine;
	struct c2_rpc_session	*session;

	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));
	C2_PRE(rpcobj_size != NULL);
	C2_PRE(fragments_nr != NULL);
	C2_PRE(rpcobj != NULL);

	/* Get slot and verno info from sessions component for
	   any unbound items in session->free list. */
	chan = frm_sm->fs_rpcchan;
	C2_ASSERT(chan != NULL);
	rpcmachine = chan->rc_rpcmachine;
	C2_ASSERT(rpcmachine != NULL);
	c2_mutex_lock(&rpcmachine->cr_ready_slots_mutex);

	/* Iterate ready slots list from rpcmachine and try to find an
	   item for each ready slot. */
	c2_list_for_each_entry_safe(&rpcmachine->cr_ready_slots, slot,
			slot_next, struct c2_rpc_slot, sl_link) {
		if (!c2_list_is_empty(&slot->sl_ready_list) ||
				(slot->sl_session->s_conn->c_rpcchan !=
				 frm_sm->fs_rpcchan))
			continue;

		c2_mutex_lock(&slot->sl_mutex);
		session = slot->sl_session;
		c2_mutex_lock(&session->s_mutex);
		c2_list_for_each_entry_safe(&session->s_unbound_items,
				item, item_next, struct c2_rpc_item,
				ri_unbound_link) {
			/* This is the way slot is supposed to be handled
			   by sessions code. */
			if (!c2_rpc_slot_can_item_add_internal(slot))
				break;
			rpc_size = *rpcobj_size;
			sz_policy_violated = frm_size_is_violated(frm_sm,
					rpc_size);
			if (!sz_policy_violated || frm_check_policies(frm_sm)) {
				fragments_policy_ok =
					frm_fragment_policy_in_bounds(
					frm_sm, item, fragments_nr);
				if (fragments_policy_ok) {
					frm_item_make_bound(slot, item);
					frm_add_to_rpc(frm_sm, rpcobj, item,
							rpcobj_size,
							fragments_nr);
					c2_list_del(&item->ri_unbound_link);
					rc = try_coalesce(frm_sm, item,
							rpcobj_size);
				}
			} else
				break;
		}
		c2_mutex_unlock(&session->s_mutex);
		c2_mutex_unlock(&slot->sl_mutex);
		/* Algorithm skips the rpc items for which policies other than
		   size policy are not satisfied */
		if (sz_policy_violated)
			break;
	}
	c2_mutex_unlock(&rpcmachine->cr_ready_slots_mutex);
}

/**
   State function for FORMING state.
   Core of formation algorithm. This state scans the rpc items cache and
   structure c2_rpc_frm_sm to form an RPC object by
   cooperation of multiple policies.
   Formation algorithm will take hints from rpc groups and will try to
   form rpc objects by keeping all group member rpc items together.
   Forming state will take care of coalescing of items.
   Coalescing Policy:
   A stream of unbound items will be coalesced together in a bound item.
   The bound item will be formed into an rpc and the member unbound items
   will be hanging off the coalesced item data structure.
   *** Formation Algorithm ***
   1. Check all formation policies to see if an rpc can be formed.
   2. If rpc can be formed, go through all bound items and add them to rpc.
      This step also includes IO coalescing which happens between a bound
      item and a stream of unbound items. On successful coalescing, the
      resultant IO vector is associated with the bound item and it is
      included in the rpc while the unbound items are hanging off the
      coalesced bound item.
   3. If space permits, add unbound items to rpc. All unbound items are
      made bound before they are included in rpc.
   4. Send the prepared rpc on wire. The rpc is encoded here and the
      resulting network buffer is sent to destination using a network
      transfer machine.
   @param frm_sm - Corresponding c2_rpc_frm_sm structure for given rpc item.
   @param item - input rpc item.
   @param event - Event which triggered state transition to FORMING state.
   @retval internal event id
 */
static enum c2_rpc_frm_evt_id sm_forming_state(
		struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event)
{
	int				 rc;
	bool				 size_optimal;
	bool				 frm_policy;
	uint64_t			 fragments_nr = 0;
	uint64_t			 rpcobj_size = 0;
	struct c2_rpc			*rpcobj;
	struct c2_rpc_formation		*formation;

	C2_PRE(item != NULL);
	C2_PRE(event != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));
	C2_PRE(frm_sm_invariant(frm_sm));

	frm_sm->fs_state = C2_RPC_FRM_STATE_FORMING;

	/* If optimal rpc can not be formed, or other formation policies
	   do not satisfy, return failure. */
	size_optimal = frm_size_is_violated(frm_sm, frm_sm->fs_cumulative_size);
	frm_policy = frm_check_policies(frm_sm);

	if (!(frm_policy || size_optimal)) {
		printf("forming state failed 1.\n");
		return C2_RPC_FRM_INTEVT_STATE_FAILED;
	}

	/* Create an rpc object in frm_sm->isu_rpcobj_list. */
	formation = &item->ri_mach->cr_formation;
	C2_ALLOC_PTR_ADDB(rpcobj, &formation->rf_rpc_form_addb, &frm_addb_loc);
	if (rpcobj == NULL)
		return C2_RPC_FRM_INTEVT_STATE_FAILED;
	c2_rpc_rpcobj_init(rpcobj);

	/* Try to include bound rpc items in rpc. This routine also includes
	   IO coalescing amongst a bound item and a stream of unbound items. */
	bound_items_add_to_rpc(frm_sm, rpcobj, &rpcobj_size, &fragments_nr);

	/* Try to include unbound rpc items in rpc. Unbound items are made
	   bound once they are included in rpc. */
	unbound_items_add_to_rpc(frm_sm, rpcobj, &rpcobj_size, &fragments_nr);

	if (c2_list_is_empty(&rpcobj->r_items)) {
		c2_rpc_rpcobj_fini(rpcobj);
		c2_free(rpcobj);
		printf("forming state failed 2.\n");
		return C2_RPC_FRM_INTEVT_STATE_FAILED;
	}

	c2_list_add(&frm_sm->fs_rpcs, &rpcobj->r_linkage);

	/* Send the prepared rpc on wire to destination. */
	rc = frm_send_onwire(frm_sm);
	C2_POST(frm_sm_invariant(frm_sm));
	if (rc == 0)
		return C2_RPC_FRM_INTEVT_STATE_SUCCEEDED;
	else {
		printf("forming state failed 3.\n");
		return C2_RPC_FRM_INTEVT_STATE_FAILED;
	}
}

/**
  Get the cumulative size of all rpc items
  @param rpc object of which size has to be calculated
  @retval sizeof rpc object
 */
uint64_t c2_rpc_get_size(const struct c2_rpc *rpc)
{
	uint64_t		 rpc_size = 0;
	struct c2_rpc_item	*item;

	C2_PRE(rpc != NULL);

	c2_list_for_each_entry(&rpc->r_items, item,
			struct c2_rpc_item, ri_rpcobject_linkage)
		rpc_size += item->ri_type->rit_ops->rito_item_size(item);

	return rpc_size;
}

/**
  Extract c2_net_transfer_mc from rpc item
  @param - rpc item
  @retval - network transfer machine
 */
static struct c2_net_transfer_mc *frm_get_tm(const struct c2_rpc_item *item)
{
	struct c2_net_transfer_mc *tm;

	C2_PRE((item != NULL) && (item->ri_session != NULL) &&
			(item->ri_session->s_conn != NULL) &&
			(item->ri_session->s_conn->c_rpcchan != NULL));

	tm = &item->ri_session->s_conn->c_rpcchan->rc_tm;
	return tm;
}

/**
   Function to send a given rpc object on wire.
   @param frm_sm - formation state machine
   @retval 0 if successful, -errno otherwise
 */
static int frm_send_onwire(struct c2_rpc_frm_sm *frm_sm)
{
	int				 rc = 0;
	uint64_t			 rpc_size = 0;
	struct c2_rpc			*rpc_obj;
	struct c2_rpc			*rpc_obj_next;
	struct c2_rpc_item		*item;
	struct c2_net_domain		*dom;
	struct c2_rpc_frm_buffer	*fb;
	struct c2_net_transfer_mc	*tm;

	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));

	/* Iterate over the rpc object list and send all rpc objects on wire. */
	c2_list_for_each_entry_safe(&frm_sm->fs_rpcs, rpc_obj,
			rpc_obj_next, struct c2_rpc, r_linkage) {
		item = c2_list_entry((c2_list_first(&rpc_obj->r_items)),
				struct c2_rpc_item, ri_rpcobject_linkage);
		if (frm_sm->fs_formation->rf_client_side &&
				frm_sm->fs_curr_rpcs_in_flight >=
				frm_sm->fs_max_rpcs_in_flight) {
			rc = -EBUSY;
			C2_ADDB_ADD(&frm_sm->fs_formation->rf_rpc_form_addb,
					&frm_addb_loc, formation_func_fail,
					"max in flight reached", rc);
			break;
		}
		tm = frm_get_tm(item);
		dom = tm->ntm_dom;
		rpc_size = c2_rpc_get_size(rpc_obj);

		/* Allocate a buffer for sending the message.*/
		rc = frm_buffer_init(&fb, rpc_obj, frm_sm, dom, rpc_size);
		if (rc < 0)
			/* Process the next rpc object in the list.*/
			continue;

		/* Populate destination net endpoint. */
		fb->fb_buffer.nb_ep = item->ri_session->s_conn->c_end_point;
		fb->fb_buffer.nb_length = rpc_size;

		/** @todo Allocate bulk i/o buffers before encoding. */
		/** @todo rpc_encode will encode the bulk i/o
		   buffer descriptors. */
#ifndef __KERNEL__
		rc = c2_rpc_encode(rpc_obj, &fb->fb_buffer);
#endif
		printf("Number of items bundled in rpc = %lu\n",
			c2_list_length(&rpc_obj->r_items));
		if (rc < 0) {
			C2_ADDB_ADD(&frm_sm->fs_formation->rf_rpc_form_addb,
					&frm_addb_loc, formation_func_fail,
					"c2_rpc_encode", 0);
			frm_buffer_fini(fb);
			/* Process the next rpc object in the list.*/
			continue;
		}

		/* Add the buffer to transfer machine.*/
		rc = c2_net_buffer_add(&fb->fb_buffer, tm);
		if (rc < 0) {
			C2_ADDB_ADD(&frm_sm->fs_formation->rf_rpc_form_addb,
					&frm_addb_loc, formation_func_fail,
					"c2_net_buffer_add", 0);
			frm_buffer_fini(fb);
			/* Process the next rpc object in the list.*/
			continue;
		}

		C2_ASSERT(fb->fb_buffer.nb_tm->ntm_dom == tm->ntm_dom);
		if (frm_sm->fs_formation->rf_client_side)
			frm_sm->fs_curr_rpcs_in_flight++;
		/* Remove the rpc object from rpcobj_list.*/
		c2_list_del(&rpc_obj->r_linkage);
	}
	return rc;
}

/**
   Decrement the current number of rpcs in flight from given rpc item.
   First, formation state machine is located from c2_rpc_conn and
   c2_rpcmachine pointed to by given rpc item and if formation state
   machine is found, its current count of in flight rpcs is decremented.
   @param item - Given rpc item.
 */
void c2_rpc_frm_rpcs_inflight_dec(struct c2_rpc_item *item)
{
	struct c2_rpc_frm_sm *frm_sm;

	C2_PRE(item != NULL);

	frm_sm = rpc_item_to_frm_sm(item);

	if (frm_sm != NULL) {
		c2_rwlock_write_lock(&item->ri_mach->cr_formation.
				rf_sm_list_lock);
		c2_mutex_lock(&frm_sm->fs_lock);

		if (frm_sm->fs_formation->rf_client_side &&
				frm_sm->fs_curr_rpcs_in_flight > 0)
			frm_sm->fs_curr_rpcs_in_flight--;

		c2_mutex_unlock(&frm_sm->fs_lock);
		c2_rwlock_write_unlock(&item->ri_mach->cr_formation.
				rf_sm_list_lock);
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

