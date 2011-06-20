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
   Temporary threashold values. Will be moved to appropriate files
   once rpc integration is done.
 */
uint64_t				max_msg_size;
uint64_t				max_fragments_size;
uint64_t				max_rpcs_in_flight;

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

/**
   A state table guiding resultant states on arrival of events
   on earlier states.
   next_state = stateTable[current_state][current_event]
 */
stateFunc c2_rpc_form_stateTable
[C2_RPC_FORM_N_STATES][C2_RPC_FORM_INTEVT_N_EVENTS-1] = {

	{ &c2_rpc_form_updating_state, &c2_rpc_form_removing_state,
	  &c2_rpc_form_removing_state, &c2_rpc_form_checking_state,
	  &c2_rpc_form_checking_state, &c2_rpc_form_updating_state,
	  &c2_rpc_form_updating_state, &c2_rpc_form_waiting_state,
	  &c2_rpc_form_waiting_state},

	{ &c2_rpc_form_updating_state, &c2_rpc_form_removing_state,
	  &c2_rpc_form_removing_state, &c2_rpc_form_checking_state,
	  &c2_rpc_form_checking_state, &c2_rpc_form_updating_state,
	  &c2_rpc_form_updating_state, &c2_rpc_form_checking_state,
	  &c2_rpc_form_waiting_state},

	{ &c2_rpc_form_updating_state, &c2_rpc_form_removing_state,
	  &c2_rpc_form_removing_state, &c2_rpc_form_checking_state,
	  &c2_rpc_form_checking_state, &c2_rpc_form_updating_state,
	  &c2_rpc_form_updating_state, &c2_rpc_form_forming_state,
	  &c2_rpc_form_waiting_state},

	{ &c2_rpc_form_updating_state, &c2_rpc_form_removing_state,
	  &c2_rpc_form_removing_state, &c2_rpc_form_checking_state,
	  &c2_rpc_form_checking_state, &c2_rpc_form_updating_state,
	  &c2_rpc_form_updating_state, &c2_rpc_form_posting_state,
	  &c2_rpc_form_waiting_state},

	{ &c2_rpc_form_updating_state, &c2_rpc_form_removing_state,
	  &c2_rpc_form_removing_state, &c2_rpc_form_checking_state,
	  &c2_rpc_form_checking_state, &c2_rpc_form_updating_state,
	  &c2_rpc_form_updating_state, &c2_rpc_form_waiting_state,
	  &c2_rpc_form_waiting_state},

	{ &c2_rpc_form_updating_state, &c2_rpc_form_removing_state,
	  &c2_rpc_form_removing_state, &c2_rpc_form_checking_state,
	  &c2_rpc_form_checking_state, &c2_rpc_form_updating_state,
	  &c2_rpc_form_updating_state, &c2_rpc_form_waiting_state,
	  &c2_rpc_form_waiting_state}
};

/**
   Set thresholds for rpc formation. Currently used by UT code.
 */
void c2_rpc_form_set_thresholds(uint64_t msg_size, uint64_t max_rpcs,
		uint64_t max_fragments)
{
	max_msg_size = msg_size;
	max_rpcs_in_flight = max_rpcs;
	max_fragments_size = max_fragments;
}

static const struct c2_addb_ctx_type c2_rpc_form_addb_ctx_type = {
        .act_name = "rpc-formation"
};

static const struct c2_addb_loc c2_rpc_form_addb_loc = {
        .al_name = "rpc-formation"
};

C2_ADDB_EV_DEFINE(formation_func_fail, "formation_func_fail",
		C2_ADDB_EVENT_FUNC_FAIL, C2_ADDB_FUNC_CALL);

/**
   Initialization for formation component in rpc.
   This will register necessary callbacks and initialize
   necessary data structures.
 */
int c2_rpc_form_init()
{
	int		i = 0;

	C2_PRE(formation_summary == NULL);
	C2_ALLOC_PTR(formation_summary);
	if (formation_summary == NULL) {
		return -ENOMEM;
	}
        c2_addb_ctx_init(&formation_summary->is_rpc_form_addb,
			&c2_rpc_form_addb_ctx_type, &c2_addb_global_ctx);
        c2_addb_choose_default_level(AEL_WARN);
	c2_rwlock_init(&formation_summary->is_endp_list_lock);
	c2_list_init(&formation_summary->is_endp_list);
	/* Init the array to keep of refcounts acquired/released by
	   incoming threads. */
	for (i = 0; i < rpc_form_ut_threads; i++) {
		memset(&thrd_reftrack[i], 0,
				sizeof(struct c2_rpc_form_ut_thread_reftrack));
	}
	n_ut_threads = 0;
	return 0;
}

/**
   Log the attempt to increment reference count.
 */
void add_ref_log()
{
	struct c2_thread_handle		handle;
	int				i = 0;
	bool				found = false;

	C2_ASSERT(n_ut_threads <= rpc_form_ut_threads);
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

/**
   Log the attempt to decrement reference count.
 */
void dec_ref_log()
{
	struct c2_thread_handle		handle;
	int				i = 0;
	bool				found = false;

	C2_ASSERT(n_ut_threads <= rpc_form_ut_threads);
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
   Returns FALSE if any of refcounts are non-zero,
   returns TRUE otherwise.
 */
bool c2_rpc_form_wait_for_completion()
{
	bool					 ret = true;
	int64_t					 refcount;
	struct c2_rpc_form_item_summary_unit	*endp_unit = NULL;

	C2_PRE(formation_summary != NULL);
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
   Delete the group info list from struct c2_rpc_form_item_summary_unit.
   Called once formation component is finied.
 */
static void c2_rpc_form_empty_groups_list(struct c2_list *list)
{
	struct c2_rpc_form_item_summary_unit_group	*group = NULL;
	struct c2_rpc_form_item_summary_unit_group	*group_next = NULL;

	C2_PRE(list != NULL);
	c2_list_for_each_entry_safe(list, group, group_next,
			struct c2_rpc_form_item_summary_unit_group,
			sug_linkage) {
		c2_list_del(&group->sug_linkage);
		c2_free(group);
	}
	c2_list_fini(list);
}

/**
   Delete the coalesced items list from struct c2_rpc_form_item_summary_unit.
 */
static void c2_rpc_form_empty_coalesced_items_list(struct c2_list *list)
{
	struct c2_rpc_form_item_coalesced	 *coalesced_item = NULL;
	struct c2_rpc_form_item_coalesced	 *coalesced_item_next = NULL;
	struct c2_rpc_form_item_coalesced_member *coalesced_member = NULL;
	struct c2_rpc_form_item_coalesced_member *coalesced_member_next = NULL;

	C2_PRE(list != NULL);
	c2_list_for_each_entry_safe(list, coalesced_item, coalesced_item_next,
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
   Delete the rpcobj items list from struct c2_rpc_form_item_summary_unit.
 */
static void c2_rpc_form_empty_rpcobj_list(struct c2_list *list)
{
	struct c2_rpc_form_rpcobj		*obj = NULL;
	struct c2_rpc_form_rpcobj		*obj_next = NULL;

	C2_PRE(list != NULL);
	c2_list_for_each_entry_safe(list, obj, obj_next,
			struct c2_rpc_form_rpcobj, ro_linkage) {
		c2_list_del(&obj->ro_linkage);
		c2_free(obj);
	}
	c2_list_fini(list);
}

/**
  Delete the unformed items list from struct c2_rpc_form_item_summary_unit.
 */
static void c2_rpc_form_empty_unformed_list(struct c2_list *list)
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
int c2_rpc_form_fini()
{
	/* stime = 10ms */
	c2_time_t				 stime;
	struct c2_rpc_form_item_summary_unit	*endp_unit = NULL;
	struct c2_rpc_form_item_summary_unit	*endp_unit_next = NULL;

	C2_PRE(formation_summary != NULL);
	/* Set the active flag of all endpoints to false indicating
	   formation component is about to finish.
	   This will help to block all new threads from entering
	   the formation component. */
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
		c2_nanosleep(stime, NULL);
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
		c2_mutex_unlock(&endp_unit->isu_unit_lock);
		c2_list_del(&endp_unit->isu_linkage);
		c2_free(endp_unit);
	}
	c2_rwlock_write_unlock(&formation_summary->is_endp_list_lock);

	c2_rwlock_fini(&formation_summary->is_endp_list_lock);
	c2_list_fini(&formation_summary->is_endp_list);
	c2_addb_ctx_fini(&formation_summary->is_rpc_form_addb);
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
	C2_PRE(endp_unit != NULL);
	c2_rwlock_write_lock(&formation_summary->is_endp_list_lock);
	/** Since the behavior is undefined for fini of mutex
	    when the mutex is locked, it is not locked here
	    for endp_unit.*/
	c2_ref_put(&endp_unit->isu_sm.isu_ref);
	dec_ref_log();
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
	C2_ALLOC_PTR(endp_unit);
	if (endp_unit == NULL) {
		C2_ADDB_ADD(&formation_summary->is_rpc_form_addb,
				&c2_rpc_form_addb_loc, c2_addb_oom);
		return NULL;
	}
	c2_mutex_init(&endp_unit->isu_unit_lock);
	c2_list_add(&formation_summary->is_endp_list,
			&endp_unit->isu_linkage);
	c2_list_init(&endp_unit->isu_groups_list);
	c2_list_init(&endp_unit->isu_coalesced_items_list);
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

/**
   Check if given 2 endpoints are equal.
 */
bool c2_rpc_form_end_point_equal(struct c2_net_end_point *ep1,
		struct c2_net_end_point *ep2)
{
	bool	status = false;

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
	struct c2_thread_handle			 handle;
	int					 i = 0;

	C2_PRE(item != NULL);
	C2_PRE(sm_event->se_event < C2_RPC_FORM_INTEVT_N_EVENTS);
	C2_PRE(sm_state <= C2_RPC_FORM_N_STATES);

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
	if (!endp->isu_form_active) {
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
	/* Instrumentation to detect reference leaks. */
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
   Callback function for addition of an rpc item to the list of
   its corresponding free slot.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_ready(struct c2_rpc_item *item)
{
	struct c2_rpc_form_sm_event		 sm_event;
	struct c2_rpc_slot			*slot = NULL;
	struct c2_rpcmachine			*rpcmachine = NULL;

	C2_PRE(item != NULL);
	printf("In callback: c2_rpc_form_extevt_rpcitem_ready\n");
	sm_event.se_event = C2_RPC_FORM_EXTEVT_RPCITEM_READY;
	sm_event.se_pvt = NULL;

	/* Add the item to ready list of its slot. */
	slot = item->ri_slot_refs[0].sr_slot;
	c2_mutex_lock(&slot->sl_mutex);
	C2_ASSERT(slot != NULL);
	c2_list_add(&slot->sl_ready_list, &item->ri_slot_link);

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
	c2_mutex_unlock(&slot->sl_mutex);

	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(item, NULL, C2_RPC_FORM_N_STATES,
			&sm_event);
}

/**
   Callback function for slot becoming idle.
   Adds the slot to the list of ready slots in concerned rpcmachine.
   @param item - slot structure for the slot which has become idle.
 */
int c2_rpc_form_extevt_slot_idle(struct c2_rpc_slot *slot)
{
	struct c2_rpcmachine		*rpcmachine = NULL;
	C2_PRE(slot != NULL);
	printf("In callback: c2_rpc_form_extevt_slot_idle\n");
	/* Add the slot to list of ready slots in its rpcmachine. */
	c2_mutex_lock(&slot->sl_mutex);
	rpcmachine = slot->sl_session->s_conn->c_rpcmachine;
	C2_ASSERT(rpcmachine != NULL);
	c2_mutex_lock(&rpcmachine->cr_ready_slots_mutex);
	c2_list_add(&rpcmachine->cr_ready_slots, &slot->sl_link);
	c2_mutex_unlock(&rpcmachine->cr_ready_slots_mutex);
	c2_mutex_unlock(&slot->sl_mutex);
	return 0;
}

/**
   Callback function for unbounded item getting added to session.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_unbounded_rpcitem_added(struct c2_rpc_item *item)
{
	struct c2_rpc_form_sm_event		 sm_event;
	struct c2_rpc_session			*session;

	C2_PRE(item != NULL);
	printf("In callback: c2_rpc_form_extevt_unbounded_rpcitem_added\n");
	sm_event.se_event = C2_RPC_FORM_EXTEVT_UNBOUNDED_RPCITEM_ADDED;
	sm_event.se_pvt = NULL;

	/* Add the item to list of unbound items in its session. */
	session = item->ri_session;
	C2_ASSERT(session != NULL);
	c2_mutex_lock(&session->s_mutex);
	c2_list_add(&session->s_unbound_items, &item->ri_unbound_link);
	c2_mutex_unlock(&session->s_mutex);
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
int c2_rpc_form_extevt_rpcitem_reply_received(struct c2_rpc_item *reply_item,
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
	item->ri_deadline = 0;
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
static int c2_rpc_form_intevt_state_failed(struct c2_rpc_form_item_summary_unit
		*endp_unit, struct c2_rpc_item *item, const int state)
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
	struct c2_rpc_form_item_coalesced_member	*member;
	struct c2_rpc_form_item_coalesced_member	*next_member;
	struct c2_rpc_item				*item = NULL;

	C2_PRE(endp_unit != NULL);
	C2_PRE(coalesced_struct != NULL);

	c2_list_for_each_entry_safe(&coalesced_struct->ic_member_list,
			member, next_member,
			struct c2_rpc_form_item_coalesced_member,
			im_linkage) {
		item = member->im_member_item;
		/*member->im_member_item->ri_type->rit_ops->
		  rio_replied(member->im_member, rc);*/
		c2_list_del(&member->im_linkage);
		//c2_rpc_item_replied(&member->im_member_item);
		//item->ri_type->rit_ops->rio_replied(item);
		coalesced_struct->ic_nmembers--;
	}
	C2_ASSERT(coalesced_struct->ic_nmembers == 0);
	c2_list_del(&coalesced_struct->ic_linkage);
	item = coalesced_struct->ic_resultant_item;
	//c2_rpc_item_replied(coalesced_struct->ic_resultant_item);
	//item->ri_type->rit_ops->rio_replied(item);
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
		C2_ADDB_ADD(&formation_summary->is_rpc_form_addb,
				&c2_rpc_form_addb_loc, formation_func_fail,
				"c2_rpc_form_change_rpcitem_from_summary_unit",
				res);
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
		C2_ADDB_ADD(&formation_summary->is_rpc_form_addb,
				&c2_rpc_form_addb_loc, formation_func_fail,
				"c2_rpc_form_change_rpcitem_from_summary_unit",
				res);
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
	//summary_group->sug_total_size -=  c2_rpc_item_size(item);
	summary_group->sug_total_size -=  item->ri_type->rit_ops->
		rio_item_size(item);
	summary_group->sug_avg_timeout =
		((summary_group->sug_nitems * summary_group->sug_avg_timeout)
		 - item->ri_deadline) / (summary_group->sug_nitems);

	return 0;
}

/**
   Sort the c2_rpc_form_item_summary_unit_group structs according to
   increasing value of average timeout.
   This helps to select groups with least average timeouts in formation.
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
	int						 res = 0;
	struct c2_rpc_form_item_summary_unit_group	*summary_group = NULL;
	bool						 found = false;
	struct c2_rpc_item				*rpc_item = NULL;
	struct c2_rpc_item				*rpc_item_next = NULL;
	bool						 item_inserted = false;

	C2_PRE(item != NULL);
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));
	C2_PRE(item->ri_state == RPC_ITEM_SUBMITTED);

	/** Insert the item into unformed list sorted according to timeout*/
	c2_list_for_each_entry_safe(&endp_unit->isu_unformed_list,
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
	/*c2_list_add(&endp_unit->isu_unformed_list,
			&item->ri_unformed_linkage);*/
	/* Search for the group of rpc item in list of rpc groups in
	  summary_unit. */
	c2_list_for_each_entry(&endp_unit->isu_groups_list, summary_group,
			struct c2_rpc_form_item_summary_unit_group,
			sug_linkage) {
		if (summary_group->sug_group == item->ri_group) {
			found = true;
			break;
		}
	}
	/* If not found, create a c2_rpc_form_item_summary_unit_group
	     structure and fill all necessary data. */
	if (!found) {
		C2_ALLOC_PTR(summary_group);
		if(summary_group == NULL) {
			C2_ADDB_ADD(&formation_summary->is_rpc_form_addb,
					&c2_rpc_form_addb_loc, c2_addb_oom);
			return -ENOMEM;
		}
		c2_list_link_init(&summary_group->sug_linkage);
		c2_list_add(&endp_unit->isu_groups_list,
				&summary_group->sug_linkage);
		printf("New summary unit group added.\n");
		if (item->ri_group == NULL) {
			printf("Creating a c2_rpc_form_item_summary_unit_group \
					struct for items with no groups.\n");
		}
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

	//summary_group->sug_total_size += c2_rpc_item_size(item);
	summary_group->sug_total_size += item->ri_type->rit_ops->
		rio_item_size(item);
	/* XXX Need to handle floating point operations in kernel.
	   Prerequisite => Kernel needs to be compiled with FPE support
	   if we want to use FP operations in kernel. */
	summary_group->sug_avg_timeout =
		((summary_group->sug_nitems * summary_group->sug_avg_timeout)
		 + item->ri_deadline) / (summary_group->sug_nitems + 1);
	summary_group->sug_nitems++;

	/* Put the corresponding c2_rpc_form_item_summary_unit_group
	   struct in its correct position on "least value first" basis of
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
	if(item->ri_deadline != 0) {
		/* C2_TIMER_SOFT creates a different thread to handle the
		   callback. */
		/*c2_timer_init(&item->ri_timer, C2_TIMER_SOFT,
				item->ri_deadline, 1,
				c2_rpc_form_item_timer_callback,
				(unsigned long)item);*/
		//res = c2_timer_start(&item->ri_timer);
		if (res != 0) {
			C2_ADDB_ADD(&formation_summary->is_rpc_form_addb,
				&c2_rpc_form_addb_loc,
				formation_func_fail,
				"c2_rpc_form_add_rpcitem_to_summary_unit",
				res);
			return res;
		}
	}
	return 0;
}

/**
   State function for UPDATING state.
 */
int c2_rpc_form_updating_state(struct c2_rpc_form_item_summary_unit *endp_unit,
		struct c2_rpc_item *item, const struct c2_rpc_form_sm_event
		*event)
{
	int			 res = 0;
	int			 ret = 0;
	struct c2_rpc_session	*session = NULL;
	bool			 item_unbound = true;

	C2_PRE(item != NULL);
	C2_PRE(event->se_event == C2_RPC_FORM_EXTEVT_RPCITEM_READY ||
		event->se_event == C2_RPC_FORM_EXTEVT_SLOT_IDLE ||
		event->se_event == C2_RPC_FORM_EXTEVT_UNBOUNDED_RPCITEM_ADDED);
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));

	printf("In state: updating\n");
	printf("updating_state: item state = %d\n", item->ri_state);
	endp_unit->isu_sm.isu_endp_state = C2_RPC_FORM_STATE_UPDATING;

	/* If the item doesn't belong to unbound list of its session,
	   add it to the c2_rpc_form_item_summary_unit data structure.*/
	session = item->ri_session;
	C2_ASSERT(session != NULL);
	c2_mutex_lock(&session->s_mutex);
	item_unbound = c2_list_contains(&session->s_unbound_items,
				&item->ri_unbound_link);
	c2_mutex_unlock(&session->s_mutex);
	if (!item_unbound) {
		res = c2_rpc_form_add_rpcitem_to_summary_unit(endp_unit, item);
		printf("Bound item %p added to unformed list.\n", item);
	}
	/* If rpcobj_formed_list already contains formed rpc objects,
	   succeed the state and let it proceed to posting state. */
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
	bool				 io_op = false;
	uint64_t			 current_fragments = 0;
	struct c2_rpc_slot		*slot = NULL;
	struct c2_rpc_session		*session = NULL;
	c2_time_t			 now;
	bool				 session_locked = false;

	C2_PRE(endp_unit != NULL);
	C2_PRE(item != NULL);
	C2_PRE(rpcobj_size != NULL);

	/* Fragment count check. */
	io_op = item->ri_type->rit_ops->rio_is_io_req(item);
	//io_op = c2_rpc_item_is_io_req(item);
	if (io_op) {
		current_fragments = item->ri_type->rit_ops->
			rio_get_io_fragment_count(item);
		//current_fragments = c2_rpc_item_get_io_fragment_count(item);
		if ((*nfragments + current_fragments) >
				endp_unit->isu_max_fragments_size) {
			return 0;
		}
	}
	/* Get size of rpc item. */
	//item_size = c2_rpc_item_size(item);
	item_size = item->ri_type->rit_ops->rio_item_size(item);

	/* If size of rpc object after adding current rpc item is
	   within max_message_size, add it the rpc object. */
	if (((*rpcobj_size + item_size) < endp_unit->isu_max_message_size)) {
		c2_list_add(&rpc->r_items, &item->ri_rpcobject_linkage);
		*rpcobj_size += item_size;
		*nfragments += current_fragments;
		c2_rpc_form_remove_rpcitem_from_summary_unit(endp_unit, item);
		item->ri_state = RPC_ITEM_ADDED;
		printf("Item added to rpc object. item_size = %lu, rpcobj_size = %lu\n", item_size, *rpcobj_size);
		/* If timer of rpc item is still running, change the
		   deadline in rpc item as per remaining time and
		   stop and fini the timer. */
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
		c2_list_del(&item->ri_unformed_linkage);

		session = item->ri_session;
		C2_ASSERT(session != NULL);
		if (c2_mutex_is_not_locked(&session->s_mutex)) {
			c2_mutex_lock(&session->s_mutex);
			session_locked = true;
		}
		/* If current added rpc item is unbound, remove it from
		   session->unbound_items list.*/
		if (c2_list_contains(&session->s_unbound_items,
					&item->ri_unbound_link)) {
			c2_list_del(&item->ri_unbound_link);
		}
		/* OR If current added rpc item is bound, remove it from
		   slot->ready_items list AND if slot->ready_items list
		   is empty, remove slot from rpcmachine->ready_slots list.*/
		else {
			slot = item->ri_slot_refs[0].sr_slot;
			C2_ASSERT(slot != NULL);
			c2_list_del(&item->ri_slot_link);
			c2_mutex_lock(&slot->sl_mutex);
			if (c2_list_is_empty(&slot->sl_ready_list)) {
				c2_list_del(&slot->sl_link);
			}
			c2_mutex_unlock(&slot->sl_mutex);
		}
		if (session_locked) {
			c2_mutex_unlock(&session->s_mutex);
		}
		return 0;
	}
	else {
		return -1;
	}
}

/**
   Coalesce the multiple write IO vectors into one.
 */
#if 0
static int c2_rpc_form_coalesce_writeio_vector(struct c2_fop_io_vec *item_vec,
		struct c2_list *aggr_list, int *nsegs)
{
	int						 i = 0;
	struct c2_rpc_form_write_segment		*write_seg = NULL;
	struct c2_rpc_form_write_segment		*write_seg_next = NULL;
	struct c2_rpc_form_write_segment		*new_seg = NULL;
	struct c2_rpc_form_write_segment		*new_seg_next = NULL;
	bool						 list_empty = true;

	C2_PRE(item_vec != NULL);
	C2_PRE(aggr_list != NULL);
	C2_PRE(nsegs != NULL);

	/* For all write segments in the write vector, check if they
	   can be merged with any of the segments from the aggregate list.
	   Merge if they can till all segments from write vector are
	   processed. */
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
				break;
			}
			/* If off2 + count2 = off1, then merge two segments.*/
			else if (write_seg->ws_seg.f_offset +
					write_seg->ws_seg.f_buf.f_count
					== item_vec->iov_seg[i].f_offset) {
				write_seg->ws_seg.f_buf.f_count +=
					item_vec->iov_seg[i].f_buf.f_count;
				break;
			}
			/* If (off1 + count1) < off2, OR
			   (off1 < off2),
			   add a new segment in the merged list. */
			else if (((item_vec->iov_seg[i].f_offset +
					item_vec->iov_seg[i].f_buf.f_count) <
					write_seg->ws_seg.f_offset) ||
					(item_vec->iov_seg[i].f_offset <
					 write_seg->ws_seg.f_offset)) {
				C2_ALLOC_PTR(new_seg);
				if (new_seg == NULL) {
					C2_ADDB_ADD(&formation_summary->
							is_rpc_form_addb,
							&c2_rpc_form_addb_loc,
							c2_addb_oom);
					return -ENOMEM;
				}
				(*nsegs)++;
				new_seg->ws_seg.f_offset = item_vec->
					iov_seg[i].f_offset;
				new_seg->ws_seg.f_buf.f_count = item_vec->
					iov_seg[i].f_buf.f_count;
				c2_list_add_before(&write_seg->ws_linkage,
						&new_seg->ws_linkage);
				break;
			}
		}
		/* If the loop has run till end of list or
		   if the list is empty, add the new write segment
		   in the list. */
		if ((&write_seg->ws_linkage == (void*)aggr_list)
				|| list_empty) {
			C2_ALLOC_PTR(new_seg);
			if (new_seg == NULL) {
				C2_ADDB_ADD(&formation_summary->
						is_rpc_form_addb,
						&c2_rpc_form_addb_loc,
						c2_addb_oom);

				return -ENOMEM;
			}
			new_seg->ws_seg.f_offset = item_vec->
				iov_seg[i].f_offset;
			new_seg->ws_seg.f_buf.f_count = item_vec->
				iov_seg[i].f_buf.f_count;
			c2_list_link_init(&new_seg->ws_linkage);
			c2_list_add_tail(aggr_list, &new_seg->ws_linkage);
			(*nsegs)++;
		}
	}
	/* Check if aggregate list can be contracted further by
	   merging adjacent segments. */
	c2_list_for_each_entry_safe(aggr_list, new_seg, new_seg_next,
			struct c2_rpc_form_write_segment, ws_linkage) {
		if ((new_seg->ws_seg.f_offset + new_seg->ws_seg.f_buf.f_count)
				== new_seg_next->ws_seg.f_offset) {
			new_seg_next->ws_seg.f_offset = new_seg->
				ws_seg.f_offset;
			new_seg_next->ws_seg.f_buf.f_count += new_seg->
				ws_seg.f_buf.f_count;
			c2_list_del(&new_seg->ws_linkage);
			c2_free(new_seg);
			(*nsegs)--;
		}
	}
	return 0;
}
#endif

/**
   Coalesce the multiple read IO vectors into one.
 */
#if 0
static int c2_rpc_form_coalesce_readio_vector(
		struct c2_fop_segment_seq *item_vec,
		struct c2_list *aggr_list, int *res_segs)
{
	int						 i = 0;
	struct c2_rpc_form_read_segment			*read_seg = NULL;
	struct c2_rpc_form_read_segment			*read_seg_next = NULL;
	struct c2_rpc_form_read_segment			*new_seg = NULL;
	struct c2_rpc_form_read_segment			*new_seg_next = NULL;
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
				break;
			}
			/* If off2 + count2 = off1, then merge two segments.*/
			else if ((read_seg->rs_seg.f_offset +
					read_seg->rs_seg.f_count) ==
					item_vec->fs_segs[i].f_offset) {
				read_seg->rs_seg.f_count +=
					item_vec->fs_segs[i].f_count;
				break;
			}
			/* If (off1 + count1) < off2, OR
			   if (off1 < off2),
			   add a new segment in the merged list. */
			else if ( ((item_vec->fs_segs[i].f_offset +
					item_vec->fs_segs[i].f_count) <
					read_seg->rs_seg.f_offset) ||
					((item_vec->fs_segs[i].f_offset <
					  read_seg-> rs_seg.f_offset)) ) {
				C2_ALLOC_PTR(new_seg);
				if (new_seg == NULL) {
					C2_ADDB_ADD(&formation_summary->
							is_rpc_form_addb,
							&c2_rpc_form_addb_loc,
							c2_addb_oom);
					return -ENOMEM;
				}
				(*res_segs)++;
				new_seg->rs_seg.f_offset = item_vec->fs_segs[i].
					f_offset;
				new_seg->rs_seg.f_count = item_vec->fs_segs[i].
					f_count;
				c2_list_link_init(&new_seg->rs_linkage);
				c2_list_add_before(&read_seg->rs_linkage,
						&new_seg->rs_linkage);
				break;
			}
		}
		/* If the loop has run till the end of list or
		   if the list is empty, add the new read segment
		   to the list. */
		if ((&read_seg->rs_linkage == (void*)aggr_list) || list_empty) {
			C2_ALLOC_PTR(new_seg);
			if (new_seg == NULL) {
				C2_ADDB_ADD(&formation_summary->
						is_rpc_form_addb,
						&c2_rpc_form_addb_loc,
						c2_addb_oom);
				return -ENOMEM;
			}
			new_seg->rs_seg.f_offset = item_vec->fs_segs[i].f_offset;
			new_seg->rs_seg.f_count = item_vec->fs_segs[i].f_count;
			c2_list_link_init(&new_seg->rs_linkage);
			c2_list_add_tail(aggr_list, &new_seg->rs_linkage);
			(*res_segs)++;
		}
	}
	/* Check if aggregate list can be contracted further by
	   merging adjacent segments. */
	c2_list_for_each_entry_safe(aggr_list, new_seg, new_seg_next,
			struct c2_rpc_form_read_segment, rs_linkage) {
		if ((new_seg->rs_seg.f_offset + new_seg->rs_seg.f_count)
				== new_seg_next->rs_seg.f_offset) {
			new_seg_next->rs_seg.f_offset = new_seg->
				rs_seg.f_offset;
			new_seg_next->rs_seg.f_count += new_seg->
				rs_seg.f_count;
			c2_list_del(&new_seg->rs_linkage);
			c2_free(new_seg);
			(*res_segs)--;
		}
	}
	return 0;
}
#endif

/**
   Try to coalesce items sharing same fid and intent(read/write).
 */
int c2_rpc_form_coalesce_fid_intent(struct c2_rpc_item *b_item,
		struct c2_rpc_form_item_coalesced *coalesced_item)
{
	/*
	struct c2_rpc_item				*item = NULL;
	struct c2_rpc_form_item_coalesced_member	*member = NULL;
	struct c2_rpc_form_item_coalesced_member	*member_next = NULL;
	struct c2_list					 aggr_vec_list;
	struct c2_fop_segment_seq			*item_read_vec = NULL;
	struct c2_fop_segment_seq			*read_vec = NULL;
	struct c2_fop_io_vec				*item_write_vec = NULL;
	struct c2_fop_io_vec				*write_vec = NULL;
	struct c2_rpc_form_read_segment			*read_seg = NULL;
	struct c2_rpc_form_read_segment			*read_seg_next = NULL;
	struct c2_rpc_form_write_segment		*write_seg = NULL;
	struct c2_rpc_form_write_segment		*write_seg_next = NULL;
	int						 curr_segs = 0;
	*/
	int						 res = 0;

	C2_PRE(b_item != NULL);
	C2_PRE(coalesced_item != NULL);

	printf("c2_rpc_form_coalesce_fid_intent entered.\n");
	res = b_item->ri_type->rit_ops->
		rio_io_coalesce((void*)coalesced_item, b_item);
	//res = c2_rpc_item_io_coalesce((void*)coalesced_item, b_item);
	return res;

#if 0
	/* Check if the bound item and the member unbound items
	   share same fid and intent(read/write). */
	opcode = coalesced_item->ic_op_intent;
        c2_list_for_each_entry_safe(&coalesced_item->ic_member_list, member,
                        member_next, struct c2_rpc_form_item_coalesced_member,
			im_linkage) {
                //C2_PRE((c2_rpc_item_io_get_opcode(member->im_member_item))
                 //               == opcode);
		C2_PRE(c2_rpc_item_equal(member->im_member_item,
					member_next->im_member_item));
        }

	/* Try to coalesce IO vectors of all member items with
	   IO vector of bound item. */
	c2_list_for_each_entry(&coalesced_item->ic_member_list, member,
			struct c2_rpc_form_item_coalesced_member, im_linkage) {
		item = member->im_member_item;
		if (opcode == C2_RPC_FORM_IO_READ) {
			item_read_vec = c2_rpc_item_read_get_vector(item);
			res = c2_rpc_form_coalesce_readio_vector(item_read_vec,
					&aggr_vec_list, &curr_segs);
		}
		else if (opcode == C2_RPC_FORM_IO_WRITE) {
			item_write_vec = c2_rpc_item_write_get_vector(item);
			res = c2_rpc_form_coalesce_writeio_vector(
					item_write_vec, &aggr_vec_list,
					&curr_segs);
		}
	}

	/* Build a resultant IO vector from a list of IO segments. */
	if (opcode == C2_RPC_FORM_IO_READ) {
		C2_ALLOC_PTR(read_vec);
		if (read_vec == NULL) {
			printf("Failed to allocate memory for struct\
					c2_fop_segment_seq.\n");
			return -ENOMEM;
		}
		C2_ASSERT(curr_segs == c2_list_length(&aggr_vec_list));
		C2_ALLOC_ARR(read_vec->fs_segs, curr_segs);
		if (read_vec->fs_segs == NULL) {
			printf("Failed to allocate memory for struct\
					c2_fop_segment.\n");
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
		/* Assign this vector to the current bound rpc item. */
		c2_rpc_item_set_read_vec(b_item, read_vec);
	}
	else if (opcode == C2_RPC_FORM_IO_WRITE) {
		C2_ALLOC_PTR(write_vec);
		if (write_vec == NULL) {
			printf("Failed to allocate memory for struct\
					c2_fop_io_vec.\n");
			return -ENOMEM;
		}
		C2_ASSERT(curr_segs == c2_list_length(&aggr_vec_list));
		C2_ALLOC_ARR(write_vec->iov_seg, curr_segs);
		if (write_vec->iov_seg == NULL) {
			printf("Failed to allocate memory for struct\
					c2_fop_io_seg.\n");
			return -ENOMEM;
		}
		c2_list_for_each_entry_safe(&aggr_vec_list, write_seg,
				write_seg_next, struct
				c2_rpc_form_write_segment, ws_linkage) {
			write_vec->iov_seg[i] = write_seg->ws_seg;
			c2_list_del(&write_seg->ws_linkage);
			c2_free(write_seg);
			i++;
		}
		/* Assign this vector to the current bound rpc item. */
		c2_rpc_item_set_write_vec(b_item, write_vec);
	}
	coalesced_item->ic_resultant_item = b_item;
	c2_list_fini(&aggr_vec_list);
	C2_POST(coalesced_item->ic_resultant_item != NULL);
	return 0;
#endif
}

/**
   Try to coalesce rpc items from the session->free list.
 */
int c2_rpc_form_try_coalesce(struct c2_rpc_form_item_summary_unit *endp_unit,
		struct c2_rpc_item *item, uint64_t *rpcobj_size)
{
	struct c2_rpc_item				*ub_item = NULL;
	struct c2_rpc_session				*session = NULL;
	struct c2_fid					 fid;
	struct c2_fid					 ufid;
	uint64_t					 item_size = 0;
	struct c2_rpc_form_item_coalesced		*coalesced_item = NULL;
	struct c2_rpc_form_item_coalesced_member	*coalesced_member =
		NULL;
	struct c2_rpc_form_item_coalesced_member	*coalesced_member_next =
		NULL;
	uint64_t					 old_size = 0;
	bool						 coalesced_item_found =
		false;
	int						 res = 0;
	int						 item_rw = 0;
	bool						 item_equal = 0;

	C2_PRE(item != NULL);
	C2_PRE(endp_unit != NULL);
	//session = item->ri_slot_refs[0].sr_slot->sl_session;
	session = item->ri_session;
	C2_ASSERT(session != NULL);
	old_size = item->ri_type->rit_ops->rio_item_size(item);
	//old_size = c2_rpc_item_size(item);

	printf("c2_rpc_form_try_coalesce entered.\n");
	/* If there are no unbound items to coalesce,
	   return right away. */
	c2_mutex_lock(&session->s_mutex);
	if (!c2_list_length(&session->s_unbound_items)) {
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
	//fid = c2_rpc_item_io_get_fid(item);
	item_size = item->ri_type->rit_ops->rio_item_size(item);
	//item_size = c2_rpc_item_size(item);


	/* Find out/Create the coalesced_item struct for this bound item. */
	/* If fid and intent(read/write) of current unbound item matches
	   with fid and intent of the bound item, see if a corresponding
	   struct c2_rpc_form_item_coalesced exists for this
	   {fid, intent} tuple. */
	c2_list_for_each_entry(&endp_unit->isu_coalesced_items_list,
			coalesced_item, struct c2_rpc_form_item_coalesced,
			ic_linkage) {
		item_rw = item->ri_type->rit_ops->rio_io_get_opcode(item);
		if (c2_fid_eq(&fid, &coalesced_item->ic_fid)
				&& (coalesced_item->ic_op_intent == item_rw)) {
			coalesced_item_found = true;
			break;
		}
	}
	/* If such a coalesced_item does not exist, create one. */
	if (!coalesced_item_found) {
		C2_ALLOC_PTR(coalesced_item);
		if (!coalesced_item) {
			C2_ADDB_ADD(&formation_summary->is_rpc_form_addb
					, &c2_rpc_form_addb_loc, c2_addb_oom);
			return -ENOMEM;
		}
		c2_list_link_init(&coalesced_item->ic_linkage);
		coalesced_item->ic_fid = fid;
		coalesced_item->ic_op_intent = item->ri_type->
			rit_ops->rio_io_get_opcode(item);
		//coalesced_item->ic_op_intent =
		//	c2_rpc_item_get_opcode(item);
		coalesced_item->ic_resultant_item = NULL;
		coalesced_item->ic_nmembers = 0;
		c2_list_init(&coalesced_item->ic_member_list);
		/* Add newly created coalesced_item into list of
		   isu_coalesced_items_list in endp_unit.*/
		c2_list_add(&endp_unit->isu_coalesced_items_list,
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
		//ufid = c2_rpc_item_io_get_fid(ub_item);
		item_equal = item->ri_type->rit_ops->
			rio_items_equal(item, ub_item);
		if (c2_fid_eq(&fid, &ufid) && item_equal)
		{
			coalesced_item->ic_nmembers++;
			C2_ALLOC_PTR(coalesced_member);
			if (!coalesced_member) {
				C2_ADDB_ADD(&formation_summary->
						is_rpc_form_addb,
						&c2_rpc_form_addb_loc,
						c2_addb_oom);
				return -ENOMEM;
			}
			/* Create a new struct c2_rpc_form_item_coalesced
			   _member and add it to the list(ic_member_list)
			   of coalesced_item above. */
			coalesced_member->im_member_item = ub_item;
			c2_list_link_init(&coalesced_member->im_linkage);
			c2_list_add(&coalesced_item->ic_member_list,
					&coalesced_member->im_linkage);
		}
	}
	c2_mutex_unlock(&session->s_mutex);

	/* If no coalesced_item could be made, return from the function.*/
	if (!coalesced_item) {
		return 0;
	}
	/* If number of member rpc items in the current coalesced_item
	   struct are less than 2, reject the coalesced_item
	   and return back. */
	if (coalesced_item->ic_nmembers < 2) {
		c2_list_for_each_entry_safe(&coalesced_item->ic_member_list,
				coalesced_member, coalesced_member_next,
				struct c2_rpc_form_item_coalesced_member,
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

	printf("A coalesced_item struct created successfully.\n");
	/* Add the bound item to the list of member rpc items in
	   coalesced_item structure so that it will be coalesced as well. */
	C2_ALLOC_PTR(coalesced_member);
	if (!coalesced_member) {
		C2_ADDB_ADD(&formation_summary->is_rpc_form_addb,
				&c2_rpc_form_addb_loc, c2_addb_oom);
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
	res = c2_rpc_form_coalesce_fid_intent(item, coalesced_item);
	if (res == 0) {
		if (item->ri_state != RPC_ITEM_ADDED) {
			item->ri_state = RPC_ITEM_ADDED;
		}
		*rpcobj_size -= old_size;
		*rpcobj_size += item->ri_type->rit_ops->
			rio_item_size(item);
		//*rpcobj_size += c2_rpc_item_size(item);
		/* Delete all members items for which coalescing was
		   successful from the session->free list. */
		c2_list_for_each_entry(&coalesced_item->ic_member_list,
				coalesced_member, struct
				c2_rpc_form_item_coalesced_member, im_linkage) {
			c2_list_del(&coalesced_member->im_member_item->
					ri_unbound_link);
		}
	}
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
	struct c2_rpc_form_item_coalesced		*coalesced_item = NULL;
	uint64_t					 rpcobj_size = 0;
	uint64_t					 item_size = 0;
	struct c2_rpc_form_item_summary_unit_group	*sg = NULL;
	struct c2_rpc_form_item_summary_unit_group	*sg_partial = NULL;
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
	struct c2_rpcmachine				*rpcmachine = NULL;
	struct c2_rpc_session				*session = NULL;
	struct c2_rpc_slot				*slot = NULL;
	struct c2_rpc_slot				*slot_next = NULL;
	struct c2_rpc_item				*ub_item = NULL;
	struct c2_rpc_item				*ub_item_next = NULL;
	uint64_t					 counter = 0;
	bool						 slot_found = false;

	C2_PRE(item != NULL);
	C2_PRE((event->se_event == C2_RPC_FORM_EXTEVT_RPCITEM_REPLY_RECEIVED)
			|| (event->se_event ==
				C2_RPC_FORM_EXTEVT_RPCITEM_TIMEOUT) ||
			(event->se_event == C2_RPC_FORM_INTEVT_STATE_SUCCEEDED));
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));

	endp_unit->isu_sm.isu_endp_state = C2_RPC_FORM_STATE_CHECKING;

	/* If isu_rpcobj_formed_list is not empty, it means an rpc
	   object was formed successfully some time back but it
	   could not be sent due to some error conditions.
	   We send it back here again. */
	if(!c2_list_is_empty(&endp_unit->isu_rpcobj_formed_list)) {
		return C2_RPC_FORM_INTEVT_STATE_SUCCEEDED;
	}
	if(c2_list_is_empty(&endp_unit->isu_unformed_list)) {
		return C2_RPC_FORM_INTEVT_STATE_FAILED;
	}
	printf("In state: checking\n");
	/** Returning failure will lead the state machine to
	    waiting state and then the thread will exit the
	    state machine. */
	if (endp_unit->isu_curr_rpcs_in_flight ==
			endp_unit->isu_max_rpcs_in_flight) {
		return C2_RPC_FORM_INTEVT_STATE_FAILED;
	}

	/* Create an rpc object in endp_unit->isu_rpcobj_checked_list. */
	C2_ALLOC_PTR(rpcobj);
	if (rpcobj == NULL) {
		C2_ADDB_ADD(&formation_summary->is_rpc_form_addb,
				&c2_rpc_form_addb_loc, c2_addb_oom);
		return C2_RPC_FORM_INTEVT_STATE_FAILED;
	}
	c2_list_link_init(&rpcobj->ro_linkage);
	C2_ALLOC_PTR(rpcobj->ro_rpcobj);
	if (rpcobj->ro_rpcobj == NULL) {
		C2_ADDB_ADD(&formation_summary->is_rpc_form_addb,
				&c2_rpc_form_addb_loc, c2_addb_oom);
		return C2_RPC_FORM_INTEVT_STATE_FAILED;
	}
	c2_list_link_init(&rpcobj->ro_rpcobj->r_linkage);
	c2_list_init(&rpcobj->ro_rpcobj->r_items);

	if (event->se_event == C2_RPC_FORM_EXTEVT_RPCITEM_REPLY_RECEIVED) {
		c2_list_for_each_entry(&endp_unit->isu_coalesced_items_list,
				coalesced_item, struct
				c2_rpc_form_item_coalesced, ic_linkage) {
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
		}
	}
	else if (event->se_event == C2_RPC_FORM_EXTEVT_RPCITEM_TIMEOUT) {
		if (item->ri_state != RPC_ITEM_SUBMITTED) {
			return C2_RPC_FORM_INTEVT_STATE_FAILED;
		}
		res = c2_rpc_form_item_add_to_forming_list(endp_unit, item,
				&rpcobj_size, &nfragments, rpcobj->ro_rpcobj);
		/* If item can not be added due to size restrictions, move it
		   to the head of unformed list and it will be handled on
		   next formation attempt.*/
		if (res != 0) {
			c2_list_move(&endp_unit->isu_unformed_list,
					&item->ri_unformed_linkage);
		}
		else {
			urgent_items = true;
		}
	}
	/* Iterate over the c2_rpc_form_item_summary_unit_group list in the
	   endpoint structure to find out which rpc groups can be included
	   in the rpc object. */
	c2_list_for_each_entry(&endp_unit->isu_groups_list, sg,
			struct c2_rpc_form_item_summary_unit_group,
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
			sg_partial = sg;
			break;
		}
	}

	/* Core of formation algorithm. */
	printf("Length of unformed list = %lu\n", c2_list_length(&endp_unit->
				isu_unformed_list));
	c2_list_for_each_entry_safe(&endp_unit->isu_unformed_list, rpc_item,
			rpc_item_next, struct c2_rpc_item,
			ri_unformed_linkage) {
		counter++;
		item_size = rpc_item->ri_type->rit_ops->
			rio_item_size(rpc_item);
		//item_size = c2_rpc_item_size(rpc_item);
		/* 1. If there are urgent items, form them immediately. */
		if (rpc_item->ri_deadline == 0) {
			if (urgent_items == false) {
				urgent_items = true;
			}
			if (rpc_item->ri_state != RPC_ITEM_SUBMITTED) {
				continue;
			}
			res = c2_rpc_form_item_add_to_forming_list(endp_unit,
					rpc_item, &rpcobj_size,
					&nfragments, rpcobj->ro_rpcobj);
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
			if (ncurrent_groups > nselected_groups) {
				break;
			}
			if (sg_partial && (rpc_item->ri_group ==
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
			res = c2_rpc_form_item_add_to_forming_list(endp_unit,
					rpc_item, &rpcobj_size, &nfragments,
					rpcobj->ro_rpcobj);
			if (res != 0) {
				break;
			}
			item_added = false;
		}
		/* Try to coalesce items from session->free list
		   with the current rpc item. */
		res = c2_rpc_form_try_coalesce(endp_unit, rpc_item,
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
			//item_size = c2_rpc_item_size(rpc_item);
			if ((partial_size - item_size) <= 0) {
				break;
			}
			if(c2_list_contains(&endp_unit->isu_unformed_list,
						&rpc_item->ri_unformed_linkage)) {

				partial_size -= item_size;
				res = c2_rpc_form_item_add_to_forming_list(
						endp_unit, rpc_item,
						&rpcobj_size, &nfragments,
						rpcobj->ro_rpcobj);
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
			if (!c2_list_length(&slot->sl_ready_list)) {
				slot_found = true;
				break;
			}
		}
		if (!slot_found) {
			printf("No ready slots found in rpcmachine.\n");
			break;
		}
		/* Now that the item is bound, remove it from
		   session->free list. */
		res = c2_rpc_form_item_add_to_forming_list(endp_unit, ub_item,
				&rpcobj_size, &nfragments, rpcobj->ro_rpcobj);
		if (res != 0) {
			break;
		}
		item_add_internal(slot, ub_item);
	}
	c2_mutex_unlock(&rpcmachine->cr_ready_slots_mutex);
	c2_mutex_unlock(&session->s_mutex);

	/* If there are no urgent items in the formed rpc object,
	   send the rpc for submission only if it is optimal.
	   For rpc objects containing urgent items, there are chances
	   that it will be passed for submission even if it is
	   sub-optimal. */
	if (!urgent_items) {
		/* If size of formed rpc object is less than 90% of
		   max_message_size, discard the rpc object. */
		if (rpcobj_size < ((0.9) * endp_unit->isu_max_message_size)) {
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

	/* For every rpc item from the rpc object, call
	   sessions_prepare() and get the slot and sequence number info. */
	c2_list_for_each_entry_safe(&endp_unit->isu_rpcobj_checked_list,
			rpcobj, rpcobj_next, struct c2_rpc_form_rpcobj,
			ro_linkage) {
		c2_list_for_each_entry_safe(&rpcobj->ro_rpcobj->r_items,
				rpc_item, rpc_item_next,
				struct c2_rpc_item, ri_rpcobject_linkage) {
			res = 0;
			if (res != 0) {
				/* If sessions_prepare() fails, add the
				   rpc item back to the unformed list
				   and it will be considered for formation
				   the next time. */
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
			res = c2_net_send(endp, rpc_obj->ro_rpcobj);
			/* XXX curr rpcs in flight will be taken care by
			   output component. */
			//endp_unit->isu_curr_rpcs_in_flight++;
			if(res == 0) {
				c2_list_del(&rpc_obj->ro_linkage);
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
   Function to map the on-wire FOP format to in-core FOP format.
 */
void c2_rpc_form_item_io_fid_wire2mem(struct c2_fop_file_fid *in,
		struct c2_fid *out)
{
        out->f_container = in->f_seq;
        out->f_key = in->f_oid;
}

struct c2_update_stream *c2_rpc_get_update_stream(struct c2_rpc_item *item)
{
	return NULL;
}

/**
   Retrieve slot and verno information from sessions component
   for an unbound item.
 */
void item_add_internal(struct c2_rpc_slot *slot, struct c2_rpc_item *item)
{
	C2_PRE(item != NULL);
	C2_PRE(slot != NULL);
	item->ri_slot_refs[0].sr_slot = slot;
}

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

/**
   Coalesce rpc items that share same fid and intent(read/write)
 */
int c2_rpc_item_io_coalesce(void *c_item, struct c2_rpc_item *b_item)
{
	struct c2_list					 fop_list;
	struct c2_rpc_form_item_coalesced_member	*c_member = NULL;
	struct c2_io_fop_member				*fop_member = NULL;
	struct c2_io_fop_member				*fop_member_next = NULL;
	struct c2_fop					*fop = NULL;
	struct c2_fop					*b_fop = NULL;
	struct c2_rpc_item				*item = NULL;
	struct c2_rpc_form_item_coalesced		*coalesced_item = NULL;
	int						 res = 0;

	printf("c2_rpc_item_io_coalesce entered.\n");
	C2_PRE(b_item != NULL);
	coalesced_item = (struct c2_rpc_form_item_coalesced*)c_item;
	C2_PRE(coalesced_item != NULL);
	c2_list_init(&fop_list);
	c2_list_for_each_entry(&coalesced_item->ic_member_list, c_member,
			struct c2_rpc_form_item_coalesced_member, im_linkage) {
		C2_ALLOC_PTR(fop_member);
		if (fop_member == NULL) {
			printf("Failed to allocate memory for struct\
					c2_io_fop_member.\n");
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

