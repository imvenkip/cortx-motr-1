#ifndef __C2_RPC_FORMATION_H__
#define __C2_RPC_FORMATION_H__

/**
  Formation component runs as a state machine driven by external events. 
  There are no internal threads belonging to formation component.
  Formation component uses the "RPC Items cache" as input and sends out
  RPC objects which are containers of rpc items as soon as they are ready.
  The grouping component populates the rpc items cache which is then
  used by formation component to form them into rpc objects.
  The rpc items cache is grouped into several lists, one per endpoint.
  Each list contains rpc items destined for given endpoint.
  These lists are sorted by timeouts. So the items with least timeout
  will be at the HEAD of list.
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

/**
   This structure is an internal data structure which builds up the 
   summary form of data for all endpoints. 
   It contains a list of sub structures, one for each endpoint. */
struct c2_rpc_form_item_summary {
	/** List of internal data structures containing data for each 
	    endpoint. */
	struct c2_list 		endp_list;
	/** Mutex protecting the list from concurrent access. */
	struct c2_mutex 	endp_list_lock;
};

/** 
   This structure contains the files involved in any IO operation 
   in a given rpc group. 
   So if, an rpc group contains IO requests on 2 files, this list 
   will contain these 2 fids. */
struct c2_rpc_form_file_list {
	/** List of file ids for IO requests to this endpoint. */
	struct c2_list		io_list;
};

/**
   Structure containing fid for io requests. 
   This will help to make quick decisions to select candidates 
   for coalescing of io requests. */
struct c2_rpc_form_fid_summary {
	/* File id on which IO request has come. */
	struct c2_fid		*fid;
	/* List of read requests on this fid. */
	struct c2_list 		read_list;
	/* List of write requests on this fid. */
	struct c2_list 		write_list;
	/* Linkage into list of fids for this endpoint. */
	struct c2_list_link 	*linkage;
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
   state variable. */
struct c2_rpc_form_item_summary_unit {
	/** Mutex protecting the unit from concurrent access. */
	struct c2_mutex			unit_lock;
	/** Linkage into the endpoint list. */
	struct c2_list_link 		*linkage;
	/** List of structures containing data for each group. */
	struct c2_list			groups_list;
	/** List of files being operated upon, for this endpoint. */
	struct c2_rpc_form_file_list	file_list;
	/** State of the state machine for this endpoint. 
	    Threads will have to take the unit_lock above before
	    state can be changed. This variable will bear one value
	    from enum c2_rpc_form_state. */
	int 				endp_state;
	/* List of coalesced rpc items. */
	struct c2_list			coalesced_items;
};

/**
   An internal data structure to connect coalesced rpc items with
   its constituent rpc items. When a reply is received for a 
   coalesced rpc item, it will find out the requesting coalesced 
   rpc item and using this data structure, it will find out the 
   constituent rpc items and invoke their completion callbacks accordingly. 
   Formation does not bother about splitting of reply for coalesced 
   rpc item into sub replies for constituent rpc items. */
struct c2_rpc_form_item_coalesced {
	/* Linkage to list of such coalesced rpc items. */
	struct c2_list_link		*linkage;
	/* Intent of operation, read or write */
	int				op_intent;
	/* Resultant coalesced rpc item*/
	struct c2_rpc_item		*resultant_item;
	/* No of constituent rpc items. */
	uint64_t			nmembers;
	/* List of constituent rpc items for this coalesced item. */
	struct c2_list			member_list;
};

/** 
   This structure represents the summary data for a given rpc group 
   destined for a given endpoint. */
struct c2_rpc_form_item_summary_unit_group {
	/** Linkage into the list of groups belonging to this 
	    endpoint. */
	struct c2_list_link	*linkage;
	/** The group number this data belongs to. */
	uint64_t		 group_no;
	/** Number of items from this group found so far. */
	uint64_t		 nitems;
	/** Number of expected items from this group. */
	uint64_t		 expected_items;
	/** Number of highest priority items from this group. 
	    This does not inlcude urgent items, they are 
	    handled elsewhere. */
	uint64_t 		 priority_items;
	/** Average time out for items in this group. This number 
	    gives an indication about relative position of group
	    within the cache list. */
	uint64_t		avg_timeout;
	/** Cumulative size of rpc items in this group so far. */
	uint64_t		total_size;
};

/** 
   This structure contains the list of rpc objects which are formed
   but not yet sent on wire. It contains a lock to protect concurrent
   access to the structure. */
struct c2_rpc_form_rpcobj_list {
	/** Mutex protecting the list of rpc objects from concurrent access. */
	struct c2_mutex		rpcobj_lock;
	/** List of rpc objects formed but not yet sent on wire. */
	struct c2_list		rpcobj_list;
};

/**
   Enumeration of all possible states. */
enum c2_rpc_form_state {
	C2_RPC_FORM_STATE_WAITING = 0,
	C2_RPC_FORM_STATE_UPDATING,
	C2_RPC_FORM_STATE_CHECKING,
	C2_RPC_FORM_STATE_FORMING,
	C2_RPC_FORM_STATE_POSTING,
	C2_RPC_FORM_STATE_REMOVING,
	C2_RPC_FORM_N_STATES
};

/** 
   Enumeration of external events. */
enum c2_rpc_form_ext_event {
	C2_RPC_FORM_EXTEVT_RPCITEM_ADDED = 0,
	C2_RPC_FORM_EXTEVT_RPCITEM_REMOVED,
	C2_RPC_FORM_EXTEVT_RPCITEM_CHANGED,
	C2_RPC_FORM_EXTEVT_RPCITEM_REPLY_RECEIVED,
	C2_RPC_FORM_EXTEVT_RPCITEM_TIMEOUT,
	C2_RPC_FORM_EXTEVT_N_EVENTS
};

/**
   Enumeration of internal events. */
enum c2_rpc_form_int_event {
	C2_RPC_FORM_INTEVT_STATE_SUCCEEDED = 0,
	C2_RPC_FORM_INTEVT_STATE_FAILED,
	C2_RPC_FORM_INTEVT_N_EVENTS
};

/**
   A state table guiding resultant states on arrival of events 
   on earlier states. 
   next_state = stateTable[current_state][current_event] */
(int (*ptr)(struct c2_rpc_item*, int)) c2_rpc_form_stateTable
[C2_RPC_FORM_N_STATES][C2_RPC_FORM_EXTEVT_N_EVENTS + C2_RPC_FORM_INTEVT_N_EVENTS] = {

	{ c2_rpc_form_updating_state, c2_rpc_form_updating_state, 
	  c2_rpc_form_updating_state, c2_rpc_form_waiting_state, 
	  c2_rpc_form_checking_state, c2_rpc_form_waiting_state, 
	  c2_rpc_form_waiting_state},

	{ c2_rpc_form_updating_state, c2_rpc_form_removing_state, 
	  c2_rpc_form_removing_state, c2_rpc_form_waiting_state, 
	  c2_rpc_form_checking_state, c2_rpc_form_checking_state, 
	  c2_rpc_form_updating_state},

	{ c2_rpc_form_updating_state, c2_rpc_form_removing_state, 
	  c2_rpc_form_removing_state, c2_rpc_form_waiting_state, 
	  c2_rpc_form_checking_state, c2_rpc_form_forming_state, 
	  c2_rpc_form_waiting_state},

	{ c2_rpc_form_updating_state, c2_rpc_form_removing_state, 
	  c2_rpc_form_removing_state, c2_rpc_form_waiting_state, 
	  c2_rpc_form_checking_state, c2_rpc_form_posting_state, 
	  c2_rpc_form_forming_state},

	{ c2_rpc_form_updating_state, c2_rpc_form_removing_state, 
	  c2_rpc_form_removing_state, c2_rpc_form_waiting_state, 
	  c2_rpc_form_checking_state, c2_rpc_form_waiting_state, 
	  c2_rpc_form_posting_state},

	{ c2_rpc_form_updating_state, c2_rpc_form_removing_state, 
	  c2_rpc_form_removing_state, c2_rpc_form_waiting_state, 
	  c2_rpc_form_checking_state, c2_rpc_form_waiting_state, 
	  c2_rpc_form_waiting_state}
};

/**
   Return the function pointer to next state given the current state
   and current event as input. */
(int (*ptr) (struct c2_rpc_item*, int)) c2_rpc_form_next_state
	(int current_state, int current_event)
{
	/* Return the next state by consulting the state table. */
}

/** 
   A default handler function for invoking all state functions
   based on incoming event. */
void c2_rpc_form_default_handler(struct c2_rpc_item *item, int event)
{
	/* Find out the endpoint for given rpc item. */
	/* Lock the c2_rpc_form_item_summary_unit data structure. */
	/* Fetch the state for this endpoint and find out the resulting state
	   from the state table given this event.*/
	/* Call the respective state function for resulting state. */
	/* Release the lock. */
	/* Handle further events on basis of return value of 
	   recent state function. */
}

/** 
   Callback functions for handling of external events. 
   These functions call the default handler which calls corresponding 
   state functions. */

/** 
   Callback function for addition of an rpc item to the rpc items cache. */
int c2_rpc_form_extevt_rpcitem_added_in_cache(struct c2_rpc_item *item) 
{
	/* Call the default handler function passing the rpc item and 
	   the corresponding event enum. */
}

/** 
   Callback function for deletion of an rpc item from the rpc items cache. */
int c2_rpc_form_extevt_rpcitem_deleted_from_cache(struct c2_rpc_item *item) 
{
	/* Call the default handler function passing the rpc item and 
	   the corresponding event enum. */
}

/** 
   Callback function for change in parameter of an rpc item. */
int c2_rpc_form_extevt_rpcitem_changed(struct c2_rpc_item *item) 
{
	/* Call the default handler function passing the rpc item and 
	   the corresponding event enum. */
}

/** 
   Callback function for reply received of an rpc item. */
int c2_rpc_form_extevt_rpcitem_reply_received(struct c2_rpc_item *item) 
{
	/* Call the default handler function passing the rpc item and 
	   the corresponding event enum. */
}

/** 
   Callback function for deadline expiry of an rpc item. */
int c2_rpc_form_extevt_rpcitem_deadline_expired(struct c2_rpc_item *item) 
{
	/* Call the default handler function passing the rpc item and 
	   the corresponding event enum. */
}

/** 
   Callback functions for handling of internal events. 
   These functions call the default handler which calls corresponding
   state functions. */

/**
   Callback function for successful completion of a state. */
int c2_rpc_form_intevt_state_succeeded(int state)
{
	/* Call the default handler function. Depending upon the 
	   input state, the default handler will invoke the next state
	   for state succeeded event. */
}

/**
   Callback function for failure of a state. */
int c2_rpc_form_intevt_state_failed(int state)
{
	/* Call the default handler function. Depending upon the 
	   input state, the default handler will invoke the next state
	   for state succeeded event. */
}

/**
   Function to do the coalescing of related rpc items. 
   This is invoked from FORMING state, so a list of selected 
   rpc items is input to this function which coalesces 
   possible items and shrinks the list. */
int c2_rpc_form_coalesce_items(struct c2_list *items)
{
}

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
   @param item - input rpc item.
   @param event - Since WAITING state handles a lot of events,
   it needs some way of identifying the events. */
int c2_rpc_form_waiting_state(struct c2_rpc_item *item, int event);

/** 
   State function for UPDATING state. 
   Formation is updating its internal data structure by taking necessary locks.
   @param item - input rpc item.
   @param event - Since UPDATING state handles a lot of events,
   it needs some way of identifying the events. */
int c2_rpc_form_updating_state(struct c2_rpc_item *item, int event);

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
   @param item - input rpc item.
   @param event - Since CHECKING state handles a lot of events,
   it needs some way of identifying the events. */
int c2_rpc_form_checking_state(struct c2_rpc_item *item, int event)
{
	/**
	   Formation Algorithm. 
	   1. Read rpc items from the cache for this endpoint. 
	   2. If the item deadline is zero(urgent), add it to a local
	      list of rpc items to be formed. 
	   3. Check size of formed rpc object so far to see if its optimal.
	      Here size of rpc is compared with max_message_size. If size of
	      rpc is far less than max_message_size, goto #1. 
	   4. If #3 is true and if the number of disjoint memory buffers 
	      is less than parameter max_fragment_size, a probable rpc object 
	      is in making. The selected rpc items are put on a list
	      and the state machine transitions to next state.
	   5. Consult the structure c2_rpc_form_item_summary_unit to find out
	      data about all rpc groups. Select groups that have combination of 
	      lowest average timeout and highest size that fits into optimal
	      size. Keep selecting such groups till optimal size rpc is 
	      not formed.
	   6. Consult the structure c2_rpc_form_file_list to find out files
	      on which IO requests have come for this endpoint. Do coalescing
	      within groups selected for formation according to read/write 
	      intents. Later if rpc has still not reached its optimal size,
	      coalescing across rpc groups will be done.
	   7. Negate the data of selected rpc items from internal data 
	      structure. 
	   8. If the formed rpc object is sub optimal but it contains
	      an urgent item, it will be formed immediately. Else, it will
	      be discarded.
	 
	 
	 
	 */
}

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
   it needs some way of identifying the events. */
int c2_rpc_form_forming_state(struct c2_rpc_item *item, int event);

/** 
   State function for POSTING state. 
   This state will post the formed rpc object to the output component.
   @param item - input rpc item.
   @param event - Since POSTING state handles a lot of events,
   it needs some way of identifying the events. */
int c2_rpc_form_posting_state(struct c2_rpc_item *item, int event);

/** 
   State function for REMOVING state. 
   This state is invoked due to events like rpc item being deleted or 
   change in parameter of rpc item. It removes the given rpc item 
   if it is selected to be a part of rpc object.
   @param item - input rpc item.
   @param event - Since REMOVING state handles a lot of events,
   it needs some way of identifying the events. */
int c2_rpc_form_removing_state(struct c2_rpc_item *item, int event);

#endif 

