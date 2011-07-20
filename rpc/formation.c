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
 * Original author: Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 04/28/2011
 */

#include "formation.h"
#include "fid/fid.h"
#include <string.h>
#include "ioservice/io_fops.h"
#ifdef __KERNEL__
#include "ioservice/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#endif
#include "rpc/rpc_onwire.h"

/**
   Forward declarations of local static functions
 */
static void rpc_frm_item_remove(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item);

static int rpc_frm_state_failed(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item, const int state);

static int rpc_frm_state_succeeded(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item, const int state);

static int rpc_frm_item_add(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item);

static int rpc_frm_send_onwire(struct c2_rpc_frm_sm *frm_sm);

static int rpc_frm_item_change(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item,
		struct c2_rpc_frm_sm_event *event);

static void rpc_frm_item_coalesced_reply_post(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_frm_item_coalesced *coalesced_struct);

static struct c2_rpc_frm_sm *rpc_frm_sm_create(struct c2_net_end_point *net_ep,
		struct c2_rpc_formation *formation_summary);

static enum c2_rpc_frm_int_evt_id rpc_frm_waiting_state(
		struct c2_rpc_frm_sm *frm_sm ,struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event);

static enum c2_rpc_frm_int_evt_id rpc_frm_updating_state(
		struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event);

static enum c2_rpc_frm_int_evt_id rpc_frm_forming_state(
		struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event);

/**
  Sleep time till refcounts of all the formation state machines become zero.
 */
enum {
	PTIME = 10000000,
};

/**
   Temporary threashold values. Will be moved to appropriate files
   once rpc integration is done.
 */
uint64_t				max_msg_size;
uint64_t				max_fragments_size;
uint64_t				max_rpcs_in_flight;

/**
  This routine will change the state of each rpc item
  in the rpc object to RPC_ITEM_SENT
 */
static void rpc_frm_set_state_sent(const struct c2_rpc *rpc)
{
	struct c2_rpc_item	*item = NULL;

	C2_PRE(rpc != NULL);

	/* Change the state of each rpc item in the rpc object
	   to RPC_ITEM_SENT. */
	c2_list_for_each_entry(&rpc->r_items, item,
			struct c2_rpc_item, ri_rpcobject_linkage) {
		C2_ASSERT(item->ri_state == RPC_ITEM_ADDED);
		item->ri_state = RPC_ITEM_SENT;
		c2_rpc_item_set_outgoing_exit_stats(item);
	}
}

/**
   Invariant subroutine for struct c2_rpc_frm_buffer.
 */
bool c2_rpc_frm_buf_invariant(const struct c2_rpc_frm_buffer *fbuf)
{
	return (fbuf != NULL && fbuf->fb_frm_sm != NULL && fbuf->fb_rpc != NULL
			&& fbuf->fb_magic == C2_RPC_FRM_BUFFER_MAGIC);
}

/**
   Allocate a buffer of type struct c2_rpc_frm_buffer.
   The net buffer is allocated and registered with the net domain.
 */
static int rpc_frm_buffer_allocate(struct c2_rpc_frm_buffer **fb,
		struct c2_rpc *rpc, struct c2_rpc_frm_sm *frm_sm,
		struct c2_net_domain *net_dom)
{
	int				 rc = 0;
	struct c2_rpc_frm_buffer	*fbuf = NULL;

	C2_PRE(fb != NULL);
	C2_PRE(rpc != NULL);
	C2_PRE(frm_sm != NULL);
	C2_PRE(net_dom != NULL);

	C2_ALLOC_PTR(fbuf);
	if (fbuf == NULL) {
		return -ENOMEM;
	}
	fbuf->fb_magic = C2_RPC_FRM_BUFFER_MAGIC;
	fbuf->fb_frm_sm = frm_sm;
	fbuf->fb_rpc = rpc;
	c2_rpc_net_send_buffer_allocate(net_dom, &fbuf->fb_buffer);
	*fb = fbuf;
	C2_POST(c2_rpc_frm_buf_invariant(fbuf));
	return rc;
}

/**
   Deallocate a buffer of type struct c2_rpc_frm_buffer. The
   c2_net_buffer is deregistered and deallocated.
 */
static void rpc_frm_buffer_deallocate(struct c2_rpc_frm_buffer *fb)
{
	int	rc = 0;

	C2_PRE(fb != NULL);
	C2_PRE(c2_rpc_frm_buf_invariant(fb));

	/* Currently, our policy is to release the buffer on completion. */
	rc = c2_rpc_net_send_buffer_deallocate(&fb->fb_buffer,
			fb->fb_buffer.nb_dom);
	c2_free(fb);
}

/**
   A state table guiding resultant states on arrival of events
   on earlier states.
   next_state = statetable[current_state][current_event]
 */
statefunc c2_rpc_frm_statetable
[C2_RPC_FRM_STATES_NR][C2_RPC_FRM_INTEVT_NR - 1] = {

	[C2_RPC_FRM_STATE_UNINITIALIZED] = { NULL },

	[C2_RPC_FRM_STATE_WAITING] = {
		[C2_RPC_FRM_EXTEVT_RPCITEM_READY] = rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED] =
			rpc_frm_forming_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT] = rpc_frm_forming_state,
		[C2_RPC_FRM_EXTEVT_SLOT_IDLE] = rpc_frm_forming_state,
		[C2_RPC_FRM_EXTEVT_UBRPCITEM_ADDED] = rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_USRPCITEM_ADDED] = rpc_frm_updating_state,
		[C2_RPC_FRM_INTEVT_STATE_SUCCEEDED] = rpc_frm_waiting_state,
		[C2_RPC_FRM_INTEVT_STATE_FAILED] = rpc_frm_waiting_state,
	},

	[C2_RPC_FRM_STATE_UPDATING] = {
		[C2_RPC_FRM_EXTEVT_RPCITEM_READY] = rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED] =
			rpc_frm_forming_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT] = rpc_frm_forming_state,
		[C2_RPC_FRM_EXTEVT_SLOT_IDLE] = rpc_frm_forming_state,
		[C2_RPC_FRM_EXTEVT_UBRPCITEM_ADDED] = rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_USRPCITEM_ADDED] = rpc_frm_updating_state,
		[C2_RPC_FRM_INTEVT_STATE_SUCCEEDED] = rpc_frm_forming_state,
		[C2_RPC_FRM_INTEVT_STATE_FAILED] = rpc_frm_waiting_state,
	},

	[C2_RPC_FRM_STATE_FORMING] = {
		[C2_RPC_FRM_EXTEVT_RPCITEM_READY] = rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED] =
			rpc_frm_forming_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT] = rpc_frm_forming_state,
		[C2_RPC_FRM_EXTEVT_SLOT_IDLE] = rpc_frm_forming_state,
		[C2_RPC_FRM_EXTEVT_UBRPCITEM_ADDED] = rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_USRPCITEM_ADDED] = rpc_frm_updating_state,
		[C2_RPC_FRM_INTEVT_STATE_SUCCEEDED] = rpc_frm_waiting_state,
		[C2_RPC_FRM_INTEVT_STATE_FAILED] = rpc_frm_waiting_state,
	},
};

/**
   Set thresholds for rpc formation. Currently used by UT code.
 */
void c2_rpc_frm_set_thresholds(uint64_t msg_size, uint64_t max_rpcs,
		uint64_t max_fragments)
{
	max_msg_size = msg_size;
	max_rpcs_in_flight = max_rpcs;
	max_fragments_size = max_fragments;
}

static const struct c2_addb_ctx_type rpc_frm_addb_ctx_type = {
        .act_name = "rpc-formation"
};

static const struct c2_addb_loc rpc_frm_addb_loc = {
        .al_name = "rpc-formation"
};

C2_ADDB_EV_DEFINE(formation_func_fail, "formation_func_fail",
		C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

/**
   Initialization for formation component in rpc. This will register
   necessary callbacks and initialize necessary data structures.
   @pre formation summary is non-null
   @retval 0 if init completed, else nonzero
 */
int c2_rpc_frm_init(struct c2_rpc_formation **frm)
{
	int rc = 0;

	C2_PRE(frm != NULL);

	C2_ALLOC_PTR(*frm);
	if ((*frm)== NULL) {
		return -ENOMEM;
	}
        c2_addb_ctx_init(&(*frm)->rf_rpc_form_addb,
			&rpc_frm_addb_ctx_type, &c2_addb_global_ctx);
        c2_addb_choose_default_level(AEL_WARN);
	c2_rwlock_init(&(*frm)->rf_sm_list_lock);
	c2_list_init(&(*frm)->rf_frm_sm_list);
	return rc;
}

/**
   Check if refcounts of all state machines drop to zero.
   @retval FALSE if any of refcounts are non-zero,
   @retval TRUE otherwise.
 */
static bool rpc_frm_wait_for_completion(struct c2_rpc_formation
		*formation_summary)
{
	bool			 ret = true;
	int64_t			 refcount;
	struct c2_rpc_frm_sm	*frm_sm = NULL;

	C2_PRE(formation_summary != NULL);

	c2_rwlock_read_lock(&formation_summary->rf_sm_list_lock);
	c2_list_for_each_entry(&formation_summary->rf_frm_sm_list,
			frm_sm, struct c2_rpc_frm_sm,
			fs_linkage) {
		c2_mutex_lock(&frm_sm->fs_lock);
		refcount = c2_atomic64_get(&frm_sm->fs_ref.ref_cnt);
		if (refcount != 0) {
			ret = false;
			c2_mutex_unlock(&frm_sm->fs_lock);
			break;
		}
		c2_mutex_unlock(&frm_sm->fs_lock);
	}
	c2_rwlock_read_unlock(&formation_summary->rf_sm_list_lock);
	return ret;
}

/**
   Delete the group info list from struct c2_rpc_frm_sm.
   Called once formation component is finied.
 */
static void rpc_frm_empty_groups_list(struct c2_list *list)
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
 */
static void rpc_frm_empty_coalesced_items_list(struct c2_list *list)
{
	struct c2_rpc_frm_item_coalesced	 *coalesced_item = NULL;
	struct c2_rpc_frm_item_coalesced	 *coalesced_item_next = NULL;
	struct c2_rpc_frm_item_coalesced_member *coalesced_member = NULL;
	struct c2_rpc_frm_item_coalesced_member *coalesced_member_next = NULL;

	C2_PRE(list != NULL);

	c2_list_for_each_entry_safe(list, coalesced_item, coalesced_item_next,
			struct c2_rpc_frm_item_coalesced, ic_linkage) {
		c2_list_del(&coalesced_item->ic_linkage);
		c2_list_for_each_entry_safe(&coalesced_item->ic_member_list,
				coalesced_member, coalesced_member_next,
				struct c2_rpc_frm_item_coalesced_member,
				im_linkage) {
			c2_list_del(&coalesced_member->im_linkage);
			c2_free(coalesced_member);
		}
		c2_free(coalesced_item);
	}
	c2_list_fini(list);
}

/**
   Delete the rpcobj items list from struct c2_rpc_frm_sm.
 */
static void rpc_frm_empty_rpcobj_list(struct c2_list *list)
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
 */
static void rpc_frm_empty_unformed_prio(struct c2_rpc_frm_sm *frm_sm)
{
	int			 cnt;
	struct c2_rpc_item	*item = NULL;
	struct c2_rpc_item	*item_next = NULL;
	struct c2_list		*list;

	C2_PRE(list != NULL);

	for (cnt = 0; cnt <= C2_RPC_ITEM_PRIO_NR; cnt++) {
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
 */
void c2_rpc_frm_fini(struct c2_rpc_formation *formation_summary)
{
	c2_time_t		 stime;
	struct c2_rpc_frm_sm	*frm_sm = NULL;
	struct c2_rpc_frm_sm	*frm_sm_next = NULL;

	C2_PRE(formation_summary != NULL);

	/* Set the active flag of all state machines to false indicating
	   formation component is about to finish.
	   This will help to block all new threads from entering
	   the formation component. */
	c2_rwlock_read_lock(&formation_summary->rf_sm_list_lock);
	c2_list_for_each_entry(&formation_summary->rf_frm_sm_list,
			frm_sm, struct c2_rpc_frm_sm,
			fs_linkage) {
		c2_mutex_lock(&frm_sm->fs_lock);
		frm_sm->fs_active = false;
		c2_mutex_unlock(&frm_sm->fs_lock);
	}
	c2_rwlock_read_unlock(&formation_summary->rf_sm_list_lock);
	c2_time_set(&stime, 0, PTIME);

	/* Iterate over the list of state machines until refcounts of all
	   become zero. */
	while (!rpc_frm_wait_for_completion(formation_summary)) {
		c2_nanosleep(stime, NULL);
	}

	/* Delete all state machines, all lists within each state machine. */
	c2_rwlock_write_lock(&formation_summary->rf_sm_list_lock);
	c2_list_for_each_entry_safe(&formation_summary->rf_frm_sm_list,
			frm_sm, frm_sm_next, struct
			c2_rpc_frm_sm, fs_linkage) {
		c2_mutex_lock(&frm_sm->fs_lock);
		rpc_frm_empty_groups_list(&frm_sm->fs_groups);
		rpc_frm_empty_coalesced_items_list(&frm_sm->
				fs_coalesced_items);
		rpc_frm_empty_rpcobj_list(&frm_sm->fs_rpcs);
		rpc_frm_empty_unformed_prio(frm_sm);
		c2_mutex_unlock(&frm_sm->fs_lock);
		c2_list_del(&frm_sm->fs_linkage);
		c2_free(frm_sm);
	}
	c2_rwlock_write_unlock(&formation_summary->rf_sm_list_lock);

	c2_rwlock_fini(&formation_summary->rf_sm_list_lock);
	c2_list_fini(&formation_summary->rf_frm_sm_list);
	c2_addb_ctx_fini(&formation_summary->rf_rpc_form_addb);
	c2_free(formation_summary);
}

/**
   Exit path from a state machine. An incoming thread which executed
   the formation state machine so far, is let go and it will return
   to do its own job.
 */
static void rpc_frm_sm_exit(struct c2_rpc_formation *formation_summary,
		struct c2_rpc_frm_sm *frm_sm)
{
	C2_PRE(frm_sm != NULL);
	C2_PRE(formation_summary != NULL);

	c2_rwlock_write_lock(&formation_summary->rf_sm_list_lock);
	/** Since the behavior is undefined for fini of mutex
	    when the mutex is locked, it is not locked here
	    for frm_sm.*/
	c2_ref_put(&frm_sm->fs_ref);
	c2_rwlock_write_unlock(&formation_summary->rf_sm_list_lock);
}

/**
   Check if the state machine structure is empty.
 */
static bool rpc_frm_sm_invariant(struct c2_rpc_frm_sm *frm_sm)
{
	if (!frm_sm)
		return false;
	if (frm_sm->fs_state == C2_RPC_FRM_STATE_UNINITIALIZED)
		return false;
	if (!c2_list_is_empty(&frm_sm->fs_groups))
		return false;
	if (!c2_list_is_empty(&frm_sm->fs_coalesced_items))
		return false;
	if (!c2_list_is_empty(&frm_sm->fs_rpcs))
		return false;
	return true;
}

/**
   Destroy a state machine structure since it no longer contains
   any rpc items.
 */
static void rpc_frm_sm_destroy(struct c2_ref *ref)
{
	int			 cnt;
	struct c2_rpc_frm_sm	*frm_sm;

	C2_PRE(ref != NULL);

	frm_sm = container_of(ref, struct c2_rpc_frm_sm, fs_ref);
	/* Delete the frm_sm only if all lists are empty.*/
	if (rpc_frm_sm_invariant(frm_sm)) {
		c2_mutex_fini(&frm_sm->fs_lock);
		c2_list_del(&frm_sm->fs_linkage);
		c2_list_fini(&frm_sm->fs_groups);
		c2_list_fini(&frm_sm->fs_coalesced_items);
		c2_list_fini(&frm_sm->fs_rpcs);
		for (cnt = 0; cnt <= C2_RPC_ITEM_PRIO_NR; cnt++)
			c2_list_fini(&frm_sm->fs_unformed_prio[cnt].
					pl_unformed_items);
		frm_sm->fs_dest_netep = NULL;
		c2_free(frm_sm);
	}
}

/**
   Add a formation state machine structure when the first rpc item gets added
   for a network endpoint.
 */
static struct c2_rpc_frm_sm *rpc_frm_sm_add(struct c2_net_end_point *endp,
		struct c2_rpc_formation *formation_summary)
{
	uint64_t		 cnt;
	struct c2_rpc_frm_sm	*frm_sm;

	C2_PRE(endp != NULL);

	C2_ALLOC_PTR(frm_sm);
	if (frm_sm == NULL) {
		C2_ADDB_ADD(&formation_summary->rf_rpc_form_addb,
				&rpc_frm_addb_loc, c2_addb_oom);
		return NULL;
	}
	c2_mutex_init(&frm_sm->fs_lock);
	c2_list_add(&formation_summary->rf_frm_sm_list,
			&frm_sm->fs_linkage);
	c2_list_init(&frm_sm->fs_groups);
	c2_list_init(&frm_sm->fs_coalesced_items);
	c2_list_init(&frm_sm->fs_rpcs);
	c2_ref_init(&frm_sm->fs_ref, 1, rpc_frm_sm_destroy);
	frm_sm->fs_dest_netep = endp;
	frm_sm->fs_state = C2_RPC_FRM_STATE_WAITING;
	frm_sm->fs_active = true;
	frm_sm->fs_max_msg_size = max_msg_size;
	frm_sm->fs_max_frags = max_fragments_size;
	frm_sm->fs_max_rpcs_in_flight = max_rpcs_in_flight;
	frm_sm->fs_curr_rpcs_in_flight = 0;
	for (cnt = 0; cnt <= C2_RPC_ITEM_PRIO_NR; cnt++) {
		frm_sm->fs_unformed_prio[cnt].pl_prio = C2_RPC_ITEM_PRIO_NR -
			cnt;
		c2_list_init(&frm_sm->fs_unformed_prio[cnt].pl_unformed_items);
	}
	frm_sm->fs_cumulative_size = 0;
	frm_sm->fs_urgent_items_nr = 0;
	return frm_sm;
}

/**
   Return the function pointer to next state given the current state
   and current event as input.
   @param current_state - current state of state machine.
   @param current_event - current event posted to the state machine.
 */
static statefunc rpc_frm_next_state(const int current_state,
		const int current_event)
{
	C2_PRE(current_state < C2_RPC_FRM_STATES_NR);
	C2_PRE(current_event < C2_RPC_FRM_INTEVT_NR);

	return c2_rpc_frm_statetable[current_state][current_event];
}

/**
   Get the endpoint given an rpc item.
   This is a placeholder and will be replaced when a concrete
   definition of endpoint is available.
   The net endpoint reference count is not incremented here.
 */
static struct c2_net_end_point *rpc_frm_get_endpoint(
		const struct c2_rpc_item *item)
{
	return item->ri_session->s_conn->c_end_point;
}

/**
   Check if given network endpoints are equal.
 */
static bool rpc_frm_endp_equal(struct c2_net_end_point *ep1,
		struct c2_net_end_point *ep2)
{
	C2_PRE((ep1 != NULL) && (ep2 != NULL));

	return (ep1 == ep2) || !strcmp(ep1->nep_addr, ep2->nep_addr);
}

/**
  For a given network endpoint, return an existing formation state machine
  object.
 */
static struct c2_rpc_frm_sm *rpc_frm_sm_locate(struct c2_net_end_point *ep,
		struct c2_rpc_formation *formation_summary)
{
	struct c2_rpc_frm_sm *frm_sm = NULL;
	struct c2_rpc_frm_sm *sm = NULL;

	C2_PRE(ep != NULL);

	c2_rwlock_read_lock(&formation_summary->rf_sm_list_lock);
	c2_list_for_each_entry(&formation_summary->rf_frm_sm_list, sm,
			struct c2_rpc_frm_sm, fs_linkage) {
		if (rpc_frm_endp_equal(sm->fs_dest_netep, ep)) {
			frm_sm = sm;
			break;
		}
	}
	if (frm_sm != NULL)
		c2_ref_get(&frm_sm->fs_ref);

	c2_rwlock_read_unlock(&formation_summary->rf_sm_list_lock);
	return frm_sm;
}

/**
   Create a new formation state machine object.
 */
static struct c2_rpc_frm_sm *rpc_frm_sm_create(struct c2_net_end_point *net_ep,
		struct c2_rpc_formation *formation_summary)
{
	struct c2_rpc_frm_sm	*sm = NULL;

	C2_PRE(net_ep != NULL);
	C2_PRE(formation_summary != NULL);

	/* Add a new formation state machine. */
	c2_rwlock_write_lock(&formation_summary->rf_sm_list_lock);
	sm = rpc_frm_sm_add(net_ep, formation_summary);
	if(sm == NULL)
		C2_ADDB_ADD(&formation_summary->rf_rpc_form_addb,
				&rpc_frm_addb_loc, formation_func_fail,
				"rpc_frm_sm_add", 0);

	c2_rwlock_write_unlock(&formation_summary->rf_sm_list_lock);
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
   @param event - event posted to the state machine.
 */
static int rpc_frm_default_handler(struct c2_rpc_item *item,
		struct c2_rpc_frm_sm *frm_sm, int sm_state,
		const struct c2_rpc_frm_sm_event *sm_event)
{
	int			 res = 0;
	int			 prev_state = 0;
	struct c2_net_end_point	*endpoint = NULL;
	struct c2_rpc_frm_sm	*sm = NULL;
	struct c2_rpc_formation	*formation_summary;

	C2_PRE(item != NULL);
	C2_PRE(sm_event->se_event < C2_RPC_FRM_INTEVT_NR);
	C2_PRE(sm_state <= C2_RPC_FRM_STATES_NR);

	formation_summary = item->ri_mach->cr_formation;
	endpoint = rpc_frm_get_endpoint(item);

	/* If state machine pointer from rpc item is NULL, locate it
	   from list in list of state machines. If found, lock it and
	   increment its refcount. If not found, create a new state machine.
	   In any case, find out the previous state of state machine. */
	if (frm_sm == NULL) {
		sm = rpc_frm_sm_locate(endpoint, formation_summary);
		if (sm == NULL) {
			sm = rpc_frm_sm_create(endpoint, formation_summary);
			if (sm == NULL)
				return -ENOMEM;
		}
		c2_mutex_lock(&sm->fs_lock);
		prev_state = sm->fs_state;
	} else {
		prev_state = sm_state;
		c2_mutex_lock(&frm_sm->fs_lock);
		sm = frm_sm;
	}

	/* If the formation component is not active (form_fini is called)
	   exit the state machine and return back. */
	if (!sm->fs_active) {
		c2_mutex_unlock(&sm->fs_lock);
		rpc_frm_sm_exit(formation_summary, sm);
		return 0;
	}

	/* Transition to next state.*/
	res = (rpc_frm_next_state(prev_state, sm_event->se_event))
		(sm, item, sm_event);

	/* The return value should be an internal event.
	   Assert if its not. */
	C2_ASSERT((res >= C2_RPC_FRM_INTEVT_STATE_SUCCEEDED) &&
			(res <= C2_RPC_FRM_INTEVT_STATE_DONE));
	/* Get latest state of state machine. */
	prev_state = sm->fs_state;
	c2_mutex_unlock(&sm->fs_lock);

	/* Exit point for state machine. */
	if(res == C2_RPC_FRM_INTEVT_STATE_DONE) {
		rpc_frm_sm_exit(formation_summary, sm);
		return 0;
	}

	if (res == C2_RPC_FRM_INTEVT_STATE_FAILED) {
		/* Post a state failed event. */
		rpc_frm_state_failed(sm, item, prev_state);
	} else if (res == C2_RPC_FRM_INTEVT_STATE_SUCCEEDED){
		/* Post a state succeeded event. */
		rpc_frm_state_succeeded(sm, item, prev_state);
	}
	return 0;
}

/**
   Callback function for addition of an rpc item to the list of
   its corresponding free slot.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
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
	if (!c2_list_link_is_in(&slot->sl_link)) {
		c2_list_add(&rpcmachine->cr_ready_slots, &slot->sl_link);
	}
	c2_mutex_unlock(&rpcmachine->cr_ready_slots_mutex);

	/* Curent state is not known at the moment. */
	return rpc_frm_default_handler(item, NULL, C2_RPC_FRM_STATES_NR,
			&sm_event);
}

/**
   Callback function for slot becoming idle.
   Adds the slot to the list of ready slots in concerned rpcmachine.
   @param item - slot structure for the slot which has become idle.
 */
int c2_rpc_frm_slot_idle(struct c2_rpc_slot *slot)
{
	struct c2_rpcmachine		*rpcmachine = NULL;
	struct c2_rpc_item		*item;
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

	if (!c2_list_is_empty(&session->s_unbound_items)) {
		item = c2_list_entry((c2_list_first(&session->s_unbound_items)),
				struct c2_rpc_item, ri_unbound_link);
		return rpc_frm_default_handler(item, NULL,
				C2_RPC_FRM_STATES_NR, &sm_event);
	}
	return 0;
}

/**
   Callback function for unbounded item getting added to session.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
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
	return rpc_frm_default_handler(item, NULL, C2_RPC_FRM_STATES_NR,
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
	struct c2_rpc_formation		*formation_summary = NULL;

	C2_PRE((ev != NULL) && (ev->nbe_buffer != NULL) &&
			(ev->nbe_buffer->nb_qtype == C2_NET_QT_MSG_SEND));

	nb = ev->nbe_buffer;
	dom = nb->nb_dom;
	tm = nb->nb_tm;
	fb = container_of(nb, struct c2_rpc_frm_buffer, fb_buffer);
	C2_PRE(c2_rpc_frm_buf_invariant(fb));
	item = c2_list_entry(c2_list_first(&fb->fb_rpc->r_items),
			struct c2_rpc_item, ri_rpcobject_linkage);
	C2_ASSERT(item->ri_state == RPC_ITEM_ADDED);
	formation_summary = item->ri_mach->cr_formation;
	C2_ASSERT(formation_summary != NULL);

	/* The buffer should have been dequeued by now. */
	C2_ASSERT((nb->nb_flags & C2_NET_BUF_QUEUED) == 0);

	if (ev->nbe_status == 0) {
		rpc_frm_set_state_sent(fb->fb_rpc);
		rpc_frm_buffer_deallocate(fb);
	} else {
		/* If the send event fails, add the rpc back to concerned
		   queue so that it will be processed next time.*/
		c2_mutex_lock(&fb->fb_frm_sm->fs_lock);
		c2_list_add(&fb->fb_frm_sm->fs_rpcs,
				&fb->fb_rpc->r_linkage);
		c2_mutex_unlock(&fb->fb_frm_sm->fs_lock);
	}

	/* Try to send the rpc object on wire once again. Return value
	   does not matter since we will exit callback anyway. */
	c2_mutex_lock(&fb->fb_frm_sm->fs_lock);
	rpc_frm_send_onwire(fb->fb_frm_sm);
	c2_mutex_unlock(&fb->fb_frm_sm->fs_lock);

	/* Release reference on the c2_rpc_frm_sm here. */
	rpc_frm_sm_exit(formation_summary, fb->fb_frm_sm);
}

/**
   Callback function for deletion of an rpc item from the rpc items cache.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_frm_item_delete(struct c2_rpc_item *item)
{
	int				 rc = 0;
	struct c2_rpc_frm_sm_event	 sm_event;
	struct c2_rpc_frm_sm		*frm_sm;
	struct c2_net_end_point		*net_ep;

	C2_PRE(item != NULL);

	sm_event.se_event = C2_RPC_FRM_EXTEVT_RPCITEM_REMOVED;
	sm_event.se_pvt = NULL;

	/* Retrieve the net endpoint and then the formation state machine. */
	net_ep = rpc_frm_get_endpoint(item);
	frm_sm = rpc_frm_sm_locate(net_ep, item->ri_mach->cr_formation);
	if (frm_sm == NULL)
		return -EINVAL;
	c2_mutex_lock(&frm_sm->fs_lock);
	rc = rpc_frm_item_change(frm_sm, item, &sm_event);
	c2_mutex_unlock(&frm_sm->fs_lock);
	return rc;
}

/**
   Callback function for change in parameter of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_frm_item_changed(struct c2_rpc_item *item,
		int field_type, void *pvt)
{
	int					 rc = 0;
	struct c2_rpc_frm_sm			*frm_sm;
	struct c2_net_end_point			*net_ep;
	struct c2_rpc_frm_sm_event		 sm_event;
	struct c2_rpc_frm_item_change_req	 req;

	C2_PRE(item != NULL);
	C2_PRE(pvt != NULL);
	C2_PRE(field_type < C2_RPC_ITEM_CHANGES_NR);

	/* Prepare rpc item change request. */
	sm_event.se_event = C2_RPC_FRM_EXTEVT_RPCITEM_CHANGED;
	req.field_type = field_type;
	req.value = pvt;
	sm_event.se_pvt = &req;

	/* Retrieve the net endpoint and then the formation state machine. */
	net_ep = rpc_frm_get_endpoint(item);
	frm_sm = rpc_frm_sm_locate(net_ep, item->ri_mach->cr_formation);
	if (frm_sm == NULL)
		return -EINVAL;
	c2_mutex_lock(&frm_sm->fs_lock);
	rc = rpc_frm_item_change(frm_sm, item, &sm_event);
	c2_mutex_unlock(&frm_sm->fs_lock);
	return rc;
}

/**
   Process the rpc item for which reply has been received.
 */
static void rpc_frm_reply_received(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item)
{
	struct c2_rpc_frm_item_coalesced	*coalesced_item;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);

	/* If event received is reply_received, do all the post processing
	   for a coalesced item.*/
	c2_list_for_each_entry(&frm_sm->fs_coalesced_items,
			coalesced_item, struct c2_rpc_frm_item_coalesced,
			ic_linkage) {
		if (coalesced_item->ic_resultant_item == item)
			rpc_frm_item_coalesced_reply_post(frm_sm,
					coalesced_item);
		else
			item->ri_type->rit_ops->rio_replied(item, 0);
	}
}

/**
   Callback function for reply received of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_frm_item_reply_received(struct c2_rpc_item *reply_item,
		struct c2_rpc_item *req_item)
{
	struct c2_net_end_point		*net_ep;
	struct c2_rpc_frm_sm		*frm_sm;
	struct c2_rpc_frm_sm_event	 sm_event;

	C2_PRE(req_item != NULL);

	sm_event.se_event = C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED;
	sm_event.se_pvt = NULL;

	/* Retrieve the net endpoint and then the formation state machine. */
	net_ep = rpc_frm_get_endpoint(req_item);
	frm_sm = rpc_frm_sm_locate(net_ep, req_item->ri_mach->cr_formation);
	if (frm_sm == NULL)
		return -EINVAL;
	c2_mutex_lock(&frm_sm->fs_lock);
	rpc_frm_reply_received(frm_sm, req_item);
	c2_mutex_unlock(&frm_sm->fs_lock);

	/* Curent state is not known at the moment. */
	return rpc_frm_default_handler(req_item, frm_sm, C2_RPC_FRM_STATES_NR,
			&sm_event);
}

/**
   Add the given rpc item to the list of urgent items in state machine.
 */
static void rpc_frm_item_timeout_handle(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item)
{
	struct c2_list *list;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);

	if (item->ri_state != RPC_ITEM_SUBMITTED)
		return;

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
   @param item - incoming rpc item.
 */
int c2_rpc_frm_item_timeout(struct c2_rpc_item *item)
{
	struct c2_rpc_frm_sm_event	 sm_event;
	struct c2_net_end_point		*net_ep;
	struct c2_rpc_frm_sm		*frm_sm;

	C2_PRE(item != NULL);

	sm_event.se_event = C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT;
	sm_event.se_pvt = NULL;
	item->ri_deadline = 0;

	/* Retrieve the net endpoint and then the formation state machine. */
	net_ep = rpc_frm_get_endpoint(item);
	frm_sm = rpc_frm_sm_locate(net_ep, item->ri_mach->cr_formation);
	if (frm_sm == NULL)
		return -EINVAL;
	c2_mutex_lock(&frm_sm->fs_lock);
	rpc_frm_item_timeout_handle(frm_sm, item);
	c2_mutex_unlock(&frm_sm->fs_lock);

	/* Curent state is not known at the moment. */
	return rpc_frm_default_handler(item, frm_sm, C2_RPC_FRM_STATES_NR,
			&sm_event);
}

/**
   Callback function for successful completion of a state.
   Call the default handler function. Depending upon the
   input state, the default handler will invoke the next state
   for state succeeded event.
   @param state - previous state of state machine.
 */
static int rpc_frm_state_succeeded(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item, const int state)
{
	struct c2_rpc_frm_sm_event		sm_event;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);

	sm_event.se_event = C2_RPC_FRM_INTEVT_STATE_SUCCEEDED;
	sm_event.se_pvt = NULL;

	/* Curent state is not known at the moment. */
	return rpc_frm_default_handler(item, frm_sm, state, &sm_event);
}

/**
   Callback function for failure of a state.
   Call the default handler function. Depending upon the
   input state, the default handler will invoke the next state
   for state failed event.
   @param state - previous state of state machine.
 */
static int rpc_frm_state_failed(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item, const int state)
{
	struct c2_rpc_frm_sm_event		sm_event;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);

	sm_event.se_event = C2_RPC_FRM_INTEVT_STATE_FAILED;
	sm_event.se_pvt = NULL;

	/* Curent state is not known at the moment. */
	return rpc_frm_default_handler(item, frm_sm, state, &sm_event);
}

/**
   Call the completion callbacks for member rpc items of
   a coalesced rpc item.
 */
static void rpc_frm_item_coalesced_reply_post(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_frm_item_coalesced *coalesced_struct)
{
	int					 rc = 0;
	struct c2_rpc_item			*item = NULL;
	struct c2_rpc_frm_item_coalesced_member	*member;
	struct c2_rpc_frm_item_coalesced_member	*next_member;

	C2_PRE(frm_sm != NULL);
	C2_PRE(coalesced_struct != NULL);

	/* For all member items of coalesced_item struct, call
	   their completion callbacks.*/
	c2_list_for_each_entry_safe(&coalesced_struct->ic_member_list,
			member, next_member,
			struct c2_rpc_frm_item_coalesced_member,
			im_linkage) {
		item = member->im_member_item;
		c2_list_del(&member->im_linkage);
		item->ri_type->rit_ops->rio_replied(item, rc);
		c2_free(member);
		coalesced_struct->ic_nmembers--;
	}
	C2_ASSERT(coalesced_struct->ic_nmembers == 0);
	c2_list_del(&coalesced_struct->ic_linkage);
	item = coalesced_struct->ic_resultant_item;
	item->ri_type->rit_ops->rio_replied(item, rc);
	c2_list_fini(&coalesced_struct->ic_member_list);
	c2_free(coalesced_struct);
}

/**
   State function for WAITING state.
   Formation is waiting for any event to trigger. This is an exit point
   for the formation state machine.
   @param item - input rpc item.
   @param event - Since WAITING state handles a lot of events,
   @param frm_sm - Corresponding c2_rpc_frm_sm structure for given rpc item.
   it needs some way of identifying the events.
 */
static enum c2_rpc_frm_int_evt_id rpc_frm_waiting_state(
		struct c2_rpc_frm_sm *frm_sm ,struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event)
{
	C2_PRE(item != NULL);
	C2_PRE((event->se_event == C2_RPC_FRM_INTEVT_STATE_SUCCEEDED) ||
			(event->se_event == C2_RPC_FRM_INTEVT_STATE_FAILED));
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));

	frm_sm->fs_state = C2_RPC_FRM_STATE_WAITING;

	return C2_RPC_FRM_INTEVT_STATE_DONE;
}

/**
   Callback used to trigger the "deadline expired" event
   for an rpc item.
 */
static unsigned long c2_rpc_frm_item_timer_callback(unsigned long data)
{
	int			 rc = 0;
	struct c2_rpc_item	*item;

	item = (struct c2_rpc_item*)data;

	if (item->ri_state == RPC_ITEM_SUBMITTED) {
		rc = c2_rpc_frm_item_timeout(item);
	}
	return rc;
}

/**
   Change the data of an rpc item embedded within the endpoint unit
   structure.
 */
static int rpc_frm_item_update(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item, void *pvt)
{
	int					 res = 0;
	int					 field_type = 0;
	struct c2_rpc_frm_item_change_req	*chng_req = NULL;
	struct c2_rpc_formation			*formation_summary;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);
	C2_PRE(pvt != NULL);
	C2_PRE(item->ri_state == RPC_ITEM_SUBMITTED);

	formation_summary = item->ri_mach->cr_formation;
	chng_req = (struct c2_rpc_frm_item_change_req*)pvt;
	field_type = chng_req->field_type;

	/* First remove the item data from formation state machine. */
	rpc_frm_item_remove(frm_sm, item);

	/* Change the item parameter to new values. */
	switch (field_type) {
	case C2_RPC_ITEM_CHANGE_PRIORITY:
		item->ri_prio =
			(enum c2_rpc_item_priority)chng_req->value;
		break;
	case C2_RPC_ITEM_CHANGE_DEADLINE:
		item->ri_deadline = (c2_time_t)chng_req->value;
		break;
	case C2_RPC_ITEM_CHANGE_RPCGROUP:
		item->ri_group = (struct c2_rpc_group*)chng_req->value;
	default:
		C2_ADDB_ADD(&formation_summary->rf_rpc_form_addb,
				&rpc_frm_addb_loc, formation_func_fail,
				"rpc_frm_item_update", res);
		C2_ASSERT(0);
	};

	/* Then, add the new data of rpc item to formation state machine. */
	res = rpc_frm_item_add(frm_sm, item);
	if (res != 0)
		C2_ADDB_ADD(&formation_summary->rf_rpc_form_addb,
				&rpc_frm_addb_loc, formation_func_fail,
				"rpc_frm_item_update", res);
	return res;
}

/**
   Remove the data of an rpc item embedded within the endpoint unit
   structure.
 */
static void rpc_frm_item_remove(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item)
{
	bool				 found = false;
	size_t				 item_size = 0;
	c2_time_t			 now;
	struct c2_rpc_frm_rpcgroup	*summary_group = NULL;

	C2_PRE(item != NULL);
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));
	C2_PRE(item->ri_state == RPC_ITEM_SUBMITTED);

	/* Reduce the cumulative size of rpc items from formation
	   state machine. */
	item_size = item->ri_type->rit_ops->rio_item_size(item);
	frm_sm->fs_cumulative_size -= item_size;

	/* If timer of rpc item is still running, change the deadline in
	   rpc item as per remaining time and stop and fini the timer. */
	if (item->ri_deadline != 0) {
		c2_time_now(&now);
		if (c2_time_after(item->ri_timer.t_expire, now)) {
			item->ri_deadline =
				c2_time_sub(item->ri_timer.t_expire, now);
		}
		c2_timer_stop(&item->ri_timer);
		c2_timer_fini(&item->ri_timer);
	} else
		frm_sm->fs_urgent_items_nr--;

	/* If item is bound, remove it from formation state machine data. */
	if (c2_rpc_item_is_bound(item))
		c2_list_del(&item->ri_unformed_linkage);

	if (item->ri_group == NULL)
		return;

	/* Find out the group, given rpc item belongs to.*/
	c2_list_for_each_entry(&frm_sm->fs_groups, summary_group,
			struct c2_rpc_frm_rpcgroup, frg_linkage) {
		if (summary_group->frg_group == item->ri_group) {
			found = true;
			break;
		}
	}
	if (!found)
		return;

	/* Remove the data entered by this item in this summary group.*/
	if (--summary_group->frg_items_nr == 0) {
		c2_list_del(&summary_group->frg_linkage);
		c2_free(summary_group);
	}
}

/**
   Update the summary_unit data structure on addition of
   an rpc item.
   @retval 0 if item is successfully added to internal data structure
   @retval non-zero if item is not successfully added in internal data structure
 */
static int rpc_frm_item_add(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item)
{
	int				 res = 0;
	struct c2_rpc_frm_rpcgroup	*summary_group = NULL;
	struct c2_rpc_item		*rpc_item = NULL;
	struct c2_rpc_item		*rpc_item_next = NULL;
	bool				 item_inserted = false;
	bool				 found = false;
	size_t				 item_size = 0;
	struct c2_rpc_formation		*formation_summary;
	struct c2_list			*list;

	C2_PRE(item != NULL);
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));

	if (item->ri_state != RPC_ITEM_SUBMITTED) {
		return -EINVAL;
	}

	item_size = item->ri_type->rit_ops->rio_item_size(item);
	frm_sm->fs_cumulative_size += item_size;

	/* Initialize the timer only when the deadline value is non-zero
	   i.e. dont initialize the timer for URGENT items */
	formation_summary = item->ri_mach->cr_formation;
	if (item->ri_deadline != 0) {
		/* C2_TIMER_SOFT creates a different thread to handle the
		   callback. */
		c2_timer_init(&item->ri_timer, C2_TIMER_SOFT,
				item->ri_deadline, 1,
				c2_rpc_frm_item_timer_callback,
				(unsigned long)item);
		res = c2_timer_start(&item->ri_timer);
		if (res != 0) {
			C2_ADDB_ADD(&formation_summary->rf_rpc_form_addb,
				&rpc_frm_addb_loc, formation_func_fail,
				"rpc_frm_item_add", res);
			return res;
		}
	} else
		frm_sm->fs_urgent_items_nr++;

	/* Index into the array to find out correct list as per
	   priority of current rpc item. */
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

	/* Search for the group of rpc item in list of rpc groups in
	  summary_unit. */
	c2_list_for_each_entry(&frm_sm->fs_groups, summary_group,
			struct c2_rpc_frm_rpcgroup, frg_linkage) {
		if (summary_group->frg_group == item->ri_group) {
			found = true;
			break;
		}
	}

	/* If not found, create a c2_rpc_frm_rpcgroup structure and
	   fill all necessary data. */
	if (!found) {
		C2_ALLOC_PTR(summary_group);
		if (summary_group == NULL) {
			C2_ADDB_ADD(&formation_summary->rf_rpc_form_addb,
					&rpc_frm_addb_loc, c2_addb_oom);
			return -ENOMEM;
		}
		c2_list_link_init(&summary_group->frg_linkage);
		c2_list_add(&frm_sm->fs_groups, &summary_group->frg_linkage);
		summary_group->frg_group = item->ri_group;
	}

	summary_group->frg_expected_items_nr = item->ri_group->rg_expected;
	summary_group->frg_items_nr++;

	return res;
}

/**
   Given an endpoint, tell if an optimal rpc can be prepared from
   the items submitted to this endpoint.
   @param frm_sm - the c2_rpc_frm_sm structure
   based on whose data, it will be found if an optimal rpc can be made.
   @param rpcobj_size - check if given size of rpc object is optimal or not.
 */
static bool rpc_frm_is_rpc_optimal(struct c2_rpc_frm_sm *frm_sm,
		uint64_t rpcobj_size, bool urgent_unbound)
{
	bool		ret = false;
	uint64_t	size = 0;

	C2_PRE(frm_sm != NULL);

	if (urgent_unbound) {
		ret = true;
	}
	/* If given rpcobj_size is nonzero, caller expects to tell if
	   and rpc object of this size would be optimal.
	   If rpcobj_size is zero, caller expects to tell if given
	   summary_unit structure has enough items with it to form
	   an optimal rpc. */
	if (!rpcobj_size) {
		size = frm_sm->fs_cumulative_size;
	} else {
		size = rpcobj_size;
	}
	if (frm_sm->fs_urgent_items_nr > 0) {
		ret = true;
	} else if (size >= (0.9 * frm_sm->fs_max_msg_size)) {
		ret = true;
	}
	return ret;
}

/**
   Checks whether the items gathered so far in formation state machine
   are good enough to form a potential rpc object and proceed to
   forming state.
   @param frm_sm - Formation state machine.
   @retval - TRUE if qualified, FALSE otherwise.
 */
static bool rpc_frm_qualify(struct c2_rpc_frm_sm *frm_sm)
{
	C2_PRE(frm_sm != NULL);

	if (frm_sm->fs_urgent_items_nr > 0 ||
			frm_sm->fs_cumulative_size >=
			(0.9 * frm_sm->fs_max_msg_size))
		return true;
	return false;
}

/**
   State function for UPDATING state.
   Formation is updating its internal data structure by taking necessary locks.
   @param item - input rpc item.
   @param event - Since UPDATING state handles a lot of events,
   it needs some way of identifying the events.
   @param frm_sm - Corresponding c2_rpc_frm_sm structure for given rpc item.
 */
static enum c2_rpc_frm_int_evt_id rpc_frm_updating_state(
		struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event)
{
	int 				res;
	enum c2_rpc_frm_int_evt_id 	ret;

	C2_PRE(item != NULL);
	C2_PRE(event->se_event == C2_RPC_FRM_EXTEVT_RPCITEM_READY ||
		event->se_event == C2_RPC_FRM_EXTEVT_UBRPCITEM_ADDED ||
		event->se_event == C2_RPC_FRM_EXTEVT_USRPCITEM_ADDED);
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));

	frm_sm->fs_state = C2_RPC_FRM_STATE_UPDATING;

	/* Add the item to summary unit and subsequently to unformed list
	   only if it is bound item. */
	res = rpc_frm_item_add(frm_sm, item);
	if (res != 0)
		return C2_RPC_FRM_INTEVT_STATE_FAILED;

	/* Move the thread to the checking state only if an optimal rpc
	   can be formed.*/
	if (rpc_frm_qualify(frm_sm))
		ret = C2_RPC_FRM_INTEVT_STATE_SUCCEEDED;
	else
		ret = C2_RPC_FRM_INTEVT_STATE_FAILED;

	return ret;
}

/**
   Add an rpc item to the formed list of an rpc object.
   @retval 0 if item added to the forming list successfully
   @retval -1 if item is not added due to some check failure
 */
static int rpc_frm_item_add_to_forming_list(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item, uint64_t *rpcobj_size,
		uint64_t *nfragments, struct c2_rpc *rpc)
{
	size_t				 item_size = 0;
	uint64_t			 current_fragments = 0;
	struct c2_rpc_slot		*slot = NULL;
	struct c2_rpc_session		*session = NULL;
	bool				 session_locked = false;
	bool				 io_op = false;

	C2_PRE(frm_sm != NULL);
	C2_PRE(item != NULL);
	C2_PRE(rpcobj_size != NULL);
	C2_PRE(item->ri_state != RPC_ITEM_ADDED);

	/* Fragment count check. */
	io_op = item->ri_type->rit_ops->rio_is_io_req(item);
	if (io_op) {
		current_fragments = item->ri_type->rit_ops->
			rio_get_io_fragment_count(item);
		if ((*nfragments + current_fragments) >
				frm_sm->fs_max_frags) {
			return -1;
		}
	}
	/* Get size of rpc item. */
	item_size = item->ri_type->rit_ops->rio_item_size(item);

	/* If size of rpc object after adding current rpc item is
	   within max_message_size, add it the rpc object. */
	if (((*rpcobj_size + item_size) < frm_sm->fs_max_msg_size)) {
		c2_list_add(&rpc->r_items, &item->ri_rpcobject_linkage);
		*rpcobj_size += item_size;
		*nfragments += current_fragments;
		/* Remove the item data from summary_unit structure. */
		rpc_frm_item_remove(frm_sm, item);
		item->ri_state = RPC_ITEM_ADDED;

		session = item->ri_session;
		C2_ASSERT(session != NULL);
		if (c2_mutex_is_not_locked(&session->s_mutex)) {
			c2_mutex_lock(&session->s_mutex);
			session_locked = true;
		}
		/* If current added rpc item is unbound, remove it from
		   session->unbound_items list.*/
		if ((c2_list_link_is_in(&item->ri_unbound_link)) &&
				!item->ri_slot_refs[0].sr_slot) {
			c2_list_del(&item->ri_unbound_link);
		} else {
		/* OR If current added rpc item is bound, remove it from
		   slot->ready_items list AND if slot->ready_items list
		   is empty, remove slot from rpcmachine->ready_slots list.*/
			slot = item->ri_slot_refs[0].sr_slot;
			C2_ASSERT(slot != NULL);
			c2_list_del(&item->ri_slot_refs[0].sr_ready_link);
			if (c2_list_is_empty(&slot->sl_ready_list)) {
				c2_list_del(&slot->sl_link);
			}
		}
		if (session_locked) {
			c2_mutex_unlock(&session->s_mutex);
		}
		return 0;
	} else {
		return -1;
	}
}

/**
   Try to coalesce items sharing same fid and intent(read/write).
   @param b_item - given bound rpc item.
   @param coalesced_item - item_coalesced structure for which coalescing
   will be done.
 */
static int rpc_frm_coalesce_fid_intent(struct c2_rpc_item *b_item,
		struct c2_rpc_frm_item_coalesced *coalesced_item)
{
	C2_PRE(b_item != NULL);
	C2_PRE(coalesced_item != NULL);

	return b_item->ri_type->rit_ops->
		rio_io_coalesce((void*)coalesced_item, b_item);
}

/**
   Try to coalesce rpc items from the session->free list.
   @param frm_sm - the c2_rpc_frm_sm structure in which these activities
   are taking place.
   @param item - given bound rpc item.
   @param rpcobj_size - current size of rpc object.
 */
static int rpc_frm_try_coalesce(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item, uint64_t *rpcobj_size)
{
	struct c2_rpc_item				*ub_item = NULL;
	struct c2_rpc_session				*session = NULL;
	struct c2_fid					 fid;
	struct c2_fid					 ufid;
	struct c2_rpc_frm_item_coalesced		*coalesced_item = NULL;
	struct c2_rpc_frm_item_coalesced_member	*coalesced_member =
		NULL;
	struct c2_rpc_frm_item_coalesced_member	*coalesced_member_next =
		NULL;
	uint64_t					 old_size = 0;
	size_t						 item_size = 0;
	bool						 coalesced_item_found =
		false;
	bool						 item_equal = 0;
	int						 res = 0;
	int						 item_rw = 0;
	struct c2_rpc_formation				*formation_summary;

	C2_PRE(item != NULL);
	C2_PRE(frm_sm != NULL);

	formation_summary = item->ri_mach->cr_formation;
	session = item->ri_session;
	C2_ASSERT(session != NULL);

	old_size = item->ri_type->rit_ops->rio_item_size(item);

	/* If there are no unbound items to coalesce,
	   return right away. */
	c2_mutex_lock(&session->s_mutex);
	if (c2_list_is_empty(&session->s_unbound_items)) {
		c2_mutex_unlock(&session->s_mutex);
		return 0;
	}
	c2_mutex_unlock(&session->s_mutex);

	/* Similarly, if given rpc item is not part of an IO request,
	   return right away. */
	if (!item->ri_type->rit_ops->rio_is_io_req(item)) {
		return 0;
	}

	fid = item->ri_type->rit_ops->rio_io_get_fid(item);
	item_size = item->ri_type->rit_ops->rio_item_size(item);
	item_rw = item->ri_type->rit_ops->rio_io_get_opcode(item);


	/* Find out/Create the coalesced_item struct for this bound item.
	   If fid and intent(read/write) of current unbound item matches
	   with fid and intent of the bound item, see if a corresponding
	   struct c2_rpc_frm_item_coalesced exists for this
	   {fid, intent} tuple. */
	c2_list_for_each_entry(&frm_sm->fs_coalesced_items,
			coalesced_item, struct c2_rpc_frm_item_coalesced,
			ic_linkage) {
		if (c2_fid_eq(&fid, &coalesced_item->ic_fid)
				&& (coalesced_item->ic_op_intent == item_rw)) {
			coalesced_item_found = true;
			break;
		}
	}
	/* If such a coalesced_item does not exist, create one. */
	if (!coalesced_item_found) {
		C2_ALLOC_PTR(coalesced_item);
		if (coalesced_item == NULL) {
			C2_ADDB_ADD(&formation_summary->rf_rpc_form_addb
					, &rpc_frm_addb_loc, c2_addb_oom);
			return -ENOMEM;
		}
		c2_list_link_init(&coalesced_item->ic_linkage);
		coalesced_item->ic_fid = fid;
		coalesced_item->ic_op_intent = item_rw;
		coalesced_item->ic_resultant_item = NULL;
		coalesced_item->ic_nmembers = 0;
		c2_list_init(&coalesced_item->ic_member_list);
		/* Add newly created coalesced_item into list of
		   fs_coalesced_items in frm_sm.*/
		c2_list_add(&frm_sm->fs_coalesced_items,
				&coalesced_item->ic_linkage);
	}
	C2_ASSERT(coalesced_item != NULL);

	c2_mutex_lock(&session->s_mutex);
	c2_list_for_each_entry(&session->s_unbound_items, ub_item,
			struct c2_rpc_item, ri_unbound_link) {
		/* If current rpc item is not part of an IO request, skip
		   the item and move to next one from the unbound_items list.*/
		if (!ub_item->ri_type->rit_ops->rio_is_io_req(ub_item)) {
			continue;
		}
		ufid = ub_item->ri_type->rit_ops->rio_io_get_fid(ub_item);
		item_equal = item->ri_type->rit_ops->
			rio_items_equal(item, ub_item);
		if (c2_fid_eq(&fid, &ufid) && item_equal) {
			coalesced_item->ic_nmembers++;
			C2_ALLOC_PTR(coalesced_member);
			if (coalesced_member == NULL) {
				C2_ADDB_ADD(&formation_summary->
						rf_rpc_form_addb,
						&rpc_frm_addb_loc,
						c2_addb_oom);
				c2_mutex_unlock(&session->s_mutex);
				return -ENOMEM;
			}
			/* Create a new struct c2_rpc_frm_item_coalesced
			   _member and add it to the list(ic_member_list)
			   of coalesced_item above. */
			coalesced_member->im_member_item = ub_item;
			c2_list_link_init(&coalesced_member->im_linkage);
			c2_list_add(&coalesced_item->ic_member_list,
					&coalesced_member->im_linkage);
		}
	}
	c2_mutex_unlock(&session->s_mutex);

	/* If number of member rpc items in the current coalesced_item
	   struct are less than 2, reject the coalesced_item
	   and return back. i.e. Coalescing can only be when there are
	   more than 2 items to merge*/
	if (coalesced_item->ic_nmembers < 2) {
		c2_list_for_each_entry_safe(&coalesced_item->ic_member_list,
				coalesced_member, coalesced_member_next,
				struct c2_rpc_frm_item_coalesced_member,
				im_linkage) {
			c2_list_del(&coalesced_member->im_linkage);
			c2_free(coalesced_member);
		}
		c2_list_fini(&coalesced_item->ic_member_list);
		c2_list_del(&coalesced_item->ic_linkage);
		c2_free(coalesced_item);
		coalesced_item = NULL;
		return 0;
	}

	/* Add the bound item to the list of member rpc items in
	   coalesced_item structure so that it will be coalesced as well. */
	C2_ALLOC_PTR(coalesced_member);
	if (coalesced_member == NULL) {
		C2_ADDB_ADD(&formation_summary->rf_rpc_form_addb,
				&rpc_frm_addb_loc, c2_addb_oom);
		return -ENOMEM;
	}
	coalesced_member->im_member_item = item;
	c2_list_add(&coalesced_item->ic_member_list,
			&coalesced_member->im_linkage);
	coalesced_item->ic_nmembers++;
	/* If there are more than 1 rpc items sharing same fid
	   and intent, there is a possibility of successful coalescing.
	   Try to coalesce all member rpc items in the coalesced_item
	   and put the resultant IO vector in the given bound item. */
	res = rpc_frm_coalesce_fid_intent(item, coalesced_item);
	/* Remove the bound item from list of member elements
	   from a coalesced_item struct.*/
	c2_list_del(&coalesced_member->im_linkage);

	if (res == 0) {
		item->ri_state = RPC_ITEM_ADDED;
		*rpcobj_size -= old_size;
		*rpcobj_size += item->ri_type->rit_ops->
			rio_item_size(item);
		/* Delete all members items for which coalescing was
		   successful from the session->free list. */
		c2_list_for_each_entry(&coalesced_item->ic_member_list,
				coalesced_member, struct
				c2_rpc_frm_item_coalesced_member, im_linkage) {
			c2_list_del(&coalesced_member->im_member_item->
					ri_unbound_link);
		}
	}
	return res;
}

/**
   State function for FORMING state.
   Core of formation algorithm. This state scans the rpc items cache and
   structure c2_rpc_frm_sm to form an RPC object by
   cooperation of multiple policies.
   Formation algorithm will take hints from rpc groups and will try to
   form rpc objects by keeping all group member rpc items together.
   For update streams, formation algorithm checks their status.
   If update stream is FREE(Not Busy), it will be considered for
   formation. Checking state will take care of coalescing of items.
   Coalescing Policy:
   Groups and coalescing: Formation algorithm will try to coalesce rpc items
   from same rpc groups as far as possible, otherwise items from different
   groups will be coalesced.
   Update streams and coalescing: Formation algorithm will not coalesce rpc
   items across update streams. Rpc items belonging to same update stream
   will be coalesced if possible since there is no sequence number assigned
   yet to the rpc items.
   Formation Algorithm.
   1. Read rpc items from the cache destined for given network endpoint.
   2. If the item deadline is zero(urgent), add it to a local
   list of rpc items to be formed.
   3. Check size of formed rpc object so far to see if its optimal.
   Here size of rpc is compared with max_message_size. If size of
   rpc is far less than max_message_size and no urgent item, goto #1.
   4. If #3 is true and if the number of disjoint memory buffers
   is less than parameter max_fragment_size, a probable rpc object
   is in making. The selected rpc items are put on a list
   and the state machine transitions to next state.
   5. Consult the structure c2_rpc_frm_sm to find out
   data about all rpc groups. Select groups that have combination of
   lowest average timeout and highest size that fits into optimal
   size. Keep selecting such groups till optimal size rpc is formed.
   6. Consult the list of files from internal data to find out files
   on which IO requests have come for this state machine. Do coalescing
   within groups selected for formation according to read/write
   intents. Later if rpc has still not reached its optimal size,
   coalescing across rpc groups will be done.
   7. Remove the data of selected rpc items from internal data
   structure so that it will not be considered for processing
   henceforth.
   8. If the formed rpc object is sub optimal but it contains
   an urgent item, it will be formed immediately. Else, it will
   be discarded.
   9. This process is repeated until the size of formed rpc object
   is sub optimal and there is no urgent item in the list.
   @param item - input rpc item.
   @param event - Since FORMING state handles a lot of events,
   it needs some way of identifying the events.
   @param frm_sm - Corresponding c2_rpc_frm_sm structure for given rpc item.
 */
static enum c2_rpc_frm_int_evt_id rpc_frm_forming_state(
		struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event)
{
	int					 res = 0;
	bool					 slot_found = false;
	bool					 urgent_items = false;
	bool					 item_added = false;
	bool					 urgent_unbound = false;
	size_t					 item_size = 0;
	size_t					 partial_size = 0;
	uint64_t				 nselected_groups = 0;
	uint64_t				 rpcobj_size = 0;
	uint64_t				 ncurrent_groups = 0;
	uint64_t				 nfragments = 0;
	uint64_t				 group_size = 0;
	uint64_t				 counter = 0;
	struct c2_rpc				*rpcobj = NULL;
	struct c2_rpc_item			*rpc_item = NULL;
	struct c2_rpc_item			*rpc_item_next = NULL;
	struct c2_rpcmachine			*rpcmachine = NULL;
	struct c2_rpc_session			*session = NULL;
	struct c2_rpc_slot			*slot = NULL;
	struct c2_rpc_slot			*slot_next = NULL;
	struct c2_rpc_item			*ub_item = NULL;
	struct c2_rpc_item			*ub_item_next = NULL;
	struct c2_rpc_formation			*formation_summary;
	struct c2_rpc_frm_rpcgroup		*sg = NULL;
	struct c2_rpc_frm_rpcgroup		*sg_partial = NULL;
	struct c2_rpc_frm_rpcgroup		*group = NULL;
	struct c2_rpc_frm_rpcgroup		*group_next = NULL;

	C2_PRE(item != NULL);
	C2_PRE(event->se_event == C2_RPC_FRM_INTEVT_STATE_SUCCEEDED);
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));

	frm_sm->fs_state = C2_RPC_FRM_STATE_FORMING;
	formation_summary = item->ri_mach->cr_formation;

	/* If isu_rpcobj_formed_list is not empty, it means an rpc
	   object was formed successfully some time back but it
	   could not be sent due to some error conditions.
	   We send it back here again. */
	if (!c2_list_is_empty(&frm_sm->fs_rpcs)) {
		res = rpc_frm_send_onwire(frm_sm);
		if (res == 0)
			return C2_RPC_FRM_INTEVT_STATE_SUCCEEDED;
		else
			return C2_RPC_FRM_INTEVT_STATE_FAILED;
	}

	/* Returning failure will lead the state machine to
	    waiting state and then the thread will exit the
	    state machine. */
	if (frm_sm->fs_curr_rpcs_in_flight == frm_sm->fs_max_rpcs_in_flight) {
		return C2_RPC_FRM_INTEVT_STATE_FAILED;
	}

	/* Create an rpc object in frm_sm->isu_rpcobj_checked_list. */
	C2_ALLOC_PTR(rpcobj);
	if (rpcobj == NULL) {
		C2_ADDB_ADD(&formation_summary->rf_rpc_form_addb,
				&rpc_frm_addb_loc, c2_addb_oom);
		return C2_RPC_FRM_INTEVT_STATE_FAILED;
	}
	c2_rpc_rpcobj_init(rpcobj);

	/* Iterate over the c2_rpc_frm_rpcgroup list in the
	   endpoint structure to find out which rpc groups can be included
	   in the rpc object. */
	c2_list_for_each_entry(&frm_sm->fs_groups, sg,
			struct c2_rpc_frm_rpcgroup, frg_linkage) {
		/* nselected_groups number includes the last partial
		   rpc group(if any).*/
		nselected_groups++;
		if ((group_size + sg->sug_total_size) <
				frm_sm->fs_max_msg_size) {
			group_size += sg->sug_total_size;
		} else {
			partial_size = (group_size + sg->sug_total_size) -
				frm_sm->fs_max_msg_size;
			sg_partial = sg;
			break;
		}
	}

	/* Core of formation algorithm. */
	c2_list_for_each_entry_safe(&frm_sm->fs_unformed_prio, rpc_item,
			rpc_item_next, struct c2_rpc_item,
			ri_unformed_linkage) {
		counter++;
		item_size = rpc_item->ri_type->rit_ops->
			rio_item_size(rpc_item);
		/* 1. If there are urgent items, form them immediately. */
		if (rpc_item->ri_deadline == 0) {
			if (!urgent_items) {
				urgent_items = true;
			}
			if (rpc_item->ri_state != RPC_ITEM_SUBMITTED) {
				continue;
			}
			res = rpc_frm_item_add_to_forming_list(frm_sm,
					rpc_item, &rpcobj_size,
					&nfragments, rpcobj);
			if (res != 0) {
				/* Forming list complete.*/
				break;
			}
			continue;
		}
		/* 2. Check if current rpc item belongs to any of the selected
		   groups. If yes, add it to forming list. For the last partial
		   group (if any), check if current rpc item belongs to this
		   partial group and add the item till size of items in this
		   partial group reaches its limit within max_message_size. */
		c2_list_for_each_entry_safe(&frm_sm->fs_groups, group,
				group_next, struct c2_rpc_frm_rpcgroup,
				frg_linkage) {
			ncurrent_groups++;
			/* If selected groups are exhausted, break the loop. */
			if (ncurrent_groups > nselected_groups) {
				break;
			}
			if ((sg_partial != NULL) && (rpc_item->ri_group ==
						sg_partial->frg_group)) {
				break;
			}
			if (rpc_item->ri_group == group->frg_group) {
				item_added = true;
				ncurrent_groups = 0;
				break;
			}
		}
		if(item_added) {
			if (rpc_item->ri_state != RPC_ITEM_SUBMITTED) {
				continue;
			}
			res = rpc_frm_item_add_to_forming_list(frm_sm,
					rpc_item, &rpcobj_size, &nfragments,
					rpcobj);
			if (res != 0) {
				break;
			}
			item_added = false;
		}
		/* Try to coalesce items from session->unbound_items list
		   with the current rpc item. */
		res = rpc_frm_try_coalesce(frm_sm, rpc_item,
				&rpcobj_size);
	}

	/* Add the rpc items in the partial group to the forming
	   list separately. This group is sorted according to priority.
	   So the partial number of items will be picked up
	   for formation in increasing order of priority */
	if (sg_partial != NULL) {
		c2_mutex_lock(&sg_partial->frg_group->rg_guard);
		c2_list_for_each_entry_safe(&sg_partial->frg_group->rg_items,
				rpc_item, rpc_item_next,  struct c2_rpc_item,
				ri_group_linkage) {
			item_size = rpc_item->ri_type->rit_ops->
				rio_item_size(rpc_item);
			if ((partial_size - item_size) <= 0) {
				break;
			}
			if (c2_list_link_is_in(&rpc_item->
						ri_unformed_linkage)) {
				if (rpc_item->ri_state == RPC_ITEM_SUBMITTED) {
					partial_size -= item_size;
					res = rpc_frm_item_add_to_forming_list(
							frm_sm, rpc_item,
							&rpcobj_size,
							&nfragments,
							rpcobj);
				}
			}
		}
		c2_mutex_unlock(&sg_partial->frg_group->rg_guard);
	}

	/* Get slot and verno info from sessions component for
	   any unbound items in session->free list. */
	session = item->ri_session;
	C2_ASSERT(session != NULL);
	c2_mutex_lock(&session->s_mutex);
	rpcmachine = session->s_conn->c_rpcmachine;
	C2_ASSERT(rpcmachine != NULL);
	c2_mutex_lock(&rpcmachine->cr_ready_slots_mutex);
	c2_list_for_each_entry_safe(&session->s_unbound_items, ub_item,
			ub_item_next, struct c2_rpc_item, ri_unbound_link) {
		/* Get rpcmachine from item.
		   Get first ready slot from rpcmachine->ready_slots list.
		   (Meaning, a slot whose ready_items list is empty = IDLE.)
		   Send it to item_add_internal and then remove it
		   from rpcmachine->ready_slots list. */
		c2_list_for_each_entry_safe(&rpcmachine->cr_ready_slots, slot,
				slot_next, struct c2_rpc_slot, sl_link) {
			if (c2_list_is_empty(&slot->sl_ready_list) &&
				(slot->sl_session == item->ri_session)) {
				slot_found = true;
				break;
			}
		}
		if (!slot_found) {
			break;
		}
		/* Now that the item is bound, remove it from
		   session->unbound_items list. */
		if(ub_item->ri_state == RPC_ITEM_SUBMITTED) {
			c2_mutex_lock(&slot->sl_mutex);
			c2_rpc_slot_item_add_internal(slot, ub_item);
			c2_list_del(&ub_item->ri_unbound_link);
			c2_mutex_unlock(&slot->sl_mutex);
			c2_list_add(&frm_sm->fs_unformed_prio,
				&ub_item->ri_unformed_linkage);
			res = rpc_frm_item_add_to_forming_list(frm_sm,
					ub_item,
					&rpcobj_size, &nfragments,
					rpcobj);
			if (res != 0) {
				break;
			}
			urgent_unbound = true;
		}
	}
	c2_mutex_unlock(&rpcmachine->cr_ready_slots_mutex);
	c2_mutex_unlock(&session->s_mutex);

	if (c2_list_is_empty(&rpcobj->r_items)) {
		return C2_RPC_FRM_INTEVT_STATE_FAILED;
	}

	/* If there are no urgent items in the formed rpc object,
	   send the rpc for submission only if it is optimal.
	   For rpc objects containing urgent items, there are chances
	   that it will be passed for submission even if it is
	   sub-optimal. */
	/* If size of formed rpc object is less than 90% of
	   max_message_size, discard the rpc object. */
	if (!rpc_frm_is_rpc_optimal(frm_sm, rpcobj_size, urgent_unbound)) {
		/* Delete the formed RPC object. */
		c2_list_for_each_entry_safe(&rpcobj->r_items,
				rpc_item, rpc_item_next, struct c2_rpc_item,
				ri_rpcobject_linkage) {
			c2_list_del(&rpc_item->ri_rpcobject_linkage);
			rpc_item->ri_state = RPC_ITEM_SUBMITTED;
			rpc_frm_item_add(frm_sm, rpc_item);
		}
		c2_list_del(&rpcobj->r_linkage);
		c2_free(rpcobj);
		rpcobj = NULL;
		return C2_RPC_FRM_INTEVT_STATE_FAILED;
	}

	c2_list_add(&frm_sm->fs_rpcs, &rpcobj->r_linkage);
	C2_POST(!c2_list_is_empty(&rpcobj->r_items));
	res = rpc_frm_send_onwire(frm_sm);
	if (res == 0)
		return C2_RPC_FRM_INTEVT_STATE_SUCCEEDED;
	else
		return C2_RPC_FRM_INTEVT_STATE_FAILED;
}

/**
  Get the cumulative size of all rpc items
  @param rpc object of which size has to be calculated
 */
uint64_t c2_rpc_get_size(const struct c2_rpc *rpc)
{
	struct c2_rpc_item	*item = NULL;
	uint64_t		 rpc_size = 0;

	C2_PRE(rpc != NULL);

	c2_list_for_each_entry(&rpc->r_items, item,
			struct c2_rpc_item, ri_rpcobject_linkage) {
		rpc_size += item->ri_type->rit_ops->rio_item_size(item);
	}

	return rpc_size;
}

/**
  Extract c2_net_transfer_mc from rpc item
  @param - rpc item
  @retval - network transfer machine
 */
struct c2_net_transfer_mc *c2_rpc_frm_get_tm(const struct c2_rpc_item *item)
{
	struct c2_net_transfer_mc	*tm = NULL;

	C2_PRE((item != NULL) && (item->ri_session != NULL) &&
			(item->ri_session->s_conn != NULL) &&
			(item->ri_session->s_conn->c_rpcchan != NULL));

	tm = &item->ri_session->s_conn->c_rpcchan->rc_xfermc;
	return tm;
}

/**
   Function to send a given rpc object on wire.
 */
static int rpc_frm_send_onwire(struct c2_rpc_frm_sm *frm_sm)
{
	int				 res = 0;
	int				 ret = 0;
	struct c2_rpc			*rpc_obj = NULL;
	struct c2_rpc			*rpc_obj_next = NULL;
	struct c2_net_domain		*dom = NULL;
	struct c2_net_transfer_mc	*tm = NULL;
	struct c2_rpc_item		*first_item = NULL;
	struct c2_rpc_frm_buffer	*fb = NULL;
	uint64_t			 rpc_size = 0;
	int				 rc = 0;

	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));

	/* Iterate over the rpc object list and send all rpc objects
	   on wire. */
	c2_list_for_each_entry_safe(&frm_sm->fs_rpcs, rpc_obj,
			rpc_obj_next, struct c2_rpc, r_linkage) {
		first_item = c2_list_entry((c2_list_first(&rpc_obj->r_items)),
				struct c2_rpc_item, ri_rpcobject_linkage);
		if (frm_sm->fs_curr_rpcs_in_flight <
				frm_sm->fs_max_rpcs_in_flight) {
			tm = c2_rpc_frm_get_tm(first_item);
			dom = tm->ntm_dom;

			/* Allocate a buffer for sending the message.*/
			rc = rpc_frm_buffer_allocate(&fb, rpc_obj, frm_sm,
					dom);
			if (rc < 0) {
				/* Process the next rpc object in the list.*/
				continue;
			}
			/* Populate destination net endpoint. */
			fb->fb_buffer.nb_ep =
				first_item->ri_session->s_conn->c_end_point;
			rpc_size = c2_rpc_get_size(rpc_obj);

			/* XXX What to do if rpc size is bigger than
			   size of net buffer??? */
			if (rpc_size > c2_vec_count(&fb->fb_buffer.nb_buffer.
						ov_vec)) {
			}
			fb->fb_buffer.nb_length = rpc_size;

			/* Encode the rpc contents. */
			rc = c2_rpc_encode(rpc_obj, &fb->fb_buffer);
			if (rc < 0) {
				rpc_frm_buffer_deallocate(fb);
				ret = rc;
				/* Process the next rpc object in the list.*/
				continue;
			}

			/* Add the buffer to transfer machine.*/
			C2_ASSERT(fb->fb_buffer.nb_ep->nep_dom == tm->ntm_dom);
			res = c2_net_buffer_add(&fb->fb_buffer, tm);
			if (res < 0) {
				rpc_frm_buffer_deallocate(fb);
				/* Process the next rpc object in the list.*/
				continue;
			} else {
				/* Remove the rpc object from rpcobj_formed
				   list.*/
				frm_sm->fs_curr_rpcs_in_flight++;
				c2_list_del(&rpc_obj->r_linkage);
				/* Get a reference on c2_rpc_frm_sm so that
				 it is pinned in memory. */
				c2_ref_get(&frm_sm->fs_ref);
			}
		} else {
			ret = -EBUSY;
			break;
		}
	}
	return ret;
}

/**
   Subroutine to change the given rpc item.
 */
static int rpc_frm_item_change(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item,
		struct c2_rpc_frm_sm_event *event)
{
	int res = 0;

	C2_PRE(item != NULL);
	C2_PRE((event->se_event == C2_RPC_FRM_EXTEVT_RPCITEM_REMOVED) ||
			(event->se_event == C2_RPC_FRM_EXTEVT_RPCITEM_CHANGED));
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->fs_lock));

	/*1. Check the state of incoming rpc item.
	  2. If it is RPC_ITEM_ADDED, deny the request to
	     change/remove rpc item and return state failed event.
	  3. If state is RPC_ITEM_SUBMITTED, the rpc item is still due
	     for formation (not yet gone through checking state) and
	     it will be removed/changed before it is considered for
	     further formation activities.  */
	if (item->ri_state == RPC_ITEM_SUBMITTED) {
		if (event->se_event == C2_RPC_FRM_EXTEVT_RPCITEM_REMOVED) {
			rpc_frm_item_remove(frm_sm, item);
		} else if (event->se_event ==
				C2_RPC_FRM_EXTEVT_RPCITEM_CHANGED) {
			res = rpc_frm_item_update(frm_sm, item, event->se_pvt);
		}
	}
	return res;
}

/**
   Function to map the on-wire FOP format to in-core FOP format.
 */
void c2_rpc_frm_item_io_fid_wire2mem(struct c2_fop_file_fid *in,
		struct c2_fid *out)
{
        out->f_container = in->f_seq;
        out->f_key = in->f_oid;
}

/**
   Coalesce rpc items that share same fid and intent(read/write)
 */
int c2_rpc_item_io_coalesce(void *c_item, struct c2_rpc_item *b_item)
{
	struct c2_list					 fop_list;
	struct c2_rpc_frm_item_coalesced_member	*c_member = NULL;
	struct c2_io_fop_member				*fop_member = NULL;
	struct c2_io_fop_member				*fop_member_next = NULL;
	struct c2_fop					*fop = NULL;
	struct c2_fop					*b_fop = NULL;
	struct c2_rpc_item				*item = NULL;
	struct c2_rpc_frm_item_coalesced		*coalesced_item = NULL;
	int						 res = 0;

	C2_PRE(b_item != NULL);

	coalesced_item = (struct c2_rpc_frm_item_coalesced*)c_item;
	C2_ASSERT(coalesced_item != NULL);
	c2_list_init(&fop_list);
	c2_list_for_each_entry(&coalesced_item->ic_member_list, c_member,
			struct c2_rpc_frm_item_coalesced_member, im_linkage) {
		C2_ALLOC_PTR(fop_member);
		if (fop_member == NULL) {
			return -ENOMEM;
		}
		item = c_member->im_member_item;
		fop = c2_rpc_item_to_fop(item);
		fop_member->fop = fop;
		c2_list_add(&fop_list, &fop_member->fop_linkage);
	}
	b_fop = container_of(b_item, struct c2_fop, f_item);
	res = fop->f_type->ft_ops->fto_io_coalesce(&fop_list, b_fop);

	c2_list_for_each_entry_safe(&fop_list, fop_member, fop_member_next,
			struct c2_io_fop_member, fop_linkage) {
		c2_list_del(&fop_member->fop_linkage);
		c2_free(fop_member);
	}
	c2_list_fini(&fop_list);
	coalesced_item->ic_resultant_item = b_item;
	return res;
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

