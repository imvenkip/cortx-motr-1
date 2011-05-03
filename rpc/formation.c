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
bool c2_rpc_form_state_machine_exit(struct c2_rpc_form_item_summary_unit 
		*endp_unit, int state, int event)
{
	if ((state == C2_RPC_FORM_STATE_WAITING) && 
			((event == C2_RPC_FORM_INTEVT_STATE_SUCCEEDED) ||
			(event == C2_RPC_FORM_INTEVT_STATE_FAILED))) {
		c2_rwlock_write_lock(&formation_summary.is_endp_list_lock);
		c2_ref_put(&endp_unit->isu_ref);
		c2_rwlock_write_unlock(&formation_summary.is_endp_list_lock);
		return true;
	}
	return false;
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
	c2_list_fini(endp_unit->isu_file_list);
	c2_list_fini(endp_unit->isu_groups_list);
	c2_list_fini(endp_unit->isu_coalesced_items_list);
	c2_free(endp_unit);
}

/**
   Add an endpoint structure when the first rpc item gets added
   for an endpoint.
 */
struct c2_rpc_form_item_summary_unit *c2_rpc_form_item_summary_unit_add(int endp)
{
	struct c2_rpc_form_item_summary_unit	*endp_unit;

	endp_unit = c2_alloc(sizeof(struct c2_rpc_form_item_summary_unit));
	c2_list_add(formation_summary.is_endp_list.l_head, endp_unit->isu_linkage);
	c2_list_init(endp_unit->isu_groups_list);
	c2_list_init(endp_unit->isu_files_list);
	c2_list_init(endp_unit->isu_coalesced_items_list);
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
				int sm_state, int sm_event)
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
		c2_rwlock_read_lock(&formation_summary.is_endp_list_lock);
		c2_list_for_each_entry(&formation_summary.is_endp_list.l_head, endp_unit, struct c2_rpc_form_item_summary_unit, isu_linkage) {
			if (endp_unit->isu_endp_id == endpoint) {
				found = true;
				break;
			}
		}
		if (found == true) {
			c2_mutex_lock(endp_unit->isu_unit_lock);
			c2_ref_get(&endp_unit->isu_ref);
			c2_rwlock_read_unlock(&formation_summary.is_endp_list_lock);
			prev_state = endp_unit->isu_endp_state;
		}
		else {
			/** XXX Add a new summary unit */
			endp_unit = c2_rpc_form_item_summary_unit_add(endpoint);
			c2_rwlock_read_unlock(&formation_summary.is_endp_list_lock);
			c2_mutex_lock(endp_unit->isu_unit_lock);
			prev_state = sm_state;
		}
	}
	res = (c2_rpc_form_next_state(prev_state, sm_event))(endp_unit, item, sm_event);
	prev_state = endp_unit->isu_endp_state;
	c2_mutex_unlock(endp_unit->isu_unit_lock);
	/*XXX Handle exit path as an event. */
	/* Exit point for state machine. */
	exit = c2_rpc_form_state_machine_exit(endp_unit, prev_state, sm_event);
	if(exit == true)
		return 0;

	if (res != 0) {
		/** Post a state failed event. */
		c2_rpc_form_intevt_state_failed(item, prev_state);
	}
	else {
		/** Post a state succeeded event. */
		c2_rpc_form_intevt_state_succeeded(item, prev_state);
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
			C2_RPC_FORM_N_STATES, C2_RPC_FORM_EXTEVT_RPCITEM_ADDED);
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
			C2_RPC_FORM_N_STATES, C2_RPC_FORM_EXTEVT_RPCITEM_REMOVED);
}

/**
   Callback function for change in parameter of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_changed(struct c2_rpc_item *item)
{
	printf("In callback: c2_rpc_form_extevt_rpcitem_changed\n");
	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(item, NULL,
			C2_RPC_FORM_N_STATES, C2_RPC_FORM_EXTEVT_RPCITEM_CHANGED);
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
			C2_RPC_FORM_N_STATES, C2_RPC_FORM_EXTEVT_RPCITEM_REPLY_RECEIVED);
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
			C2_RPC_FORM_N_STATES, C2_RPC_FORM_EXTEVT_RPCITEM_TIMEOUT);
}

/**
   Callback function for successful completion of a state.
   Call the default handler function. Depending upon the
   input state, the default handler will invoke the next state
   for state succeeded event.
   @param state - previous state of state machine.
 */
int c2_rpc_form_intevt_state_succeeded(struct c2_rpc_item *item, int state)
{
	printf("In callback: c2_rpc_form_intevt_state_succeeded\n");
	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(item, NULL,
			state, C2_RPC_FORM_INTEVT_STATE_SUCCEEDED);
}

/**
   Callback function for failure of a state.
   Call the default handler function. Depending upon the
   input state, the default handler will invoke the next state
   for state failed event.
   @param state - previous state of state machine.
 */
int c2_rpc_form_intevt_state_failed(struct c2_rpc_item *item, int state)
{
	printf("In callback: c2_rpc_form_intevt_state_failed\n");
	/* Curent state is not known at the moment. */
	return c2_rpc_form_default_handler(item, NULL,
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
		,struct c2_rpc_item *item, int event)
{
	int 					 res = 0;
	struct c2_rpc_item 			*req_item = NULL;
	struct c2_rpc_form_item_coalesced	*coalesced_item = NULL;

	C2_PRE(item != NULL);
	C2_PRE(event < C2_RPC_FORM_INTEVT_N_EVENTS);
	C2_PRE(endp_unit != NULL);
	C2_PRE(c2_mutex_is_locked(&endp_unit->isu_unit_lock));

	printf("In state: waiting\n");
	endp_unit->isu_endp_state = C2_RPC_FORM_STATE_WAITING;
	/* Internal events will invoke a nop from waiting state. */
	if ((event == C2_RPC_FORM_INTEVT_STATE_SUCCEEDED) ||
			(event == C2_RPC_FORM_INTEVT_STATE_FAILED))
		return 0;

	switch(event) {
		case C2_RPC_FORM_INTEVT_STATE_SUCCEEDED:
			return 0;
		case C2_RPC_FORM_INTEVT_STATE_FAILED:
			return 0;
		case C2_RPC_FORM_EXTEVT_RPCITEM_REPLY_RECEIVED:
			/* Find out the request rpc item, given the reply
			   rpc item. */
			res = c2_rpc_session_reply_item_received(item, &req_item);
			if (res != 0) {
				printf("Error finding out request rpc item for the given reply rpc item. Error = %d\n", res);
				return res;
			}
			C2_ASSERT(req_item != NULL);
			c2_list_for_each_entry(&endp_unit->isu_coalesced_items_list->l_head, coalesced_item, struct c2_rpc_form_item_coalesced, isu_coalesced_items_list) {
				if (coalesced_item->ic_resultant_item == req_item) {
					res = c2_rpc_form_item_coalesced_reply_post(endp_unit, coalesced_item);
					if (res != 0)
						printf("Failed to process a coalesced rpc item.\n");
				}
			}
			endp_unit->isu_curr_rpcs_in_flight--;
			/* XXX Post a done event and exit the state machine. */
	};
}

/**
   State function for UPDATING state.
 */
int c2_rpc_form_updating_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, int event)
{
	printf("In state: updating\n");
}

/**
   State function for CHECKING state.
 */
int c2_rpc_form_checking_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, int event)
{
	printf("In state: checking\n");
}

/**
   State function for FORMING state.
 */
int c2_rpc_form_forming_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, int event)
{
	printf("In state: forming\n");
}

/**
   State function for POSTING state.
 */
int c2_rpc_form_posting_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, int event)
{
	printf("In state: posting\n");
}

/**
   State function for REMOVING state.
 */
int c2_rpc_form_removing_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, int event)
{
	printf("In state: removing\n");
}

