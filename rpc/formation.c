#include "formation.h"

/**
   There will be only one instance of struct c2_rpc_form_item_summary
   since this structure represents data for all endpoints.
 */
struct c2_rpc_form_item_summary 	formation_summary;

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
int c2_rpc_form_default_handler(struct c2_rpc_item *item, int sm_state,
		int sm_event)
{
	int 					 endpoint = 0;
	int 					 res = 0;
	int 					 prev_state = 0;
	struct c2_rpc_form_item_summary_unit	*endp_unit;
	bool 					 found = false;

	C2_PRE(item != NULL);
	C2_PRE(current_event < C2_RPC_FORM_INTEVT_N_EVENTS);

	/* This is an external event. */
	if (current_state == C2_RPC_FORM_N_STATES) {
		endpoint = c2_rpc_form_get_endpoint(item);
		c2_mutex_lock(&formation_summary.is_endp_list_lock);
		c2_list_for_each_entry(&formation_summary.is_endp_list.l_head, endp_unit, struct c2_rpc_form_item_summary_unit, isu_linkage) {
			if (endp_unit->isu_endp_id == endpoint) {
				found = true;
				break;
			}
		}
	}
	if (found == true) {
		c2_mutex_lock(endp_unit->isu_unit_lock);
		c2_mutex_unlock(formation_summary.is_endp_list_lock);
		prev_state = endp_unit->isu_endp_state;
	}
	else {
		/** XXX Add a new summary unit */
		endp_unit = c2_rpc_form_summary_unit_add();
		c2_mutex_lock(endp_unit->isu_unit_lock);
		prev_state = sm_state;
	}
	res = c2_rpc_form_next_state(prev_state, sm_event);
	c2_mutex_unlock(endp_unit->isu_unit_lock);
	if (res != 0) {
		/** Post a state failed event. */
		c2_rpc_form_intevt_state_failed(item, prev_state);
	}
	else {
		/** Post a state succeeded event. */
		c2_rpc_form_intevt_state_succeeded(item, prev_state);
	}
}

