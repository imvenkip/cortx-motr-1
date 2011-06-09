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
#include <string.h>
#ifdef __KERNEL__
#include "ioservice/io_fops_k.h"
#else
#include "ioservice/io_fops_u.h"
#endif

/**
   There will be only one instance of struct c2_rpc_form_item_summary
   since this structure represents data for all endpoints.
 */
struct c2_rpc_form_item_summary		*formation_summary;

/**
   XXX Done for UTing the formation code. The original rpc items cache
   will be populated by grouping component.
 */
struct c2_rpc_form_items_cache		*items_cache;

/**
   Temporary threashold values.
 */
uint64_t				max_msg_size;
uint64_t				max_fragments_size;
uint64_t				max_rpcs_in_flight;

uint64_t				refcnt = 0;

/**
    Forward declarations of local static functions 
 */
static int c2_rpc_form_remove_rpcitem_from_summary_unit(struct
              c2_rpc_form_item_summary_unit *endp_unit,
              struct c2_rpc_item *item);

static int c2_rpc_form_intevt_state_failed(struct
		c2_rpc_form_item_summary_unit *endp_unit,
		struct c2_rpc_item *item, const int state);

static int c2_rpc_form_intevt_state_succeeded(struct
                c2_rpc_form_item_summary_unit *endp_unit,
                struct c2_rpc_item *item, const int state);

static int c2_rpc_form_add_rpcitem_to_summary_unit(
                struct c2_rpc_form_item_summary_unit *endp_unit,
                struct c2_rpc_item *item);

void c2_rpc_form_set_thresholds(uint64_t msg_size, uint64_t max_rpcs,
		uint64_t max_fragments)
{
	max_msg_size = msg_size;
	max_rpcs_in_flight = max_rpcs;
	max_fragments_size = max_fragments;
}

/**
   TBD Add addb data points and log events.
 */

/**     
   Initialization for formation component in rpc.
   This will register necessary callbacks and initialize
   necessary data structures.
 */
int c2_rpc_form_init()
{
	int		i = 0;
	formation_summary = c2_alloc(sizeof(struct c2_rpc_form_item_summary));
	if (formation_summary == NULL) {
		printf("Failed to allocate memory for \
				struct c2_rpc_form_item_summary.\n");
		return -ENOMEM;
	}
	c2_rwlock_init(&formation_summary->is_endp_list_lock);
	c2_list_init(&formation_summary->is_endp_list);
	for (i = 0; i < 256; i++) {
		memset(&thrd_reftrack[i], 0,
				sizeof(struct c2_rpc_form_ut_thread_reftrack));
	}
	n_ut_threads = 0;
	return 0;
}

void add_ref_log()
{
	struct c2_thread_handle		handle;
	int				i = 0;
	bool				found = false;

	C2_ASSERT(n_ut_threads < 256);
	c2_thread_self(&handle);
	for (i = 0; i < n_ut_threads; i++) {
		if (c2_thread_handle_eq(&thrd_reftrack[i].handle, &handle)) {
			found = true;
			thrd_reftrack[i].refcount++;
		}
	}
	if (!found) {
		thrd_reftrack[n_ut_threads].handle = handle;
		thrd_reftrack[n_ut_threads].refcount = 1;
		n_ut_threads++;
	}
}

void dec_ref_log()
{
	struct c2_thread_handle		handle;
	int 				i = 0;
	bool				found = false;

	C2_ASSERT(n_ut_threads <= 256);
	c2_thread_self(&handle);
	for (i = 0; i < n_ut_threads; i++) {
		if (c2_thread_handle_eq(&thrd_reftrack[i].handle, &handle)) {
			found = true;
			thrd_reftrack[i].refcount--;
		}
	}
	C2_ASSERT(found);
}

/**
   Check if refcounts of all endpoints are zero.
 */
bool c2_rpc_form_wait_for_completion()
{
	bool				ret = true;
	int64_t				refcount;
	struct c2_rpc_form_item_summary_unit	*endp_unit = NULL;
	c2_rwlock_read_lock(&formation_summary->is_endp_list_lock);
	c2_list_for_each_entry(&formation_summary->is_endp_list,
			endp_unit, struct c2_rpc_form_item_summary_unit,
			isu_linkage) {
		c2_mutex_lock(&endp_unit->isu_unit_lock);
		refcount = c2_ref_read(&endp_unit->isu_sm.isu_ref);
		if (refcount != 0) {
			ret = false;
			c2_mutex_unlock(&endp_unit->isu_unit_lock);
			break;
		}
		c2_mutex_unlock(&endp_unit->isu_unit_lock);
	}
	c2_rwlock_read_unlock(&formation_summary->is_endp_list_lock);
	return ret;
}

/** 
  Delete the group info list in endpoint unit
 */
static void c2_rpc_form_empty_groups_list(struct c2_list *list)
{
	struct c2_rpc_form_item_summary_unit_group	*group = NULL;
	struct c2_rpc_form_item_summary_unit_group	*group_next = NULL;
	c2_list_for_each_entry_safe(list, group, group_next,
			struct c2_rpc_form_item_summary_unit_group, sug_linkage) {
		c2_list_del(&group->sug_linkage);
		c2_free(group);
	}
	c2_list_fini(list);
}

/** 
  Delete the coalesced items list in endpoint unit
 */
static void c2_rpc_form_empty_coalesced_items_list(struct c2_list *list)
{
	struct c2_rpc_form_item_coalesced 		*coalesced_item = NULL;
	struct c2_rpc_form_item_coalesced 		*coalesced_item_next = NULL;
	struct c2_rpc_form_item_coalesced_member 	*coalesced_member = NULL;
	struct c2_rpc_form_item_coalesced_member 	*coalesced_member_next = NULL;

	c2_list_for_each_entry_safe(list, coalesced_item,
			coalesced_item_next,
			struct c2_rpc_form_item_coalesced, ic_linkage) {
		c2_list_del(&coalesced_item->ic_linkage);
		c2_list_for_each_entry_safe(&coalesced_item->ic_member_list,
				coalesced_member, coalesced_member_next,
				struct c2_rpc_form_item_coalesced_member,
				im_linkage) {
			c2_list_del(&coalesced_member->im_linkage);
			c2_free(coalesced_member);
		}
		c2_free(coalesced_item);
	}
	c2_list_fini(list);
}

/** 
  Delete the rpcobj items list in endpoint unit
 */
static void c2_rpc_form_empty_rpcobj_list(struct c2_list *list)
{
	struct c2_rpc_form_rpcobj		*obj = NULL;
	struct c2_rpc_form_rpcobj		*obj_next = NULL;

	c2_list_for_each_entry_safe(list, obj, obj_next,
			struct c2_rpc_form_rpcobj, ro_linkage) {
		c2_list_del(&obj->ro_linkage);
		c2_free(obj);
	}
	c2_list_fini(list);
}

/** 
  Delete the unformed items list in endpoint unit
 */
static void c2_rpc_form_empty_unformed_list(struct c2_list *list)
{
	struct c2_rpc_item		*item = NULL;
	struct c2_rpc_item		*item_next = NULL;

	c2_list_for_each_entry_safe(list, item, item_next,
			struct c2_rpc_item, ri_unformed_linkage) {
		c2_list_del(&item->ri_unformed_linkage);
		//c2_free(item);
	}
	c2_list_fini(list);
}

/** 
  Delete the fid list in endpoint unit
 */
static void c2_rpc_form_empty_fid_list(struct c2_list *list)
{
	struct c2_rpc_form_fid_summary_member	*fid_member = NULL;
	struct c2_rpc_form_fid_summary_member	*fid_member_next = NULL;
	struct c2_rpc_form_fid_units		*fid_units = NULL;
	struct c2_rpc_form_fid_units		*fid_units_next = NULL;

	c2_list_for_each_entry_safe(list, fid_member, fid_member_next,
			struct c2_rpc_form_fid_summary_member, fsm_linkage) {
		c2_list_del(&fid_member->fsm_linkage);
		c2_list_for_each_entry_safe(&fid_member->fsm_items,
				fid_units, fid_units_next,
				struct c2_rpc_form_fid_units, fu_linkage) {
			c2_list_del(&fid_units->fu_linkage);
			c2_free(fid_units);
		}
		c2_free(fid_member);
	}
	c2_list_fini(list);
}

/**
   Finish method for formation component in rpc.
   This will deallocate all memory claimed by formation
   and do necessary cleanup.
 */
int c2_rpc_form_fini()
{
	/* stime = 10ms */
	c2_time_t			 	 stime;
	struct c2_rpc_form_item_summary_unit	*endp_unit = NULL;
	struct c2_rpc_form_item_summary_unit	*endp_unit_next = NULL;

	/* Set the active flag of all endpoints to false indicating
	   formation component is about to finish.
	   This will help to block all new threads from entering
	   the formation component.*/
	c2_rwlock_read_lock(&formation_summary->is_endp_list_lock);
	c2_list_for_each_entry(&formation_summary->is_endp_list,
			endp_unit, struct c2_rpc_form_item_summary_unit,
			isu_linkage) {
		c2_mutex_lock(&endp_unit->isu_unit_lock);
		endp_unit->isu_form_active = false;
		c2_mutex_unlock(&endp_unit->isu_unit_lock);
	}
	c2_rwlock_read_unlock(&formation_summary->is_endp_list_lock);
	c2_time_set(&stime, 0, 10000000);

	/* Iterate over the list of endpoints until refcounts of all
	   become zero. */
	while(!c2_rpc_form_wait_for_completion()) {
		//c2_nanosleep(stime, NULL);
	}

	/* Delete all endpoint units, all lists within each endpoint unit. */
	c2_rwlock_write_lock(&formation_summary->is_endp_list_lock);
	c2_list_for_each_entry_safe(&formation_summary->is_endp_list,
			endp_unit, endp_unit_next, struct
			c2_rpc_form_item_summary_unit, isu_linkage) {
		c2_mutex_lock(&endp_unit->isu_unit_lock);
		c2_rpc_form_empty_groups_list(&endp_unit->isu_groups_list);
		c2_rpc_form_empty_coalesced_items_list(&endp_unit->
				isu_coalesced_items_list);
		c2_rpc_form_empty_rpcobj_list(&endp_unit->
				isu_rpcobj_formed_list);
		c2_rpc_form_empty_rpcobj_list(&endp_unit->
				isu_rpcobj_checked_list);
		c2_rpc_form_empty_unformed_list(&endp_unit->isu_unformed_list);
		c2_rpc_form_empty_fid_list(&endp_unit->isu_fid_list);
		c2_mutex_unlock(&endp_unit->isu_unit_lock);
		c2_list_del(&endp_unit->isu_linkage);
		c2_free(endp_unit);
	}
	c2_rwlock_write_unlock(&formation_summary->is_endp_list_lock);

	c2_rwlock_fini(&formation_summary->is_endp_list_lock);
	c2_list_fini(&formation_summary->is_endp_list);
	c2_free(formation_summary);
	formation_summary = NULL;
	return 0;
}

/**
   Exit path from a state machine. An incoming thread which executed
   the formation state machine so far, is let go and it will return
   to do its own job.
 */
static void c2_rpc_form_state_machine_exit(struct 
		c2_rpc_form_item_summary_unit *endp_unit)
{
	c2_rwlock_write_lock(&formation_summary->is_endp_list_lock);
	/** Since the behavior is undefined for fini of mutex
	    when the mutex is locked, it is not locked here
	    for endp_unit.*/
	c2_ref_put(&endp_unit->isu_sm.isu_ref);
	dec_ref_log();
	refcnt++;
	printf("Endp reference decreased, refcnt = %lu.\n", refcnt);
	c2_rwlock_write_unlock(&formation_summary->is_endp_list_lock);
}

/**
   Check if the endpoint unit structure is empty.
 */
static bool c2_rpc_form_is_endp_empty(struct c2_rpc_form_item_summary_unit
		*endp_unit)
{
	C2_PRE(endp_unit != NULL);

	if (!c2_list_is_empty(&endp_unit->isu_groups_list))
		return false;
	if (!c2_list_is_empty(&endp_unit->isu_coalesced_items_list))
		return false;
	if (!c2_list_is_empty(&endp_unit->isu_fid_list))
		return false;
	if (!c2_list_is_empty(&endp_unit->isu_unformed_list))
		return false;
	if (!c2_list_is_empty(&endp_unit->isu_rpcobj_formed_list))
		return false;
	if (!c2_list_is_empty(&endp_unit->isu_rpcobj_checked_list))
		return false;
	return true;
}

/**
   Destroy an endpoint structure since it no longer contains
   any rpc items.
 */
static void c2_rpc_form_item_summary_unit_destroy(struct c2_ref *ref)
{
	struct c2_rpc_form_item_summary_unit	*endp_unit;
	struct c2_rpc_form_state_machine	*sm = NULL;
	C2_PRE(ref != NULL);

	sm = container_of(ref, struct c2_rpc_form_state_machine, isu_ref);
	endp_unit = container_of(sm, struct c2_rpc_form_item_summary_unit, 
			isu_sm);
	/* Delete the endp_unit only if all lists are empty.*/
	if (c2_rpc_form_is_endp_empty(endp_unit)) {
		c2_mutex_fini(&endp_unit->isu_unit_lock);
		c2_list_del(&endp_unit->isu_linkage);
		c2_list_fini(&endp_unit->isu_groups_list);
		c2_list_fini(&endp_unit->isu_coalesced_items_list);
		c2_list_fini(&endp_unit->isu_fid_list);
		c2_list_fini(&endp_unit->isu_unformed_list);
		c2_list_fini(&endp_unit->isu_rpcobj_formed_list);
		c2_list_fini(&endp_unit->isu_rpcobj_checked_list);
		endp_unit->isu_endp_id = NULL;
		c2_free(endp_unit);
	}
}

/**
   Add an endpoint structure when the first rpc item gets added
   for an endpoint.
 */
static struct c2_rpc_form_item_summary_unit *c2_rpc_form_item_summary_unit_add(
		const struct c2_net_end_point *endp)
{
	struct c2_rpc_form_item_summary_unit	*endp_unit;

	C2_PRE(endp != NULL);
	endp_unit = c2_alloc(sizeof(struct c2_rpc_form_item_summary_unit));
	if (endp_unit == NULL) {
		printf("Failed to allocate memory to \
				struct c2_rpc_form_item_summary_unit.\n");
		return NULL;
	}
	c2_mutex_init(&endp_unit->isu_unit_lock);
	c2_list_add(&formation_summary->is_endp_list, 
			&endp_unit->isu_linkage);
	c2_list_init(&endp_unit->isu_groups_list);
	c2_list_init(&endp_unit->isu_coalesced_items_list);
	c2_list_init(&endp_unit->isu_fid_list);
	c2_list_init(&endp_unit->isu_unformed_list);
	c2_list_init(&endp_unit->isu_rpcobj_formed_list);
	c2_list_init(&endp_unit->isu_rpcobj_checked_list);
	c2_ref_init(&endp_unit->isu_sm.isu_ref, 1, 
			c2_rpc_form_item_summary_unit_destroy);
	add_ref_log();
	endp_unit->isu_endp_id = (struct c2_net_end_point*)endp;
	endp_unit->isu_sm.isu_endp_state = C2_RPC_FORM_STATE_WAITING;
	endp_unit->isu_form_active = true;
	/* XXX Need appropriate values.*/
	endp_unit->isu_max_message_size = max_msg_size;
	endp_unit->isu_max_fragments_size = max_fragments_size;
	endp_unit->isu_max_rpcs_in_flight = max_rpcs_in_flight;
	endp_unit->isu_curr_rpcs_in_flight = 0;
	return endp_unit;
}

/**
   Return the function pointer to next state given the current state
   and current event as input.
   @param current_state - current state of state machine.
   @param current_event - current event posted to the state machine.
 */
static stateFunc c2_rpc_form_next_state(const int current_state, 
		const int current_event)
{
	C2_PRE(current_state < C2_RPC_FORM_N_STATES);
	C2_PRE(current_event < C2_RPC_FORM_INTEVT_N_EVENTS);
	return c2_rpc_form_stateTable[current_state][current_event];
}

/**
   Get the endpoint given an rpc item.
   This is a placeholder and will be replaced when a concrete
   definition of endpoint is available.
 */
struct c2_net_end_point *c2_rpc_form_get_endpoint(struct c2_rpc_item *item)
{
	struct c2_net_end_point *ep = NULL;
	/* XXX Need to be defined by sessions. */
	ep = &item->ri_endp;
	return ep;
}

bool c2_rpc_form_end_point_equal(struct c2_net_end_point *ep1,
		struct c2_net_end_point *ep2)
{
	bool		status = false;

	if (!memcmp(ep1, ep2, sizeof(struct c2_net_end_point))) {
		status = true;
	}
	return status;
}

/**
   A default handler function for invoking all state functions
   based on incoming event.
   1. Find out the endpoint for given rpc item.
   2. Lock the c2_rpc_form_item_summary_unit data structure.   
   3. Fetch the state for this endpoint and find out the resulting state
   from the state table given this event.
   4. Call the respective state function for resulting state.
   5. Release the lock.
   6. Handle further events on basis of return value of
   recent state function.
   @param item - incoming rpc item needed for external events.
   @param event - event posted to the state machine.
 */
static int c2_rpc_form_default_handler(struct c2_rpc_item *item, 
		struct c2_rpc_form_item_summary_unit *endp_unit, 
		int sm_state, const struct c2_rpc_form_sm_event *sm_event)
{
	struct c2_net_end_point			*endpoint = NULL;
	int					 res = 0;
	int					 prev_state = 0;
	bool					 found = false;
	struct c2_rpc_form_item_summary_unit	*endp = NULL;
	int					 ls = 0;
	struct c2_thread_handle			 handle;
	int					 i = 0;

	C2_PRE(item != NULL);
	C2_PRE(sm_event->se_event < C2_RPC_FORM_INTEVT_N_EVENTS);
	C2_PRE(sm_state <= C2_RPC_FORM_N_STATES);

	printf("default_handler: item state = %d\n", item->ri_state);
	ls = item->ri_state;
	endpoint = c2_rpc_form_get_endpoint(item);

	/* If endpoint unit is NULL, locate it from list in
	   formation summary. If found, take its lock and
	   increment its refcount. If not found, create a
	   new endpoint unit structure. In any case, find out
	   the previous state of endpoint unit. */
	if (endp_unit == NULL) {
		c2_rwlock_write_lock(&formation_summary->is_endp_list_lock);
		c2_list_for_each_entry(&formation_summary->is_endp_list,
				endp, struct c2_rpc_form_item_summary_unit,
				isu_linkage) {
			if (c2_rpc_form_end_point_equal(endp->isu_endp_id,
						endpoint)) {
				found = true;
				break;
			}
		}
		if (found) {
			c2_mutex_lock(&endp->isu_unit_lock);
			c2_ref_get(&endp->isu_sm.isu_ref);
			add_ref_log();
			printf("Endp reference increased.\n");
			c2_rwlock_write_unlock(&formation_summary->
					is_endp_list_lock);
		}
		else {
			/** Add a new endpoint summary unit */
			printf("New endpoint unit created.\n");
			endp = c2_rpc_form_item_summary_unit_add(endpoint);
			c2_mutex_lock(&endp->isu_unit_lock);
			c2_rwlock_write_unlock(&formation_summary->
					is_endp_list_lock);
		}
		prev_state = endp->isu_sm.isu_endp_state;
	}
	else {
		c2_mutex_lock(&endp_unit->isu_unit_lock);
		prev_state = sm_state;
		endp = endp_unit;
	}
	/* If the formation component is not active (form_fini is called)
	   exit the state machine and return back. */
	if (endp->isu_form_active == false) {
		c2_mutex_unlock(&endp->isu_unit_lock);
		c2_rpc_form_state_machine_exit(endp);
		return 0;
	}
	/* Transition to next state.*/
	res = (c2_rpc_form_next_state(prev_state, sm_event->se_event))
		(endp, item, sm_event);
	/* The return value should be an internal event.
	   Assert if its not. */
	C2_ASSERT((res >= C2_RPC_FORM_INTEVT_STATE_SUCCEEDED) ||
			(res <= C2_RPC_FORM_INTEVT_STATE_DONE));
	/* Get latest state of state machine. */
	prev_state = endp->isu_sm.isu_endp_state;
	c2_mutex_unlock(&endp->isu_unit_lock);
	/* Exit point for state machine. */
	if(res == C2_RPC_FORM_INTEVT_STATE_DONE) {
		c2_rpc_form_state_machine_exit(endp);
		return 0;
	}

	if (res == C2_RPC_FORM_INTEVT_STATE_FAILED) {
		/** Post a state failed event. */
		c2_rpc_form_intevt_state_failed(endp, item, prev_state);
	}
	else if (res == C2_RPC_FORM_INTEVT_STATE_SUCCEEDED){
		/** Post a state succeeded event. */
		c2_rpc_form_intevt_state_succeeded(endp, item, prev_state);
	}
	c2_thread_self(&handle);
	for (i = 0; i < n_ut_threads; i++) {
		if (c2_thread_handle_eq(&thrd_reftrack[i].handle, &handle)) {
			if (thrd_reftrack[i].refcount != 0)
				C2_ASSERT(0);
		}
	}
	return 0;
}

/**
   Callback function for addition of an rpc item to the rpc items cache.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_added_in_cache(struct c2_rpc_item *item)
{
	struct c2_rpc_form_sm_event		sm_event;
	int					ls = 0;

	C2_PRE(item != NULL);
	printf("In callback: c2_rpc_form_extevt_rpcitem_added_in_cache\n");
	printf("rpcitem_added_in_cache: item state = %d\n", item->ri_state);
	ls = item->ri_state;
	sm_event.se_event = C2_RPC_FORM_EXTEVT_RPCITEM_ADDED;
	sm_event.se_pvt = NULL;
	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(item, NULL, C2_RPC_FORM_N_STATES,
			&sm_event);
}

/**
   Callback function for deletion of an rpc item from the rpc items cache.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_deleted_from_cache(struct c2_rpc_item *item)
{
	struct c2_rpc_form_sm_event		sm_event;

	C2_PRE(item != NULL);
	printf("In callback: c2_rpc_form_extevt_rpcitem_deleted_from_cache\n");
	sm_event.se_event = C2_RPC_FORM_EXTEVT_RPCITEM_REMOVED;
	sm_event.se_pvt = NULL;
	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(item, NULL, C2_RPC_FORM_N_STATES,
			&sm_event);
}

/**
   Callback function for change in parameter of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_changed(struct c2_rpc_item *item,
		int field_type, void *pvt)
{
	struct c2_rpc_form_sm_event		sm_event;
	struct c2_rpc_form_item_change_req	req;

	C2_PRE(item != NULL);
	C2_PRE(pvt != NULL);
	C2_PRE(field_type < C2_RPC_ITEM_N_CHANGES);

	printf("In callback: c2_rpc_form_extevt_rpcitem_changed\n");

	sm_event.se_event = C2_RPC_FORM_EXTEVT_RPCITEM_CHANGED;
	req.field_type = field_type;
	req.value = pvt;
	sm_event.se_pvt = &req;
	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(item, NULL, C2_RPC_FORM_N_STATES,
			&sm_event);
}

/**
   Callback function for reply received of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_reply_received(
		struct c2_rpc_item *reply_item,
		struct c2_rpc_item *req_item)
{
	struct c2_rpc_form_sm_event		sm_event;

	C2_PRE(req_item != NULL);
	C2_PRE(reply_item != NULL);
	printf("In callback: c2_rpc_form_extevt_rpcitem_reply_received\n");
	sm_event.se_event = C2_RPC_FORM_EXTEVT_RPCITEM_REPLY_RECEIVED;
	sm_event.se_pvt = NULL;
	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(req_item, NULL, C2_RPC_FORM_N_STATES,
			&sm_event);
}

/**
   Callback function for deadline expiry of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_deadline_expired(struct c2_rpc_item *item)
{
	struct c2_rpc_form_sm_event		sm_event;

	C2_PRE(item != NULL);
	printf("In callback: c2_rpc_form_extevt_rpcitem_deadline_expired\n");
	sm_event.se_event = C2_RPC_FORM_EXTEVT_RPCITEM_TIMEOUT;
	sm_event.se_pvt = NULL;
	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(item, NULL, C2_RPC_FORM_N_STATES,
			&sm_event);
}

/**
   Callback function for successful completion of a state.
   Call the default handler function. Depending upon the
   input state, the default handler will invoke the next state
   for state succeeded event.
   @param state - previous state of state machine.
 */
static int c2_rpc_form_intevt_state_succeeded(struct
		c2_rpc_form_item_summary_unit *endp_unit,
		struct c2_rpc_item *item, const int state)
{
	struct c2_rpc_form_sm_event		sm_event;

	C2_PRE(endp_unit != NULL);
	C2_PRE(item != NULL);
	printf("In callback: c2_rpc_form_intevt_state_succeeded\n");
	sm_event.se_event = C2_RPC_FORM_INTEVT_STATE_SUCCEEDED;
	sm_event.se_pvt = NULL;
	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(item, endp_unit, state, &sm_event);
}

/**
   Callback function for failure of a state.
   Call the default handler function. Depending upon the
   input state, the default handler will invoke the next state
   for state failed event.
   @param state - previous state of state machine.
 */
static int c2_rpc_form_intevt_state_failed(struct
		c2_rpc_form_item_summary_unit *endp_unit,
		struct c2_rpc_item *item, const int state)
{
	struct c2_rpc_form_sm_event		sm_event;

	C2_PRE(endp_unit != NULL);
	C2_PRE(item != NULL);
	sm_event.se_event = C2_RPC_FORM_INTEVT_STATE_FAILED;
	sm_event.se_pvt = NULL;
	printf("In callback: c2_rpc_form_intevt_state_failed\n");
	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(item, endp_unit, state, &sm_event);
}

/**
   Call the completion callbacks for member rpc items of
   a coalesced rpc item.
 */
static int c2_rpc_form_item_coalesced_reply_post(struct
		c2_rpc_form_item_summary_unit *endp_unit,
		struct c2_rpc_form_item_coalesced *coalesced_struct)
{
	//int						 rc = 0;
	struct c2_rpc_form_item_coalesced_member	*member;
	struct c2_rpc_form_item_coalesced_member	*next_member;

	C2_PRE(endp_unit != NULL);
	C2_PRE(coalesced_struct != NULL);

	c2_list_for_each_entry_safe(&coalesced_struct->ic_member_list, 
			member, next_member, 
			struct c2_rpc_form_item_coalesced_member, 
			im_linkage) {
		//member->im_member_item->rio_replied(member->im_member, rc);
		c2_list_del(&member->im_linkage);
		//c2_rpc_item_replied(&member->im_member_item);
		coalesced_struct->ic_nmembers--;
	}
	//coalesced_struct->ic_resultant_item->ri_type->rit_ops->rio_replied(
	//		coalesced_struct->ic_resultant_item);
	C2_ASSERT(coalesced_struct->ic_nmembers == 0);
	c2_list_del(&coalesced_struct->ic_linkage);
	//c2_rpc_item_replied(coalesced_struct->ic_resultant_item);
	c2_list_fini(&coalesced_struct->ic_member_list);
	c2_free(coalesced_struct);
	return 0;
}

/**
   State function for WAITING state.
   endp_unit is locked.
 */
int c2_rpc_form_waiting_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, const struct c2_rpc_form_sm_event
		*event)
{
	C2_PRE(item != NULL);
	C2_PRE((event->se_event == C2_RPC_FORM_INTEVT_STATE_SUCCEEDED) || 
			(event->se_event == C2_RPC_FORM_INTEVT_STATE_FAILED));
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));

	printf("In state: waiting\n");
	endp_unit->isu_sm.isu_endp_state = C2_RPC_FORM_STATE_WAITING;
	/* Internal events will invoke a nop from waiting state. */

	return C2_RPC_FORM_INTEVT_STATE_DONE;
}

/**
   Callback used to trigger the "deadline expired" event
   for an rpc item.
 */
unsigned long c2_rpc_form_item_timer_callback(unsigned long data)
{
	struct c2_rpc_item	*item;

	item = (struct c2_rpc_item*)data;
	if (item->ri_state == RPC_ITEM_SUBMITTED) {
		c2_rpc_form_extevt_rpcitem_deadline_expired(item);
	}
	return 0;
}

/**
   Change the data of an rpc item embedded within the endpoint unit
   structure.
 */
static int c2_rpc_form_change_rpcitem_from_summary_unit(struct 
		c2_rpc_form_item_summary_unit *endp_unit, 
		struct c2_rpc_item *item, void *pvt)
{
	int					 res = 0;
	struct c2_rpc_form_item_change_req	*chng_req = NULL;
	int					 field_type = 0;

	C2_PRE(endp_unit != NULL);
	C2_PRE(item != NULL);
	C2_PRE(pvt != NULL);
	C2_PRE(item->ri_state == RPC_ITEM_SUBMITTED);

	chng_req = (struct c2_rpc_form_item_change_req*)pvt;
	field_type = chng_req->field_type;

	res = c2_rpc_form_remove_rpcitem_from_summary_unit(endp_unit, item);
	if (res != 0) {
		printf("Failed to remove data of an rpc item \
				from summary unit.\n");
		return -1;
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

	};
	res = c2_rpc_form_add_rpcitem_to_summary_unit(endp_unit, item);
	if (res != 0) {
		printf("Failed to add data of an rpc item to summary unit.\n");
		return -1;
	}
	return res;
}

/**
   Remove the data of an rpc item embedded within the endpoint unit
   structure.
 */
static int c2_rpc_form_remove_rpcitem_from_summary_unit(
		struct c2_rpc_form_item_summary_unit *endp_unit, 
		struct c2_rpc_item *item)
{
	struct c2_rpc_form_item_summary_unit_group	 *summary_group = NULL;
	bool						  found = false;

	C2_PRE(item != NULL);
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));
	C2_PRE(item->ri_state == RPC_ITEM_SUBMITTED);

	c2_list_for_each_entry(&endp_unit->isu_groups_list, 
			summary_group, 
			struct c2_rpc_form_item_summary_unit_group, 
			sug_linkage) {
		if (summary_group->sug_group == item->ri_group) {
			found = true;
			break;
		}
	}
	if (!found) {
		return 0;
	}

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
	/*XXX struct c2_rpc_item_type_ops will have a rio_item_size
	 method to find out size of rpc item. */
	summary_group->sug_total_size -=  c2_rpc_form_item_size(item);
	/* summary_group->sug_total_size -= 
	   item->ri_type->rit_ops->rio_item_size(item); */
	summary_group->sug_avg_timeout = 
		((summary_group->sug_nitems * summary_group->sug_avg_timeout) 
		 - item->ri_deadline) / (summary_group->sug_nitems);

	return 0;
}

/**
   Sort the c2_rpc_form_item_summary_unit_group structs according to
   increasing value of average timeout.
 */
static int c2_rpc_form_summary_groups_sort(
		struct c2_rpc_form_item_summary_unit *endp_unit, 
		struct c2_rpc_form_item_summary_unit_group *summary_group)
{
	bool						 placed = false;
	struct c2_rpc_form_item_summary_unit_group	*sg = NULL;
	struct c2_rpc_form_item_summary_unit_group	*sg_next = NULL;

	C2_PRE(endp_unit != NULL);
	C2_PRE(summary_group != NULL);

	c2_list_del(&summary_group->sug_linkage);
	/* Do a simple incremental search for a group having
	   average timeout value bigger than that of given group. */
	c2_list_for_each_entry_safe(&endp_unit->isu_groups_list, sg, sg_next,
			struct c2_rpc_form_item_summary_unit_group, 
			sug_linkage) {
		if (sg->sug_avg_timeout > summary_group->sug_avg_timeout) {
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
 */
static int c2_rpc_form_add_rpcitem_to_summary_unit(
		struct c2_rpc_form_item_summary_unit *endp_unit, 
		struct c2_rpc_item *item)
{
	int						  res = 0;
	struct c2_rpc_form_item_summary_unit_group	 *summary_group = NULL;
	bool						  found = false;
	//struct c2_timer					 *item_timer = NULL;

	C2_PRE(item != NULL);
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));
	C2_PRE(item->ri_state == RPC_ITEM_SUBMITTED);

	/*
	  1. Search for the group of rpc item in list of rpc groups in
	     summary_unit.
	  2. If found, add data from rpc item like priority, deadline,
	     size, rpc item type.
	     XXX To find out the size of rpc item, find out the size of
	     fop it is carrying, as well as size of fop structure itself.
	  3. If not found, create a c2_rpc_form_item_summary_unit_group
	     structure and fill necessary data.
	 */

	//c2_list_link_init(&item->ri_unformed_linkage);
	c2_list_add(&endp_unit->isu_unformed_list,
			&item->ri_unformed_linkage);
	printf("Inside c2_rpc_form_add_rpcitem_to_summary_unit, isu_groups_list_length = %lu\n", c2_list_length(&endp_unit->isu_groups_list));
	c2_list_for_each_entry(&endp_unit->isu_groups_list, 
			summary_group, 
			struct c2_rpc_form_item_summary_unit_group, 
			sug_linkage) {
		if (summary_group->sug_group == item->ri_group) {
			found = true;
			break;
		}
	}
	if (!found) {
		summary_group = c2_alloc(
				sizeof(struct 
					c2_rpc_form_item_summary_unit_group));
		if(summary_group == NULL) {
			printf("Failed to allocate memory for a new \
					c2_rpc_form_item_summary_unit_group \
					structure.\n");
			return -ENOMEM;
		}
		c2_list_link_init(&summary_group->sug_linkage);
		c2_list_add(&endp_unit->isu_groups_list, &summary_group->sug_linkage);
		printf("New summary unit group added.\n");
		if (item->ri_group == NULL) {
			printf("Creating a c2_rpc_form_item_summary_unit_group \
					struct for items with no groups.\n");
		}
		summary_group->sug_group = item->ri_group;
		printf("Length of groups list = %lu\n", c2_list_length(&endp_unit->isu_groups_list));
		printf("Length of unformed list = %lu\n", c2_list_length(&endp_unit->isu_unformed_list));
	}

	printf("Inside c2_rpc_form_add_rpcitem_to_summary_unit, grpid = %d\n", item->ri_group->rg_grpid);
	if(item->ri_group != NULL) {
		summary_group->sug_expected_items = item->ri_group->rg_expected;
	}
	if (item->ri_prio == C2_RPC_ITEM_PRIO_MAX) {
		summary_group->sug_priority_items++;
	}
	/*XXX struct c2_rpc_item_type_ops will have a rio_item_size
	 method to find out size of rpc item. */
	summary_group->sug_total_size += c2_rpc_form_item_size(item);
	/* summary_group->sug_total_size += 
	   item->ri_type->rit_ops->rio_item_size(item); */
	summary_group->sug_avg_timeout = 
		((summary_group->sug_nitems * summary_group->sug_avg_timeout) 
		 + item->ri_deadline) / (summary_group->sug_nitems + 1);
	summary_group->sug_nitems++;
	/* Put the corresponding c2_rpc_form_item_summary_unit_group
	   struct in its correct position on least value first basis of
	   average timeout of group. */
	if (item->ri_group != NULL) {
		res = c2_rpc_form_summary_groups_sort(endp_unit, summary_group);
	}
	/* Special handling for group of items which belong to no group,
	   so that they are handled as the last option for formation. */
	else {
		c2_list_move_tail(&endp_unit->isu_groups_list, 
				&summary_group->sug_linkage);
	}

	/* Initialize the timer only when the deadline value is non-zero
	   i.e. dont initialize the timer for URGENT items */
#if 0
	if(item->ri_deadline != 0) {
		/* Init and start the timer for rpc item. */
		item_timer = c2_alloc(sizeof(struct c2_timer));
		if (item_timer == NULL) {
			printf("Failed to allocate memory for a new c2_timer\
					struct.\n");
			return -ENOMEM;
		}
		/* C2_TIMER_SOFT creates a different thread to handle the
		   callback. */
#if 0
		c2_timer_init(item_timer, C2_TIMER_SOFT, item->ri_deadline, 1, 
				c2_rpc_form_item_timer_callback, 
				(unsigned long)item);
		res = c2_timer_start(item_timer);
		if (res != 0) {
			printf("Failed to start the timer for rpc item.\n");
			return -1;
		}
		item->ri_timer = *item_timer;
#endif
	}
#endif
	/* Assumption: c2_rpc_item_type_ops methods can access
	   the fields of corresponding fop. */
	return 0;
}

/**
   State function for UPDATING state.
 */
int c2_rpc_form_updating_state(struct c2_rpc_form_item_summary_unit *endp_unit,
		struct c2_rpc_item *item, const struct c2_rpc_form_sm_event
		*event)
{
	int		res = 0;
	int		ret = 0;
	int		ls = 0;

	C2_PRE(item != NULL);
	C2_PRE(event->se_event == C2_RPC_FORM_EXTEVT_RPCITEM_ADDED);
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));

	printf("In state: updating\n");
	printf("updating_state: item state = %d\n", item->ri_state);
	ls = item->ri_state;
	endp_unit->isu_sm.isu_endp_state = C2_RPC_FORM_STATE_UPDATING;

	res = c2_rpc_form_add_rpcitem_to_summary_unit(endp_unit, item);
	/* If rpcobj_formed_list already contains formed rpc objects,
	   send them as well.*/
	if ((res == 0) || ((!c2_list_is_empty(
			&endp_unit->isu_rpcobj_formed_list)))) {
		ret = C2_RPC_FORM_INTEVT_STATE_SUCCEEDED;
	}
	else {
		ret = C2_RPC_FORM_INTEVT_STATE_FAILED;
	}

	return ret;
}

/**
   Add an rpc item to the formed list of an rpc object.
 */
static int c2_rpc_form_item_add_to_forming_list(
		struct c2_rpc_form_item_summary_unit *endp_unit, 
		struct c2_rpc_item *item,
		uint64_t *rpcobj_size, uint64_t *nfragments, 
		struct c2_rpc *rpc)
{
	uint64_t			 item_size = 0;
	//struct c2_update_stream		*item_update_stream = NULL;
	//bool				 update_stream_busy = false;
	bool				 io_op = false;
	uint64_t			 current_fragments = 0;
	//c2_time_t			 now;
	int				 ls = 0;

	C2_PRE(endp_unit != NULL);
	C2_PRE(item != NULL);
	C2_PRE(rpcobj_size != NULL);

	printf("Inside add_to_forming_list \n");
	ls = item->ri_state;

	/* io_op = item->ri_type->rit_ops->rio_is_io_req(item); */
	io_op = c2_rpc_item_is_io_req(item); 
	if (io_op) {
		/* current_fragments = 
		   item->ri_type->rit_ops->rio_get_io_fragment_count(item); */
		current_fragments = c2_rpc_item_get_io_fragment_count(item);
		if ((*nfragments + current_fragments) > 
				endp_unit->isu_max_fragments_size) {
			return 0;
		}
	}
	/* item_size = item->ri_type->rit_ops->rio_item_size(item); */
	item_size = c2_rpc_form_item_size(item);
		
	printf("1. rpc object size updated. rpcobjsize = %lu, item_size = %lu\n", *rpcobj_size, item_size);
	if (((*rpcobj_size + item_size) < endp_unit->isu_max_message_size)) {
		/** XXX Need this API from rpc-core. */
		//item_update_stream = c2_rpc_get_update_stream(item);
		/** XXX Need this API from rpc-core. */
		//update_stream_busy = c2_rpc_get_update_stream_status(
				//item_update_stream);
		//if(!update_stream_busy) {
			/** XXX Need this API from rpc-core. */
			//c2_rpc_set_update_stream_status(item_update_stream, 
			//		BUSY);
			/* XXX Need a rpbobject_linkage in c2_rpc_item. */
			c2_list_add(&rpc->r_items, 
					&item->ri_rpcobject_linkage);
			*rpcobj_size += item_size;
			printf("2. rpc object size updated. rpcobjsize = %lu, item_size = %lu\n", *rpcobj_size, item_size);
			printf("New rpcobj size = %lu\n", *rpcobj_size);
			*nfragments += current_fragments;
			c2_rpc_form_remove_rpcitem_from_summary_unit(endp_unit,
					item);
			item->ri_state = RPC_ITEM_ADDED;
#if 0
			if(item->ri_deadline != 0) {
				c2_time_now(&now);
				if (c2_time_after(item->ri_timer.t_expire, now)) {
					item->ri_deadline = 
						c2_time_sub(item->ri_timer.t_expire, 
								now);
					//c2_timer_stop(&item->ri_timer);
				}
				//c2_timer_fini(&item->ri_timer);
			}
#endif
			c2_list_del(&item->ri_unformed_linkage);
		//}
			printf("No of elements in rpcobject list = %lu.\n",
					c2_list_length(&rpc->r_items));
		return 0;
	}
	else {
		return -1;
	}
}

/**
   Coalesce the multiple write IO vectors into one.
 */
static int c2_rpc_form_coalesce_writeio_vector(struct c2_fop_io_vec *item_vec,
		struct c2_list *aggr_list, int *nsegs)
{
	int						 i = 0;
	struct c2_rpc_form_write_segment		*write_seg = NULL;
	struct c2_rpc_form_write_segment		*write_seg_next = NULL;
	struct c2_rpc_form_write_segment		*new_seg = NULL;
	bool						 list_empty = true;

	C2_PRE(item_vec != NULL);
	C2_PRE(aggr_list != NULL);
	C2_PRE(nsegs != NULL);
	printf("coalesce_writeio_vector entered.\n");

	for (i = 0; i < item_vec->iov_count; i++) {
		c2_list_for_each_entry_safe(aggr_list, write_seg,
				write_seg_next,
				struct c2_rpc_form_write_segment, ws_linkage) {
			list_empty = false;
			/* If off1 + count1 = off2, then merge two segments.*/
			if ((item_vec->iov_seg[i].f_offset + 
					item_vec->iov_seg[i].f_buf.f_count) 
					== write_seg->ws_seg.f_offset) {
				write_seg->ws_seg.f_offset = 
					item_vec->iov_seg[i].f_offset;
				write_seg->ws_seg.f_buf.f_count += 
					item_vec->iov_seg[i].f_buf.f_count;
			}
			/* If off2 + count2 = off1, then merge two segments.*/
			else if (write_seg->ws_seg.f_offset +
					write_seg->ws_seg.f_buf.f_count
					== item_vec->iov_seg[i].f_offset) {
				write_seg->ws_seg.f_buf.f_count +=
					item_vec->iov_seg[i].f_buf.f_count;
			}
			/* If (off1 + count1) < off2, add a new segment 
			   in the merged list. */
			else if ((item_vec->iov_seg[i].f_offset + 
					item_vec->iov_seg[i].f_buf.f_count) <
					write_seg->ws_seg.f_offset) {
				new_seg = c2_alloc(sizeof(struct 
						c2_rpc_form_write_segment));
				if (new_seg == NULL) {
					printf("Failed to allocate memory \
						for struct \
						c2_rpc_form_write_segment.\n");
					return -ENOMEM;
				}
				(*nsegs)++;
				c2_list_add_before(&write_seg->ws_linkage,
						&new_seg->ws_linkage);
			}
		}
		if ((&write_seg->ws_linkage == (void*)aggr_list)
				|| list_empty) {
			C2_ALLOC_PTR(new_seg);
			if (new_seg == NULL) {
				printf("Failed to allocate memory \
						for struct \
						c2_rpc_form_write_segment.\n");
				return -ENOMEM;
			}
			c2_list_link_init(&new_seg->ws_linkage);
			c2_list_add_tail(aggr_list, &new_seg->ws_linkage);
			(*nsegs)++;
		}
	}
	printf("Length of aggr list for write vector= %lu\n",
			c2_list_length(aggr_list));
	return 0;
}

/**
   Coalesce the multiple read IO vectors into one.
 */
static int c2_rpc_form_coalesce_readio_vector(
		struct c2_fop_segment_seq *item_vec,
		struct c2_list *aggr_list, int *res_segs)
{
	int						 i = 0;
	struct c2_rpc_form_read_segment			*read_seg = NULL;
	struct c2_rpc_form_read_segment			*read_seg_next = NULL;
	struct c2_rpc_form_read_segment			*new_seg = NULL;
	bool						 list_empty = true;

	C2_PRE(item_vec != NULL);
	C2_PRE(aggr_list != NULL);
	C2_PRE(res_segs != NULL);

	/* For each segment from incoming IO vector, check if it can
	   be merged with any of the existing segments from aggr_list.
	   If yes, merge it else, add a new entry in aggr_list. */
	for (i = 0; i < item_vec->fs_count; i++) {
		c2_list_for_each_entry_safe(aggr_list, read_seg,
				read_seg_next,
				struct c2_rpc_form_read_segment, rs_linkage) {
			list_empty = false;
			/* If off1 + count1 = off2, then merge two segments.*/
			if ((item_vec->fs_segs[i].f_offset + 
					item_vec->fs_segs[i].f_count) == 
					read_seg->rs_seg.f_offset) {
				read_seg->rs_seg.f_offset = 
					item_vec->fs_segs[i].f_offset;
				read_seg->rs_seg.f_count += 
					item_vec->fs_segs[i].f_count;
			}
			/* If off2 + count2 = off1, then merge two segments.*/
			else if ((read_seg->rs_seg.f_offset +
					read_seg->rs_seg.f_count) ==
					item_vec->fs_segs[i].f_offset) {
				read_seg->rs_seg.f_count +=
					item_vec->fs_segs[i].f_count;
			}
			/* If (off1 + count1) < off2, add a new segment 
			   in the merged list. */
			else if ((item_vec->fs_segs[i].f_offset + 
					item_vec->fs_segs[i].f_count) < 
					read_seg->rs_seg.f_offset) {
				new_seg = c2_alloc(sizeof(struct 
						c2_rpc_form_read_segment));
				if (new_seg == NULL) {
					printf("Failed to allocate memory \
						for struct \
						c2_rpc_form_read_segment.\n");
					return -ENOMEM;
				}
				(*res_segs)++;
				c2_list_add_before(&read_seg->rs_linkage, 
						&new_seg->rs_linkage);
			}
		}
		if ((&read_seg->rs_linkage == (void*)aggr_list) || list_empty) {
			C2_ALLOC_PTR(new_seg);
			if (new_seg == NULL) {
				printf("Failed to allocate memory \
						for struct \
						c2_rpc_form_read_segment.\n");
				return -ENOMEM;
			}
			c2_list_link_init(&new_seg->rs_linkage);
			c2_list_add_tail(aggr_list, &new_seg->rs_linkage);
			(*res_segs)++;
		}
	}
	printf("Length of aggr list for read vector= %lu\n",
			c2_list_length(aggr_list));
	return 0;
}

/**
   This is a rpc_item_type_op and is registered with associated
   rpc item.
   Coalesce IO vectors from a list of rpc items into one
   and arrange the IO vector in increasing order of file offset.
 */
static int c2_rpc_form_io_items_coalesce(struct c2_rpc_form_item_coalesced 
		*coalesced_item)
{
	int						 res = 0;
	int						 opcode;
	struct c2_rpc_form_item_coalesced_member	*member = NULL;
	struct c2_rpc_item				*item = NULL;
	struct c2_fop_segment_seq			*read_vec = NULL;
	struct c2_fop_segment_seq			*item_read_vec = NULL;
	int						 curr_segs = 0;
	int						 i = 0;
	struct c2_fop_io_vec				*write_vec = NULL;
	struct c2_list					 aggr_vec_list;
	struct c2_rpc_form_read_segment			*read_seg = NULL;
	struct c2_rpc_form_read_segment			*read_seg_next = NULL;
	struct c2_rpc_form_write_segment		*write_seg = NULL;
	struct c2_rpc_form_write_segment		*write_seg_next = NULL;
	struct c2_fop_io_vec				*item_write_vec = NULL;

	C2_PRE(coalesced_item != NULL);

	c2_list_init(&aggr_vec_list);
	opcode = coalesced_item->ic_op_intent;
	c2_list_for_each_entry(&coalesced_item->ic_member_list, 
			member, 
			struct c2_rpc_form_item_coalesced_member, 
			im_linkage) {
		/* C2_PRE((member->im_member_item.ri_type->
		   rit_ops->rio_io_get_opcode(&member->im_member_item)) 
		   == opcode); */
		C2_PRE((c2_rpc_item_io_get_opcode(member->im_member_item)) 
				== opcode);
	}

	/* 1. Retrieve IO vector from the FOP of each rpc item
	   in the list. */
	/* 2. Create a new IO vector and put all IO segments from
	   the retrieved IO vector in the new IO vector in a
	   sorted fashion.*/
	c2_list_for_each_entry(&coalesced_item->ic_member_list, member,
			struct c2_rpc_form_item_coalesced_member, im_linkage) {
		item = member->im_member_item;
		if (opcode == C2_RPC_FORM_IO_READ) {
			/* item_read_vec = item.ri_type->rit_ops->
			   rio_io_get_vector(&item);*/
			item_read_vec = c2_rpc_item_read_get_vector(item);
			res = c2_rpc_form_coalesce_readio_vector(item_read_vec,
					&aggr_vec_list, &curr_segs);
		}
		else if (opcode == C2_RPC_FORM_IO_WRITE) {
			/* item_write_vec = item.ri_type->rit_ops->
			   rio_io_get_vector(&item);*/
			item_write_vec = c2_rpc_item_write_get_vector(item);
			res = c2_rpc_form_coalesce_writeio_vector(
					item_write_vec, &aggr_vec_list, 
					&curr_segs);
		}
	}
	/* 3. Once the whole IO vector is aggregated, create a new
	   read FOP and put the IO vector in the new FOP.*/
	/* 4. For read requests, the IO segments which are subset of
	   any existing IO segments in the new IO vector
	   will not be included. */
	/* 5. Since read requests can be coalesced, total number of
	   IO segments can reduce. Hence a new IO vector is allocated
	   with exact number of segments needed, data is copied and
	   old IO vector is deleted. */
	member = c2_list_entry(c2_list_first(&coalesced_item->ic_member_list),
			struct c2_rpc_form_item_coalesced_member, im_linkage);
	if (opcode == C2_RPC_FORM_IO_READ) {
		read_vec = c2_alloc(sizeof(struct c2_fop_segment_seq));
		if (read_vec == NULL) {
			printf("Failed to allocate memory for \
					struct c2_fop_segment_seq.\n");
			return -ENOMEM;
		}
		C2_ASSERT(curr_segs == c2_list_length(&aggr_vec_list));
		C2_ALLOC_ARR(read_vec->fs_segs, curr_segs);
		if (read_vec->fs_segs == NULL) {
			printf("Failed to allocate memory for \
					struct c2_fop_segment.\n");
			return -ENOMEM;
		}
		c2_list_for_each_entry_safe(&aggr_vec_list, read_seg, 
				read_seg_next, struct c2_rpc_form_read_segment, 
				rs_linkage) {
			read_vec->fs_segs[i] = read_seg->rs_seg;
			c2_list_del(&read_seg->rs_linkage);
			c2_free(read_seg);
			i++;
		}
		/* res = member->im_member_item->ri_type->rit_ops->
		   rio_get_new_io_item(
				&member->im_member_item,
				&coalesced_item->ic_resultant_item, read_vec);*/
		res = c2_rpc_item_get_new_read_item(
				member->im_member_item,
				&coalesced_item->ic_resultant_item, read_vec);
		if(res != 0){
			return -1;
		}
	}
	else if (opcode == C2_RPC_FORM_IO_WRITE) {
		write_vec = c2_alloc(sizeof(struct c2_fop_io_vec));
		if (write_vec == NULL) {
			printf("Failed to allocate memory for \
					struct c2_fop_io_vec.\n");
			return -ENOMEM;
		}
		C2_ASSERT(curr_segs == c2_list_length(&aggr_vec_list));
		C2_ALLOC_ARR(write_vec->iov_seg, curr_segs);
		if (write_vec->iov_seg == NULL) {
			printf("Failed to allocate memory for \
					struct c2_fop_io_seg.\n");
			return -ENOMEM;
		}
		c2_list_for_each_entry_safe(&aggr_vec_list, write_seg, 
				write_seg_next,
				struct c2_rpc_form_write_segment, ws_linkage) {
			write_vec->iov_seg[i] = write_seg->ws_seg;
			c2_list_del(&write_seg->ws_linkage);
			c2_free(write_seg);
			i++;
		}
		/* res = member->im_member_item->ri_type->rit_ops->
		   rio_get_new_io_item(
				&member->im_member_item,
				&coalesced_item->ic_resultant_item, write_vec);*/
		res = c2_rpc_item_get_new_write_item(
				member->im_member_item,
				&coalesced_item->ic_resultant_item, write_vec);
		if(res != 0){
			return -1;
		}
	}
	c2_list_fini(&aggr_vec_list);
	C2_POST(coalesced_item->ic_resultant_item != NULL);
	/* 6. The newly created rpc item is placed with struct
	   c2_rpc_form_item_coalesced.*/
	return 0;
}

/**
   Coalesce possible rpc items and replace them by a aggregated
   rpc item.
 */
static int c2_rpc_form_items_coalesce(
		struct c2_rpc_form_item_summary_unit *endp_unit,
		struct c2_rpc *rpc, uint64_t *rpcobj_size)
{
	int						 res = 0;
	int						 nfsm = 0;
	int						 ncoalesced = 0;
	struct c2_rpc_item				*item = NULL;
	struct c2_fid					 fid;
	int						 item_rw;
	struct c2_rpc_form_fid_units			*fid_unit = NULL;
	bool						 fid_found = false;
	struct c2_rpc_form_fid_summary_member		*fid_member = NULL;
	struct c2_rpc_form_fid_summary_member		*fid_member_next = NULL;
	uint64_t					 item_size = 0;
	struct c2_rpc_form_item_coalesced_member	*item_member = NULL;
	struct c2_rpc_form_item_coalesced		*coalesced_item = NULL;
	struct c2_rpc_form_fid_units			*fsm_item = NULL;
	struct c2_rpc_form_fid_units			*fsm_item_next = NULL;
	struct c2_rpc_form_item_coalesced_member	*coalesced_member =
		NULL;

	C2_PRE(endp_unit != NULL);
	C2_PRE(rpc != NULL);
	C2_PRE(rpcobj_size != NULL);

	/* 1. Iterate over the forming list to find out fids from
	      IO requests it contains. */
	/* 2. For each found fid, check its read and write list to
	      see if it belongs to any of the selected rpc groups. */
	/* 3. For every unique combination of fid and intent(read/write)
	      locate/create a struct c2_rpc_form_fid_summary_member and put
	      it in endp_unit->isu_fid_list. */
	printf("items_coalesce entered.\n");
	c2_list_for_each_entry(&rpc->r_items, item, 
			struct c2_rpc_item, ri_rpcobject_linkage) {
		/* if (item->ri_type->rit_ops->rio_is_io_req(item)) */
		if(c2_rpc_item_is_io_req(item)) {
			/*fid = item->ri_type->rit_ops->rio_io_get_fid(item); */
			fid = c2_rpc_item_io_get_fid(item);
			/* item_rw = item->ri_type->rit_ops->
			   rio_io_get_opcode(item);*/
			item_rw = c2_rpc_item_io_get_opcode(item);
			/* item_size = item->ri_type->rit_ops->
			   rio_item_size(item); */
			item_size = c2_rpc_form_item_size(item);
			/* If item belongs to an update stream, do not
			   consider it for coalescing.*/
			/* XXX need to write c2_rpc_get_update_stream.*/
			if (c2_rpc_get_update_stream(item) != NULL) {
				continue;
			}
			fid_found = false;
			c2_list_for_each_entry(&endp_unit->isu_fid_list,
					fid_member, 
					struct c2_rpc_form_fid_summary_member, 
					fsm_linkage) {
				if ((fid_member->fsm_fid.f_container ==
						 fid.f_container) &&
						(fid_member->fsm_fid.f_key ==
						 fid.f_key) &&
						(fid_member->fsm_rw 
						 == item_rw)) {
					fid_found = true;
					break;
				}
			}
			if (!fid_found) {
				fid_member = c2_alloc(sizeof(struct 
						c2_rpc_form_fid_summary_member));
				if (fid_member == NULL) {
					printf("Failed to allocate memory \
						for struct \
						c2_rpc_form_fid_summary_member \
						\n");
					return -ENOMEM;
				}
				fid_member->fsm_rw = item_rw;
				fid_member->fsm_fid = fid;
				c2_list_init(&fid_member->fsm_items);
				c2_list_add(&endp_unit->isu_fid_list,
						&fid_member->fsm_linkage);
				nfsm++;
			}
			fid_member->fsm_nitems++;
			fid_unit = c2_alloc(sizeof(struct 
						c2_rpc_form_fid_units));
			if (fid_unit == NULL) {
				printf("Failed to allocate memory for \
						struct c2_rpc_form_fid_units \
						\n");
				return -ENOMEM;
			}
			fid_unit->fu_item = item;
			c2_list_link_init(&fid_unit->fu_linkage);
			c2_list_add(&fid_member->fsm_items, 
					&fid_unit->fu_linkage);
			fid_member->fsm_total_size += item_size;
		}
	}
	printf("Total %d fid_summary_member structures added.\n", nfsm);
	/* 4. Now, traverse the endp_unit->isu_fid_list and coalesce the
	      rpc items from the list of rpc items in each struct
	      c2_rpc_form_fid_summary_member. */
	c2_list_for_each_entry_safe(&endp_unit->isu_fid_list, 
			fid_member, fid_member_next, 
			struct c2_rpc_form_fid_summary_member, 
			fsm_linkage) {
		if (fid_member->fsm_nitems > 1) {
			/* 5. For every possible coalescing situation, 
			   create a struct c2_rpc_form_item_coalesced
			   and populate it.*/
			coalesced_item = c2_alloc(sizeof(struct 
						c2_rpc_form_item_coalesced));
			if (coalesced_item == NULL) {
				printf("Failed to allocate memory for \
						struct \
						c2_rpc_form_item_coalesced.\n");
				return -ENOMEM;
			}
			coalesced_item->ic_op_intent = fid_member->fsm_rw;
			coalesced_item->ic_nmembers = fid_member->fsm_nitems;
			c2_list_init(&coalesced_item->ic_member_list);
			/* Move the members from fid_member fsm_items list
			   to ic_member_list of item_coalesced structure. */
			c2_list_for_each_entry_safe(&fid_member->fsm_items,
					fsm_item, fsm_item_next,
					struct c2_rpc_form_fid_units,
					fu_linkage) {
				//c2_list_move(&coalesced_item->ic_member_list,
					//	&fsm_item->fu_linkage);
				C2_ALLOC_PTR(coalesced_member);
				if (coalesced_member == NULL) {
					printf("Failed to allocate memory for\
							struct\
							c2_rpc_form_item_coalesced_member.\n");
					return -ENOMEM;
				}
				coalesced_member->im_member_item =
					fsm_item->fu_item;
				c2_list_link_init(&coalesced_member->
						im_linkage);
				c2_list_add(&coalesced_item->ic_member_list,
						&coalesced_member->
						im_linkage);
				c2_list_del(&fsm_item->fu_linkage);
				c2_free(fsm_item);
			}

			/* Coalesce IO vectors into one. */
			res = c2_rpc_form_io_items_coalesce(coalesced_item);
			if (res == 0) {
				/*delete fid member*/
				printf("io_items_coalesce succeeded.\n");
				ncoalesced++;
				c2_list_del(&fid_member->fsm_linkage);
				c2_list_fini(&fid_member->fsm_items);
				c2_free(fid_member);
				coalesced_item->ic_resultant_item->ri_state = 
					RPC_ITEM_ADDED;
				/* 6. Remove the corresponding member rpc items
				   from forming list, calculate their cumulative
				   size and deduct it from rpcobj_size. */
				/* rpcobj_size += coalesced_item->
				   ic_resultant_item-> ri_type->rit_ops->
				   rio_item_size( coalesced_item->
				   ic_resultant_item);*/
				*rpcobj_size -= fid_member->fsm_total_size;
				*rpcobj_size += c2_rpc_form_item_size(
						coalesced_item->
						ic_resultant_item);
				c2_list_add(&endp_unit->isu_coalesced_items_list
						, &coalesced_item->ic_linkage);
			}
			/* 7. Add the newly formed rpc item into the 
			   forming list and
			   increment rpcobj_size by its size. */
			c2_list_for_each_entry(
					&coalesced_item->ic_member_list, 
					item_member, struct 
					c2_rpc_form_item_coalesced_member, 
					im_linkage) {
				c2_list_del(&item_member->im_member_item->
						ri_rpcobject_linkage);
			}
			c2_list_add(&rpc->r_items, 
					&coalesced_item->ic_resultant_item->
					ri_rpcobject_linkage);
		}
	}
	printf("Coalescing succeeded for %d iterations.\n", ncoalesced);
	return 0;
}

/**
   State function for CHECKING state.
 */
int c2_rpc_form_checking_state(struct c2_rpc_form_item_summary_unit *endp_unit,
		struct c2_rpc_item *item, const struct c2_rpc_form_sm_event
		*event)
{
	int						 res = 0;
	int						 ls = 0;
	struct c2_rpc_form_item_coalesced		*coalesced_item = NULL;
	struct c2_rpc_form_items_cache			*cache_list =
		items_cache;
	uint64_t					 rpcobj_size = 0;
	uint64_t					 item_size = 0;
	struct c2_rpc_form_item_summary_unit_group	*sg = NULL;
	uint64_t					 group_size = 0;
	uint64_t					 partial_size = 0;
	struct c2_rpc_form_item_summary_unit_group	*group = NULL;
	struct c2_rpc_form_item_summary_unit_group	*group_next = NULL;
	uint64_t					 nselected_groups = 0;
	uint64_t					 ncurrent_groups = 0;
	uint64_t					 nfragments = 0;
	struct c2_rpc_form_rpcobj			*rpcobj = NULL;
	bool						 item_coalesced = false;
	struct c2_rpc_item				*rpc_item = NULL;
	struct c2_rpc_item				*rpc_item_next = NULL;
	bool						 urgent_items = false;
	bool						 item_added = false;

	C2_PRE(item != NULL);
	C2_PRE((event->se_event == C2_RPC_FORM_EXTEVT_RPCITEM_REPLY_RECEIVED) ||
			(event->se_event == C2_RPC_FORM_EXTEVT_RPCITEM_TIMEOUT) ||
			(event->se_event  == C2_RPC_FORM_INTEVT_STATE_SUCCEEDED));
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));

	endp_unit->isu_sm.isu_endp_state = C2_RPC_FORM_STATE_CHECKING;

	if(!c2_list_is_empty(&endp_unit->isu_rpcobj_formed_list)) {
		return C2_RPC_FORM_INTEVT_STATE_SUCCEEDED;
	}
	if(c2_list_is_empty(&endp_unit->isu_unformed_list)) {
		return C2_RPC_FORM_INTEVT_STATE_FAILED;
	}
	printf("In state: checking\n");
	printf("checking_state : item state = %d\n", item->ri_state);
	ls = item->ri_state;
	/** Returning failure will lead the state machine to 
	    waiting state and then the thread will exit the
	    state machine. */
	if (endp_unit->isu_curr_rpcs_in_flight == 
			endp_unit->isu_max_rpcs_in_flight) {
		return C2_RPC_FORM_INTEVT_STATE_FAILED;
	}

	/* Create an rpc object in endp_unit->isu_rpcobj_checked_list. */
	rpcobj = c2_alloc(sizeof(struct c2_rpc_form_rpcobj));
	if (rpcobj == NULL) {
		printf("Failed to allocate memory for \
				struct c2_rpc_form_rpcobj.\n");
		return C2_RPC_FORM_INTEVT_STATE_FAILED;
	}
	c2_list_link_init(&rpcobj->ro_linkage);
	rpcobj->ro_rpcobj = c2_alloc(sizeof(struct c2_rpc));
	if (rpcobj->ro_rpcobj == NULL) {
		printf("Failed to allocate memory for struct c2_rpc.\n");
		return C2_RPC_FORM_INTEVT_STATE_FAILED;
	}
	c2_list_link_init(&rpcobj->ro_rpcobj->r_linkage);
	c2_list_init(&rpcobj->ro_rpcobj->r_items);

	if (event->se_event == C2_RPC_FORM_EXTEVT_RPCITEM_REPLY_RECEIVED) {
		c2_list_for_each_entry(&endp_unit->
				isu_coalesced_items_list,
				coalesced_item,
				struct c2_rpc_form_item_coalesced,
				ic_linkage) {
			if (coalesced_item->ic_resultant_item == item) {
				item_coalesced = true;
				res = c2_rpc_form_item_coalesced_reply_post(
						endp_unit, coalesced_item);
				if (res != 0) {
					printf("Failed to process a \
							coalesced rpc item.\n");
				}
				break;
			}
		}
		if (item_coalesced == false) {
			//item->ri_type->rit_ops->rio_replied(item);
			//XXX c2_rpc_item_replied(item);
		}
	}
	else if (event->se_event == C2_RPC_FORM_EXTEVT_RPCITEM_TIMEOUT) {
		res = c2_rpc_form_item_add_to_forming_list(endp_unit, item, 
				&rpcobj_size, &nfragments, rpcobj->ro_rpcobj);
		/* If item can not be added due to size restrictions, move it
		   to the head of unformed list and it will be handled on
		   next formation attempt.*/
		if (res != 0) {
			c2_list_move(&endp_unit->isu_unformed_list, &item->ri_unformed_linkage);
		}
	}
	/* Iterate over the c2_rpc_form_item_summary_unit_group list in the
	   endpoint structure to find out which rpc groups can be included
	   in the rpc object. */
	c2_list_for_each_entry(&endp_unit->isu_groups_list, 
			sg, struct c2_rpc_form_item_summary_unit_group, 
			sug_linkage) {
		/* nselected_groups number includes the last partial
		   rpc group(if any).*/
		nselected_groups++;
		if ((group_size + sg->sug_total_size) < 
				endp_unit->isu_max_message_size) {
			group_size += sg->sug_total_size;
		}
		else {
			partial_size = (group_size + sg->sug_total_size) - 
				endp_unit->isu_max_message_size;
			break;
		}
	}

	/* XXX curr rpcs in flight will be taken care by
	   output component. */
	/* Core of formation algorithm. */
	//res = c2_rpc_form_get_items_cache_list(endp_unit->isu_endp_id, 
	//		&cache_list);
	c2_mutex_lock(&cache_list->ic_mutex);
	printf("Unformed list length = %lu \n",
			c2_list_length(&endp_unit->isu_unformed_list));
	c2_list_for_each_entry_safe(&endp_unit->isu_unformed_list, rpc_item,
			rpc_item_next, struct c2_rpc_item, 
			ri_unformed_linkage) {
		/* item_size = rpc_item->ri_type->rit_ops->
		   rio_item_size(item);*/
		item_size = c2_rpc_form_item_size(rpc_item);
		/* 1. If there are urgent items, form them immediately. */
		if (rpc_item->ri_deadline == 0) {
			if (urgent_items == false) {
				urgent_items = true;
			}
			res = c2_rpc_form_item_add_to_forming_list(endp_unit, 
					rpc_item, &rpcobj_size, 
					&nfragments, rpcobj->ro_rpcobj);
			printf("1. Size of rpc object in checking state = %lu\n",
					rpcobj_size);
			if (res != 0) {
				/* Forming list complete.*/
				break;
			}
		}
		/* 2. Check if current rpc item belongs to any of the selected
		   groups. If yes, add it to forming list. For the last partial
		   group (if any), check if current rpc item belongs to this
		   partial group and add the item till size of items in this
		   partial group reaches its limit within max_message_size. */
		c2_list_for_each_entry_safe(&endp_unit->isu_groups_list, group,
				group_next, struct
				c2_rpc_form_item_summary_unit_group,
				sug_linkage) {
			ncurrent_groups++;
			/* If selected groups are exhausted, break the loop. */
			if (ncurrent_groups > nselected_groups)
				break;
			if (rpc_item->ri_group == group->sug_group) {
				/* If the size of selected groups is bigger than
				   max_message_size, the last group will be
				   partially selected and is present in variable
				   'sg'. */
				if ((rpc_item->ri_group == sg->sug_group) &&
					((partial_size - item_size) > 0)) {
					partial_size -= item_size;
				}
				item_added = true;
				ncurrent_groups = 0;
				break;
			}
		}
		if(item_added) {
			res = c2_rpc_form_item_add_to_forming_list(
					endp_unit, rpc_item, 
					&rpcobj_size, &nfragments, 
					rpcobj->ro_rpcobj);
			if (res != 0) {
				break;
			}
			item_added = false;
			printf("2. Size of rpc object in checking state = %lu\n",
					rpcobj_size);
		}
	}
	c2_mutex_unlock(&cache_list->ic_mutex);
	if (!urgent_items) {
		/* If size of formed rpc object is less than 90% of 
		   max_message_size, discard the rpc object. */
		if (rpcobj_size < (0.9 * endp_unit->isu_max_message_size)) {
			printf("Discarding the formed rpc object since \
					it is sub-optimal size \
					rpcobj_size = %lu.\n",rpcobj_size);
			/* Delete the formed RPC object. */
			c2_list_for_each_entry_safe(&rpcobj->ro_rpcobj->r_items,
					rpc_item, rpc_item_next,
					struct c2_rpc_item,
					ri_rpcobject_linkage) {
				c2_list_del(&rpc_item->ri_rpcobject_linkage);
				rpc_item->ri_state = RPC_ITEM_SUBMITTED;
				c2_rpc_form_add_rpcitem_to_summary_unit(endp_unit, rpc_item);
			}
			c2_list_del(&rpcobj->ro_rpcobj->r_linkage);
			c2_free(rpcobj->ro_rpcobj);
			c2_list_del(&rpcobj->ro_linkage);
			c2_free(rpcobj);
			rpcobj = NULL;
			return C2_RPC_FORM_INTEVT_STATE_FAILED;
		}
	}
	/* Try to do IO colescing for items in forming list. */
	res = c2_rpc_form_items_coalesce(endp_unit, rpcobj->ro_rpcobj,
			&rpcobj_size);
	c2_list_add(&endp_unit->isu_rpcobj_checked_list,
			&rpcobj->ro_linkage);
	return C2_RPC_FORM_INTEVT_STATE_SUCCEEDED;
}

/**
   State function for FORMING state.
 */
int c2_rpc_form_forming_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, const struct c2_rpc_form_sm_event
		*event)
{
	int				 res = 0;
	struct c2_rpc_form_rpcobj	*rpcobj = NULL;
	struct c2_rpc_form_rpcobj	*rpcobj_next = NULL;
	struct c2_rpc_item		*rpc_item = NULL;
	struct c2_rpc_item		*rpc_item_next = NULL;

	C2_PRE(item != NULL);
	C2_PRE(event->se_event == C2_RPC_FORM_INTEVT_STATE_SUCCEEDED);
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));
	printf("In state: forming\n");

	endp_unit->isu_sm.isu_endp_state = C2_RPC_FORM_STATE_FORMING;
	if(!c2_list_is_empty(&endp_unit->isu_rpcobj_formed_list)) {
		return C2_RPC_FORM_INTEVT_STATE_SUCCEEDED;
	}

	c2_list_for_each_entry_safe(&endp_unit->isu_rpcobj_checked_list,
			rpcobj, rpcobj_next, struct c2_rpc_form_rpcobj,
			ro_linkage) {
		c2_list_for_each_entry_safe(&rpcobj->ro_rpcobj->r_items, 
				rpc_item, rpc_item_next,
				struct c2_rpc_item, ri_rpcobject_linkage) {
			//res = c2_rpc_session_item_prepare(rpc_item);
			res = 0;
			if (res != 0) {
				c2_list_del(&rpc_item->ri_rpcobject_linkage);
				rpc_item->ri_state = RPC_ITEM_SUBMITTED;
				c2_list_add(&endp_unit->isu_unformed_list, 
						&rpc_item->ri_unformed_linkage);
				c2_rpc_form_add_rpcitem_to_summary_unit(
						endp_unit, rpc_item);
			}
		}
		c2_list_del(&rpcobj->ro_linkage);
		c2_list_add(&endp_unit->isu_rpcobj_formed_list, 
				&rpcobj->ro_linkage);
	}
	return C2_RPC_FORM_INTEVT_STATE_SUCCEEDED;
}

/**
   State function for POSTING state.
 */
int c2_rpc_form_posting_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, const struct c2_rpc_form_sm_event
		*event)
{
	int				 res = 0;
	int				 ret =
		C2_RPC_FORM_INTEVT_STATE_SUCCEEDED;
	struct c2_rpc_form_rpcobj	*rpc_obj = NULL;
	struct c2_rpc_form_rpcobj	*rpc_obj_next = NULL;
	struct c2_net_end_point		*endp = NULL;
	struct c2_rpc_item		*first_item = NULL;
	printf("In state: posting\n");

	C2_PRE(item != NULL);
	/* POSTING state is reached only by a state succeeded event with
	   FORMING as previous state. */
	C2_PRE(event->se_event == C2_RPC_FORM_INTEVT_STATE_SUCCEEDED);
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));

	endp_unit->isu_sm.isu_endp_state = C2_RPC_FORM_STATE_POSTING;

	/* Iterate over the rpc object list and send all rpc objects
	   to the output component. */
	c2_list_for_each_entry_safe(&endp_unit->isu_rpcobj_formed_list,
			rpc_obj, rpc_obj_next, struct c2_rpc_form_rpcobj, 
			ro_linkage) {
		first_item = c2_list_entry((c2_list_first(&rpc_obj->
						ro_rpcobj->r_items)),
				struct c2_rpc_item, ri_rpcobject_linkage);
		endp = c2_rpc_form_get_endpoint(first_item);
		if (endp_unit->isu_curr_rpcs_in_flight < 
				endp_unit->isu_max_rpcs_in_flight) {
			/*XXX TBD: Before sending the c2_rpc on wire,
			   it needs to be serialized into one buffer. */
			   printf("posting list length = %lu\n",c2_list_length(&rpc_obj->ro_rpcobj->r_items));
			res = c2_net_send(endp, rpc_obj->ro_rpcobj);
			/* XXX curr rpcs in flight will be taken care by
			   output component. */
			//endp_unit->isu_curr_rpcs_in_flight++;
			if(res == 0) {
				c2_list_del(&rpc_obj->ro_linkage);
				//c2_free(rpc_obj);
				ret = C2_RPC_FORM_INTEVT_STATE_SUCCEEDED;
			}
			else {
				ret = C2_RPC_FORM_INTEVT_STATE_FAILED;
				break;
			}
		}
		else {
			printf("Formation could not send rpc items to \
					output since max_rpcs_in_flight \
					limit is reached.\n");
			ret = C2_RPC_FORM_INTEVT_STATE_FAILED;
			break;
		}
	}
	return ret;
}

/**
   State function for REMOVING state.
 */
int c2_rpc_form_removing_state(struct c2_rpc_form_item_summary_unit *endp_unit,
		struct c2_rpc_item *item, const struct c2_rpc_form_sm_event
		*event)
{
	int		res = 0;

	C2_PRE(item != NULL);
	C2_PRE((event->se_event == C2_RPC_FORM_EXTEVT_RPCITEM_REMOVED) ||
			(event->se_event == C2_RPC_FORM_EXTEVT_RPCITEM_CHANGED));
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));
	printf("In state: removing\n");

	endp_unit->isu_sm.isu_endp_state = C2_RPC_FORM_STATE_REMOVING;

	/* 
	  1. Check the state of incoming rpc item.
	  2. If it is RPC_ITEM_ADDED, deny the request to
	     change/remove rpc item and return state failed event.
	  3. If state is RPC_ITEM_SUBMITTED, the rpc item is still due
	     for formation (not yet gone through checking state) and
	     it will be removed/changed before it is considered for
	     further formation activities.
	 */
	if (item->ri_state == RPC_ITEM_ADDED) {
		return C2_RPC_FORM_INTEVT_STATE_FAILED;
	}
	else if (item->ri_state == RPC_ITEM_SUBMITTED) {
		if (event->se_event == C2_RPC_FORM_EXTEVT_RPCITEM_REMOVED) {
			res = c2_rpc_form_remove_rpcitem_from_summary_unit(
					endp_unit, item);
		}
		else if (event->se_event == C2_RPC_FORM_EXTEVT_RPCITEM_CHANGED) {
			res = c2_rpc_form_change_rpcitem_from_summary_unit(
					endp_unit, item, event->se_pvt);
		}
		if (res == 0)
			return C2_RPC_FORM_INTEVT_STATE_SUCCEEDED;
		else
			return C2_RPC_FORM_INTEVT_STATE_FAILED;
	}
	return C2_RPC_FORM_INTEVT_STATE_SUCCEEDED;
}

/**
   XXX rio_replied op from rpc type ops.
   If this is an IO request, free the IO vector
   and free the fop.
 */
int c2_rpc_item_replied(struct c2_rpc_item *item)
{
	struct c2_fop			*fop = NULL;
	struct c2_fop_cob_readv		*read_fop = NULL;
	struct c2_fop_cob_writev	*write_fop = NULL;

	C2_PRE(item != NULL);
	/* Find out fop from the rpc item,
	   Find out opcode of rpc item,
	   Deallocate the io vector of rpc item accordingly.*/

	fop = container_of(item, struct c2_fop, f_item);
	C2_ASSERT(fop != NULL);

	if (fop->f_type->ft_code == c2_io_service_readv_opcode) {
		read_fop = c2_fop_data(fop);
		c2_free(read_fop->frd_ioseg.fs_segs);
	}
	else if (fop->f_type->ft_code == c2_io_service_writev_opcode) {
		write_fop = c2_fop_data(fop);
		c2_free(write_fop->fwr_iovec.iov_seg);
	}
	c2_fop_free(fop);
	return 0;
}

/**
  XXX Need to move to appropriate file
   RPC item ops function
   Function to return size of fop
 */
uint64_t c2_rpc_form_item_size(struct c2_rpc_item *item)
{
	struct c2_fop			*fop = NULL;
	int				 opcode = 0;
	uint64_t			 size = 0;
	struct c2_fop_cob_writev	*write_fop = NULL;
	struct c2_fop_cob_readv_rep	*read_rep_fop = NULL;
	struct c2_fop_cob_readv		*read_fop = NULL;
	uint64_t			 vec_count = 0;
	int				 i = 0;

	C2_PRE(item != NULL);

	fop = container_of(item, struct c2_fop, f_item);
	C2_ASSERT(fop != NULL);

	/** Size of fop layout */
	size = fop->f_type->ft_fmt->ftf_layout->fm_sizeof;
	size += sizeof(struct c2_fop_type);
	size += sizeof(struct c2_fop);

	opcode = fop->f_type->ft_code;
	switch (opcode) {
		case c2_io_service_readv_opcode:
			/* Buffer payload for read request. */
			read_fop = c2_fop_data(fop);
			/* Size of holding structure. */
			size += (read_fop->frd_ioseg.fs_count *
					sizeof(struct c2_fop_segment));
			break;
		case c2_io_service_readv_rep_opcode:
			/** Add buffer payload for read reply */
			read_rep_fop = c2_fop_data(fop);
			/* Size of actual user data. */
			size += read_rep_fop->frdr_buf.f_count;
			/* For read reply type, there is only one buffer.*/
			break;
		case c2_io_service_writev_opcode:
			/** Add buffer payload for write request */
			write_fop = c2_fop_data(fop);
			vec_count = write_fop->fwr_iovec.iov_count;
			/* Size of actual user data. */
			for(i = 0; i < vec_count; i++) {
				size += write_fop->fwr_iovec.iov_seg[i].f_buf.f_count;
			}
			/* Size of holding structure. */
			size += write_fop->fwr_iovec.iov_count *
				sizeof(struct c2_fop_io_seg);
	};

	return size;
}

/**
  XXX Need to move to appropriate file 
   RPC item ops function
   Function to return the opcode given an rpc item
 */
int c2_rpc_item_io_get_opcode(struct c2_rpc_item *item)
{
	struct c2_fop		*fop = NULL;
	int			 opcode = 0;

	C2_PRE(item != NULL);

	fop = container_of(item, struct c2_fop, f_item);
	C2_ASSERT(fop != NULL);

	opcode = fop->f_type->ft_code;
	if(opcode == c2_io_service_readv_opcode ) {
		return C2_RPC_FORM_IO_READ;
	}
	else if (opcode == c2_io_service_writev_opcode) {
		return C2_RPC_FORM_IO_WRITE;
	}
	return opcode;
}

/**
  XXX Need to move to appropriate file 
   RPC item ops function
   Function to map the on-wire FOP format to in-core FOP format.
 */
static void c2_rpc_form_item_io_fid_wire2mem(struct c2_fop_file_fid *in, 
		struct c2_fid *out)
{
        out->f_container = in->f_seq;
        out->f_key = in->f_oid;
}

/**
  XXX Need to move to appropriate file 
   RPC item ops function
   Function to get the fid for an IO request from the rpc item
 */
struct c2_fid c2_rpc_item_io_get_fid(struct c2_rpc_item *item)
{
	struct c2_fop			*fop = NULL;
	struct c2_fid			 fid;
	struct c2_fop_file_fid		*ffid = NULL;
	struct c2_fop_cob_writev	*write_fop = NULL;
	struct c2_fop_cob_readv		*read_fop = NULL;
	int				 opcode;

	C2_PRE(item != NULL);

	fop = container_of(item, struct c2_fop, f_item);
	C2_ASSERT(fop != NULL);

	opcode = fop->f_type->ft_code;

	if(opcode == c2_io_service_readv_opcode) {
		read_fop = c2_fop_data(fop);
		ffid = &read_fop->frd_fid;
	}
	else if (opcode == c2_io_service_writev_opcode) {
		write_fop = c2_fop_data(fop);
		ffid = &write_fop->fwr_fid;
	}
	c2_rpc_form_item_io_fid_wire2mem(ffid, &fid);
	return fid;
}

/**
  XXX Need to move to appropriate file 
   RPC item ops function
   Function to find out if the item belongs to an IO request or not 
 */
bool c2_rpc_item_is_io_req(struct c2_rpc_item *item)
{
	struct c2_fop		*fop = NULL;
	int			 opcode = 0;

	C2_PRE(item != NULL);

	fop = container_of(item, struct c2_fop, f_item);
	C2_ASSERT(fop != NULL);

	opcode = fop->f_type->ft_code;
	if(opcode == c2_io_service_readv_opcode ||
		opcode == c2_io_service_writev_opcode) {
		return true;
	}
	return false;
}

/**
  XXX Need to move to appropriate file 
   RPC item ops function
   Function to find out number of fragmented buffers in IO request 
 */
uint64_t c2_rpc_item_get_io_fragment_count(struct c2_rpc_item *item)
{

	struct c2_fop			*fop;
	struct c2_fop_cob_writev	*write_fop = NULL;
	struct c2_fop_cob_readv		*read_fop = NULL;
	int				 opcode;
	uint64_t			 nfragments = 0;
	uint64_t			 s_offset = 0;
	uint64_t			 s_count = 0;
	uint64_t			 next_s_offset = 0;
	uint64_t			 seg_count = 0;
	int				 i = 0;

	C2_PRE(item != NULL);

	fop = container_of(item, struct c2_fop, f_item);
	C2_ASSERT(fop != NULL);

	opcode = fop->f_type->ft_code;

	if(opcode == c2_io_service_readv_opcode) {
		read_fop = c2_fop_data(fop);
		seg_count = read_fop->frd_ioseg.fs_count;
		for (i = 0; i < seg_count - 1; i++) {
			s_offset = read_fop->frd_ioseg.fs_segs[i].f_offset;
			s_count = read_fop->frd_ioseg.fs_segs[i].f_count;
			next_s_offset = read_fop->frd_ioseg.fs_segs[i+1].
				f_offset;
			if ((s_offset + s_count) != next_s_offset) {
				nfragments++;
			}
		}
	}
	else if (opcode == c2_io_service_writev_opcode) {
		write_fop = c2_fop_data(fop);
		seg_count = write_fop->fwr_iovec.iov_count;
		for (i = 0; i < seg_count - 1; i++) {
			s_offset = write_fop->fwr_iovec.iov_seg[i].f_offset;
			s_count = write_fop->fwr_iovec.iov_seg[i].f_buf.f_count;
			next_s_offset = write_fop->fwr_iovec.iov_seg[i+1].
				f_offset;
			if((s_offset + s_count) != next_s_offset) {
				nfragments++;
			}
		}
	}
	return nfragments;
}

/**
  XXX Need to move to appropriate file 
   RPC item ops function
   Function to return segment for read fop from given rpc item 
 */
struct c2_fop_segment_seq *c2_rpc_item_read_get_vector(struct c2_rpc_item *item)
 {
	struct c2_fop			*fop = NULL;
	struct c2_fop_segment_seq	*seg = NULL;
	struct c2_fop_cob_readv		*read_fop = NULL;

	C2_PRE(item != NULL);

	fop = container_of(item, struct c2_fop, f_item);
	C2_ASSERT(fop != NULL);

	read_fop = c2_fop_data(fop);
	seg = &read_fop->frd_ioseg;
	C2_ASSERT(seg != NULL);

	return seg;
 }

/**
  XXX Need to move to appropriate file 
   RPC item ops function
   Function to return segment for write fop from given rpc item 
 */
struct c2_fop_io_vec *c2_rpc_item_write_get_vector(struct c2_rpc_item *item)
 {
	struct c2_fop			*fop = NULL;
	struct c2_fop_io_vec		*vec = NULL;
	struct c2_fop_cob_writev	*write_fop = NULL;

	C2_PRE(item != NULL);

	fop = container_of(item, struct c2_fop, f_item);
	C2_ASSERT(fop != NULL);

	write_fop = c2_fop_data(fop);
	vec = &write_fop->fwr_iovec;
	C2_ASSERT(vec != NULL);

	return vec;
 }

/**
  XXX Need to move to appropriate file
  RPC item ops function
  Function to return new rpc item embedding the given write vector,
  by creating a new fop calling new fop op
 */
int c2_rpc_item_get_new_write_item(struct c2_rpc_item *curr_item,
		struct c2_rpc_item **res_item, struct c2_fop_io_vec *vec)
{
	struct c2_fop                   *fop = NULL;
	struct c2_fop                   *res_fop = NULL;
	int                              res = 0;

	C2_PRE(curr_item != NULL);

	fop = container_of(curr_item, struct c2_fop, f_item);
	C2_ASSERT(fop != NULL);

	res = fop->f_type->ft_ops->fto_get_io_fop(fop, &res_fop, vec);
	*res_item = &res_fop->f_item;
	return 0;
}

/**
  XXX Need to move to appropriate file
  RPC item ops function
  Function to return new rpc item embedding the given read segment,
  by creating a new fop calling new fop op
 */
int c2_rpc_item_get_new_read_item(struct c2_rpc_item *curr_item,
		struct c2_rpc_item **res_item, struct c2_fop_segment_seq *seg)
{
	struct c2_fop                   *fop = NULL;
	struct c2_fop                   *res_fop = NULL;
	int                              res = 0;

	C2_PRE(curr_item != NULL);

	fop = container_of(curr_item, struct c2_fop, f_item);
	C2_ASSERT(fop != NULL);

	res = fop->f_type->ft_ops->fto_get_io_fop(fop, &res_fop, seg);
	*res_item = &res_fop->f_item;
	return 0;
}

struct c2_update_stream *c2_rpc_get_update_stream(struct c2_rpc_item *item)
{
	return NULL;
}

/*
int c2_rpc_session_item_prepare(struct c2_rpc_item *item)
{
	return 0;
}*/

/**
  This routine will 
  - Call the encode routine 
  - Change the state of each rpc item in the rpc object to RPC_ITEM_SENT
 */
int c2_net_send(struct c2_net_end_point *endp, struct c2_rpc *rpc)
{
	struct c2_rpc_item	*item = NULL;

	C2_PRE(rpc != NULL);

	/** XXX call the encode routine which will perform wire encoding
	  into an preallocated buffer and send the rpc object on wire*/

	/** Change the state of each rpc item in the 
	    rpc object to RPC_ITEM_SENT */

	c2_list_for_each_entry(&rpc->r_items, item, 
			struct c2_rpc_item, ri_rpcobject_linkage) {
		C2_ASSERT(item->ri_state == RPC_ITEM_ADDED);
		item->ri_state = RPC_ITEM_SENT;
	}

	return 0;
}

