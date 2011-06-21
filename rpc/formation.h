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
 * Original creation date: 04/25/2011
 */

#ifndef __C2_RPC_FORMATION_H__
#define __C2_RPC_FORMATION_H__

#include "fop/fop.h"
#include "rpc/rpccore.h"
#include "rpc/session.h"
#include "lib/timer.h"
#include "lib/list.h"
#include "lib/mutex.h"
#include "lib/rwlock.h"
#include "ioservice/io_fops_u.h"
#include "ioservice/io_fops.h"
#include "lib/refs.h"
#include "lib/errno.h"
#include "lib/cdefs.h"
#include "lib/memory.h"
#include "addb/addb.h"

/**
   @defgroup rpc_formation Formation sub component from RPC layer.
   @{
   Formation component runs as a state machine driven by external events.
   The state machine runs per endpoint and it maintains state and its
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
   events which are used to transit the state machine from one state
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
   Hierarchy of data structures in formation component.

   c2_rpc_form_item_summary

     +--> c2_list <c2_rpc_form_item_summary_unit>

	    +--> c2_list <c2_rpc_form_item_summary_unit_group>
 */

/**
   Locking order =>
   This order will ensure there are no deadlocks due to locking
   order reversal.
   1. Always take the is_endp_list_lock rwlock from struct
      c2_rpc_form_item_summary first and release when not
      needed any more.
   2. Always take the isu_unit_lock mutex from struct
      c2_rpc_form_item_summary_unit next and release when
      not needed any more.
 */

/**
   This structure is an internal data structure which builds up the
   summary form of data for all endpoints.
   It contains a list of sub structures, one for each endpoint.
 */
struct c2_rpc_form_item_summary {
	/** List of internal data structures with data for each endpoint */
	/** c2_list <struct c2_rpc_form_item_summary_unit> */
	struct c2_list			is_endp_list;
	/** Read/Write lock protecting the list from concurrent access. */
	struct c2_rwlock		is_endp_list_lock;
	/** ADDB context for this item summary */
	struct c2_addb_ctx		is_rpc_form_addb;
};

/**
   The global instance of summary data structure.
 */
extern struct c2_rpc_form_item_summary	*formation_summary;


/**
   Check if refcounts of all endpoints are zero.
 */
bool c2_rpc_form_wait_for_completion();

/**
   The global instance of rpc items cache.
 */
extern struct c2_rpc_form_items_cache	*items_cache;

/** XXX The cache of rpc items. Ideally, it should
  come from grouping component, which does not exist at the
  moment. Hence emulating this list here.
 */
struct c2_rpc_form_items_cache {
	/** Destination Endpoint */
	struct c2_net_end_point		*ic_endp;
	/** Mutex to protect the list from concurrent access. */
	struct c2_mutex			 ic_mutex;
	/** List of rpc items destined for this endpoint. */
	/** c2_list <struct c2_rpc_item> */
	struct c2_list			 ic_cache_list;
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
   State machine object for rpc formation.
   There is one state machine per c2_rpc_form_item_summary_unit
   structure.
 */
struct c2_rpc_form_state_machine {
	/** State of the state machine for this endpoint.
	    Threads will have to take the unit_lock above before
	    state can be changed. This variable will bear one value
	    from enum c2_rpc_form_state. */
	enum c2_rpc_form_state		isu_endp_state;
	/** Refcount for this summary unit.
	    Refcount is related to an incoming thread as well as
	    the number of rpc items it refers to.
	    Only when the endpoint unit doesn't contain any
	    data about unformed rpc items and there are no threads
	    operating on the endpoint unit, will it be deallocated. */
	struct c2_ref			 isu_ref;
};

/**
   Event object for rpc formation state machine.
 */
struct c2_rpc_form_sm_event {
	/** Event identifier. */
	int				 se_event;
	/** Private data of event. */
	void				*se_pvt;
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
	/** Mutex protecting the unit from concurrent access. */
	struct c2_mutex			 isu_unit_lock;
	/** Linkage into the endpoint list. */
	struct c2_list_link		 isu_linkage;
	/** Referenced Endpoint */
	struct c2_net_end_point		*isu_endp_id;
	/** Flag indicating the formation component is still active. */
	bool				 isu_form_active;
	/** State machine for this endpoint. */
	struct c2_rpc_form_state_machine isu_sm;
	/** List of structures containing data for each group. */
	/** c2_list <struct c2_rpc_form_item_summary_unit_group>*/
	struct c2_list			 isu_groups_list;
	/** List of coalesced rpc items. */
	/** c2_list <c2_rpc_form_item_coalesced> */
	struct c2_list			 isu_coalesced_items_list;
	/** List of formed RPC objects kept with formation. The
	    POSTING state will send RPC objects from this list
	    to the output component.
	    c2_list <struct c2_rpc_form_rpcobj> */
	struct c2_list			 isu_rpcobj_formed_list;
	/** A list of checked rpc objects. This list is populated
	    by CHECKING state while FORMING state removes rpc objects
	    from this list, populates sessions information for all
	    member rpc items and puts the rpc object on formed list of
	    rpc objects.
	    c2_list <struct c2_rpc_form_rpcobj>*/
	struct c2_list			 isu_rpcobj_checked_list;
	/** List of unformed rpc items. */
	/** c2_list <struct c2_rpc_item>*/
	struct c2_list			 isu_unformed_list;
	/** List of fids on which IO requests are made in this rpc group. */
	/** c2_list <struct c2_rpc_form_fid_summary_member > */
	struct c2_list			 isu_fid_list;
	/** These numbers will be subsequently kept with the statistics
	  component. Defining here for the sake of UT. */
	uint64_t			 isu_max_message_size;
	uint64_t			 isu_max_fragments_size;
	/** Statistics data. Currently stationed with formation but
	    later will be moved to Output sub component from rpc. */
	uint64_t			 isu_max_rpcs_in_flight;
	uint64_t			 isu_curr_rpcs_in_flight;
	/** Cumulative size of items added to this endpoint so far.
	    Will help to determine if an optimal rpc can be formed.*/
	uint64_t			 isu_cumulative_size;
	/** Number of urgent items added to this endpoint so far.
	    Any number > 0 will trigger formation.*/
	uint64_t			 isu_n_urgent_items;
};

/**
   Given an endpoint, tell if an optimal rpc can be prepared from
   the items submitted to this endpoint.
   @param endp_unit - the c2_rpc_form_item_summary_unit structure
   based on whose data, it will be found if an optimal rpc can be made.
 */
bool c2_rpc_form_can_form_optimal_rpc(struct c2_rpc_form_item_summary_unit
		*endp_unit);

/**
   Get the endpoint given an rpc item.
   This is a placeholder and will be replaced when a concrete
   association between rpc item and endpoint is available.
   @param item - incoming rpc item.
 */
struct c2_net_end_point *c2_rpc_form_get_endpoint(struct c2_rpc_item *item);

/**
   An enumeration of IO opcodes.
 */
enum c2_rpc_form_io_opcode {
	C2_RPC_FORM_IO_READ = 1,
	C2_RPC_FORM_IO_WRITE = 2
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
	struct c2_list_link	        ic_linkage;
	/** Concerned fid. */
	struct c2_fid			ic_fid;
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
	struct c2_list_link		 im_linkage;
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
	struct c2_list_link		 sug_linkage;
	/** The rpc group, this data belongs to. */
	struct c2_rpc_group		*sug_group;
	/** Number of items from this group found so far. */
	uint64_t			 sug_nitems;
	/** Number of expected items from this group. */
	uint64_t			 sug_expected_items;
	/** Number of highest priority items from this group.
	    This does not inlcude urgent items, they are
	    handled elsewhere. */
	uint64_t			 sug_priority_items;
	/** Average time out for items in this group. This number
	    gives an indication about relative position of group
	    within the cache list. */
	double				 sug_avg_timeout;
	/** Cumulative size of rpc items in this group so far. */
	uint64_t			 sug_total_size;
};

/**
   This is a wrapper structure around struct c2_rpc to engage
   rpc objects in a list.
 */
struct c2_rpc_form_rpcobj {
	/** Linkage into list of c2_rpc
	    from struct c2_rpc_form_rpcobj_list */
	struct c2_list_link		 ro_linkage;
	/** Actual rpc object. */
	struct c2_rpc			*ro_rpcobj;
};

/**
   Enumeration of external events.
 */
enum c2_rpc_form_ext_event {
	/** Slot ready to send next item. */
	C2_RPC_FORM_EXTEVT_RPCITEM_READY = 0,
	/** RPC item removed from cache. */
	C2_RPC_FORM_EXTEVT_RPCITEM_REMOVED,
	/** Parameter change for rpc item. */
	C2_RPC_FORM_EXTEVT_RPCITEM_CHANGED,
	/** Reply received for an rpc item. */
	C2_RPC_FORM_EXTEVT_RPCITEM_REPLY_RECEIVED,
	/** Deadline expired for rpc item. */
	C2_RPC_FORM_EXTEVT_RPCITEM_TIMEOUT,
	/** Slot has become idle */
	C2_RPC_FORM_EXTEVT_SLOT_IDLE,
	/** Freestanding (unbounded) item added to session */
	C2_RPC_FORM_EXTEVT_UNBOUNDED_RPCITEM_ADDED,
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

/**
   Callback used to trigger the "deadline expired" event
   for an rpc item.
   @param data - private data of user of timer.
 */
unsigned long c2_rpc_form_item_timer_callback(unsigned long data);

/**
   Enumeration of fields which are subject to change.
 */
enum c2_rpc_form_item_change_fields {
	/** Change priority of item. */
	C2_RPC_ITEM_CHANGE_PRIORITY,
	/** Change deadline of item. */
	C2_RPC_ITEM_CHANGE_DEADLINE,
	/** Change rpc group of item. */
	C2_RPC_ITEM_CHANGE_RPCGROUP,
	/** Max number of fields subject to change.*/
	C2_RPC_ITEM_N_CHANGES,
};

/**
   Used to track the parameter changes in an rpc item.
 */
struct c2_rpc_form_item_change_req {
	/* Specifies which field is going to change. */
	int			 field_type;
	/* New value of the field. */
	void			*value;
};

/**
   Callback function for addition of an rpc item to the list of
   its corresponding free slot.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_ready(struct c2_rpc_item *item);

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
int c2_rpc_form_extevt_rpcitem_changed(struct c2_rpc_item *item,
		int field_type, void *value);

/**
   Callback function for reply received of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_reply_received(struct c2_rpc_item *rep_item,
		struct c2_rpc_item *req_item);

/**
   Callback function for deadline expiry of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_rpcitem_deadline_expired(struct c2_rpc_item *item);

/**
   Callback function for slot becoming idle.
   Adds the slot to the list of ready slots in concerned rpcmachine.
   @param item - slot structure for the slot which has become idle.
 */
int c2_rpc_form_extevt_slot_idle(struct c2_rpc_slot *slot);

/**
   Callback function for unbounded item getting added to session.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
 */
int c2_rpc_form_extevt_unbounded_rpcitem_added(struct c2_rpc_item *item);

/**
   Function to do the coalescing of related rpc items.
   This is invoked from FORMING state, so a list of selected
   rpc items is input to this function which coalesces
   possible items and shrinks the list.
   @param items - list of items to be coalesced.
 */
int c2_rpc_form_coalesce_items(struct c2_list *items);

/**
   Try to coalesce rpc items from the session->free list.
   @param endp_unit - the item_summary_unit structure in which these activities
   are taking place.
   @param item - given bound rpc item.
   @param rpcobj_size - current size of rpc object.
 */
int c2_rpc_form_try_coalesce(struct c2_rpc_form_item_summary_unit *endp_unit,
                struct c2_rpc_item *item, uint64_t *rpcobj_size);

/**
   Try to coalesce items sharing same fid and intent(read/write).
   @param b_item - given bound rpc item.
   @param coalesced_item - item_coalesced structure for which coalescing
   will be done.
 */
int c2_rpc_form_coalesce_fid_intent(struct c2_rpc_item *b_item,
                struct c2_rpc_form_item_coalesced *coalesced_item);

/**
   State function for WAITING state.
   Formation is waiting for any event to trigger.
   1. At first, this state will handle the "reply received" event and if
   reply is for a coalesced item, it will call completion callbacks for
   all constituent rpc items by referring internal structure
   c2_rpc_form_item_coalesced.
   2. If current rpcs in flight ares less than max_rpcs_in_flight,
   this state can transit into next state. During this state,
   rpc objects could be in flight. If current rpcs in flight [n]
   is less than max_rpcs_in_flight, state transits to UPDATING,
   else keeps waiting till n is less than max_rpcs_in_flight.
   ** WAITING state should be a nop for internal events. **
   @param item - input rpc item.
   @param event - Since WAITING state handles a lot of events,
   @param endp_unit - Corresponding summary_unit structure for given rpc item.
   it needs some way of identifying the events.
 */
int c2_rpc_form_waiting_state(struct c2_rpc_form_item_summary_unit *endp_unit,
		struct c2_rpc_item *item, const struct c2_rpc_form_sm_event
		*event);

/**
   State function for UPDATING state.
   Formation is updating its internal data structure by taking necessary locks.
   @param item - input rpc item.
   @param event - Since UPDATING state handles a lot of events,
   it needs some way of identifying the events.
   @param endp_unit - Corresponding summary_unit structure for given rpc item.
 */
int c2_rpc_form_updating_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, const struct c2_rpc_form_sm_event
		*event);

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
		,struct c2_rpc_item *item, const struct c2_rpc_form_sm_event
		*event);

/**
   State function for FORMING state.
   This state will iterate through list of checked rpc objects
   which are ready to be sent but don't have sessions information yet.
   It will check for sessions details for each rpc item in an rpc object.
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
		,struct c2_rpc_item *item, const struct c2_rpc_form_sm_event
		*event);

/**
   State function for POSTING state.
   This state will post the formed rpc object to the output component.
   @param item - input rpc item.
   @param event - Since POSTING state handles a lot of events,
   it needs some way of identifying the events.
   @param endp_unit - Corresponding summary_unit structure for given rpc item.
 */
int c2_rpc_form_posting_state(struct c2_rpc_form_item_summary_unit *endp_unit
		,struct c2_rpc_item *item, const struct c2_rpc_form_sm_event
		*event);

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
		,struct c2_rpc_item *item, const struct c2_rpc_form_sm_event
		*event);

/**
   Type definition of a state function.
   @param endp_unit - given item_summary_unit structure.
   @param item - incoming rpc item.
   @param event - triggered event.
   @param pvt - private data of rpc item.
 */
typedef int (*stateFunc)(struct c2_rpc_form_item_summary_unit *endp_unit,
		struct c2_rpc_item *item,
		const struct c2_rpc_form_sm_event *event);

/**
   XXX rio_replied op from rpc type ops.
   If this is an IO request, free the IO vector
   and free the fop.
 */
void c2_rpc_item_replied(struct c2_rpc_item *item, int rc);

/**
   XXX Need to move to appropriate file
   RPC item ops function
   Function to return size of fop
 */
uint64_t c2_rpc_item_size(struct c2_rpc_item *item);

/**
   XXX Need to move to appropriate file
   RPC item ops function
   Function to return the opcode given an rpc item
 */
int c2_rpc_item_io_get_opcode(struct c2_rpc_item *item);

/**
   XXX Need to move to appropriate file
   RPC item ops function
   Function to get the fid for an IO request from the rpc item
 */
struct c2_fid c2_rpc_item_io_get_fid(struct c2_rpc_item *item);

/**
   XXX Need to move to appropriate file
   RPC item ops function
   Function to find out if the item belongs to an IO request or not
 */
bool c2_rpc_item_is_io_req(struct c2_rpc_item *item);

/**
   XXX Need to move to appropriate file
   RPC item ops function
   Function to find out number of fragmented buffers in IO request
 */
uint64_t c2_rpc_item_get_io_fragment_count(struct c2_rpc_item *item);

/**
   XXX Needs to be implemented.
 */
struct c2_update_stream *c2_rpc_get_update_stream(struct c2_rpc_item *item);

/**
   Check if two rpc items belong to same type.
 */
bool c2_rpc_item_equal(struct c2_rpc_item *item1, struct c2_rpc_item *item2);

/**
   Return opcode of a given fop referenced by this item.
 */
int c2_rpc_item_get_opcode(struct c2_rpc_item *item);

/**
   Try to coalesce rpc items with similar fid and intent.
 */
int c2_rpc_item_io_coalesce(void *coalesced_item, struct c2_rpc_item *b_item);

/**
   Function to map the on-wire FOP format to in-core FOP format.
 */
void c2_rpc_form_item_io_fid_wire2mem(struct c2_fop_file_fid *in,
		struct c2_fid *out);

/**
   XXX Needs to be implemented.
 */
int c2_net_send(struct c2_net_end_point *endp, struct c2_rpc *rpc);

/**
  XXX Temporary fix.
 */
void c2_rpc_form_set_thresholds(uint64_t msg_size, uint64_t max_rpcs,
		uint64_t max_fragments);

/**
   Retrieve slot and verno information from sessions component
   for an unbound item.
 */
void item_add_internal(struct c2_rpc_slot *slot, struct c2_rpc_item *item);


/* Instrumentation for detecting reference leaks. Used for testing. */
struct c2_rpc_form_ut_thread_reftrack {
	struct c2_thread_handle		handle;
	int				refcount;
};

/* nthreads in UT  = 256,  + 256 * ((rpcitem_changed | rpcitem_replied) &&
   rpcitem_deadline_expired)  = 256*3. */
#define rpc_form_ut_threads	256*3

struct c2_rpc_form_ut_thread_reftrack thrd_reftrack[rpc_form_ut_threads];
int	n_ut_threads;

/** @} endgroup of rpc_formation */

#endif /* __C2_RPC_FORMATION_H__ */

