#ifndef __C2_RPC_FORMATION_H__
#define __C2_RPC_FORMATION_H__

#include <rpccore.h>
#include <session.h>

/**
   @defgroup rpc_formation Formation sub component from RPC layer.
   @{
   Formation component runs as a state machine driven by external events.
   The state machine is run per endpoint and it maintains state and its
   internal data per endpoint.
   There are no internal threads belonging to formation component.
   Formation component uses the "RPC Items cache" as input and sends out
   RPC objects which are containers of rpc items as soon as they are ready.
   The grouping component populates the rpc items cache which is then
   used by formation component to form them into rpc objects.
   The rpc items cache is grouped into several lists, one per endpoint.
   Each list contains rpc items destined for given endpoint.
   These lists are sorted by timeouts. So the items with least timeout
   will be at the HEAD of list.
   Refer to the HLD of RPC Formation -
   https://docs.google.com/a/xyratex.com/Doc?docid=0AXXBPOl-5oGtZGRzMzZ2NXdfMGQ0ZjNweGdz&hl=en

   Formation is done on basis of various criterion
   * timeout
   * priority
   * rpc group
   * URGENT
   * max_message_size (max permissible size of RPC object)
   * max_rpcs_in_flight (max number of RPCs that could be in flight)
   * max_message_fragments (max number of disjoint memory buffers)

   The Formation component will use an internal data structure that gives
   summary of data in the input list. This data structure will help in
   making quick decisions so as to select best candidates for rpc formation
   and can prevent multiple scans of the rpc items list.
   There are a multitude of external events that can come to formation and
   it handles them and moves the state machine to appropriate states as
   a result.

   The external events are
   * addition of an rpc item to the input list.
   * deletion of rpc item from the input list.
   * change of parameter for an rpc item.
   * reply received.
   * deadline expired(timeout) of an rpc item.
   Also, there are a number of states through which the formation state
   machine transitions.
   * WAITING (waiting for an event to trigger)
   * UPDATING (updates the internal data structure)
   * CHECKING (core of formation algorithm)
   * FORMING (Forming an rpc object in memory)
   * POSTING (Sending the rpc object over wire?)

   Along with these external events, there are some implicit internal
   events which are used to transition the state machine from one state
   to the next state on the back of same thread.
   These internal events are
   * state succeeded.
   * state failed.
   The formation component maintains its current state in a state variable
   which is protected by a lock. At any time, the state machine can only be
   in one state.
   The lifecycle of any thread executing the formation state machine is
   something like this
   * execute a state function as a result of triggering of some event.
   * acquire the state lock.
   * change the state of state machine.
   * release the state lock.
   * lock the internal data structure.
   * operate on the internal data structure.
   * release the internal data structure lock.
   * pass through the sub sequent states as states succeed and exit
 */

/* XXX A lot of data structures here, use a c2_list. Instead a 
   hash function will be used to enhance the performance. This will
   be done after UT of rpc formation. */

/**
   XXX There are no retries in the state machine. Any failure event will
   take the executing thread out of this state machine. */

/**
   This structure is an internal data structure which builds up the
   summary form of data for all endpoints.
   It contains a list of sub structures, one for each endpoint.
 */
struct c2_rpc_form_item_summary {
	/** List of internal data structures with data for each endpoint */
	struct c2_list			is_endp_list;
	/** Read/Write lock protecting the list from concurrent access. */
	struct c2_rwlock		is_endp_list_lock;
};

/**
   The global instance of summary data structure. 
 */
extern struct c2_rpc_form_item_summary	formation_summary;

/**
   Structure containing fid for io requests.
   This will help to make quick decisions to select candidates
   for coalescing of io requests.
 */
struct c2_rpc_form_fid_summary {
	/** File id on which IO request has come. */
	struct c2_fid			*fs_fid;
	/** List of read requests on this fid. */
	struct c2_list			 fs_read_list;
	/** List of write requests on this fid. */
	struct c2_list			 fs_write_list;
	/** Linkage into list of fids for this endpoint. */
	struct c2_list_link		*fs_linkage;
};

/**
   This structure represents the summary data for a given endpoint.
   It contains a list containing the summary data for each rpc group
   and a list of files involved in any IO operations destined for
   the given endpoint.
   There is a state variable per summary unit(per endpoint).
   This will help to ensure that requests belonging to different
   endpoints can proceed in their own state machines. Only the
   requests belonging to same endpoint will contest for the
   state variable.
 */
struct c2_rpc_form_item_summary_unit {
	/** Referenced Endpoint */
	int				 isu_endp_id;
	/** Mutex protecting the unit from concurrent access. */
	struct c2_mutex			 isu_unit_lock;
	/** Linkage into the endpoint list. */
	struct c2_list_link		*isu_linkage;
	/** List of structures containing data for each group. */
	struct c2_list			 isu_groups_list;
	/** List of files being operated upon, for this endpoint. */
	struct c2_list			 isu_file_list;
	/** State of the state machine for this endpoint.
	    Threads will have to take the unit_lock above before
	    state can be changed. This variable will bear one value
	    from enum c2_rpc_form_state. */
	/** XXX Put a state machine object per summary_unit and move
	    the state to the state machine object. */
	int				 isu_endp_state;
	/** Refcount for this summary unit */
	struct c2_ref			 isu_ref;
	/** List of coalesced rpc items. */
	/** c2_list <c2_rpc_form_item_coalesced> */
	struct c2_list			 isu_coalesced_items_list;
	/** Statistics data. Currently stationed with formation but
	    later will be moved to Output sub component from rpc. */
	uint64_t			 isu_max_rpcs_in_flight;
	uint64_t			 isu_curr_rpcs_in_flight;
};

/**
   An internal data structure to connect coalesced rpc items with
   its constituent rpc items. When a reply is received for a
   coalesced rpc item, it will find out the requesting coalesced
   rpc item and using this data structure, it will find out the
   constituent rpc items and invoke their completion callbacks accordingly.
   Formation does not bother about splitting of reply for coalesced
   rpc item into sub replies for constituent rpc items.
 */
struct c2_rpc_form_item_coalesced {
	/** Linkage to list of such coalesced rpc items. */
	struct c2_list_link	       *ic_linkage;
	/** Intent of operation, read or write */
	int				ic_op_intent;
	/** Resultant coalesced rpc item */
	struct c2_rpc_item	       *ic_resultant_item;
	/** No of constituent rpc items. */
	uint64_t			ic_nmembers;
	/** List of constituent rpc items for this coalesced item. */
	/** c2_list<c2_rpc_form_item_coalesced_member> */
	struct c2_list			ic_member_list;
};

/**
   Member rpc item coalesced into one rpc item. 
 */
struct c2_rpc_form_item_coalesced_member {
	/** Linkage into list of such member rpc items. */
	struct c2_list_link		*im_linkage;
	/** c2_rpc_item */
	struct c2_rpc_item		*im_member_item;
};

/**
   This structure represents the summary data for a given rpc group
   destined for a given endpoint.
 */
struct c2_rpc_form_item_summary_unit_group {
	/** Linkage into the list of groups belonging to this
	    endpoint. */
	struct c2_list_link	       *sug_linkage;
	/** The group number this data belongs to. */
	uint64_t			sug_group_no;
	/** Number of items from this group found so far. */
	uint64_t			sug_nitems;
	/** Number of expected items from this group. */
	uint64_t			sug_expected_items;
	/** Number of highest priority items from this group.
	    This does not inlcude urgent items, they are
	    handled elsewhere. */
	uint64_t			sug_priority_items;
	/** Average time out for items in this group. This number
	    gives an indication about relative position of group
	    within the cache list. */
	uint64_t			sug_avg_timeout;
	/** Cumulative size of rpc items in this group so far. */
	uint64_t			sug_total_size;
};

/**
   This structure contains the list of rpc objects which are formed
   but not yet sent on wire. It contains a lock to protect concurrent
   access to the structure.
 */
struct c2_rpc_form_rpcobj_list {
	/** Mutex protecting the list of rpc objects from concurrent access. */
	struct c2_mutex			rl_lock;
	/** List of rpc objects formed but not yet sent on wire. */
	/** c2_list <struct c2_rpc> */
	struct c2_list			rl_list;
};

/**
   Enumeration of all possible states.
 */
enum c2_rpc_form_state {
	/** WAITING state for state machine, where it waits for
	    any event to trigger. */
	C2_RPC_FORM_STATE_WAITING = 0,
	/** UPDATING state for state machine, where it updates
	    internal data structure (struct c2_rpc_form_item_summary_unit) */
	C2_RPC_FORM_STATE_UPDATING,
	/** CHECKING state for state machine, which employs formation
	    algorithm. */
	C2_RPC_FORM_STATE_CHECKING,
	/** FORMING state for state machine, which forms the struct c2_rpc
	    object from a list of member rpc items. */
	C2_RPC_FORM_STATE_FORMING,
	/** POSTING state for state machine, which posts the formed rpc
	    object to output component. */
	C2_RPC_FORM_STATE_POSTING,
	/** REMOVING state for state machine, which changes the internal
	    data structure according to the changed rpc item. */
	C2_RPC_FORM_STATE_REMOVING,
	/** MAX States of state machine. */
	C2_RPC_FORM_N_STATES
};

/**
   Enumeration of external events.
 */
enum c2_rpc_form_ext_event {
	/** RPC Item added to cache. */
	C2_RPC_FORM_EXTEVT_RPCITEM_ADDED = 0,
	/** RPC item removed from cache. */
	C2_RPC_FORM_EXTEVT_RPCITEM_REMOVED,
	/** Parameter change for rpc item. */
	C2_RPC_FORM_EXTEVT_RPCITEM_CHANGED,
	/** Reply received for an rpc item. */
	C2_RPC_FORM_EXTEVT_RPCITEM_REPLY_RECEIVED,
	/** Deadline expired for rpc item. */
	C2_RPC_FORM_EXTEVT_RPCITEM_TIMEOUT,
	/** Max external events. */
	C2_RPC_FORM_EXTEVT_N_EVENTS
};

/**
   Enumeration of internal events.
 */
enum c2_rpc_form_int_event {
	/** Execution succeeded in current state. */
	C2_RPC_FORM_INTEVT_STATE_SUCCEEDED = C2_RPC_FORM_EXTEVT_N_EVENTS,
	/** Execution failed in current state. */
	C2_RPC_FORM_INTEVT_STATE_FAILED,
	/** Execution completed, exit the state machine. */
	C2_RPC_FORM_INTEVT_STATE_DONE,
	/** Max internal events. */
	C2_RPC_FORM_INTEVT_N_EVENTS
};

/**
   Initialization for formation component in rpc. 
   This will register necessary callbacks and initialize
   necessary data structures.
 */
int c2_rpc_form_init();

/**
   Finish method for formation component in rpc.
   This will deallocate all memory claimed by formation
   and do necessary cleanup.
 */
int c2_rpc_form_fini();

typedef int (*stateFunc)(struct c2_rpc_item*, int);

/**
   A state table guiding resultant states on arrival of events
   on earlier states.
   next_state = stateTable[current_state][current_event]
 */
stateFunc c2_rpc_form_stateTable
[C2_RPC_FORM_N_STATES][C2_RPC_FORM_INTEVT_N_EVENTS] = {

	{ c2_rpc_form_updating_state, c2_rpc_form_updating_state,
	  c2_rpc_form_updating_state, c2_rpc_form_waiting_state,
	  c2_rpc_form_checking_state, c2_rpc_form_waiting_state,
	  c2_rpc_form_waiting_state},

	{ c2_rpc_form_updating_state, c2_rpc_form_removing_state,
	  c2_rpc_form_removing_state, c2_rpc_form_waiting_state,
	  c2_rpc_form_checking_state, c2_rpc_form_checking_state,
	  c2_rpc_form_waiting_state},

	{ c2_rpc_form_updating_state, c2_rpc_form_removing_state,
	  c2_rpc_form_removing_state, c2_rpc_form_waiting_state,
	  c2_rpc_form_checking_state, c2_rpc_form_forming_state,
	  c2_rpc_form_waiting_state},

	{ c2_rpc_form_updating_state, c2_rpc_form_removing_state,
	  c2_rpc_form_removing_state, c2_rpc_form_waiting_state,
	  c2_rpc_form_checking_state, c2_rpc_form_posting_state,
	  c2_rpc_form_waiting_state},

	{ c2_rpc_form_updating_state, c2_rpc_form_removing_state,
	  c2_rpc_form_removing_state, c2_rpc_form_waiting_state,
	  c2_rpc_form_checking_state, c2_rpc_form_waiting_state,
	  c2_rpc_form_waiting_state},

	{ c2_rpc_form_updating_state, c2_rpc_form_removing_state,
	  c2_rpc_form_removing_state, c2_rpc_form_waiting_state,
	  c2_rpc_form_checking_state, c2_rpc_form_waiting_state,
	  c2_rpc_form_waiting_state}
};

/**
   Return the function pointer to next state given the current state
   and current event as input.
   @param current_state - current state of state machine.
   @param current_event - current event posted to the state machine.
 */
stateFunc c2_rpc_form_next_state(int current_state, int current_event);

/**
   Exit path from a state machine. An incoming thread which executed
   the formation state machine so far, is let go and it will return
   to do its own job. */
void c2_rpc_form_state_machine_exit(struct c2_rpc_form_item_summary_unit *endp_unit, int state, int event);

/**
   Get the endpoint given an rpc item.
   This is a placeholder and will be replaced when a concrete
   definition of endpoint is available.
 */
int c2_rpc_form_get_endpoint(struct c2_rpc_item *item)
{
	return item->endpoint;
}

/**
   Call the completion callbacks for member rpc items of 
   a coalesced rpc item. 
 */
int c2_rpc_form_item_coalesced_reply_post(struct c2_rpc_form_item_coalesced *coalesced_struct);

/**
   Destroy an endpoint structure since it no longer contains
   any rpc items.
 */
void c2_rpc_form_item_summary_unit_destroy(struct c2_ref *ref);

/**
   Add an endpoint structure when the first rpc item gets added
   for an endpoint. */
struct c2_rpc_form_item_summary_unit *c2_rpc_form_item_summary_unit_add(int endp);

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
int c2_rpc_form_default_handler(struct c2_rpc_item *item, int state, int event);

/**
   Callback function for addition of an rpc item to the rpc items cache.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_added_in_cache(struct c2_rpc_item *item);

/**
   Callback function for deletion of an rpc item from the rpc items cache.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_deleted_from_cache(struct c2_rpc_item *item);

/**
   Callback function for change in parameter of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_changed(struct c2_rpc_item *item);

/**
   Callback function for reply received of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_reply_received(struct c2_rpc_item *item);

/**
   Callback function for deadline expiry of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_deadline_expired(struct c2_rpc_item *item);

/**
   Callback function for successful completion of a state.
   Call the default handler function. Depending upon the
   input state, the default handler will invoke the next state
   for state succeeded event.
   @param state - previous state of state machine.
 */
int c2_rpc_form_intevt_state_succeeded(struct c2_rpc_item *item, int state);

/**
   Callback function for failure of a state.
   Call the default handler function. Depending upon the
   input state, the default handler will invoke the next state
   for state failed event.
   @param state - previous state of state machine.
 */
int c2_rpc_form_intevt_state_failed(struct c2_rpc_item *item, int state);

/**
   Function to do the coalescing of related rpc items.
   This is invoked from FORMING state, so a list of selected
   rpc items is input to this function which coalesces
   possible items and shrinks the list.
   @param items - list of items to be coalesced.
 */
int c2_rpc_form_coalesce_items(struct c2_list *items);

/**
   State function for WAITING state.
   Formation is waiting for any event to trigger.
   1. At first, this state will handle the "reply received" event and if
   reply is for a coalesced item, it will call completion callbacks for
   all constituent rpc items by referring internal structure
   c2_rpc_form_item_coalesced.
   2. If current rpcs in flight ares less than max_rpcs_in_flight,
   this state can transition into next state. During this state,
   rpc objects could be in flight. If current rpcs in flight [n]
   is less than max_rpcs_in_flight, state transitions to UPDATING,
   else keeps waiting till n is less than max_rpcs_in_flight.
   ** WAITING state should be a nop for internal events. **
   @param item - input rpc item.
   @param event - Since WAITING state handles a lot of events,
   @param endp_unit - Corresponding summary_unit structure for given rpc item.
   it needs some way of identifying the events.
 */
int c2_rpc_form_waiting_state(struct c2_rpc_form_item_summary_unit *endp_unit,
		struct c2_rpc_item *item, int event);

/**
   State function for UPDATING state.
   Formation is updating its internal data structure by taking necessary locks.
   @param item - input rpc item.
   @param event - Since UPDATING state handles a lot of events,
   it needs some way of identifying the events.
   @param endp_unit - Corresponding summary_unit structure for given rpc item.
 */
int c2_rpc_form_updating_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, int event);

/**
   State function for CHECKING state.
   Core of formation algorithm. This state scans the rpc items cache and
   structure c2_rpc_form_item_summary_unit to form an RPC object by
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
   1. Read rpc items from the cache for this endpoint.
   2. If the item deadline is zero(urgent), add it to a local
   list of rpc items to be formed.
   3. Check size of formed rpc object so far to see if its optimal.
   Here size of rpc is compared with max_message_size. If size of
   rpc is far less than max_message_size and no urgent item, goto #1.
   4. If #3 is true and if the number of disjoint memory buffers
   is less than parameter max_fragment_size, a probable rpc object
   is in making. The selected rpc items are put on a list
   and the state machine transitions to next state.
   5. Consult the structure c2_rpc_form_item_summary_unit to find out
   data about all rpc groups. Select groups that have combination of
   lowest average timeout and highest size that fits into optimal
   size. Keep selecting such groups till optimal size rpc is formed.
   6. Consult the list of files from internal data to find out files
   on which IO requests have come for this endpoint. Do coalescing
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
   @param event - Since CHECKING state handles a lot of events,
   it needs some way of identifying the events.
   @param endp_unit - Corresponding summary_unit structure for given rpc item.
 */
int c2_rpc_form_checking_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, int event);

/**
   State function for FORMING state.
   This state creates an RPC object structure (struct c2_rpc)
   by putting selected rpc items into the rpc object.
   It will also check for sessions details in each rpc item.
   If there are any unbounded items, sessions component will be
   queried to fetch sessions information for such items.
   Sessions information: Formation algorithm will call a sessions API
   c2_rpc_session_item_prepare() for all rpc items which will give
   the needed sessions information. This way unbound items will also
   get the sessions information. For items for which this API fails,
   they will evicted out of the formed rpc object.
   @param item - input rpc item.
   @param event - Since FORMING state handles a lot of events,
   it needs some way of identifying the events.
   @param endp_unit - Corresponding summary_unit structure for given rpc item.
 */
int c2_rpc_form_forming_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, int event);

/**
   State function for POSTING state.
   This state will post the formed rpc object to the output component.
   @param item - input rpc item.
   @param event - Since POSTING state handles a lot of events,
   it needs some way of identifying the events.
   @param endp_unit - Corresponding summary_unit structure for given rpc item.
 */
int c2_rpc_form_posting_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, int event);

/**
   State function for REMOVING state.
   This state is invoked due to events like rpc item being deleted or
   change in parameter of rpc item. It removes the given rpc item
   if it is selected to be a part of rpc object.
   @param item - input rpc item.
   @param event - Since REMOVING state handles a lot of events,
   it needs some way of identifying the events.
   @param endp_unit - Corresponding summary_unit structure for given rpc item.
 */
int c2_rpc_form_removing_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, int event);

/** @} endgroup of rpc_formation */

#endif /* __C2_RPC_FORMATION_H__ */

