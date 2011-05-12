#include "formation.h"

/**
   There will be only one instance of struct c2_rpc_form_item_summary
   since this structure represents data for all endpoints.
 */
struct c2_rpc_form_item_summary 	formation_summary;

/**     
   Initialization for formation component in rpc. 
   This will register necessary callbacks and initialize
   necessary data structures.
 */     
int c2_rpc_form_init()
{
	c2_rwlock_init(&formation_summary.is_endp_list_lock);
}

/**
   Finish method for formation component in rpc.
   This will deallocate all memory claimed by formation
   and do necessary cleanup.
 */
int c2_rpc_form_fini()
{
	c2_rwlock_fini(&formation_summary.is_endp_list_lock);
}

/**
   Exit path from a state machine. An incoming thread which executed
   the formation state machine so far, is let go and it will return
   to do its own job. 
 */
void c2_rpc_form_state_machine_exit(struct c2_rpc_form_item_summary_unit 
		*endp_unit)
{
	c2_rwlock_write_lock(&formation_summary.is_endp_list_lock);
	c2_ref_put(&endp_unit->isu_ref);
	c2_rwlock_write_unlock(&formation_summary.is_endp_list_lock);
}

/**
   Destroy an endpoint structure since it no longer contains
   any rpc items.
 */
void c2_rpc_form_item_summary_unit_destroy(struct c2_ref *ref)
{
	struct c2_rpc_form_item_summary_unit	*endp_unit;

	endp_unit = container_of(ref, struct c2_rpc_form_item_summary_unit, isu_ref);
	c2_mutex_fini(endp_unit->isu_unit_lock);
	c2_list_del(endp_unit->isu_linkage);
	c2_list_fini(endp_unit->isu_groups_list);
	c2_list_fini(endp_unit->isu_coalesced_items_list);
	c2_list_fini(endp_unit->isu_fid_list);
	c2_list_fini(endp_unit->isu_unformed_items);
	c2_mutex_fini(&endp_unit->isu_rpcobj_formed_list.rl_lock);
	c2_mutex_fini(&endp_unit->isu_rpcobj_checked_list.rl_lock);
	c2_free(endp_unit);
}

/**
   Add an endpoint structure when the first rpc item gets added
   for an endpoint.
 */
struct c2_rpc_form_item_summary_unit *c2_rpc_form_item_summary_unit_add(struct c2_net_endpoint endp)
{
	struct c2_rpc_form_item_summary_unit	*endp_unit;

	endp_unit = c2_alloc(sizeof(struct c2_rpc_form_item_summary_unit));
	c2_list_add(formation_summary.is_endp_list.l_head, endp_unit->isu_linkage);
	c2_list_init(endp_unit->isu_groups_list);
	c2_list_init(endp_unit->isu_coalesced_items_list);
	c2_list_init(endp_unit->isu_unformed_list);
	c2_list_init(endp_unit->isu_fid_list);
	c2_mutex_init(&endp_unit->isu_rpcobj_formed_list.rl_lock);
	c2_mutex_init(&endp_unit->isu_rpcobj_checked_list.rl_lock);
	c2_ref_init(&endp_unit->isu_ref, 1, c2_rpc_form_item_summary_unit_destroy);
}

/**
   Return the function pointer to next state given the current state
   and current event as input.
   @param current_state - current state of state machine.
   @param current_event - current event posted to the state machine.
 */
stateFunc c2_rpc_form_next_state(int current_state, int current_event)
{
	C2_PRE(current_state < C2_RPC_FORM_N_STATES);
	C2_PRE(current_event < C2_RPC_FORM_INTEVT_N_EVENTS);
	return c2_rpc_form_stateTable[current_state][current_event];
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
int c2_rpc_form_default_handler(struct c2_rpc_item *item, 
		struct c2_rpc_form_item_summary_unit *endp_unit, 
				int sm_state, int sm_event, void *pvt)
{
	int 					 endpoint = 0;
	int 					 res = 0;
	int 					 prev_state = 0;
	bool 					 found = false;
	bool					 exit = false;

	C2_PRE(item != NULL);
	C2_PRE(current_event < C2_RPC_FORM_INTEVT_N_EVENTS);

	endpoint = c2_rpc_form_get_endpoint(item);
	if (endp_unit == NULL) {
		c2_rwlock_write_lock(&formation_summary.is_endp_list_lock);
		c2_list_for_each_entry(&formation_summary.is_endp_list.l_head, endp_unit, struct c2_rpc_form_item_summary_unit, isu_linkage) {
			if (endp_unit->isu_endp_id == endpoint) {
				found = true;
				break;
			}
		}
		if (found == true) {
			c2_mutex_lock(endp_unit->isu_unit_lock);
			c2_ref_get(&endp_unit->isu_ref);
			c2_rwlock_write_unlock(&formation_summary.is_endp_list_lock);
			prev_state = endp_unit->isu_endp_state;
		}
		else {
			/** XXX Add a new summary unit */
			endp_unit = c2_rpc_form_item_summary_unit_add(endpoint);
			c2_rwlock_write_unlock(&formation_summary.is_endp_list_lock);
			c2_mutex_lock(endp_unit->isu_unit_lock);
			prev_state = sm_state;
		}
	}
	res = (c2_rpc_form_next_state(prev_state, sm_event))(endp_unit, item, sm_event, pvt);
	prev_state = endp_unit->isu_endp_state;
	c2_mutex_unlock(endp_unit->isu_unit_lock);
	/*XXX Handle exit path as an event. */
	/* Exit point for state machine. */
	if(res == C2_RPC_FORM_INTEVT_STATE_DONE)
		return 0;

	if (res == C2_RPC_FORM_INTEVT_STATE_FAILED) {
		/** Post a state failed event. */
		c2_rpc_form_intevt_state_failed(endp_unit, item, prev_state);
	}
	else if (res == C2_RPC_FORM_INTEVT_STATE_SUCCEEDED){
		/** Post a state succeeded event. */
		c2_rpc_form_intevt_state_succeeded(endp_unit, item, prev_state);
	}
}

/**
   Callback function for addition of an rpc item to the rpc items cache.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_added_in_cache(struct c2_rpc_item *item)
{
	printf("In callback: c2_rpc_form_extevt_rpcitem_added_in_cache\n");
	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(item, NULL,
			C2_RPC_FORM_N_STATES, C2_RPC_FORM_EXTEVT_RPCITEM_ADDED, NULL);
}

/**
   Callback function for deletion of an rpc item from the rpc items cache.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */ 
int c2_rpc_form_extevt_rpcitem_deleted_from_cache(struct c2_rpc_item *item)
{
	printf("In callback: c2_rpc_form_extevt_rpcitem_deleted_from_cache\n");
	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(item, NULL,
			C2_RPC_FORM_N_STATES, C2_RPC_FORM_EXTEVT_RPCITEM_REMOVED, NULL);
}

/**
   Callback function for change in parameter of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_changed(struct c2_rpc_item *item, int field_type, void *pvt)
{
	struct c2_rpc_form_item_change_req 	req;
	printf("In callback: c2_rpc_form_extevt_rpcitem_changed\n");

	req.field_type = field_type;
	req.value = pvt;
	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(item, NULL,
			C2_RPC_FORM_N_STATES, C2_RPC_FORM_EXTEVT_RPCITEM_CHANGED, &req);
}

/**
   Callback function for reply received of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_reply_received(struct c2_rpc_item *item)
{
	printf("In callback: c2_rpc_form_extevt_rpcitem_reply_received\n");
	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(item, NULL,
			C2_RPC_FORM_N_STATES, C2_RPC_FORM_EXTEVT_RPCITEM_REPLY_RECEIVED, NULL);
}

/**
   Callback function for deadline expiry of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_deadline_expired(struct c2_rpc_item *item)
{
	printf("In callback: c2_rpc_form_extevt_rpcitem_deadline_expired\n");
	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(item, NULL,
			C2_RPC_FORM_N_STATES, C2_RPC_FORM_EXTEVT_RPCITEM_TIMEOUT, NULL);
}

/**
   Callback function for successful completion of a state.
   Call the default handler function. Depending upon the
   input state, the default handler will invoke the next state
   for state succeeded event.
   @param state - previous state of state machine.
 */
int c2_rpc_form_intevt_state_succeeded(struct 
		c2_rpc_form_item_summary_unit *endp_unit, 
		struct c2_rpc_item *item, int state)
{
	printf("In callback: c2_rpc_form_intevt_state_succeeded\n");
	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(item, endp_unit,
			state, C2_RPC_FORM_INTEVT_STATE_SUCCEEDED);
}

/**
   Callback function for failure of a state.
   Call the default handler function. Depending upon the
   input state, the default handler will invoke the next state
   for state failed event.
   @param state - previous state of state machine.
 */
int c2_rpc_form_intevt_state_failed(struct 
		c2_rpc_form_item_summary_unit *endp_unit, 
		struct c2_rpc_item *item, int state)
{
	printf("In callback: c2_rpc_form_intevt_state_failed\n");
	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(item, endp_unit,
			state, C2_RPC_FORM_INTEVT_STATE_FAILED);
}

/**
   Call the completion callbacks for member rpc items of 
   a coalesced rpc item. 
 */
int c2_rpc_form_item_coalesced_reply_post(struct c2_rpc_form_item_summary_unit *endp_unit, struct c2_rpc_form_item_coalesced *coalesced_struct)
{
	int 						rc = 0;
	struct c2_rpc_form_item_coalesced_member	*member;
	struct c2_rpc_form_item_coalesced_member	*next_member;
	C2_PRE(coalesced_struct != NULL);

	c2_list_for_each_entry_safe(&coalesced_struct->ic_member_list->l_head, member, next_member, struct c2_rpc_form_item_coalesced_member, ic_member_list) {
		/* XXX what is rc for completion callback?*/
		member->im_member_item->rio_replied(member->im_member, rc);
		c2_list_del(member->im_linkage);
		member->ic_nmembers--;
	}
	c2_list_fini(coalesced_struct->ic_member_list);
	c2_list_del(coalesced_struct->ic_linkage);
	c2_free(coalesced_struct);
	return 0;
}

/**
   State function for WAITING state.
   endp_unit is locked.
 */
int c2_rpc_form_waiting_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, int event, void *pvt)
{
	C2_PRE(item != NULL);
	C2_PRE((event == C2_RPC_FORM_INTEVT_STATE_SUCCEEDED) || 
			(event == C2_RPC_FORM_INTEVT_STATE_FAILED));
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));

	printf("In state: waiting\n");
	endp_unit->isu_endp_state = C2_RPC_FORM_STATE_WAITING;
	/* Internal events will invoke a nop from waiting state. */

	c2_rpc_form_state_machine_exit(endp_unit);
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
	c2_rpc_form_extevt_rpcitem_deadline_expired(item);
}

/**
   Change the data of an rpc item embedded within the endpoint unit
   structure.
 */
int c2_rpc_form_change_rpcitem_from_summary_unit(struct c2_rpc_form_item_summary_unit *endp_unit, struct c2_rpc_item *item, void *pvt)
{
	int					 res = 0;
	struct c2_rpc_form_item_change_req 	*chng_req = NULL;
	int					 field_type = 0;

	C2_PRE(endp_unit != NULL);
	C2_PRE(item != NULL);
	C2_PRE(pvt != NULL);

	chng_req = (struct c2_rpc_form_item_change_req*)pvt;
	field_type = chng_req->field_type;

	res = c2_rpc_form_remove_rpcitem_from_summary_unit(endp_unit, item);
	if (res != 0) {
		printf("Failed to remove data of an rpc item from summary unit.\n");
		return -1;
	}
	switch (field_type) {
		case C2_RPC_ITEM_CHANGE_PRIORITY:
			item->ri_prio = (enum c2_rpc_item_priority)chng_req->value;
		case C2_RPC_ITEM_CHANGE_DEADLINE:
			if (item->ri_state == RPC_ITEM_SUBMITTED) {
				c2_timer_stop(item->timer);
				c2_timer_fini(item->timer);
				item->ri_deadline = (struct c2_time_t)chng_req->value;
			}
		case C2_RPC_ITEM_CHANGE_RPCGROUP:
			item->ri_group = (struct c2_rpc_group*)chng_req->value;

	}
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
int c2_rpc_form_remove_rpcitem_from_summary_unit(struct c2_rpc_form_item_summary_unit *endp_unit, struct c2_rpc_item *item)
{
	int						  res = 0;
	struct c2_rpc_form_item_summary_unit_group	 *summary_group = NULL;
	bool						  found = false;
	struct c2_timer					 *item_timer = NULL;
	uint64_t					  item_size = 0;

	C2_PRE(item != NULL);
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));
	C2_PRE(item->ri_state == RPC_ITEM_SUBMITTED);

	c2_list_for_each_entry(&endp_unit->isu_groups_list.l_head, summary_group, struct c2_rpc_form_item_summary_unit_group, sug_linkage) {
		if (summary_group->sug_group == item->ri_group) {
			found = true;
			break;
		}
	}
	if (found == false) {
		return 0;
	}

	summary_group->sug_expected_items -= item->ri_group->rg_expected;
	if (item->ri_prio == C2_RPC_ITEM_PRIO_MAX && 
			summary_group->sug_priority_items > 0) {
		summary_group->sug_priority_items--;
	}
	/*XXX struct c2_rpc_item_type_ops will have a rio_item_size
	 method to find out size of rpc item. */
	summary_group->sug_total_size -= item->ri_type->rit_ops->rio_item_size(item);
	summary_group->sug_avg_timeout = ((summary_group->sug_nitems * summary_group->sug_avg_timeout) - item->ri_deadline.tv_sec) / (summary_group->sug_nitems);
	summary_group->sug_nitems--;

	return 0;
}

/**
   Sort the c2_rpc_form_item_summary_unit_group structs according to 
   increasing value of average timeout.
 */
int c2_rpc_form_summary_groups_sort(struct c2_rpc_form_item_summary_unit *endp_unit, struct c2_rpc_form_item_summary_unit_group *summary_group)
{
	int 	list_length = 0;
	int	i = 0;
	struct c2_rpc_form_item_summary_unit_group	*sg = NULL;

	C2_PRE(endp_unit != NULL);
	C2_PRE(summary_group != NULL);

	c2_list_del(summary_group->sug_linkage);
	/* Do a simple incremental search for a group having 
	   average timeout value bigger than that of given group. */
	list_length = c2_list_length(&endp_unit->isu_groups_list);
	c2_list_for_each_entry(&endp_unit->isu_groups_list.l_head, sg, struct c2_rpc_form_item_summary_unit_group, sug_linkage) {
		if (sg->sug_avg_timeout > summary_group->sug_avg_timeout) {
			c2_list_add_before(sg->sug_linkage, summary_group->sug_linkage);
		}
	}
	return 0;
}

/**
   Update the summary_unit data structure on addition of
   an rpc item. */
int c2_rpc_form_add_rpcitem_to_summary_unit(struct c2_rpc_form_item_summary_unit *endp_unit, struct c2_rpc_item *item)
{
	int						  res = 0;
	struct c2_rpc_form_item_summary_unit_group	 *summary_group = NULL;
	bool						  found = false;
	struct c2_timer					 *item_timer = NULL;
	struct c2_fid					  fid = NULL;

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

	c2_list_add(&endp_unit->isu_unformed_items.l_head, 
			item->ri_unformed_linkage);
	c2_list_for_each_entry(&endp_unit->isu_groups_list.l_head, summary_group, struct c2_rpc_form_item_summary_unit_group, sug_linkage) {
		if (summary_group->sug_group == item->ri_group) {
			found = true;
			break;
		}
	}
	if (found == false) {
		summary_group = c2_alloc(sizeof(struct c2_rpc_form_item_summary_unit_group));
		if(summary_group == NULL) {
			printf("Failed to allocate memory for a new c2_rpc_form_item_summary_unit_group structure.\n");
			return -1;
		}
		if (item->ri_group == NULL) {
			printf("Creating a c2_rpc_form_item_summary_unit_group\
					struct for items with no groups.\n");
		}
	}

	summary_group->sug_expected_items = item->ri_group->rg_expected;
	summary_group->sug_group = item->ri_group;
	if (item->ri_prio == C2_RPC_ITEM_PRIO_MAX)
		summary_group->sug_priority_items++;
	/*XXX struct c2_rpc_item_type_ops will have a rio_item_size
	 method to find out size of rpc item. */
	summary_group->sug_total_size += item->ri_type->rit_ops->rio_item_size(item);
	summary_group->sug_avg_timeout = ((summary_group->sug_nitems * summary_group->sug_avg_timeout) + item->ri_deadline.tv_sec) / (summary_group->sug_nitems + 1);
	summary_group->sug_nitems++;
	/* Put the corresponding c2_rpc_form_item_summary_unit_group
	   struct in its correct position on least value first basis of
	   average timeout of group. */
	if (item->ri_group != NULL) {
		res = c2_rpc_form_summary_groups_sort(endp_unit, summary_group);
	}
	/* Special handling for group of items which belong to no group,
	   so that they are handled as the last option for formation. */
	if (item->ri_group == NULL) {
		c2_list_move_tail(summary_group->sug_linkage);
	}

	/* Init and start the timer for rpc item. */
	item_timer = c2_alloc(sizeof(struct c2_timer));
	if (item_timer == NULL) {
		printf("Failed to allocate memory for a new c2_timer struct.\n");
		return -1;
	}
	/* C2_TIMER_SOFT creates a different thread to handle the
	   callback. */
	c2_timer_init(&item_timer, C2_TIMER_SOFT, &item->ri_deadline, 0, c2_rpc_form_item_timer_callback, (unsigned long)item);
	res = c2_timer_start(&item_timer);
	if (res != 0) {
		printf("Failed to start the timer for rpc item.\n");
		return -1;
	}
	item->timer = item_timer;
	/* Assumption: c2_rpc_item_type_ops methods can access
	   the fields of corresponding fop. */
	return 0;
}

/**
   State function for UPDATING state.
 */
int c2_rpc_form_updating_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, int event, void *pvt)
{
	int 				res = 0;
	int 				ret = 0;

	C2_PRE(item != NULL);
	C2_PRE(event == C2_RPC_FORM_EXTEVT_RPCITEM_ADDED);
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));

	printf("In state: updating\n");
	endp_unit->isu_endp_state = C2_RPC_FORM_STATE_UPDATING;

	res = c2_rpc_form_add_rpcitem_to_summary_unit(endp_unit, rpc_item);
	if (res == 0)
		ret = C2_RPC_FORM_INTEVT_STATE_SUCCEEDED;
	else
		ret = C2_RPC_FORM_INTEVT_STATE_FAILED;

	return ret;
}

/**
   Add an rpc item to the formed list of an rpc object.
 */
int c2_rpc_form_item_add_to_forming_list(struct c2_rpc_form_item_summary_unit *endp_unit, struct c2_rpc_item *item,
		uint64_t *rpcobj_size, unit64_t *nfragments, struct c2_list *forming_list)
{
	int 					 res = 0;
	uint64_t				 item_size = 0;
	struct c2_update_stream			*item_update_stream = NULL;
	bool					 update_stream_busy = false;
	bool					 io_op = false;
	uint64_t				 current_fragments = 0;
	struct c2_time_t			 now;

	C2_PRE(endp_unit != NULL);
	C2_PRE(item != NULL);
	C2_PRE(rpcobj_size != NULL);

	io_op = item->ri_type->rit_ops->rio_is_io_req(item);
	if (io_op == true) {
		/* XXX Implement a method to find out disjoint memory buffers. */
		current_fragments = item->ri_type->rit_ops->rio_get_fragments(item);
		if ((*nfragments + current_fragments) > endp_unit->isu_max_fragments_size)
			return 0;
	}
	item_size = item->ri_type->rit_ops->rio_item_size(item);
	if (((*rpcobj_size + item_size) < endp_unit->isu_max_message_size)) {
		/** XXX Need this API from rpc-core. */
		item_update_stream = c2_rpc_get_update_stream(item);
		/** XXX Need this API from rpc-core. */
		update_stream_busy = c2_rpc_get_update_stream_status(item_update_stream);
		if(update_stream_busy != true) {
			/** XXX Need this API from rpc-core. */
			c2_rpc_set_update_stream_status(item_update_stream, BUSY);
			/* XXX Need a rpbobject_linkage in c2_rpc_item. */
			c2_list_add(&forming_list.l_head, rpc_item->rpcobject_linkage);
			*rpcobj_size += item_size;
			*nfragments += current_fragments;
			item->ri_state = RPC_ITEM_ADDED;
			c2_rpc_form_remove_rpcitem_from_summary_unit(endp_unit, item);
			c2_time_now(&now);
			if (c2_time_after(rpc_item->timer->t_expire, now))
				rpc_item->ri_deadline = c2_time_sub(rpc_item->timer->t_expire, now);
			c2_timer_stop(item->timer);
			c2_timer_fini(item->timer);
			c2_list_del(item->rio_unformed_linkage);
		}
		return 0;
	}
	else {
		return -1;
	}
}

/**
   This is a rpc_item_type_op and is registered with associated
   rpc item.
   Coalesce IO vectors from a list of rpc items into one
   and arrange the IO vector in increasing order of file offset.
 */
int c2_rpc_form_io_items_coalesce(struct c2_rpc_form_item_coalesced *coalesced_item)
{
	int				res = 0;
	int				opcode;

	C2_PRE(coalesced_item != NULL);
	C2_PRE(coalesced_item->ic_resultant_item != NULL);
	C2_PRE(c2_list_length(coalesced_item->ic_member_list) > 1);
	opcode = coalesced_item->ic_op_intent;
	c2_list_for_each_entry(&coalesced_item->ic_member_list.l_head, member, struct c2_rpc_form_item_coalesced_member, im_linkage) {
		C2_PRE((member->im_member_item.ri_type->rit_ops->rio_io_get_opcode(&member->im_member_item)) == opcode);
	}
}

/**
   Coalesce possible rpc items and replace them by a aggregated
   rpc item.
 */
int c2_rpc_form_items_coalesce(struct c2_rpc_item_summary_unit *endp_unit,
		struct c2_list *forming_list, uint64_t *rpcobj_size)
{
	int				 res = 0;
	struct c2_rpc_item 		*item = NULL;
	struct c2_fid 			 fid;
	int				 item_rw;
	struct c2_rpc_form_fid_units 	*fid_unit = NULL;
	bool				 fid_found = false;
	struct c2_rpc_form_fid_summary_member	*fid_member = NULL;
	struct c2_rpc_form_fid_summary_member	*fid_member_next = NULL;
	uint64_t			 item_size = 0;
	struct c2_rpc_form_item_coalesced_member *item_member = NULL;
	struct c2_rpc_form_item_coalesced	*coalesced_item = NULL;

	C2_PRE(endp_unit != NULL);
	C2_PRE(forming_list != NULL);
	C2_PRE(rpcobj_size != NULL);

	/* 1. Iterate over the forming list to find out fids from 
	      IO requests it contains. */
	/* 2. For each found fid, check its read and write list to
	   see if it belongs to any of the selected rpc groups. */
	/* 3. For every unique combination of fid and intent(read/write)
	   locate/create a struct c2_rpc_form_fid_summary_member and put
	   it in endp_unit->isu_fid_list. */
	c2_list_for_each_entry(forming_list->l_head, item, struct c2_rpc_item, rpcobject_linkage) {
		if (item->ri_type->rit_ops->rio_is_io_req(item)) {
			fid = item->ri_type->rit_ops->rio_io_get_fid(item);
			item_rw = item->ri_type->rit_ops->rio_io_get_opcode(item);
			item_size = item->ri_type->rit_ops->rio_item_size(item);
			c2_list_for_each_entry(endp_unit->isu_fid_list.l_head, fid_member, struct c2_rpc_form_fid_summary_member, fsm_linkage) {
				if ((fid_member->fsm_fid == fid) && 
						(fid_member->fsm_rw == item_rw)) {
					fid_found = true;
					break;
				}
			}
			if (fid_found == false) {
				fid_member = c2_alloc(sizeof(struct c2_rpc_form_fid_summary_member));
				if (fid_member == NULL) {
					printf("Failed to allocate memory for struct c2_rpc_form_fid_summary_member\n");
					return -1;
				}
				fid_member->fsm_rw = item_rw;
				fid_member->fsm_fid = fid;
				c2_list_init(&fid_member->fsm_items);
			}
			fid_member->fsm_nitems++;
			fid_unit = c2_alloc(sizeof(struct c2_rpc_form_fid_units));
			if (fid_unit == NULL) {
				printf("Failed to allocate memory for struct c2_rpc_form_fid_units\n");
				return -1;
			}
			fid_unit->fu_item = *item;
			c2_list_add(fid_member->fsm_items.l_head, fid_unit->fu_linkage);
			fid_member->fsm_total_size += item_size;
		}
	}
	/* 4. Now, traverse the endp_unit->isu_fid_list and coalesce the
	      rpc items from the list of rpc items in each struct
	      c2_rpc_form_fid_summary_member. */
	c2_list_for_each_entry_safe(&endp_unit->isu_fid_list.l_head, fid_member, fid_member_next, struct c2_rpc_form_fid_summary_member, fsm_linkage) {
		if (fid_member->fsm_nitems > 1) {
			/* 5. For every possible coalescing situation, create a struct
			   c2_rpc_form_item_coalesced and populate it.*/
			coalesced_item = c2_alloc(sizeof(struct c2_rpc_form_item_coalesced));
			if (coalesced_item == NULL) {
				printf("Failed to allocate memory for struct c2_rpc_form_item_coalesced.\n");
				return -1;
			}
			coalesced_item->ic_op_intent = fid_member->fsm_rw;
			coalesced_item->ic_nmembers = fid_member->fsm_nitems;
			coalesced_item->ic_member_list = fid_member->fsm_items;
			rpcobj_size -= fid_member->fsm_total_size;

			/*XXX Need to coalesce IO vectors into one. */
			res = c2_rpc_form_io_items_coalesce(coalesced_item);
			if (res == 0) {
				/*delete fid member*/
				c2_list_del(fid_member->fsm_linkage);
				c2_free(fid_member);
				coalesced_item->ic_resultant_item->ri_state = RPC_ITEM_ADDED;
				/* 6. Remove the corresponding member rpc items from 
				   forming list, calculate their cumulative size and deduct
				   it from rpcobj_size. */
				rpcobj_size += coalesced_item->ic_resultant_item->ri_type->rit_ops->rio_item_size(coalesced_item->ic_resultant_item);
			}
			/* 7. Add the newly formed rpc item into the forming list and
			   increment rpcobj_size by its size. */
			c2_list_for_each_entry(&coalesced_item->ic_member_list.l_head, item_member, struct c2_rpc_form_item_coalesced_member, im_linkage) {
				c2_list_del(item_member->im_member_item->rpcobject_linkage);
			}
			c2_list_add(forming_list->l_head, coalesced_item->ic_resultant_item);
		}
	}
	return 0;
}

/**
   State function for CHECKING state.
 */
int c2_rpc_form_checking_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, int event, void *pvt)
{
	int 					 res = 0;
	struct c2_rpc_item 			*req_item = NULL;
	struct c2_rpc_form_item_coalesced	*coalesced_item = NULL;
	struct c2_list				*forming_list = NULL;
	struct c2_rpc_form_items_cache		*cache_list = NULL;
	uint64_t				 rpcobj_size = 0;
	uint64_t				 item_size = 0;
	struct c2_rpc_form_item_summary_unit_group	*sg = NULL;
	uint64_t				 group_size = 0;
	uint64_t				 partial_size = 0;
	struct c2_rpc_form_item_summary_unit_group	*group= NULL;
	uint64_t				 nselected_groups = 0;
	uint64_t				 ncurrent_groups = 0;
	uint64_t				 nfragments = 0;

	printf("In state: checking\n");
	C2_PRE(item != NULL);
	C2_PRE((event == C2_RPC_FORM_EXTEVT_RPCITEM_REPLY_RECEIVED) ||
			(event == C2_RPC_FORM_EXTEVT_RPCITEM_TIMEOUT) ||
			(event == C2_RPC_FORM_INTEVT_STATE_SUCCEEDED));
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));

	endp_unit->isu_endp_state = C2_RPC_FORM_STATE_CHECKING;
	forming_list = c2_alloc(sizeof(struct c2_list));
	if (forming_list == NULL) {
		printf("Failed to allocate memory for struct c2_list.\n");
		return C2_RPC_FORM_INTEVT_STATE_DONE;
	}
	c2_list_init(forming_list);

	if (event == C2_RPC_FORM_EXTEVT_RPCITEM_REPLY_RECEIVED) {
		/* Find out the request rpc item, given the reply
		   rpc item. */
		res = c2_rpc_session_reply_item_received(item, &req_item);
		if (res != 0) {
			printf("Error finding out request rpc item for the given reply rpc item. Error = %d\n", res);
			return res;
		}
		C2_ASSERT(req_item != NULL);
		c2_list_for_each_entry(&endp_unit->
				isu_coalesced_items_list->l_head, 
				coalesced_item, 
				struct c2_rpc_form_item_coalesced, 
				isu_coalesced_items_list) {
			if (coalesced_item->ic_resultant_item == req_item) {
				res = c2_rpc_form_item_coalesced_reply_post(endp_unit, coalesced_item);
				if (res != 0)
					printf("Failed to process a coalesced rpc item.\n");
			}
		}
	}
	else if (event == C2_RPC_FORM_EXTEVT_RPCITEM_TIMEOUT) {
		res = c2_rpc_form_item_add_to_forming_list(endp_unit, item, &rpcobj_size, &nfragments, forming_list);
	}
	/* Iterate over the c2_rpc_form_item_summary_unit_group list in the 
	   endpoint structure to find out which rpc groups can be included
	   in the rpc object. */
	c2_list_for_each_entry(&endp_unit->isu_groups_list.l_head, sg, struct c2_rpc_form_item_summary_unit_group, sug_linkage) {
		nselected_groups++;
		if ((group_size + sg->sug_total_size) < endp_unit->isu_max_message_size) {
			group_size += sg->sug_total_size;
		}
		else {
			partial_size = (group_size + sg->sug_total_size) - endp_unit->isu_max_message_size;
			break;
		}
	}

	/* XXX curr rpcs in flight will be taken care by
	   output component. */
	/* Core of formation algorithm. */
	res = c2_rpc_form_get_items_cache_list(endp_unit->isu_endp_id, &cache_list);
	if ((res != 0) || (cache_list == NULL)) {
		printf("Input rpc items cache list is empty.\n");
		if (event == C2_RPC_FORM_EXTEVT_RPCITEM_TIMEOUT) {
			/* XXX Form the rpc object with just one 
			   item and send it on wire. */
			/* Need to take care of not sending rpc objects
			   with just one rpc item too often. */
		}
		else
			return C2_RPC_FORM_INTEVT_STATE_FAILED;
	}
	c2_mutex_lock(&cache_list->ic_mutex);
	c2_list_for_each_entry(&endp_unit->isu_unformed_items.l_head, rpc_item, struct c2_rpc_item, rio_unformed_linkage) {
		item_size = rpc_item->ri_type->rit_ops->rio_item_size(item);
		/* 1. If there are urgent items, form them immediately. */
		if ((rpc_item->ri_deadline.tv_sec == 0) &&
				(rpc_item->ri_deadline.tv_nsec == 0)) {
			res = c2_rpc_form_item_add_to_forming_list(endp_unit, rpc_item, &rpcobj_size, &nfragments, forming_list);
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
		c2_list_for_each_entry(&endp_unit->isu_groups_list, group, 
				struct c2_rpc_form_item_summary_unit_group,
				sug_linkage) {
			ncurrent_groups++;
			/* If selected groups are exhausted, break the loop. */
			if (ncurrent_groups > nselected_groups)
				break;
			if (item->ri_group == group->sug_group) {
				/* If the size of selected groups is bigger than
				   max_message_size, the last group will be 
				   partially selected and is present in variable 'sg'. */
				if ((item->ri_group == sg->sug_group) &&
						((partial_size - item_size) > 0)) {
					partial_size -= item_size;
				}
				res = c2_rpc_form_item_add_to_forming_list(endp_unit, rpc_item, &rpcobj_size, &nfragments, forming_list);
				if (res != 0) {
					break;
				}
			}
		}
	}
	c2_mutex_unlock(&cache_list->ic_mutex);
	/* Try to do IO colescing for items in forming list. */
	res = c2_rpc_form_items_coalesce(endp_unit, forming_list, &rpcobj_size);
	/* Create an rpc object in endp_unit->isu_rpcobj_checked_list. */
}

/**
   State function for FORMING state.
 */
int c2_rpc_form_forming_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, int event, void *pvt)
{
	int 				 res = 0;
	struct c2_rpc_form_rpcobj	 *rpcobj = NULL;
	struct c2_rpc_form_rpcobj	 *rpcobj_next = NULL;
	struct c2_rpc_item		 *rpc_item = NULL;
	struct c2_update_stream		 *item_update_stream = NULL;

	C2_PRE(item != NULL);
	C2_PRE(event == C2_RPC_FORM_INTEVT_STATE_SUCCEEDED);
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));
	printf("In state: forming\n");

	endp_unit->isu_endp_state = C2_RPC_FORM_STATE_FORMING;

	c2_mutex_lock(&endp_unit->isu_rpcobj_checked_list.rl_lock);
	c2_list_for_each_entry_safe(&endp_unit->isu_rpcobj_checked_list.rl_list.l_head, rpcobj, rpcobj_next, struct c2_rpc_form_rpcobj, ro_linkage) {
		c2_list_for_each_entry(rpcobj->ro_rpcobj->r_items.l_head, rpc_item, struct c2_rpc_item, ri_linkage) {
			res = c2_rpc_session_item_prepare(rpc_item);
			if (res != 0) {
				/** XXX Need this API from sessions */
				item_update_stream = c2_rpc_get_update_stream(item);
				/** XXX Need this API from sessions */
				c2_rpc_set_update_stream_status(item_update_stream, FREE);
				c2_list_del(&rpc_item->ri_linkage);
				rpc_item->ri_state = RPC_ITEM_SUBMITTED;
				/* XXX Need to add ri_unformed_linkage 
				   in c2_rpc_item to keep track of 
				   unformed rpc items. */
				c2_list_add(&endp_unit->isu_unformed_items.l_head, rpc_item->ri_unformed_linkage);
				c2_rpc_form_add_rpcitem_to_summary_unit(endp_unit, rpc_item);
			}
		}
		c2_list_del(rpcobj->ro_linkage);
		c2_list_add(&endp_unit->isu_rpcobj_formed_list.l_head, rpcobj->ro_linkage);
	}
	c2_mutex_unlock(&endp_unit->isu_rpcobj_checked_list.rl_lock);
	return C2_RPC_FORM_INTEVT_STATE_SUCCEEDED;
}

/**
   State function for POSTING state.
 */
int c2_rpc_form_posting_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, int event, void *pvt)
{
	int 				 res = 0;
	int 				 ret = 0;
	struct c2_rpc_form_rpcobj	*rpc_obj = NULL;
	struct c2_rpc_form_rpcobj	*rpc_obj_next = NULL;
	printf("In state: posting\n");

	C2_PRE(item != NULL);
	/* POSTING state is reached only by a state succeeded event with
	   FORMING as previous state. */
	C2_PRE(event == C2_RPC_FORM_INTEVT_STATE_SUCCEEDED);
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));

	/* Iterate over the rpc object list and send all rpc objects 
	   to the output component. */
	c2_mutex_lock(&endp_unit->isu_rpcobj_formed_list.rl_lock);
	c2_list_for_each_entry_safe(&endp_unit->isu_rpcobj_formed_list.rl_list.l_head, rpc_obj, rpc_obj_next, struct c2_rpc_form_rpcobj, ro_rpcobj) {
		endp = c2_rpc_session_get_endpoint(rpc_obj->rp_rpcobj);
		if (endp_unit->isu_curr_rpcs_in_flight < endp_unit->isu_max_rpcs_in_flight) {
			res = c2_net_send(endp, rpc_obj->ro_rpcobj);
			/* XXX curr rpcs in flight will be taken care by
			   output component. */
			//endp_unit->isu_curr_rpcs_in_flight++;
			if(res == 0) {
				c2_list_del(rpc_obj->ro_linkage);
				c2_free(rpc_obj);
				ret = C2_RPC_FORM_INTEVT_STATE_SUCCEEDED;
			}
			else {
				ret = C2_RPC_FORM_INTEVT_STATE_FAILED;
				break;
			}
		}
	}
	c2_mutex_unlock(&endp_unit->isu_rpcobj_formed_list.rl_lock);
	return ret;
}

/**
   State function for REMOVING state.
 */
int c2_rpc_form_removing_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, int event, void *pvt)
{
	int 			res = 0;

	C2_PRE(item != NULL);
	C2_PRE((event == C2_RPC_FORM_EXTEVT_RPCITEM_REMOVED) ||
			(event == C2_RPC_FORM_EXTEVT_RPCITEM_CHANGED));
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));
	printf("In state: removing\n");

	endp_unit->isu_endp_state = C2_RPC_FORM_STATE_REMOVING;

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
		if (event == C2_RPC_FORM_EXTEVT_RPCITEM_REMOVED) {
			res = c2_rpc_form_remove_rpcitem_from_summary_unit(endp_unit, item);
		}
		else if (event == C2_RPC_FORM_EXTEVT_RPCITEM_CHANGED) {
			res = c2_rpc_form_change_rpcitem_from_summary_unit(endp_unit, item, pvt);
		}
		if (res == 0)
			return C2_RPC_FORM_INTEVT_STATE_SUCCEEDED;
		else
			return C2_RPC_FORM_INTEVT_STATE_FAILED;
	}
}

