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

#include "rpc/formation.h"
#include "fid/fid.h"
#include "ioservice/io_fops.h"
#ifdef __KERNEL__
#include "ioservice/linux_kernel/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
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
typedef enum c2_rpc_frm_int_evt_id (*statefunc_t)(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event);

/**
   Forward declarations of local static functions
 */
static void frm_item_remove(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item);

static int frm_item_add(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item);

static int frm_item_change(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item,
		struct c2_rpc_frm_sm_event *event);

static int frm_state_succeeded(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item, const enum c2_rpc_frm_state state);

static int frm_state_failed(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item, const enum c2_rpc_frm_state state);

static int frm_send_onwire(struct c2_rpc_frm_sm *frm_sm);

static void coalesced_item_fini(struct c2_rpc_frm_item_coalesced *c_item);

static void coalesced_item_reply_post(struct c2_rpc_frm_item_coalesced *cs);

static struct c2_rpc_frm_sm *frm_sm_init(struct c2_rpc_conn *conn,
		struct c2_rpc_formation *formation);

static enum c2_rpc_frm_int_evt_id sm_waiting_state(
		struct c2_rpc_frm_sm *frm_sm ,struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event);

static enum c2_rpc_frm_int_evt_id sm_updating_state(
		struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event);

static enum c2_rpc_frm_int_evt_id sm_forming_state(
		struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event);

/**
   Temporary threashold values. Will be moved to appropriate files
   once rpc integration is done.
 */
uint64_t max_msg_size;
uint64_t max_fragments_size;
uint64_t max_rpcs_in_flight;

/**
  This routine will set the stats for each rpc item
  in the rpc object to RPC_ITEM_SENT
  @param rpc - rpc object
 */
static void frm_item_set_rpc_stats(const struct c2_rpc *rpc)
{
	struct c2_rpc_item	*item = NULL;

	C2_PRE(rpc != NULL);

	c2_list_for_each_entry(&rpc->r_items, item,
			struct c2_rpc_item, ri_rpcobject_linkage) {
		c2_rpc_item_exit_stats_set(item, C2_RPC_PATH_OUTGOING);
	}
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
	struct c2_rpc_item	*item = NULL;

	C2_PRE(rpc != NULL);
	C2_PRE(state <= RPC_ITEM_SENT);

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
			&& fbuf->fb_magic == C2_RPC_FRM_BUFFER_MAGIC);
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
		struct c2_rpc_frm_sm *frm_sm, struct c2_net_domain *net_dom)
{
	int				 rc = 0;
	struct c2_rpc_frm_buffer	*fbuf = NULL;

	C2_PRE(fb != NULL);
	C2_PRE(rpc != NULL);
	C2_PRE(frm_sm != NULL);
	C2_PRE(net_dom != NULL);

	C2_ALLOC_PTR(fbuf);
	if (fbuf == NULL) {
		C2_ADDB_ADD(&frm_sm->fs_formation->rf_rpc_form_addb,
				&frm_addb_loc, c2_addb_oom);
		return -ENOMEM;
	}
	fbuf->fb_magic = C2_RPC_FRM_BUFFER_MAGIC;
	fbuf->fb_frm_sm = frm_sm;
	fbuf->fb_rpc = rpc;
	c2_rpc_net_send_buffer_allocate(net_dom, &fbuf->fb_buffer);
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
	int rc = 0;

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
   @param msg_size - Max message size
   @param max_rpcs - Max rpcs in flight
   @param max_fragments - Max fragments size
 */
void c2_rpc_frm_set_thresholds(uint64_t msg_size, uint64_t max_rpcs,
		uint64_t max_fragments)
{
	max_msg_size = msg_size;
	max_rpcs_in_flight = max_rpcs;
	max_fragments_size = max_fragments;
}

/**
   Initialization for formation component in rpc. This will register
   necessary callbacks and initialize necessary data structures.
   @param frm - formation structure
   @retval 0 if init completed, -errno otherwise
 */
int c2_rpc_frm_init(struct c2_rpc_formation **frm)
{
	int			 rc = 0;
	struct c2_rpc_formation *frm_local;

	C2_PRE(frm != NULL);

	C2_ALLOC_PTR(frm_local);
	if (frm_local == NULL)
		return -ENOMEM;

        c2_addb_ctx_init(&frm_local->rf_rpc_form_addb,
			&frm_addb_ctx_type, &c2_addb_global_ctx);
        c2_addb_choose_default_level(AEL_WARN);
	c2_rwlock_init(&frm_local->rf_sm_list_lock);
	c2_list_init(&frm_local->rf_frm_sm_list);
	frm_local->rf_client_side = false;
	*frm = frm_local;
	return rc;
}

/**
   Delete the group info list from struct c2_rpc_frm_sm.
   Called once formation component is finied.
   @param list - groups list
 */
static void frm_groups_list_fini(struct c2_list *list)
{
	struct c2_rpc_frm_rpcgroup	*group = NULL;
	struct c2_rpc_frm_rpcgroup	*group_next = NULL;

	C2_PRE(list != NULL);

	c2_list_for_each_entry_safe(list, group, group_next,
			struct c2_rpc_frm_rpcgroup, frg_linkage) {
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
	struct c2_rpc	*obj = NULL;
	struct c2_rpc	*obj_next = NULL;

	C2_PRE(list != NULL);

	c2_list_for_each_entry_safe(list, obj, obj_next, struct c2_rpc,
			r_linkage) {
		c2_list_del(&obj->r_linkage);
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
	int			 cnt;
	struct c2_rpc_item	*item = NULL;
	struct c2_rpc_item	*item_next = NULL;
	struct c2_list		*list;

	C2_PRE(list != NULL);

	for (cnt = 0; cnt < ARRAY_SIZE(frm_sm->fs_unformed_prio); ++cnt) {
		list = &frm_sm->fs_unformed_prio[C2_RPC_ITEM_PRIO_NR -
			item->ri_prio].pl_unformed_items;
		c2_list_for_each_entry_safe(list, item, item_next,
				struct c2_rpc_item, ri_unformed_linkage) {
			c2_list_del(&item->ri_unformed_linkage);
		}
		c2_list_fini(list);
	}
}

/**
   Finish method for formation component in rpc.
   This will deallocate all memory claimed by formation
   and do necessary cleanup.
   @param formation - c2_rpc_formation structure to be finied
 */
void c2_rpc_frm_fini(struct c2_rpc_formation *formation)
{
	struct c2_rpc_frm_sm	*frm_sm = NULL;
	struct c2_rpc_frm_sm	*frm_sm_next = NULL;

	C2_PRE(formation != NULL);

	/* Delete all state machines, all lists within each state machine. */
	c2_rwlock_write_lock(&formation->rf_sm_list_lock);
	c2_list_for_each_entry_safe(&formation->rf_frm_sm_list,
			frm_sm, frm_sm_next, struct
			c2_rpc_frm_sm, fs_linkage) {
		c2_mutex_lock(&frm_sm->fs_lock);
		frm_groups_list_fini(&frm_sm->fs_groups);
		coalesced_items_fini(&frm_sm->fs_coalesced_items);
		rpcobj_list_fini(&frm_sm->fs_rpcs);
		unformed_prio_fini(frm_sm);
		c2_mutex_unlock(&frm_sm->fs_lock);
		c2_list_del(&frm_sm->fs_linkage);
		c2_free(frm_sm);
	}
	c2_rwlock_write_unlock(&formation->rf_sm_list_lock);

	c2_rwlock_fini(&formation->rf_sm_list_lock);
	c2_list_fini(&formation->rf_frm_sm_list);
	c2_addb_ctx_fini(&formation->rf_rpc_form_addb);
	c2_free(formation);
}

/**
   Exit path from a state machine. An incoming thread which executed
   the formation state machine so far, is let go and it will return
   to do its own job.
   @param frm_sm - formation state machine
 */
static void sm_put(struct c2_rpc_frm_sm *frm_sm)
{
	struct c2_rpc_formation	*formation;

	C2_PRE(frm_sm != NULL);

	formation = frm_sm->fs_formation;
	c2_rwlock_write_lock(&formation->rf_sm_list_lock);
	/** Since the behavior is undefined for fini of mutex
	    when the mutex is locked, it is not locked here
	    for frm_sm.*/
	c2_ref_put(&frm_sm->fs_ref);
	c2_rwlock_write_unlock(&formation->rf_sm_list_lock);
}

/**
   Check if the state machine structure is empty.
   @param frm_sm - formation state machine
   @retval - TRUE if necessary conditions satisfy, false otherwise
 */
static bool sm_invariant(const struct c2_rpc_frm_sm *frm_sm)
{
	int cnt;

	if (frm_sm == NULL)
		return false;
	if (frm_sm->fs_state == C2_RPC_FRM_STATE_UNINITIALIZED)
		return false;
	if (!c2_list_is_empty(&frm_sm->fs_groups))
		return false;
	if (!c2_list_is_empty(&frm_sm->fs_coalesced_items))
		return false;
	if (!c2_list_is_empty(&frm_sm->fs_rpcs))
		return false;
	for (cnt = 0; cnt < ARRAY_SIZE(frm_sm->fs_unformed_prio); ++cnt) {
		if (!c2_list_is_empty(&frm_sm->fs_unformed_prio[cnt].
					pl_unformed_items))
			return false;
	}
	return true;
}

/**
   Destroy a state machine structure since it no longer contains
   any rpc items.
   @param ref - c2_ref reference on formation state machine
 */
static void frm_sm_fini(struct c2_ref *ref)
{
	int			 cnt;
	struct c2_rpc_frm_sm	*frm_sm;

	C2_PRE(ref != NULL);

	frm_sm = container_of(ref, struct c2_rpc_frm_sm, fs_ref);

	/* Delete the frm_sm only if all lists are empty.*/
	if (sm_invariant(frm_sm)) {
		c2_mutex_fini(&frm_sm->fs_lock);
		c2_list_del(&frm_sm->fs_linkage);
		c2_list_fini(&frm_sm->fs_groups);
		c2_list_fini(&frm_sm->fs_coalesced_items);
		c2_list_fini(&frm_sm->fs_rpcs);
		for (cnt = 0; cnt < ARRAY_SIZE(frm_sm->fs_unformed_prio); ++cnt)
			c2_list_fini(&frm_sm->fs_unformed_prio[cnt].
					pl_unformed_items);
		frm_sm->fs_rpcconn = NULL;
		c2_free(frm_sm);
	}
}

/**
   Add a formation state machine structure when the first rpc item gets added
   for a given c2_rpc_conn structure.
   @param conn - c2_rpc_conn structure used for unique formation state machine
   @param formation - c2_rpc_formation structure
   @retval new formation state machine if success, NULL otherwise
 */
static struct c2_rpc_frm_sm *frm_sm_add(struct c2_rpc_conn *conn,
		struct c2_rpc_formation *formation)
{
	uint64_t		 cnt;
	struct c2_rpc_frm_sm	*frm_sm;

	C2_PRE(conn != NULL);
	C2_PRE(formation != NULL);

	C2_ALLOC_PTR(frm_sm);
	if (frm_sm == NULL) {
		C2_ADDB_ADD(&formation->rf_rpc_form_addb,
				&frm_addb_loc, c2_addb_oom);
		return NULL;
	}
	frm_sm->fs_formation = formation;
	c2_mutex_init(&frm_sm->fs_lock);
	c2_list_add(&formation->rf_frm_sm_list, &frm_sm->fs_linkage);
	c2_list_init(&frm_sm->fs_groups);
	c2_list_init(&frm_sm->fs_coalesced_items);
	c2_list_init(&frm_sm->fs_rpcs);
	c2_ref_init(&frm_sm->fs_ref, 1, frm_sm_fini);
	frm_sm->fs_rpcconn = conn;
	frm_sm->fs_state = C2_RPC_FRM_STATE_WAITING;
	frm_sm->fs_max_msg_size = max_msg_size;
	frm_sm->fs_max_frags = max_fragments_size;
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
	return frm_sm;
}

/**
   Return the function pointer to next state function given the current state
   and current event as input.
   @param current_state - current state of state machine.
   @param current_event - current event posted to the state machine.
   @retval function pointer
 */
static statefunc_t frm_next_state(const int current_state,
		const int current_event)
{
	C2_PRE(current_state < C2_RPC_FRM_STATES_NR);
	C2_PRE(current_event < C2_RPC_FRM_INTEVT_NR);

	return c2_rpc_frm_statetable[current_state][current_event];
}

/**
   Get the c2_rpc_conn structure given an rpc item. The
   formation state machine is tightly bound to a c2_rpc_conn
   which points to a destination network endpoint. There is
   one-to-one relationship of c2_rpc_conn with c2_rpc_frm_sm.
   @param item - Input rpc item
   @retval - Connected c2_rpc_conn structure.
 */
static struct c2_rpc_conn *rpc_item_to_conn(const struct c2_rpc_item *item)
{
	return item->ri_session->s_conn;
}

/**
  For a given c2_rpc_conn, return an existing formation state machine
  object.
  @param conn - c2_rpc_conn structure used for unique formation state machine
  @param formation - c2_rpc_formation structure
  @retval formation state machine if success, NULL otherwise
 */
static struct c2_rpc_frm_sm *frm_sm_locate(const struct c2_rpc_conn *conn,
		struct c2_rpc_formation *formation)
{
	struct c2_rpc_frm_sm *frm_sm = NULL;
	struct c2_rpc_frm_sm *sm = NULL;

	C2_PRE(conn != NULL);
	C2_PRE(formation != NULL);

	c2_rwlock_read_lock(&formation->rf_sm_list_lock);
	c2_list_for_each_entry(&formation->rf_frm_sm_list, sm,
			struct c2_rpc_frm_sm, fs_linkage) {
		if (sm->fs_rpcconn == conn) {
			frm_sm = sm;
			break;
		}
	}
	if (frm_sm != NULL)
		c2_ref_get(&frm_sm->fs_ref);

	c2_rwlock_read_unlock(&formation->rf_sm_list_lock);
	return frm_sm;
}

/**
   Create a new formation state machine object.
   @param conn - c2_rpc_conn structure used for unique formation state machine
   @param formation - c2_rpc_formation structure
   @retval formation state machine if success, NULL otherwise
 */
static struct c2_rpc_frm_sm *frm_sm_init(struct c2_rpc_conn *conn,
		struct c2_rpc_formation *formation)
{
	struct c2_rpc_frm_sm	*sm = NULL;

	C2_PRE(conn != NULL);
	C2_PRE(formation != NULL);

	/* Add a new formation state machine. */
	c2_rwlock_write_lock(&formation->rf_sm_list_lock);
	sm = frm_sm_add(conn, formation);
	if (sm == NULL)
		C2_ADDB_ADD(&formation->rf_rpc_form_addb,
				&frm_addb_loc, formation_func_fail,
				"frm_sm_add", 0);

	c2_rwlock_write_unlock(&formation->rf_sm_list_lock);
	return sm;
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
   @param sm_event - event posted to the state machine.
   @retval 0 if success, -errno othewise
 */
static int sm_default_handler(struct c2_rpc_item *item,
		struct c2_rpc_frm_sm *frm_sm, int sm_state,
		const struct c2_rpc_frm_sm_event *sm_event)
{
	enum c2_rpc_frm_int_evt_id	 res = 0;
	enum c2_rpc_frm_state		 prev_state;
	struct c2_rpc_conn		*conn = NULL;
	struct c2_rpc_frm_sm		*sm = NULL;
	struct c2_rpc_formation		*formation;

	C2_PRE(item != NULL);
	C2_PRE(sm_event->se_event < C2_RPC_FRM_INTEVT_NR);
	C2_PRE(sm_state <= C2_RPC_FRM_STATES_NR);

	/* If state machine pointer from rpc item is NULL, locate it
	   from list in list of state machines. If found, lock it and
	   increment its refcount. If not found, create a new state machine.
	   In any case, find out the previous state of state machine. */
	if (frm_sm == NULL) {
		formation = item->ri_mach->cr_formation;
		conn = rpc_item_to_conn(item);
		sm = frm_sm_locate(conn, formation);
		if (sm == NULL) {
			sm = frm_sm_init(conn, formation);
			if (sm == NULL) {
				C2_ADDB_ADD(&formation->rf_rpc_form_addb,
						&frm_addb_loc, c2_addb_oom);
				return -ENOMEM;
			}
		}
		c2_mutex_lock(&sm->fs_lock);
		prev_state = sm->fs_state;
	} else {
		c2_mutex_lock(&frm_sm->fs_lock);
		prev_state = sm_state;
		sm = frm_sm;
	}

	/* Transition to next state.*/
	res = (frm_next_state(prev_state, sm_event->se_event))
		(sm, item, sm_event);

	/* The return value should be an internal event.
	   Assert if its not. */
	C2_ASSERT((res >= C2_RPC_FRM_INTEVT_STATE_SUCCEEDED) &&
			(res < C2_RPC_FRM_INTEVT_NR));
	/* Get latest state of state machine. */
	prev_state = sm->fs_state;
	c2_mutex_unlock(&sm->fs_lock);

	/* Exit point for state machine. */
	if (res == C2_RPC_FRM_INTEVT_DONE) {
		sm_put(sm);
		return 0;
	}

	if (res == C2_RPC_FRM_INTEVT_STATE_FAILED)
		/* Post a state failed event. */
		frm_state_failed(sm, item, prev_state);
	else if (res == C2_RPC_FRM_INTEVT_STATE_SUCCEEDED)
		/* Post a state succeeded event. */
		frm_state_succeeded(sm, item, prev_state);

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
	struct c2_rpc_frm_sm_event	 sm_event;
	struct c2_rpc_slot		*slot = NULL;
	struct c2_rpcmachine		*rpcmachine = NULL;

	C2_PRE(item != NULL);

	sm_event.se_event = C2_RPC_FRM_EXTEVT_RPCITEM_READY;
	sm_event.se_pvt = NULL;

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
   @retval 0 (success) -errno (failure)
 */
void c2_rpc_frm_slot_idle(struct c2_rpc_slot *slot)
{
	struct c2_rpcmachine		*rpcmachine = NULL;
	struct c2_rpc_session		*session;
	struct c2_rpc_frm_sm_event	 sm_event;

	C2_PRE(slot != NULL);
	C2_PRE(slot->sl_session != NULL);

	sm_event.se_event = C2_RPC_FRM_EXTEVT_SLOT_IDLE;
	sm_event.se_pvt = NULL;

	/* Add the slot to list of ready slots in its rpcmachine. */
	rpcmachine = slot->sl_session->s_conn->c_rpcmachine;
	C2_ASSERT(rpcmachine != NULL);
	c2_mutex_lock(&rpcmachine->cr_ready_slots_mutex);
	c2_list_add(&rpcmachine->cr_ready_slots, &slot->sl_link);
	c2_mutex_unlock(&rpcmachine->cr_ready_slots_mutex);

	/* If unbound items are still present in the sessions unbound list,
	   start formation */
	session = slot->sl_session;
}

/**
   Callback function for unbounded item getting added to session.
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
	sm_event.se_pvt = NULL;

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
	struct c2_net_buffer		*nb = NULL;
	struct c2_net_domain		*dom = NULL;
	struct c2_net_transfer_mc	*tm = NULL;
	struct c2_rpc_frm_buffer	*fb = NULL;
	struct c2_rpc_item		*item = NULL;
	struct c2_rpc_formation		*formation = NULL;

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
	formation = item->ri_mach->cr_formation;
	C2_ASSERT(formation != NULL);

	/* The buffer should have been dequeued by now. */
	C2_ASSERT((nb->nb_flags & C2_NET_BUF_QUEUED) == 0);

	if (ev->nbe_status == 0) {
		frm_item_set_rpc_stats(fb->fb_rpc);
		frm_item_set_state(fb->fb_rpc, RPC_ITEM_SENT);
		/* Release reference on the c2_rpc_frm_sm here. */
		sm_put(fb->fb_frm_sm);
		c2_free(fb->fb_rpc);
		frm_buffer_fini(fb);
	} else {
		/* If the send event fails, add the rpc back to concerned
		   queue so that it will be processed next time.*/
		c2_mutex_lock(&fb->fb_frm_sm->fs_lock);
		c2_list_add(&fb->fb_frm_sm->fs_rpcs, &fb->fb_rpc->r_linkage);
		frm_send_onwire(fb->fb_frm_sm);
		c2_mutex_unlock(&fb->fb_frm_sm->fs_lock);
		/* Release reference on the c2_rpc_frm_sm here. */
		sm_put(fb->fb_frm_sm);
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
	struct c2_rpc_frm_sm_event	 sm_event;
	struct c2_rpc_frm_sm		*frm_sm;

	C2_PRE(item != NULL);

	sm_event.se_event = C2_RPC_FRM_EXTEVT_RPCITEM_REMOVED;
	sm_event.se_pvt = NULL;

	frm_sm = frm_sm_locate(item->ri_session->s_conn,
			item->ri_mach->cr_formation);
	if (frm_sm == NULL)
		return -EINVAL;
	c2_mutex_lock(&frm_sm->fs_lock);
	rc = frm_item_change(frm_sm, item, &sm_event);
	c2_mutex_unlock(&frm_sm->fs_lock);
	return rc;
}

/**
   Callback function for change in parameter of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
   @param field_type - type of field that has changed
   @param val - new value
   @retval 0 (success) -errno (failure)
 */
int c2_rpc_frm_item_changed(struct c2_rpc_item *item, int field_type,
		union c2_rpc_frm_item_change_val *val)
{
	int					 rc = 0;
	struct c2_rpc_frm_sm			*frm_sm;
	struct c2_rpc_frm_sm_event		 sm_event;
	struct c2_rpc_frm_item_change_req	 req;

	C2_PRE(item != NULL);
	C2_PRE(val != NULL);
	C2_PRE(field_type < C2_RPC_ITEM_CHANGES_NR);

	/* Prepare rpc item change request. */
	sm_event.se_event = C2_RPC_FRM_EXTEVT_RPCITEM_CHANGED;
	req.field_type = field_type;
	req.value = val;
	sm_event.se_pvt = &req;

	frm_sm = frm_sm_locate(item->ri_session->s_conn,
			item->ri_mach->cr_formation);
	if (frm_sm == NULL)
		return -EINVAL;

	c2_mutex_lock(&frm_sm->fs_lock);
	rc = frm_item_change(frm_sm, item, &sm_event);
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
	int					 rc;
	bool					 item_coalesced = false;
	struct c2_rpc_frm_item_coalesced	*c_item;
	struct c2_rpc_frm_item_coalesced	*c_item_next;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);

	/* If event received is reply_received, do all the post processing
	   for a coalesced item. The rito_replied callbacks are
	   called without holding the frm_sm->fs_lock. */
	c2_mutex_lock(&frm_sm->fs_lock);
	c2_list_for_each_entry_safe(&frm_sm->fs_coalesced_items,
			c_item, c_item_next, struct c2_rpc_frm_item_coalesced,
			ic_linkage) {
		if (c_item->ic_resultant_item == item) {
			item_coalesced = true;
			c2_list_del(&c_item->ic_linkage);
		}
	}
	c2_mutex_unlock(&frm_sm->fs_lock);
	if (item_coalesced)
		coalesced_item_reply_post(c_item);
	else
		item->ri_type->rit_ops->rito_replied(item, rc);
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
	sm_event.se_pvt = NULL;

	frm_sm = frm_sm_locate(req_item->ri_session->s_conn,
			req_item->ri_mach->cr_formation);
	if (frm_sm == NULL)
		return -EINVAL;

	frm_reply_received(frm_sm, req_item);
	c2_mutex_lock(&frm_sm->fs_lock);
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
	sm_event.se_pvt = NULL;

	frm_sm = frm_sm_locate(item->ri_session->s_conn,
			item->ri_mach->cr_formation);
	if (frm_sm == NULL)
		return -EINVAL;
	c2_mutex_lock(&frm_sm->fs_lock);
	if (item->ri_state == RPC_ITEM_SUBMITTED)
		item_timeout_handle(frm_sm, item);
	sm_state = frm_sm->fs_state;
	c2_mutex_unlock(&frm_sm->fs_lock);

	return sm_default_handler(item, frm_sm, sm_state, &sm_event);
}

/**
   Function for successful completion of a state.
   Call the default handler function. Depending upon the
   input state, the default handler will invoke the next state
   for state succeeded event.
   @param frm_sm - formation state machine
   @param item - rpc item
   @param state - previous state of state machine.
 */
static int frm_state_succeeded(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item, const enum c2_rpc_frm_state state)
{
	struct c2_rpc_frm_sm_event		sm_event;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);

	sm_event.se_event = C2_RPC_FRM_INTEVT_STATE_SUCCEEDED;
	sm_event.se_pvt = NULL;

	/* Curent state is not known at the moment. */
	return sm_default_handler(item, frm_sm, state, &sm_event);
}

/**
   Callback function for failure of a state.
   Call the default handler function. Depending upon the
   input state, the default handler will invoke the next state
   for state failed event.
   @param frm_sm - formation state machine
   @param item - rpc item
   @param state - previous state of state machine.
 */
static int frm_state_failed(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item, const enum c2_rpc_frm_state state)
{
	struct c2_rpc_frm_sm_event		sm_event;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);

	sm_event.se_event = C2_RPC_FRM_INTEVT_STATE_FAILED;
	sm_event.se_pvt = NULL;

	/* Curent state is not known at the moment. */
	return sm_default_handler(item, frm_sm, state, &sm_event);
}

/**
   Call the completion callbacks for member rpc items of
   a coalesced rpc item.
   @param cs - coalesced item
 */
static void coalesced_item_reply_post(struct c2_rpc_frm_item_coalesced *cs)
{
	int			 rc = 0;
	struct c2_rpc_item	*item = NULL;
	struct c2_rpc_item	*item_next = NULL;

	C2_PRE(cs != NULL);

	/* For all member items of coalesced_item struct, call
	   their completion callbacks. */
	c2_list_for_each_entry_safe(&cs->ic_member_list, item, item_next,
			struct c2_rpc_item, ri_coalesced_linkage) {
		c2_list_del(&item->ri_coalesced_linkage);
		item->ri_type->rit_ops->rito_replied(item, rc);
		cs->ic_member_nr--;
	}
	C2_ASSERT(cs->ic_member_nr == 0);
	item = cs->ic_resultant_item;
	item->ri_type->rit_ops->rito_iovec_restore(item, &cs->ic_iovec);
	item->ri_type->rit_ops->rito_replied(item, rc);
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
static enum c2_rpc_frm_int_evt_id sm_waiting_state(
		struct c2_rpc_frm_sm *frm_sm ,struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event)
{
	C2_PRE(item != NULL);
	C2_PRE((event->se_event == C2_RPC_FRM_INTEVT_STATE_SUCCEEDED) ||
			(event->se_event == C2_RPC_FRM_INTEVT_STATE_FAILED));
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));

	frm_sm->fs_state = C2_RPC_FRM_STATE_WAITING;

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
	return rc;
}

/**
   Locate a rpcgroup object from list of such objects in formation
   state machine.
   @param frm_sm - Concerned Formation state machine.
   @param item - Item from which rpc group has to be located.
   @retval - Valid struct c2_rpc_frm_rpcgroup, NULL otherwise.
 */
static struct c2_rpc_frm_rpcgroup *frm_rpcgroup_locate(
		const struct c2_rpc_frm_sm *frm_sm,
		const struct c2_rpc_item *item)
{
	struct c2_rpc_frm_rpcgroup *rg;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);
	C2_PRE(item->ri_group != NULL);

	c2_list_for_each_entry(&frm_sm->fs_groups, rg,
			struct c2_rpc_frm_rpcgroup, frg_linkage) {
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
   @retval - Valid struct c2_rpc_frm_rpcgroup, NULL otherwise.
 */
static struct c2_rpc_frm_rpcgroup *frm_rpcgroup_init(
		struct c2_rpc_frm_sm *frm_sm, const struct c2_rpc_item *item)
{
	struct c2_rpc_formation		*formation;
	struct c2_rpc_frm_rpcgroup	*rg;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);
	C2_PRE(item->ri_group != NULL);

	formation = item->ri_mach->cr_formation;
	C2_ALLOC_PTR(rg);
	if (rg == NULL) {
		C2_ADDB_ADD(&formation->rf_rpc_form_addb,
				&frm_addb_loc, c2_addb_oom);
		return NULL;
	}
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
static void frm_rpcgroup_fini(struct c2_rpc_frm_rpcgroup *rg)
{
	C2_PRE(rg != NULL);
	c2_list_del(&rg->frg_linkage);
	c2_free(rg);
}

/**
   Change the data of an rpc item embedded within the endpoint unit
   structure.
   @param frm_sm - formation state machine
   @param item - rpc item
   @param rq - structure containing change request data
   @retval 0 (success) -errno otherwise
 */
static int frm_item_update(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item,
		const struct c2_rpc_frm_item_change_req *rq)
{
	int					 res = 0;
	int					 field_type = 0;
	struct c2_rpc_formation			*formation;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);
	C2_PRE(rq != NULL);
	C2_PRE(item->ri_state == RPC_ITEM_SUBMITTED);

	formation = item->ri_mach->cr_formation;
	field_type = rq->field_type;

	/* First remove the item data from formation state machine. */
	frm_item_remove(frm_sm, item);

	/* Change the item parameter to new values. */
	switch (field_type) {
	case C2_RPC_ITEM_CHANGE_PRIORITY:
		item->ri_prio = rq->value->cv_prio;
		break;
	case C2_RPC_ITEM_CHANGE_DEADLINE:
		item->ri_deadline = rq->value->cv_deadline;
		break;
	case C2_RPC_ITEM_CHANGE_RPCGROUP:
		item->ri_group = rq->value->cv_rpcgroup;
		break;
	default:
		C2_ADDB_ADD(&formation->rf_rpc_form_addb, &frm_addb_loc,
				formation_func_fail, "frm_item_update", res);
		C2_ASSERT(0);
	};

	/* Then, add the new data of rpc item to formation state machine. */
	res = frm_item_add(frm_sm, item);
	if (res != 0)
		C2_ADDB_ADD(&formation->rf_rpc_form_addb, &frm_addb_loc,
				formation_func_fail, "frm_item_update", res);
	return res;
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
	size_t				 item_size = 0;
	struct c2_rpc_frm_rpcgroup	*rg = NULL;

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
	struct c2_rpc_frm_rpcgroup	*rg = NULL;
	struct c2_rpc_item		*rpc_item = NULL;
	struct c2_rpc_item		*rpc_item_next = NULL;
	bool				 item_inserted = false;
	size_t				 item_size = 0;
	struct c2_rpc_formation		*formation;
	struct c2_list			*list;

	C2_PRE(item != NULL);
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));

	if (item->ri_state != RPC_ITEM_SUBMITTED)
		return -EINVAL;

	item_size = item->ri_type->rit_ops->rito_item_size(item);
	frm_sm->fs_cumulative_size += item_size;

	/* Initialize the timer only when the deadline value is non-zero
	   i.e. dont initialize the timer for URGENT items */
	formation = item->ri_mach->cr_formation;
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
static bool frm_is_size_optimal(struct c2_rpc_frm_sm *frm_sm,
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
static bool frm_policy_satisfy(struct c2_rpc_frm_sm *frm_sm)
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
static enum c2_rpc_frm_int_evt_id sm_updating_state(
		struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event)
{
	int				res;
	enum c2_rpc_frm_int_evt_id	ret;

	C2_PRE(item != NULL);
	C2_PRE(event != NULL &&
	       (event->se_event == C2_RPC_FRM_EXTEVT_RPCITEM_READY ||
	       event->se_event == C2_RPC_FRM_EXTEVT_UBRPCITEM_ADDED ||
	       event->se_event == C2_RPC_FRM_EXTEVT_USRPCITEM_ADDED));
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));

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
static bool frm_fragment_policy(const struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item, uint64_t *fragments_nr)
{
	bool		io_op;
	uint64_t	curr_fragments;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);
	C2_PRE(fragments_nr != NULL);

	/* Fragment count check. */
	io_op = item->ri_type->rit_ops->rito_is_io_req(item);
	if (io_op) {
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
	struct c2_rpc_slot		*slot = NULL;

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
	if (c2_list_is_empty(&slot->sl_ready_list))
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
		struct c2_rpc_frm_sm *frm_sm, struct c2_fid *fid,
		int intent)
{
	struct c2_rpc_frm_item_coalesced *coalesced_item;

	C2_PRE(frm_sm != NULL);
	C2_PRE(fid != NULL);

	C2_ALLOC_PTR(coalesced_item);
	if (coalesced_item == NULL) {
		C2_ADDB_ADD(&frm_sm->fs_formation->rf_rpc_form_addb
				, &frm_addb_loc, c2_addb_oom);
		return NULL;
	}
	c2_list_link_init(&coalesced_item->ic_linkage);
	coalesced_item->ic_fid = fid;
	coalesced_item->ic_op_intent = intent;
	coalesced_item->ic_resultant_item = NULL;
	coalesced_item->ic_member_nr = 0;
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
	int			 item_rw = 0;
	bool			 item_equal;
	struct c2_fid		 fid;
	struct c2_fid		 ufid;
	struct c2_rpc_item	*ub_item = NULL;
	struct c2_rpc_session	*session = NULL;

	C2_PRE(b_item != NULL);
	C2_PRE(c_item != NULL);

	fid = b_item->ri_type->rit_ops->rito_io_get_fid(b_item);
	item_rw = b_item->ri_type->rit_ops->rito_io_get_opcode(b_item);
	session = b_item->ri_session;
	C2_ASSERT(session != NULL);

	/* If fid and intent(read/write) of any unbound rpc item
	   are same as that of bound rpc item, add the given
	   unbound item as a member of current coalesced item structure. */
	c2_mutex_lock(&session->s_mutex);
	c2_list_for_each_entry(&session->s_unbound_items, ub_item,
			struct c2_rpc_item, ri_unbound_link) {
		if (!ub_item->ri_type->rit_ops->rito_is_io_req(ub_item))
			continue;

		ufid = ub_item->ri_type->rit_ops->rito_io_get_fid(ub_item);
		item_equal = b_item->ri_type->rit_ops->rito_items_equal(b_item,
				ub_item);
		if (c2_fid_eq(&fid, &ufid) && item_equal) {
			c_item->ic_member_nr++;
			c2_list_add(&c_item->ic_member_list,
					&ub_item->ri_coalesced_linkage);
		}
	}
	c2_mutex_unlock(&session->s_mutex);
}

/**
   Try to coalesce rpc items from the session->free list.
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
	int					 item_rw = 0;
	uint64_t				 old_size = 0;
	struct c2_fid				 fid;
	struct c2_rpc_item			*ub_item = NULL;
	struct c2_rpc_session			*session = NULL;
	struct c2_rpc_frm_item_coalesced	*coalesced_item = NULL;

	C2_PRE(item != NULL);
	C2_PRE(frm_sm != NULL);

	session = item->ri_session;
	C2_ASSERT(session != NULL);

	/* If there are no unbound items to coalesce, return right away. */
	c2_mutex_lock(&session->s_mutex);
	if (c2_list_is_empty(&session->s_unbound_items)) {
		c2_mutex_unlock(&session->s_mutex);
		return rc;
	}
	c2_mutex_unlock(&session->s_mutex);

	/* Similarly, if given rpc item is not part of an IO request,
	   return right away. */
	if (!item->ri_type->rit_ops->rito_is_io_req(item))
		return rc;

	old_size = item->ri_type->rit_ops->rito_item_size(item);
	fid = item->ri_type->rit_ops->rito_io_get_fid(item);
	item_rw = item->ri_type->rit_ops->rito_io_get_opcode(item);

	coalesced_item = coalesced_item_init(frm_sm, &fid, item_rw);
	if (coalesced_item == NULL) {
		C2_ADDB_ADD(&frm_sm->fs_formation->rf_rpc_form_addb,
				&frm_addb_loc, c2_addb_oom);
		return -ENOMEM;
	}

	frm_coalesced_item_populate(item, coalesced_item);
	if (c2_list_is_empty(&coalesced_item->ic_member_list)) {
		c2_list_del(&coalesced_item->ic_linkage);
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
	int				 rc = 0;
	bool				 size_optimal = false;
	bool				 fragments_policy = false;
	uint64_t			 rpc_size;
	struct c2_list			*list;
	struct c2_rpc_item		*rpc_item = NULL;
	struct c2_rpc_item		*rpc_item_next = NULL;
	struct c2_rpcmachine		*rpcmachine;

	C2_PRE(frm_sm != NULL);
	C2_PRE(rpcobj_size != NULL);
	C2_PRE(fragments_nr != NULL);
	C2_PRE(rpcobj != NULL);

	rpc_size = *rpcobj_size;
	/* Iterate over the priority bands and add items arranged in
	   increasing order of timeouts till rpc is optimal. */
	for (cnt = 0; cnt < ARRAY_SIZE(frm_sm->fs_unformed_prio) &&
			!size_optimal; ++cnt) {
		list = &frm_sm->fs_unformed_prio[cnt].pl_unformed_items;
		c2_list_for_each_entry_safe(list, rpc_item, rpc_item_next,
				struct c2_rpc_item, ri_unformed_linkage) {
			fragments_policy = frm_fragment_policy(frm_sm,
					rpc_item, fragments_nr);
			size_optimal = frm_is_size_optimal(frm_sm, rpc_size);
			rpcmachine = rpc_item->ri_mach;

			/* If size threshold is not reached or other formation
			   policies are met, add item to rpc object. */
			if (!size_optimal || frm_policy_satisfy(frm_sm)) {
				if (fragments_policy) {
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
					rc = try_coalesce(frm_sm, rpc_item,
							rpcobj_size);
				}
			} else
				break;
		}
	}
}

/**
   Make an unbound item bound by calling item_add_internal(). Also
   change item type flags accordingly.
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
		item->ri_type->rit_flags |= ~C2_RPC_ITEM_UNBOUND;
		item->ri_type->rit_flags |= C2_RPC_ITEM_BOUND;
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
	bool			 size_policy = false;
	bool			 fragments_policy = false;
	uint32_t		 slot_items_nr;
	uint64_t		 rpc_size;
	struct c2_rpc_item	*item;
	struct c2_rpc_item	*item_next;
	struct c2_rpc_slot	*slot;
	struct c2_rpc_slot	*slot_next;
	struct c2_rpc_conn	*conn;
	struct c2_rpcmachine	*rpcmachine;
	struct c2_rpc_session	*session;

	C2_PRE(frm_sm != NULL);
	C2_PRE(rpcobj_size != NULL);
	C2_PRE(fragments_nr != NULL);
	C2_PRE(rpcobj != NULL);

	/* Get slot and verno info from sessions component for
	   any unbound items in session->free list. */
	conn = frm_sm->fs_rpcconn;
	C2_ASSERT(conn != NULL);
	rpcmachine = conn->c_rpcmachine;
	C2_ASSERT(rpcmachine != NULL);
	c2_mutex_lock(&rpcmachine->cr_ready_slots_mutex);

	rpc_size = *rpcobj_size;

	/* Iterate ready slots list from rpcmachine and try to find an
	   item for each ready slot. */
	c2_list_for_each_entry_safe(&rpcmachine->cr_ready_slots, slot,
			slot_next, struct c2_rpc_slot, sl_link) {
		if (!c2_list_is_empty(&slot->sl_ready_list) ||
				(slot->sl_session->s_conn !=
				 frm_sm->fs_rpcconn))
			continue;

		session = slot->sl_session;
		c2_mutex_lock(&session->s_mutex);
		c2_mutex_lock(&slot->sl_mutex);
		/* Get the max number of rpc items that can be associated
		   with current slot before slot can be called as "busy". */
		slot_items_nr = c2_rpc_slot_items_possible_inflight(slot);
		c2_list_for_each_entry_safe(&session->s_unbound_items,
				item, item_next, struct c2_rpc_item,
				ri_unbound_link) {
			fragments_policy = frm_fragment_policy(frm_sm, item,
					fragments_nr);
			size_policy = frm_is_size_optimal(frm_sm, rpc_size);
			if (!size_policy || frm_policy_satisfy(frm_sm)) {
				if (fragments_policy) {
					frm_item_make_bound(slot, item);
					frm_add_to_rpc(frm_sm, rpcobj, item,
							rpcobj_size,
							fragments_nr);
					c2_list_del(&item->ri_unbound_link);
					if (--slot_items_nr == 0)
						break;
				}
			} else
				break;
		}
		c2_mutex_unlock(&slot->sl_mutex);
		c2_mutex_unlock(&session->s_mutex);
		if (size_policy)
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
static enum c2_rpc_frm_int_evt_id sm_forming_state(
		struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event)
{
	int				 rc = 0;
	bool				 size_optimal;
	bool				 frm_policy;
	uint64_t			 fragments_nr = 0;
	uint64_t			 rpcobj_size = 0;
	struct c2_rpc			*rpcobj;
	struct c2_rpc_formation		*formation;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);
	C2_PRE(event != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));

	frm_sm->fs_state = C2_RPC_FRM_STATE_FORMING;

	/* If optimal rpc can not be formed, or other formation policies
	   do not satisfy, return failure. */
	size_optimal = frm_is_size_optimal(frm_sm, frm_sm->fs_cumulative_size);
	frm_policy = frm_policy_satisfy(frm_sm);

	if (!(frm_policy || size_optimal))
		return C2_RPC_FRM_INTEVT_STATE_FAILED;

	/* Create an rpc object in frm_sm->isu_rpcobj_list. */
	formation = item->ri_mach->cr_formation;
	C2_ALLOC_PTR(rpcobj);
	if (rpcobj == NULL) {
		C2_ADDB_ADD(&formation->rf_rpc_form_addb,
				&frm_addb_loc, c2_addb_oom);
		return C2_RPC_FRM_INTEVT_STATE_FAILED;
	}
	c2_rpc_rpcobj_init(rpcobj);

	/* Try to include bound rpc items in rpc. This routine also includes
	   IO coalescing amongst a bound item and a stream of unbound items. */
	bound_items_add_to_rpc(frm_sm, rpcobj, &rpcobj_size, &fragments_nr);

	/* Try to include unbound rpc items in rpc. Unbound items are made
	   bound once they are included in rpc. */
	unbound_items_add_to_rpc(frm_sm, rpcobj, &rpcobj_size, &fragments_nr);

	if (c2_list_is_empty(&rpcobj->r_items)) {
		c2_free(rpcobj);
		return C2_RPC_FRM_INTEVT_STATE_FAILED;
	}

	c2_list_add(&frm_sm->fs_rpcs, &rpcobj->r_linkage);

	/* Send the prepared rpc on wire to destination. */
	rc = frm_send_onwire(frm_sm);
	if (rc == 0)
		return C2_RPC_FRM_INTEVT_STATE_SUCCEEDED;
	else
		return C2_RPC_FRM_INTEVT_STATE_FAILED;
}

/**
  Get the cumulative size of all rpc items
  @param rpc object of which size has to be calculated
  @retval sizeof rpc object
 */
uint64_t c2_rpc_get_size(const struct c2_rpc *rpc)
{
	struct c2_rpc_item	*item = NULL;
	uint64_t		 rpc_size = 0;

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
	struct c2_net_transfer_mc	*tm = NULL;

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
	int				 res = 0;
	int				 ret = 0;
	struct c2_rpc			*rpc_obj = NULL;
	struct c2_rpc			*rpc_obj_next = NULL;
	struct c2_net_domain		*dom = NULL;
	struct c2_net_transfer_mc	*tm = NULL;
	struct c2_rpc_item		*item = NULL;
	struct c2_rpc_frm_buffer	*fb = NULL;
	uint64_t			 rpc_size = 0;
	int				 rc = 0;

	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));

	/* Iterate over the rpc object list and send all rpc objects on wire. */
	c2_list_for_each_entry_safe(&frm_sm->fs_rpcs, rpc_obj,
			rpc_obj_next, struct c2_rpc, r_linkage) {
		item = c2_list_entry((c2_list_first(&rpc_obj->r_items)),
				struct c2_rpc_item, ri_rpcobject_linkage);
		if (frm_sm->fs_formation->rf_client_side &&
				frm_sm->fs_curr_rpcs_in_flight ==
				frm_sm->fs_max_rpcs_in_flight) {
			ret = -EBUSY;
			break;
		}
		tm = frm_get_tm(item);
		dom = tm->ntm_dom;

		/* Allocate a buffer for sending the message.*/
		rc = frm_buffer_init(&fb, rpc_obj, frm_sm, dom);
		if (rc < 0)
			/* Process the next rpc object in the list.*/
			continue;

		/* Populate destination net endpoint. */
		fb->fb_buffer.nb_ep = item->ri_session->s_conn->c_end_point;
		rpc_size = c2_rpc_get_size(rpc_obj);

		/* if rpc size is bigger than size of net buffer,
		   post addb event and process next rpc object in the list */
		if (rpc_size > c2_vec_count(&fb->fb_buffer.nb_buffer.ov_vec)) {
			C2_ADDB_ADD(&frm_sm->fs_formation->rf_rpc_form_addb,
					&frm_addb_loc, formation_func_fail,
					"frm_send_onwire", 0);
			continue;
		}
		fb->fb_buffer.nb_length = rpc_size;

		/* Encode the rpc contents. */
#ifndef __KERNEL__
		rc = c2_rpc_encode(rpc_obj, &fb->fb_buffer);
#endif
		if (rc < 0) {
			C2_ADDB_ADD(&frm_sm->fs_formation->rf_rpc_form_addb,
					&frm_addb_loc, formation_func_fail,
					"c2_rpc_encode", 0);
			frm_buffer_fini(fb);
			ret = rc;
			/* Process the next rpc object in the list.*/
			continue;
		}

		/* Add the buffer to transfer machine.*/
		res = c2_net_buffer_add(&fb->fb_buffer, tm);
		if (res < 0) {
			frm_buffer_fini(fb);
			/* Process the next rpc object in the list.*/
			continue;
		} else {
			C2_ASSERT(fb->fb_buffer.nb_tm->ntm_dom == tm->ntm_dom);
			/* Remove the rpc object from rpcobj_list.*/
			if (frm_sm->fs_formation->rf_client_side)
				frm_sm->fs_curr_rpcs_in_flight++;
			c2_list_del(&rpc_obj->r_linkage);
			/* Get a reference on c2_rpc_frm_sm so that
			   it is pinned in memory. */
			c2_ref_get(&frm_sm->fs_ref);
		}
	}
	return ret;
}

/**
   Change the given rpc item (either change it or remove it).
   @param frm_sm - Formation state machine.
   @param item - Input rpc item.
   @param event - Incoming state event.
   @retval - 0 if routine succeeds, -ve number (errno) otherwise.
 */
static int frm_item_change(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item, struct c2_rpc_frm_sm_event *event)
{
	int res = 0;

	C2_PRE(item != NULL);
	C2_PRE((event->se_event == C2_RPC_FRM_EXTEVT_RPCITEM_REMOVED) ||
			(event->se_event == C2_RPC_FRM_EXTEVT_RPCITEM_CHANGED));
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));

	if (item->ri_state == RPC_ITEM_SUBMITTED) {
		if (event->se_event == C2_RPC_FRM_EXTEVT_RPCITEM_REMOVED)
			frm_item_remove(frm_sm, item);
		else
			res = frm_item_update(frm_sm, item, event->se_pvt);
	}
	return res;
}

/**
   Function to map the on-wire FOP format to in-core FOP format.
   @param in - file format fid
   @param out - memory format fid
 */
void c2_rpc_frm_item_io_fid_wire2mem(struct c2_fop_file_fid *in,
		struct c2_fid *out)
{
        out->f_container = in->f_seq;
        out->f_key = in->f_oid;
}

/**
   Coalesce rpc items that share same fid and intent(read/write)
   @param c_item - c2_rpc_frm_item_coalesced structure.
   @param b_item - Given bound rpc item.
   @retval - 0 if routine succeeds, -ve number(errno) otherwise.
 */
int c2_rpc_item_io_coalesce(struct c2_rpc_frm_item_coalesced *c_item,
		struct c2_rpc_item *b_item)
{
	int			 res = 0;
	struct c2_list		 fop_list;
	struct c2_fop		*fop = NULL;
	struct c2_fop		*fop_next = NULL;
	struct c2_fop		*b_fop = NULL;
	struct c2_rpc_item	*item = NULL;

	C2_PRE(b_item != NULL);
	C2_PRE(c_item != NULL);

	c2_list_init(&fop_list);
	c2_list_for_each_entry(&c_item->ic_member_list, item,
			struct c2_rpc_item, ri_coalesced_linkage) {
		fop = c2_rpc_item_to_fop(item);
		c2_list_add(&fop_list, &fop->f_link);
	}
	b_fop = container_of(b_item, struct c2_fop, f_item);

	/* Restore the original IO vector of resultant rpc item. */
	res = fop->f_type->ft_ops->fto_io_coalesce(&fop_list, b_fop,
			&c_item->ic_iovec);

	c2_list_for_each_entry_safe(&fop_list, fop, fop_next,
			struct c2_fop, f_link)
		c2_list_del(&fop->f_link);

	c2_list_fini(&fop_list);
	if (res == 0)
		c_item->ic_resultant_item = b_item;
	return res;
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

	frm_sm = frm_sm_locate(item->ri_session->s_conn,
			item->ri_mach->cr_formation);

	if (frm_sm != NULL) {
		c2_rwlock_write_lock(&item->ri_mach->cr_formation->
				rf_sm_list_lock);
		c2_mutex_lock(&frm_sm->fs_lock);

		if (frm_sm->fs_formation->rf_client_side &&
				frm_sm->fs_curr_rpcs_in_flight > 0) {
			frm_sm->fs_curr_rpcs_in_flight--;
		}
		c2_mutex_unlock(&frm_sm->fs_lock);
		c2_ref_put(&frm_sm->fs_ref);
		c2_rwlock_write_unlock(&item->ri_mach->cr_formation->
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

