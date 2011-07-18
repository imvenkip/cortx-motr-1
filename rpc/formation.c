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
int c2_rpc_frm_buffer_allocate(struct c2_rpc_frm_buffer **fb,
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
void c2_rpc_frm_buffer_deallocate(struct c2_rpc_frm_buffer *fb)
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
   Forward declarations of local static functions
 */
static int rpc_frm_item_remove(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item);

static int rpc_frm_state_failed(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item, const int state);

static int rpc_frm_state_succeeded(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item, const int state);

static int rpc_frm_item_add(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item);

int c2_rpc_encode(struct c2_rpc *rpc_obj, struct c2_net_buffer *nb);

/**
   A state table guiding resultant states on arrival of events
   on earlier states.
   next_state = statetable[current_state][current_event]
 */
statefunc c2_rpc_frm_statetable
[C2_RPC_FRM_STATES_NR][C2_RPC_FRM_INTEVT_NR - 1] = {

	[C2_RPC_FRM_STATE_UNINITIALIZED] = { NULL },

	[C2_RPC_FRM_STATE_WAITING] = {
		[C2_RPC_FRM_EXTEVT_RPCITEM_READY] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REMOVED] = c2_rpc_frm_removing_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_CHANGED] = c2_rpc_frm_removing_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED] =
			c2_rpc_frm_checking_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT] = c2_rpc_frm_checking_state,
		[C2_RPC_FRM_EXTEVT_SLOT_IDLE] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_UNBOUNDED_RPCITEM_ADDED] =
			c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_NET_BUFFER_FREE] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_INTEVT_STATE_SUCCEEDED] = c2_rpc_frm_waiting_state,
		[C2_RPC_FRM_INTEVT_STATE_FAILED] = c2_rpc_frm_waiting_state,
	},

	[C2_RPC_FRM_STATE_UPDATING] = {
		[C2_RPC_FRM_EXTEVT_RPCITEM_READY] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REMOVED] = c2_rpc_frm_removing_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_CHANGED] = c2_rpc_frm_removing_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED] =
			c2_rpc_frm_checking_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT] = c2_rpc_frm_checking_state,
		[C2_RPC_FRM_EXTEVT_SLOT_IDLE] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_UNBOUNDED_RPCITEM_ADDED] =
			c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_NET_BUFFER_FREE] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_INTEVT_STATE_SUCCEEDED] = c2_rpc_frm_checking_state,
		[C2_RPC_FRM_INTEVT_STATE_FAILED] = c2_rpc_frm_waiting_state,
	},

	[C2_RPC_FRM_STATE_CHECKING] = {
		[C2_RPC_FRM_EXTEVT_RPCITEM_READY] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REMOVED] = c2_rpc_frm_removing_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_CHANGED] = c2_rpc_frm_removing_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED] =
			c2_rpc_frm_checking_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT] = c2_rpc_frm_checking_state,
		[C2_RPC_FRM_EXTEVT_SLOT_IDLE] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_UNBOUNDED_RPCITEM_ADDED] =
			c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_NET_BUFFER_FREE] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_INTEVT_STATE_SUCCEEDED] = c2_rpc_frm_forming_state,
		[C2_RPC_FRM_INTEVT_STATE_FAILED] = c2_rpc_frm_waiting_state,
	},

	[C2_RPC_FRM_STATE_FORMING] = {
		[C2_RPC_FRM_EXTEVT_RPCITEM_READY] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REMOVED] = c2_rpc_frm_removing_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_CHANGED] = c2_rpc_frm_removing_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED] =
			c2_rpc_frm_checking_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT] = c2_rpc_frm_checking_state,
		[C2_RPC_FRM_EXTEVT_SLOT_IDLE] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_UNBOUNDED_RPCITEM_ADDED] =
			c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_NET_BUFFER_FREE] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_INTEVT_STATE_SUCCEEDED] = c2_rpc_frm_posting_state,
		[C2_RPC_FRM_INTEVT_STATE_FAILED] = c2_rpc_frm_waiting_state
	},

	[C2_RPC_FRM_STATE_POSTING] = {
		[C2_RPC_FRM_EXTEVT_RPCITEM_READY] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REMOVED] = c2_rpc_frm_removing_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_CHANGED] = c2_rpc_frm_removing_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED] =
			c2_rpc_frm_checking_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT] = c2_rpc_frm_checking_state,
		[C2_RPC_FRM_EXTEVT_SLOT_IDLE] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_UNBOUNDED_RPCITEM_ADDED] =
			c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_NET_BUFFER_FREE] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_INTEVT_STATE_SUCCEEDED] = c2_rpc_frm_waiting_state,
		[C2_RPC_FRM_INTEVT_STATE_FAILED] = c2_rpc_frm_waiting_state,
	},

	[C2_RPC_FRM_STATE_REMOVING] = {
		[C2_RPC_FRM_EXTEVT_RPCITEM_READY] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REMOVED] = c2_rpc_frm_removing_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_CHANGED] = c2_rpc_frm_removing_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED] =
			c2_rpc_frm_checking_state,
		[C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT] = c2_rpc_frm_checking_state,
		[C2_RPC_FRM_EXTEVT_SLOT_IDLE] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_UNBOUNDED_RPCITEM_ADDED] =
			c2_rpc_frm_updating_state,
		[C2_RPC_FRM_EXTEVT_NET_BUFFER_FREE] = c2_rpc_frm_updating_state,
		[C2_RPC_FRM_INTEVT_STATE_SUCCEEDED] = c2_rpc_frm_waiting_state,
		[C2_RPC_FRM_INTEVT_STATE_FAILED] = c2_rpc_frm_waiting_state,
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
   Initialization for formation component in rpc.
   This will register necessary callbacks and initialize
   necessary data structures.
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
bool c2_rpc_frm_wait_for_completion(struct c2_rpc_formation *formation_summary)
{
	bool					 ret = true;
	int64_t					 refcount;
	struct c2_rpc_frm_sm	*frm_sm = NULL;

	C2_PRE(formation_summary != NULL);

	c2_rwlock_read_lock(&formation_summary->rf_sm_list_lock);
	c2_list_for_each_entry(&formation_summary->rf_frm_sm_list,
			frm_sm, struct c2_rpc_frm_sm,
			isu_linkage) {
		c2_mutex_lock(&frm_sm->isu_unit_lock);
		refcount = c2_atomic64_get(&frm_sm->isu_ref.ref_cnt);
		if (refcount != 0) {
			ret = false;
			c2_mutex_unlock(&frm_sm->isu_unit_lock);
			break;
		}
		c2_mutex_unlock(&frm_sm->isu_unit_lock);
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
			struct c2_rpc_frm_rpcgroup, sug_linkage) {
		c2_list_del(&group->sug_linkage);
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
static void rpc_frm_empty_unformed_list(struct c2_list *list)
{
	struct c2_rpc_item		*item = NULL;
	struct c2_rpc_item		*item_next = NULL;

	C2_PRE(list != NULL);

	c2_list_for_each_entry_safe(list, item, item_next,
			struct c2_rpc_item, ri_unformed_linkage) {
		c2_list_del(&item->ri_unformed_linkage);
	}
	c2_list_fini(list);
}

/**
   Finish method for formation component in rpc.
   This will deallocate all memory claimed by formation
   and do necessary cleanup.
 */
void c2_rpc_frm_fini(struct c2_rpc_formation *formation_summary)
{
	/* stime = 10ms */
	c2_time_t				 stime;
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
			isu_linkage) {
		c2_mutex_lock(&frm_sm->isu_unit_lock);
		frm_sm->isu_form_active = false;
		c2_mutex_unlock(&frm_sm->isu_unit_lock);
	}
	c2_rwlock_read_unlock(&formation_summary->rf_sm_list_lock);
	c2_time_set(&stime, 0, PTIME);

	/* Iterate over the list of state machines until refcounts of all
	   become zero. */
	while(!c2_rpc_frm_wait_for_completion(formation_summary)) {
		c2_nanosleep(stime, NULL);
	}

	/* Delete all state machines, all lists within each state machine. */
	c2_rwlock_write_lock(&formation_summary->rf_sm_list_lock);
	c2_list_for_each_entry_safe(&formation_summary->rf_frm_sm_list,
			frm_sm, frm_sm_next, struct
			c2_rpc_frm_sm, isu_linkage) {
		c2_mutex_lock(&frm_sm->isu_unit_lock);
		rpc_frm_empty_groups_list(&frm_sm->isu_groups_list);
		rpc_frm_empty_coalesced_items_list(&frm_sm->
				isu_coalesced_items_list);
		rpc_frm_empty_rpcobj_list(&frm_sm->
				isu_rpcobj_formed_list);
		rpc_frm_empty_rpcobj_list(&frm_sm->
				isu_rpcobj_checked_list);
		rpc_frm_empty_unformed_list(&frm_sm->isu_unformed_list);
		c2_mutex_unlock(&frm_sm->isu_unit_lock);
		c2_list_del(&frm_sm->isu_linkage);
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
	c2_ref_put(&frm_sm->isu_ref);
	c2_rwlock_write_unlock(&formation_summary->rf_sm_list_lock);
}

/**
   Check if the state machine structure is empty.
 */
static bool rpc_frm_sm_invariant(struct c2_rpc_frm_sm *frm_sm)
{
	if (!frm_sm)
		return false;
	if (!c2_list_is_empty(&frm_sm->isu_groups_list))
		return false;
	if (!c2_list_is_empty(&frm_sm->isu_coalesced_items_list))
		return false;
	if (!c2_list_is_empty(&frm_sm->isu_unformed_list))
		return false;
	if (!c2_list_is_empty(&frm_sm->isu_rpcobj_formed_list))
		return false;
	if (!c2_list_is_empty(&frm_sm->isu_rpcobj_checked_list))
		return false;
	return true;
}

/**
   Destroy a state machine structure since it no longer contains
   any rpc items.
 */
static void rpc_frm_sm_destroy(struct c2_ref *ref)
{
	struct c2_rpc_frm_sm	*frm_sm;

	C2_PRE(ref != NULL);

	frm_sm = container_of(ref, struct c2_rpc_frm_sm,
			isu_ref);
	/* Delete the frm_sm only if all lists are empty.*/
	if (rpc_frm_sm_invariant(frm_sm)) {
		c2_mutex_fini(&frm_sm->isu_unit_lock);
		c2_list_del(&frm_sm->isu_linkage);
		c2_list_fini(&frm_sm->isu_groups_list);
		c2_list_fini(&frm_sm->isu_coalesced_items_list);
		c2_list_fini(&frm_sm->isu_unformed_list);
		c2_list_fini(&frm_sm->isu_rpcobj_formed_list);
		c2_list_fini(&frm_sm->isu_rpcobj_checked_list);
		frm_sm->isu_endp = NULL;
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
	struct c2_rpc_frm_sm	*frm_sm;

	C2_PRE(endp != NULL);

	C2_ALLOC_PTR(frm_sm);
	if (frm_sm == NULL) {
		C2_ADDB_ADD(&formation_summary->rf_rpc_form_addb,
				&rpc_frm_addb_loc, c2_addb_oom);
		return NULL;
	}
	c2_mutex_init(&frm_sm->isu_unit_lock);
	c2_list_add(&formation_summary->rf_frm_sm_list,
			&frm_sm->isu_linkage);
	c2_list_init(&frm_sm->isu_groups_list);
	c2_list_init(&frm_sm->isu_coalesced_items_list);
	c2_list_init(&frm_sm->isu_unformed_list);
	c2_list_init(&frm_sm->isu_rpcobj_formed_list);
	c2_list_init(&frm_sm->isu_rpcobj_checked_list);
	c2_ref_init(&frm_sm->isu_ref, 1, rpc_frm_sm_destroy);
	frm_sm->isu_endp = endp;
	frm_sm->isu_frm_state = C2_RPC_FRM_STATE_WAITING;
	frm_sm->isu_form_active = true;
	/* XXX Need appropriate values.*/
	frm_sm->isu_max_message_size = max_msg_size;
	frm_sm->isu_max_fragments_size = max_fragments_size;
	frm_sm->isu_max_rpcs_in_flight = max_rpcs_in_flight;
	frm_sm->isu_curr_rpcs_in_flight = 0;
	frm_sm->isu_cumulative_size = 0;
	frm_sm->isu_n_urgent_items = 0;
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
static struct c2_rpc_frm_sm *rpc_frm_get_endp_unit(struct c2_net_end_point *ep,
		struct c2_rpc_formation *formation_summary)
{
	struct c2_rpc_frm_sm *frm_sm = NULL;
	struct c2_rpc_frm_sm *sm = NULL;

	C2_PRE(ep != NULL);

	c2_list_for_each_entry(&formation_summary->rf_frm_sm_list,
			sm, struct c2_rpc_frm_sm, isu_linkage) {
		if (rpc_frm_endp_equal(sm->isu_endp, ep)) {
			frm_sm = sm;
			break;
		}
	}
	return frm_sm;
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
	/* XXX to be replaced by appropriate routine */
	endpoint = rpc_frm_get_endpoint(item);

	/* If state machine pointer from rpc item is NULL, locate it
	   from list in list of state machines. If found, lock it and
	   increment its refcount. If not found, create a new state machine.
	   In any case, find out the previous state of state machine. */
	if (frm_sm == NULL) {
		c2_rwlock_write_lock(&formation_summary->rf_sm_list_lock);
		sm = rpc_frm_get_endp_unit(endpoint, formation_summary);
		if (sm != NULL) {
			c2_mutex_lock(&sm->isu_unit_lock);
			c2_ref_get(&sm->isu_ref);
			c2_rwlock_write_unlock(&formation_summary->
					rf_sm_list_lock);
		} else {
			/** Add a new endpoint summary unit */
			sm = rpc_frm_sm_add(endpoint,
					formation_summary);
			if(sm == NULL) {
				C2_ADDB_ADD(&formation_summary->
					rf_rpc_form_addb, &rpc_frm_addb_loc,
					formation_func_fail, "rpc_frm_sm_add",
					res);
				return -ENOENT;
			}
			c2_mutex_lock(&sm->isu_unit_lock);
			c2_rwlock_write_unlock(&formation_summary->
					rf_sm_list_lock);
		}
		prev_state = sm->isu_frm_state;
	} else {
		prev_state = sm_state;
		c2_mutex_lock(&frm_sm->isu_unit_lock);
		sm = frm_sm;
	}
	/* If the formation component is not active (form_fini is called)
	   exit the state machine and return back. */
	if (!sm->isu_form_active) {
		c2_mutex_unlock(&sm->isu_unit_lock);
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
	prev_state = sm->isu_frm_state;
	c2_mutex_unlock(&sm->isu_unit_lock);

	/* Exit point for state machine. */
	if(res == C2_RPC_FRM_INTEVT_STATE_DONE) {
		rpc_frm_sm_exit(formation_summary, sm);
		return 0;
	}

	if (res == C2_RPC_FRM_INTEVT_STATE_FAILED) {
		/** Post a state failed event. */
		rpc_frm_state_failed(sm, item, prev_state);
	} else if (res == C2_RPC_FRM_INTEVT_STATE_SUCCEEDED){
		/** Post a state succeeded event. */
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
	struct c2_rpc_frm_sm_event		 sm_event;
	struct c2_rpc_slot			*slot = NULL;
	struct c2_rpcmachine			*rpcmachine = NULL;

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
int c2_rpc_frm_ub_item_added(struct c2_rpc_item *item)
{
	struct c2_rpc_frm_sm_event		 sm_event;
	struct c2_rpc_session			*session;

	C2_PRE(item != NULL);
	sm_event.se_event = C2_RPC_FRM_EXTEVT_UNBOUNDED_RPCITEM_ADDED;
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
	struct c2_net_buffer			*nb = NULL;
	struct c2_net_domain			*dom = NULL;
	struct c2_net_transfer_mc		*tm = NULL;
	struct c2_rpc_frm_buffer		*fb = NULL;
	struct c2_rpc_item			*item = NULL;
	struct c2_rpc_formation			*formation_summary = NULL;

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

	/* The buffer should have been dequeued by now.*/
	C2_ASSERT((nb->nb_flags & C2_NET_BUF_QUEUED) == 0);

	if (ev->nbe_status == 0) {
		rpc_frm_set_state_sent(fb->fb_rpc);
		c2_rpc_frm_buffer_deallocate(fb);
	} else {
		/* If the send event fails, add the rpc back to concerned
		   queue so that it will be processed next time.*/
		c2_mutex_lock(&fb->fb_frm_sm->isu_unit_lock);
		c2_list_add(&fb->fb_frm_sm->isu_rpcobj_formed_list,
				&fb->fb_rpc->r_linkage);
		c2_mutex_unlock(&fb->fb_frm_sm->isu_unit_lock);
	}
	/* Release reference on the c2_rpc_frm_sm here. */
	rpc_frm_sm_exit(formation_summary, fb->fb_frm_sm);
}

/**
   Callback function for deletion of an rpc item from the rpc items cache.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_frm_item_deleted(struct c2_rpc_item *item)
{
	struct c2_rpc_frm_sm_event		sm_event;

	C2_PRE(item != NULL);

	sm_event.se_event = C2_RPC_FRM_EXTEVT_RPCITEM_REMOVED;
	sm_event.se_pvt = NULL;
	/* Curent state is not known at the moment. */
	return rpc_frm_default_handler(item, NULL, C2_RPC_FRM_STATES_NR,
			&sm_event);
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
	struct c2_rpc_frm_sm_event		sm_event;
	struct c2_rpc_frm_item_change_req	req;

	C2_PRE(item != NULL);
	C2_PRE(pvt != NULL);
	C2_PRE(field_type < C2_RPC_ITEM_CHANGES_NR);

	sm_event.se_event = C2_RPC_FRM_EXTEVT_RPCITEM_CHANGED;
	req.field_type = field_type;
	req.value = pvt;
	sm_event.se_pvt = &req;
	/* Curent state is not known at the moment. */
	return rpc_frm_default_handler(item, NULL, C2_RPC_FRM_STATES_NR,
			&sm_event);
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
	struct c2_rpc_frm_sm_event		sm_event;

	C2_PRE(req_item != NULL);
	C2_PRE(reply_item != NULL);

	sm_event.se_event = C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED;
	sm_event.se_pvt = NULL;
	/* Curent state is not known at the moment. */
	return rpc_frm_default_handler(req_item, NULL, C2_RPC_FRM_STATES_NR,
			&sm_event);
}

/**
   Callback function for deadline expiry of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_frm_item_timeout(struct c2_rpc_item *item)
{
	struct c2_rpc_frm_sm_event		sm_event;

	C2_PRE(item != NULL);

	sm_event.se_event = C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT;
	sm_event.se_pvt = NULL;
	item->ri_deadline = 0;
	/* Curent state is not known at the moment. */
	return rpc_frm_default_handler(item, NULL, C2_RPC_FRM_STATES_NR,
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
static int rpc_frm_item_coalesced_reply_post(struct
		c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_frm_item_coalesced *coalesced_struct)
{
	struct c2_rpc_frm_item_coalesced_member	*member;
	struct c2_rpc_frm_item_coalesced_member	*next_member;
	struct c2_rpc_item				*item = NULL;

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
		/* To be called when reply of an item arrives
		   item->ri_type->rit_ops->rio_replied(item);
		 */
		c2_free(member);
		coalesced_struct->ic_nmembers--;
	}
	C2_ASSERT(coalesced_struct->ic_nmembers == 0);
	c2_list_del(&coalesced_struct->ic_linkage);
	item = coalesced_struct->ic_resultant_item;
	/* To be called when reply of an item arrives
	   item->ri_type->rit_ops->rio_replied(item);
	 */
	c2_list_fini(&coalesced_struct->ic_member_list);
	c2_free(coalesced_struct);
	return 0;
}

/**
   State function for WAITING state.
   frm_sm is locked.
 */
enum c2_rpc_frm_int_evt_id c2_rpc_frm_waiting_state(
		struct c2_rpc_frm_sm *frm_sm ,struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event)
{
	C2_PRE(item != NULL);
	C2_PRE((event->se_event == C2_RPC_FRM_INTEVT_STATE_SUCCEEDED) ||
			(event->se_event == C2_RPC_FRM_INTEVT_STATE_FAILED));
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->isu_unit_lock));

	frm_sm->isu_frm_state = C2_RPC_FRM_STATE_WAITING;
	/* Internal events will invoke a nop from waiting state. */

	return C2_RPC_FRM_INTEVT_STATE_DONE;
}

/**
   Callback used to trigger the "deadline expired" event
   for an rpc item.
 */
static unsigned long c2_rpc_frm_item_timer_callback(unsigned long data)
{
	struct c2_rpc_item	*item;

	item = (struct c2_rpc_item*)data;
	if (item->ri_state == RPC_ITEM_SUBMITTED) {
		c2_rpc_frm_item_timeout(item);
	}
	return 0;
}

/**
   Change the data of an rpc item embedded within the endpoint unit
   structure.
 */
static int rpc_frm_change_rpcitem_from_summary_unit(struct
		c2_rpc_frm_sm *frm_sm,
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

	/* First remove the item data from summary unit. */
	res = rpc_frm_item_remove(frm_sm, item);
	if (res != 0) {
		C2_ADDB_ADD(&formation_summary->rf_rpc_form_addb,
				&rpc_frm_addb_loc, formation_func_fail,
				"rpc_frm_change_rpcitem_from_summary_unit",
				res);
		return res;
	}
	switch (field_type) {
		case C2_RPC_ITEM_CHANGE_PRIORITY:
			item->ri_prio =
				(enum c2_rpc_item_priority)chng_req->value;
			break;
		case C2_RPC_ITEM_CHANGE_DEADLINE:
			c2_timer_stop(&item->ri_timer);
			c2_timer_fini(&item->ri_timer);
			item->ri_deadline = (c2_time_t)chng_req->value;
			break;
		case C2_RPC_ITEM_CHANGE_RPCGROUP:
			item->ri_group = (struct c2_rpc_group*)chng_req->value;
		default:
			C2_ADDB_ADD(&formation_summary->rf_rpc_form_addb,
				&rpc_frm_addb_loc,
				formation_func_fail,
				"rpc_frm_change_rpcitem_from_summary_unit",
				res);
			C2_ASSERT(0);
	};
	/* Then, add the new data of rpc item to the summary unit.*/
	res = rpc_frm_item_add(frm_sm, item);
	if (res != 0) {
		C2_ADDB_ADD(&formation_summary->rf_rpc_form_addb,
				&rpc_frm_addb_loc, formation_func_fail,
				"rpc_frm_change_rpcitem_from_summary_unit",
				res);
		return res;
	}
	return res;
}

/**
   Remove the data of an rpc item embedded within the endpoint unit
   structure.
 */
static int rpc_frm_item_remove(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item)
{
	struct c2_rpc_frm_rpcgroup	*summary_group = NULL;
	bool				 found = false;
	size_t				 item_size = 0;

	C2_PRE(item != NULL);
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->isu_unit_lock));
	C2_PRE(item->ri_state == RPC_ITEM_SUBMITTED);

	/* Find out the group, given rpc item belongs to.*/
	c2_list_for_each_entry(&frm_sm->isu_groups_list,
			summary_group, struct c2_rpc_frm_rpcgroup,
			sug_linkage) {
		if (summary_group->sug_group == item->ri_group) {
			found = true;
			break;
		}
	}
	if (!found) {
		return 0;
	}

	/* Remove the data entered by this item in this summary group.*/
	if (--summary_group->sug_nitems == 0) {
		c2_list_del(&summary_group->sug_linkage);
		c2_free(summary_group);
		return 0;
	}
	summary_group->sug_expected_items -= item->ri_group->rg_expected;
	if (item->ri_prio == C2_RPC_ITEM_PRIO_MAX &&
			summary_group->sug_priority_items > 0) {
		summary_group->sug_priority_items--;
	}
	item_size = item->ri_type->rit_ops->rio_item_size(item);
	summary_group->sug_total_size -= item_size;
	frm_sm->isu_cumulative_size -= item_size;
	summary_group->sug_avg_timeout =
		((summary_group->sug_nitems * summary_group->sug_avg_timeout)
		 - item->ri_deadline) / (summary_group->sug_nitems);
	if (item->ri_deadline == 0) {
		frm_sm->isu_n_urgent_items--;
	}

	return 0;
}

/**
   Add the c2_rpc_frm_rpcgroup structs according to
   increasing value of average timeout.
   This helps to select groups with least average timeouts in formation.
 */
static int rpc_frm_summary_groups_add(struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_frm_rpcgroup *summary_group)
{
	bool				 placed = false;
	struct c2_rpc_frm_rpcgroup	*sg = NULL;
	struct c2_rpc_frm_rpcgroup	*sg_next = NULL;

	C2_PRE(frm_sm != NULL);
	C2_PRE(summary_group != NULL);

	c2_list_del(&summary_group->sug_linkage);
	/* Do a simple incremental search for a group having
	   average timeout value bigger than that of given group. */
	c2_list_for_each_entry_safe(&frm_sm->isu_groups_list, sg, sg_next,
			struct c2_rpc_frm_rpcgroup, sug_linkage) {
		if (sg && sg->sug_avg_timeout >
				summary_group->sug_avg_timeout) {
			placed = true;
			c2_list_add_before(&sg->sug_linkage,
					&summary_group->sug_linkage);
			break;
		}
	}
	if (!placed) {
		c2_list_add_after(&sg->sug_linkage,
				&summary_group->sug_linkage);
	}
	return 0;
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

	C2_PRE(item != NULL);
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->isu_unit_lock));

	if (item->ri_state != RPC_ITEM_SUBMITTED) {
		return -EINVAL;
	}

	formation_summary = item->ri_mach->cr_formation;
	/** Insert the item into unformed list sorted according to timeout*/
	c2_list_for_each_entry_safe(&frm_sm->isu_unformed_list,
			rpc_item, rpc_item_next,
			struct c2_rpc_item, ri_unformed_linkage) {
		if (item->ri_deadline <= rpc_item->ri_deadline) {
			c2_list_add_before(&rpc_item->ri_unformed_linkage,
					&item->ri_unformed_linkage);
			item_inserted = true;
			break;
		}
	}
	if (!item_inserted) {
		c2_list_add_after(&rpc_item->ri_unformed_linkage,
				&item->ri_unformed_linkage);
	}

	/* Search for the group of rpc item in list of rpc groups in
	  summary_unit. */
	c2_list_for_each_entry(&frm_sm->isu_groups_list, summary_group,
			struct c2_rpc_frm_rpcgroup, sug_linkage) {
		if (summary_group->sug_group == item->ri_group) {
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
		c2_list_link_init(&summary_group->sug_linkage);
		c2_list_add(&frm_sm->isu_groups_list,
				&summary_group->sug_linkage);
		summary_group->sug_group = item->ri_group;
	}

	/* If found, add data from rpc item like priority, deadline,
	     size, rpc item type. */
	if(item->ri_group != NULL) {
		summary_group->sug_expected_items = item->ri_group->rg_expected;
	}
	if (item->ri_prio == C2_RPC_ITEM_PRIO_MAX) {
		summary_group->sug_priority_items++;
	}

	item_size = item->ri_type->rit_ops->rio_item_size(item);
	summary_group->sug_total_size += item_size;
	frm_sm->isu_cumulative_size += item_size;
	/* Prerequisite => Kernel needs to be compiled with FPE support
	   if we want to use FP operations in kernel. */
	summary_group->sug_avg_timeout =
		((summary_group->sug_nitems * summary_group->sug_avg_timeout)
		 + item->ri_deadline) / (summary_group->sug_nitems + 1);
	summary_group->sug_nitems++;

	/* Put the corresponding c2_rpc_frm_rpcgroup struct in its correct
	   position on "least value first" basis of average timeout of group. */
	if (item->ri_group != NULL) {
		res = rpc_frm_summary_groups_add(frm_sm, summary_group);
	} else {
		/* Special handling for group of items which belong to no group,
		   so that they are handled as the last option for formation. */
		c2_list_move_tail(&frm_sm->isu_groups_list,
				&summary_group->sug_linkage);
	}

	/* Initialize the timer only when the deadline value is non-zero
	   i.e. dont initialize the timer for URGENT items */
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
	} else {
		frm_sm->isu_n_urgent_items++;
	}
	return 0;
}

/**
   Given an endpoint, tell if an optimal rpc can be prepared from
   the items submitted to this endpoint.
   @param frm_sm - the c2_rpc_frm_sm structure
   based on whose data, it will be found if an optimal rpc can be made.
   @param rpcobj_size - check if given size of rpc object is optimal or not.
 */
bool c2_rpc_frm_is_rpc_optimal(struct c2_rpc_frm_sm *frm_sm,
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
		size = frm_sm->isu_cumulative_size;
	} else {
		size = rpcobj_size;
	}
	if (frm_sm->isu_n_urgent_items > 0) {
		ret = true;
	} else if (size >= (0.9 * frm_sm->isu_max_message_size)) {
		ret = true;
	}
	return ret;
}

/**
   State function for UPDATING state.
 */
enum c2_rpc_frm_int_evt_id c2_rpc_frm_updating_state(
		struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event)
{
	int			 res = 0;
	int			 ret = 0;
	struct c2_rpc_session	*session = NULL;
	bool			 item_unbound = true;
	bool			 item_unbound_urgent = false;

	C2_PRE(item != NULL);
	C2_PRE(event->se_event == C2_RPC_FRM_EXTEVT_RPCITEM_READY ||
		event->se_event == C2_RPC_FRM_EXTEVT_SLOT_IDLE ||
		event->se_event == C2_RPC_FRM_EXTEVT_UNBOUNDED_RPCITEM_ADDED);
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->isu_unit_lock));

	frm_sm->isu_frm_state = C2_RPC_FRM_STATE_UPDATING;

	/* If the item doesn't belong to unbound list of its session,
	   add it to the c2_rpc_frm_sm data structure.*/
	session = item->ri_session;
	C2_ASSERT(session != NULL);
	c2_mutex_lock(&session->s_mutex);
	item_unbound = c2_list_link_is_in(&item->ri_unbound_link);
	c2_mutex_unlock(&session->s_mutex);
	/* Add the item to summary unit and subsequently to unformed list
	   only if it is bound item. */
	if (!item_unbound && item->ri_slot_refs[0].sr_slot) {
		res = rpc_frm_item_add(frm_sm, item);
		if (res != 0) {
			return C2_RPC_FRM_INTEVT_STATE_FAILED;
		}
	}
	/* If rpcobj_formed_list already contains formed rpc objects,
	   succeed the state and let it proceed to posting state. */
	if (!c2_list_is_empty(&frm_sm->isu_rpcobj_formed_list)) {
		ret = C2_RPC_FRM_INTEVT_STATE_SUCCEEDED;
		return ret;
	}
	/* If the attempt to add item to summary unit failed, fail
	   the state.*/
	if (res != 0) {
		ret = C2_RPC_FRM_INTEVT_STATE_FAILED;
		return ret;
	}
	/* Check if the current item is unbound and urgent, if yes,
	   set a flag and pass it to c2_rpc_form_can_form_optimal. */
	if (item_unbound && (item->ri_deadline == 0)) {
		item_unbound_urgent = true;
	}
	/* Move the thread to the checking state only if an optimal rpc
	   can be formed.*/
	if (c2_rpc_frm_is_rpc_optimal(frm_sm, 0, item_unbound_urgent)) {
		ret = C2_RPC_FRM_INTEVT_STATE_SUCCEEDED;
	} else {
		ret = C2_RPC_FRM_INTEVT_STATE_FAILED;
	}

	return ret;
}

/**
   Add an rpc item to the formed list of an rpc object.
   @retval 0 if item added to the forming list successfully
   @retval -1 if item is not added due to some check failure
 */
static int rpc_frm_item_add_to_forming_list(
		struct c2_rpc_frm_sm *frm_sm,
		struct c2_rpc_item *item,
		uint64_t *rpcobj_size, uint64_t *nfragments,
		struct c2_rpc *rpc)
{
	size_t				 item_size = 0;
	uint64_t			 current_fragments = 0;
	struct c2_rpc_slot		*slot = NULL;
	struct c2_rpc_session		*session = NULL;
	c2_time_t			 now;
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
				frm_sm->isu_max_fragments_size) {
			return -1;
		}
	}
	/* Get size of rpc item. */
	item_size = item->ri_type->rit_ops->rio_item_size(item);

	/* If size of rpc object after adding current rpc item is
	   within max_message_size, add it the rpc object. */
	if (((*rpcobj_size + item_size) < frm_sm->isu_max_message_size)) {
		c2_list_add(&rpc->r_items, &item->ri_rpcobject_linkage);
		*rpcobj_size += item_size;
		*nfragments += current_fragments;
		/* Remove the item data from summary_unit structure. */
		rpc_frm_item_remove(frm_sm, item);
		item->ri_state = RPC_ITEM_ADDED;
		/* If timer of rpc item is still running, change the
		   deadline in rpc item as per remaining time and
		   stop and fini the timer. */
		if(item->ri_deadline != 0) {
			c2_time_now(&now);
			if (c2_time_after(item->ri_timer.t_expire, now)) {
				item->ri_deadline =
					c2_time_sub(item->ri_timer.t_expire,
							now);
			}
			c2_timer_stop(&item->ri_timer);
			c2_timer_fini(&item->ri_timer);
		}
		c2_list_del(&item->ri_unformed_linkage);

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
 */
int c2_rpc_frm_coalesce_fid_intent(struct c2_rpc_item *b_item,
		struct c2_rpc_frm_item_coalesced *coalesced_item)
{
	int						 res = 0;

	C2_PRE(b_item != NULL);
	C2_PRE(coalesced_item != NULL);

	res = b_item->ri_type->rit_ops->
		rio_io_coalesce((void*)coalesced_item, b_item);
	return res;
}

/**
   Try to coalesce rpc items from the session->free list.
 */
int c2_rpc_frm_try_coalesce(struct c2_rpc_frm_sm *frm_sm,
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
	c2_list_for_each_entry(&frm_sm->isu_coalesced_items_list,
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
		   isu_coalesced_items_list in frm_sm.*/
		c2_list_add(&frm_sm->isu_coalesced_items_list,
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
	res = c2_rpc_frm_coalesce_fid_intent(item, coalesced_item);
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
   State function for CHECKING state.
 */
enum c2_rpc_frm_int_evt_id c2_rpc_frm_checking_state(
		struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event)
{
	int					 res = 0;
	bool					 slot_found = false;
	bool					 urgent_items = false;
	bool					 item_added = false;
	bool					 item_coalesced = false;
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
	struct c2_rpc_frm_item_coalesced	*coalesced_item = NULL;

	C2_PRE(item != NULL);
	C2_PRE((event->se_event == C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED)
			|| (event->se_event ==
				C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT) ||
			(event->se_event == C2_RPC_FRM_INTEVT_STATE_SUCCEEDED));
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->isu_unit_lock));

	frm_sm->isu_frm_state = C2_RPC_FRM_STATE_CHECKING;
	formation_summary = item->ri_mach->cr_formation;

	/* If isu_rpcobj_formed_list is not empty, it means an rpc
	   object was formed successfully some time back but it
	   could not be sent due to some error conditions.
	   We send it back here again. */
	if(!c2_list_is_empty(&frm_sm->isu_rpcobj_formed_list)) {
		return C2_RPC_FRM_INTEVT_STATE_SUCCEEDED;
	}

	/* Returning failure will lead the state machine to
	    waiting state and then the thread will exit the
	    state machine. */
	if (frm_sm->isu_curr_rpcs_in_flight == frm_sm->isu_max_rpcs_in_flight) {
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

	/* If event received is reply_received, do all the post processing
	   for a coalesced item.*/
	if (event->se_event == C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED) {
		c2_list_for_each_entry(&frm_sm->isu_coalesced_items_list,
				coalesced_item, struct
				c2_rpc_frm_item_coalesced, ic_linkage) {
			if (coalesced_item->ic_resultant_item == item) {
				item_coalesced = true;
				res = rpc_frm_item_coalesced_reply_post(
						frm_sm, coalesced_item);
				if (res != 0) {
					break;
				}
			}
		}
		return C2_RPC_FRM_INTEVT_STATE_DONE;
	} else if (event->se_event == C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT) {
		/* If event received is rpcitem_timeout, add the item to
		   the rpc object immediately.*/
		if (item->ri_state != RPC_ITEM_SUBMITTED) {
			c2_free(rpcobj);
			return C2_RPC_FRM_INTEVT_STATE_FAILED;
		}
		res = rpc_frm_item_add_to_forming_list(frm_sm, item,
				&rpcobj_size, &nfragments, rpcobj);
		/* If item can not be added due to size restrictions, move it
		   to the head of unformed list and it will be handled on
		   next formation attempt.*/
		if (res != 0) {
			c2_list_move(&frm_sm->isu_unformed_list,
					&item->ri_unformed_linkage);
		} else {
			urgent_items = true;
		}
	}
	/* Iterate over the c2_rpc_frm_rpcgroup list in the
	   endpoint structure to find out which rpc groups can be included
	   in the rpc object. */
	c2_list_for_each_entry(&frm_sm->isu_groups_list, sg,
			struct c2_rpc_frm_rpcgroup, sug_linkage) {
		/* nselected_groups number includes the last partial
		   rpc group(if any).*/
		nselected_groups++;
		if ((group_size + sg->sug_total_size) <
				frm_sm->isu_max_message_size) {
			group_size += sg->sug_total_size;
		} else {
			partial_size = (group_size + sg->sug_total_size) -
				frm_sm->isu_max_message_size;
			sg_partial = sg;
			break;
		}
	}

	/* Core of formation algorithm. */
	c2_list_for_each_entry_safe(&frm_sm->isu_unformed_list, rpc_item,
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
		c2_list_for_each_entry_safe(&frm_sm->isu_groups_list, group,
				group_next, struct c2_rpc_frm_rpcgroup,
				sug_linkage) {
			ncurrent_groups++;
			/* If selected groups are exhausted, break the loop. */
			if (ncurrent_groups > nselected_groups) {
				break;
			}
			if ((sg_partial != NULL) && (rpc_item->ri_group ==
						sg_partial->sug_group)) {
				break;
			}
			if (rpc_item->ri_group == group->sug_group) {
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
		res = c2_rpc_frm_try_coalesce(frm_sm, rpc_item,
				&rpcobj_size);
	}

	/* Add the rpc items in the partial group to the forming
	   list separately. This group is sorted according to priority.
	   So the partial number of items will be picked up
	   for formation in increasing order of priority */
	if (sg_partial != NULL) {
		c2_mutex_lock(&sg_partial->sug_group->rg_guard);
		c2_list_for_each_entry_safe(&sg_partial->sug_group->rg_items,
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
		c2_mutex_unlock(&sg_partial->sug_group->rg_guard);
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
			c2_list_add(&frm_sm->isu_unformed_list,
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
	if (!c2_rpc_frm_is_rpc_optimal(frm_sm, rpcobj_size, urgent_unbound)) {
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

	/* Try to do IO colescing for items in forming list. */
	c2_list_add(&frm_sm->isu_rpcobj_checked_list,
			&rpcobj->r_linkage);
	C2_POST(!c2_list_is_empty(&rpcobj->r_items));
	return C2_RPC_FRM_INTEVT_STATE_SUCCEEDED;
}

/**
   State function for FORMING state.
 */
enum c2_rpc_frm_int_evt_id c2_rpc_frm_forming_state(
		struct c2_rpc_frm_sm *frm_sm ,struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event)
{
	struct c2_rpc	*rpcobj = NULL;
	struct c2_rpc	*rpcobj_next = NULL;

	C2_PRE(item != NULL);
	C2_PRE(event->se_event == C2_RPC_FRM_INTEVT_STATE_SUCCEEDED);
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->isu_unit_lock));

	frm_sm->isu_frm_state = C2_RPC_FRM_STATE_FORMING;
	if (!c2_list_is_empty(&frm_sm->isu_rpcobj_formed_list)) {
		return C2_RPC_FRM_INTEVT_STATE_SUCCEEDED;
	}

	/* Move all the rpc objects from rpcobj_checked list to
	   rpcobj_formed list.*/
	c2_list_for_each_entry_safe(&frm_sm->isu_rpcobj_checked_list,
			rpcobj, rpcobj_next, struct c2_rpc, r_linkage) {
		c2_list_del(&rpcobj->r_linkage);
		c2_list_add(&frm_sm->isu_rpcobj_formed_list,
				&rpcobj->r_linkage);
	}
	return C2_RPC_FRM_INTEVT_STATE_SUCCEEDED;
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
   State function for POSTING state.
 */
enum c2_rpc_frm_int_evt_id c2_rpc_frm_posting_state(
		struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event)
{
	int				 res = 0;
	enum c2_rpc_frm_state		 ret = C2_RPC_FRM_INTEVT_STATE_FAILED;
	struct c2_rpc			*rpc_obj = NULL;
	struct c2_rpc			*rpc_obj_next = NULL;
	struct c2_net_domain		*dom = NULL;
	struct c2_net_transfer_mc	*tm = NULL;
	struct c2_rpc_item		*first_item = NULL;
	struct c2_rpc_frm_buffer	*fb = NULL;
	uint64_t			 rpc_size = 0;
	int				 rc = 0;

	C2_PRE(item != NULL);
	/* POSTING state is reached only by a state succeeded event with
	   FORMING as previous state. */
	C2_PRE(event->se_event == C2_RPC_FRM_INTEVT_STATE_SUCCEEDED);
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->isu_unit_lock));

	frm_sm->isu_frm_state = C2_RPC_FRM_STATE_POSTING;

	/* Iterate over the rpc object list and send all rpc objects
	   to the output component. */
	c2_list_for_each_entry_safe(&frm_sm->isu_rpcobj_formed_list,
			rpc_obj, rpc_obj_next, struct c2_rpc,
			r_linkage) {
		first_item = c2_list_entry((c2_list_first(&rpc_obj->r_items)),
				struct c2_rpc_item, ri_rpcobject_linkage);
		if (frm_sm->isu_curr_rpcs_in_flight <
				frm_sm->isu_max_rpcs_in_flight) {
			tm = c2_rpc_frm_get_tm(first_item);
			dom = tm->ntm_dom;

			/* Allocate a buffer for sending the message.*/
			rc = c2_rpc_frm_buffer_allocate(&fb,
					rpc_obj, frm_sm, dom);
			if (rc < 0) {
				/* Process the next rpc object in the list.*/
				continue;
			}
			/* XXX populate destination EP */
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
				c2_rpc_frm_buffer_deallocate(fb);
				ret = C2_RPC_FRM_INTEVT_STATE_FAILED;
				/* Process the next rpc object in the list.*/
				continue;
			}

			/* Add the buffer to transfer machine.*/
			C2_ASSERT(fb->fb_buffer.nb_ep->nep_dom == tm->ntm_dom);
			res = c2_net_buffer_add(&fb->fb_buffer, tm);
			if (res < 0) {
				c2_rpc_frm_buffer_deallocate(fb);
				/* Process the next rpc object in the list.*/
				continue;
			} else {
				/* Remove the rpc object from rpcobj_formed
				   list.*/
				frm_sm->isu_curr_rpcs_in_flight++;
				c2_list_del(&rpc_obj->r_linkage);
				/* Get a reference on c2_rpc_frm_sm so that
				 it is pinned in memory. */
				c2_ref_get(&frm_sm->isu_ref);
				ret = C2_RPC_FRM_INTEVT_STATE_SUCCEEDED;
			}
		} else {
			ret = C2_RPC_FRM_INTEVT_STATE_FAILED;
			break;
		}
	}
	return ret;
}

/**
   State function for REMOVING state.
 */
enum c2_rpc_frm_int_evt_id c2_rpc_frm_removing_state(
		struct c2_rpc_frm_sm *frm_sm, struct c2_rpc_item *item,
		const struct c2_rpc_frm_sm_event *event)
{
	int		res = 0;

	C2_PRE(item != NULL);
	C2_PRE((event->se_event == C2_RPC_FRM_EXTEVT_RPCITEM_REMOVED) ||
			(event->se_event == C2_RPC_FRM_EXTEVT_RPCITEM_CHANGED));
	C2_PRE(frm_sm != NULL);
	C2_PRE(c2_mutex_is_locked(&frm_sm->isu_unit_lock));

	frm_sm->isu_frm_state = C2_RPC_FRM_STATE_REMOVING;

	/*1. Check the state of incoming rpc item.
	  2. If it is RPC_ITEM_ADDED, deny the request to
	     change/remove rpc item and return state failed event.
	  3. If state is RPC_ITEM_SUBMITTED, the rpc item is still due
	     for formation (not yet gone through checking state) and
	     it will be removed/changed before it is considered for
	     further formation activities.  */
	if (item->ri_state == RPC_ITEM_ADDED) {
		return C2_RPC_FRM_INTEVT_STATE_FAILED;
	} else if (item->ri_state == RPC_ITEM_SUBMITTED) {
		if (event->se_event == C2_RPC_FRM_EXTEVT_RPCITEM_REMOVED) {
			res = rpc_frm_item_remove(frm_sm, item);
		} else if (event->se_event ==
				C2_RPC_FRM_EXTEVT_RPCITEM_CHANGED) {
			res = rpc_frm_change_rpcitem_from_summary_unit(
					frm_sm, item, event->se_pvt);
		}
		if (res == 0) {
			return C2_RPC_FRM_INTEVT_STATE_SUCCEEDED;
		} else {
			return C2_RPC_FRM_INTEVT_STATE_FAILED;
		}
	}
	return C2_RPC_FRM_INTEVT_STATE_SUCCEEDED;
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

