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
   The state machine runs per network endpoint and it maintains state and its
   internal data per endpoint.
   There are no internal threads belonging to formation component.
   Formation component uses the "RPC Items cache" as input and sends out
   RPC objects which are containers of rpc items as soon as they are ready.
   The grouping component populates the rpc items cache which is then
   used by formation component to form them into rpc objects.
   The rpc items cache is grouped into several lists, either in the list
   of unbound items in some slot or list of ready items on a slot.
   Each list contains rpc items destined for given endpoint.
   These lists are sorted by timeouts. So the items with least timeout
   will be at the HEAD of list.
   Refer to the HLD of RPC Formation -
   https://docs.google.com/a/xyratex.com/Doc?docid=0AXXBPOl-5oGtZGRzMzZ2NXdfMGQ0ZjNweGdz&hl=en

   Formation is done on basis of various criterion
   - timeout, including URGENT items (timeout = 0)
   - priority
   - rpc group
   - max_message_size (max permissible size of RPC object)
   - max_rpcs_in_flight (max number of RPCs that could be in flight)
   - max_message_fragments (max number of disjoint memory buffers)

   The Formation component will use an internal data structure that gives
   summary of data in the input list. This data structure will help in
   making quick decisions so as to select best candidates for rpc formation
   and can prevent multiple scans of the rpc items list.
   There are a multitude of external events that can come to formation and
   it handles them and moves the state machine to appropriate states as
   a result.

   The external events are
   - addition of an rpc item to the slot ready list.
   - deletion of rpc item.
   - change of parameter for an rpc item.
   - reply received.
   - deadline expired (timeout) of an rpc item.
   - slot becomes idle.
   - unbounded item is added to the sessions list.
   - c2_net_buffer sent to destination, hence free it.

   Also, there are a number of states through which the formation state
   machine transitions.
   - WAITING (waiting for an event to trigger)
   - UPDATING (updates the formation state machine)
   - FORMING (core of formation algorithm)

   Along with these external events, there are some implicit internal
   events which are used to transit the state machine from one state
   to the next state on the back of same thread.

   These internal events are
   - state succeeded.
   - state failed.

   The formation component maintains its current state in a state variable
   which is protected by a lock. At any time, the state machine can only be
   in one state.

   RPC formation state machine:
   \verbatim

                              UNINITIALIZED
                                   | frm_sm_init()
                                   |
                                   |
                                   |
                 a,b,c	           V         d,e
           +------------------- WAITING -----------------+
           |                    ^    ^                   |
           |          g	        |    |       f,g         |
           |      +-------------+    +-------------+     |
           |      |                                |     |
           V      |              d,e,f             |     v
           UPDATING -----------------------------> FORMING
           | ^  ^                                  |   | ^
           | |  |                a,b,c             |   | |
     a,b,c | |  +----------------------------------+   | | d,e
           +-+                                         +-+

    \endverbatim	   

    External Events :
	- a. Item ready (bound item)
	- b. Unbound item added
	- c. Unsolicited item added
	- d. Item reply received
	- e. Item deadline expired

	Internal Events :
	- f. State succeeded
	- g. State failed

   The lifecycle of any thread executing the formation state machine is
   something like this
	- execute a state function as a result of triggering of some event.
	- acquire the state lock.
	- change the state of state machine.
	- release the state lock.
	- lock the internal data structure.
	- operate on the internal data structure.
	- release the internal data structure lock.
	- pass through the sub sequent states as states succeed and exit

   @todo A lot of data structures here, use a c2_list. Instead a
   hash function can be used wherever applicable to enhance the performance.

   There are no retries in the state machine. Any failure event will
   take the executing thread out of this state machine.

   Hierarchy of data structures in formation component.
   \verbatim
   c2_rpc_formation

     +--> c2_list <c2_rpc_frm_sm>

	    +--> c2_list <c2_rpc_frm_rpcgroup>

   \endverbatim

   Locking order =>
   This order will ensure there are no deadlocks due to locking
   order reversal.
   - Always take the rf_sm_list_lock rwlock from struct c2_rpc_formation
      first and release when not needed any more.
   - Always take the fs_lock mutex from struct c2_rpc_frm_sm next
      and release when not needed any more.
 */

/**
   This structure is an internal data structure which builds up the
   summary form of data for all formation state machines.
   It contains a list of sub structures, one for each endpoint.
 */
struct c2_rpc_formation {
	/** List of formation state machine linked through
	    c2_rpc_frm_sm::fs_linkage
	    @code c2_list <struct c2_rpc_frm_sm> @endcode */
	struct c2_list			rf_frm_sm_list;
	/** Read/Write lock protecting the list from concurrent access. */
	struct c2_rwlock		rf_sm_list_lock;
	/** Flag denoting if current side is client or server. */
	bool				rf_client_side;
	/** ADDB context for this item summary */
	struct c2_addb_ctx		rf_rpc_form_addb;
};

/**
   Enumeration of all possible states.
 */
enum c2_rpc_frm_state {
	/** An uninitialized state to formation state machine. */
	C2_RPC_FRM_STATE_UNINITIALIZED = 0,
	/** WAITING state for state machine, where it waits for
	    any event to trigger. */
	C2_RPC_FRM_STATE_WAITING,
	/** UPDATING state for state machine, where it updates
	    internal data structure (struct c2_rpc_frm_sm) */
	C2_RPC_FRM_STATE_UPDATING,
	/** FORMING state for state machine, which employs formation
	    algorithm. */
	C2_RPC_FRM_STATE_FORMING,
	/** MAX States of state machine. */
	C2_RPC_FRM_STATES_NR
};

/**
   Initialization for formation component in rpc.
   This will register necessary callbacks and initialize
   necessary data structures.
   @param frm - formation state machine
   @retval 0 if init completed, -errno otherwise
 */
int c2_rpc_frm_init(struct c2_rpc_formation **frm);

/**
   Finish method for formation component in rpc.
   This will deallocate all memory claimed by formation
   and do necessary cleanup.
   @param formation - c2_rpc_formation structure to be finied
 */
void c2_rpc_frm_fini(struct c2_rpc_formation *frm);

/**
   Structure containing a priority and list of all items belonging to
   given priority.
 */
struct c2_rpc_frm_prio_list {
	/** Priority band for this list. */
	enum c2_rpc_item_priority pl_prio;
	/** List of unformed items sharing the pl_prio priority linked
	    through c2_rpc_item::ri_unformed_linkage. */
	struct c2_list		  pl_unformed_items;
};

/**
   This structure represents the summary data for a formation state machine.
   It contains a list containing the summary data for each rpc group
   and a list of files involved in any IO operations destined for
   the given network endpoint.
   There is a state maintained per state machine.
   This will help to ensure that requests belonging to different
   network endpoints can proceed in their own state machines. Only the
   requests belonging to same network endpoint will contest for the
   same state machine.
 */
struct c2_rpc_frm_sm {
	/** Back link to struct c2_rpc_formation. */
	struct c2_rpc_formation		*fs_formation;
	/** Mutex protecting the unit from concurrent access. */
	struct c2_mutex			 fs_lock;
	/** Linkage into the list of formation state machines anchored
	    at c2_rpc_formation::rf_frm_sm_list. */
	struct c2_list_link		 fs_linkage;
	/** The c2_rpc_conn structure which points to the destination network
	    endpoint, this formation state machine is directed towards. */
	struct c2_rpc_conn		*fs_rpcconn;
	/** State of the state machine. Threads will have to take the
	    unit_lock above before state can be changed. This
	    variable will bear one value from enum c2_rpc_frm_state. */
	enum c2_rpc_frm_state		 fs_state;
	/** Refcount for this summary unit. Refcount is related to an incoming
	    thread as well as the number of rpc items it refers to.
	    Only when the state machine doesn't contain any data about
	    unformed rpc items and there are no threads operating on the
	    state machine, will it be deallocated. */
	struct c2_ref			 fs_ref;
	/** List of structures containing data for each group linked
	    through c2_rpc_frm_rpcgroup::frg_linkage.
	    @code c2_list <struct c2_rpc_frm_rpcgroup>
	    @endcode */
	struct c2_list			 fs_groups;
	/** List of coalesced rpc items linked through
	    c2_rpc_frm_item_coalesced::ic_linkage.
	    @code c2_list <c2_rpc_frm_item_coalesced> @endcode */
	struct c2_list			 fs_coalesced_items;
	/** List of formed RPC objects kept with formation linked
	    through c2_rpc::r_linkage.
	    @code c2_list <struct c2_rpc> @endcode */
	struct c2_list			 fs_rpcs;
	/** Array of lists (one per priority band) containing unformed
	    rpc items sorted according to increasing order of timeout.
	    The very first list contains list of timed out rpc items. */
	struct c2_rpc_frm_prio_list
		fs_unformed_prio[C2_RPC_ITEM_PRIO_NR+1];
	/** These numbers will be subsequently kept with the statistics
	    component. Defining here for the sake of UT. */
	uint64_t			 fs_max_msg_size;
	uint64_t			 fs_max_frags;
	/** Statistics data. Currently stationed with formation but
	    later will be moved to Output sub component from rpc. */
	uint64_t			 fs_max_rpcs_in_flight;
	/** Number of rpcs in flight sent by this state machine. */
	uint64_t			 fs_curr_rpcs_in_flight;
	/** Cumulative size of items added to this state machine so far.
	    Will help to determine if an optimal rpc can be formed.*/
	uint64_t			 fs_cumulative_size;
	/** Number of urgent items which do not belong to any rpc group
	    added to this state machine so far.
	    Any number > 0 will trigger formation. */
	uint64_t			 fs_urgent_nogrp_items_nr;
	/** Number of complete groups in the sense that this state
	    machine contains all rpc items from such rpc groups.
	    Any number > 0 will trigger formation. */
	uint64_t			 fs_complete_groups_nr;
};

/**
   A magic constant to varify the sanity of c2_rpc_frm_buffer.
 */
enum {
	C2_RPC_FRM_BUFFER_MAGIC = 0x8135797531975313ULL,
};

/**
   A structure to process callbacks to posting events.
   This structure in used to associate an rpc object being sent,
   its associated item_summary_unit structure and the c2_net_buffer.
 */
struct c2_rpc_frm_buffer {
	/** A magic constant to verify sanity of buffer. */
	uint64_t				 fb_magic;
	/** The c2_net_buffer on which callback will trigger. */
	struct c2_net_buffer			 fb_buffer;
	/** The associated item_summary_unit structure. */
	struct c2_rpc_frm_sm			*fb_frm_sm;
	/** The rpc object which was sent through the c2_net_buffer here. */
	struct c2_rpc				*fb_rpc;
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
struct c2_rpc_frm_item_coalesced {
	/** Linkage to list of coalesced items anchored at
	    c2_rpc_formation::fs_coalesced_items. */
	struct c2_list_link		 ic_linkage;
	/** Concerned fid. */
	struct c2_fid			*ic_fid;
	/** Intent of operation, read or write */
	int				 ic_op_intent;
	/** Resultant coalesced rpc item */
	struct c2_rpc_item		*ic_resultant_item;
	/** No of constituent rpc items. */
	uint64_t			 ic_member_nr;
	/** List of constituent rpc items for this coalesced item linked
	    through c2_rpc_item::ri_coalesced_linkage.
	    @code c2_list<c2_rpc_item> @endcode */
	struct c2_list			 ic_member_list;
	/** Poiner to correct IO vector to restore it, when member IO
	    operations complete. */
	union c2_io_iovec		 ic_iovec;
};

/**
   This structure represents the summary data for a given rpc group
   destined for a given state machine.
   Formation tries to send all rpc items belonging to same rpc group
   together. This is a best-case effort. So when one item belonging
   to an rpc group arrives at formation, it is not immediately formed
   since more items from same group are expected to arrive shortly.
 */
struct c2_rpc_frm_rpcgroup {
	/** Linkage into the list of groups belonging to same state machine
	    anchored at c2_rpc_frm_sm::fs_groups. */
	struct c2_list_link		 frg_linkage;
	/** The rpc group, this data belongs to. */
	struct c2_rpc_group		*frg_group;
	/** Number of items from this group found so far. */
	uint64_t			 frg_items_nr;
	/** Number of expected items from this group. */
	uint64_t			 frg_expected_items_nr;
};

/**
   Enumeration of external events.
 */
enum c2_rpc_frm_ext_evt_id {
	/** Slot ready to send next item. */
	C2_RPC_FRM_EXTEVT_RPCITEM_READY = 0,
	/** Reply received for an rpc item. */
	C2_RPC_FRM_EXTEVT_RPCITEM_REPLY_RECEIVED,
	/** Deadline expired for rpc item. */
	C2_RPC_FRM_EXTEVT_RPCITEM_TIMEOUT,
	/** Slot has become idle */
	C2_RPC_FRM_EXTEVT_SLOT_IDLE,
	/** Freestanding (unbounded) item added to session */
	C2_RPC_FRM_EXTEVT_UBRPCITEM_ADDED,
	/** An unsolicited rpc item is added. For this item, no replies
	    are expected and they should be sent as unbound items. */
	C2_RPC_FRM_EXTEVT_USRPCITEM_ADDED,
	/** Network buffer can be freed */
	C2_RPC_FRM_EXTEVT_NET_BUFFER_FREE,
	/** RPC item removed from cache. */
	C2_RPC_FRM_EXTEVT_RPCITEM_REMOVED,
	/** Parameter change for rpc item. */
	C2_RPC_FRM_EXTEVT_RPCITEM_CHANGED,
	/** Max number of external events. */
	C2_RPC_FRM_EXTEVT_NR,
};

/**
   Enumeration of internal events.
 */
enum c2_rpc_frm_int_evt_id {
	/** Execution succeeded in current state. */
	C2_RPC_FRM_INTEVT_STATE_SUCCEEDED = C2_RPC_FRM_EXTEVT_NR,
	/** Execution failed in current state. */
	C2_RPC_FRM_INTEVT_STATE_FAILED,
	/** Execution completed, exit the state machine. */
	C2_RPC_FRM_INTEVT_STATE_DONE,
	/** Max number of events. */
	C2_RPC_FRM_INTEVT_NR
};

/**
   Enumeration of fields which are subject to change.
 */
enum c2_rpc_frm_item_change_fields {
	/** Change priority of item. */
	C2_RPC_ITEM_CHANGE_PRIORITY,
	/** Change deadline of item. */
	C2_RPC_ITEM_CHANGE_DEADLINE,
	/** Change rpc group of item. */
	C2_RPC_ITEM_CHANGE_RPCGROUP,
	/** Max number of fields subject to change.*/
	C2_RPC_ITEM_CHANGES_NR,
};

/**
   Union of all possible values to be changed from c2_rpc_item.
 */
union c2_rpc_frm_item_change_val {
	/** New priority of rpc item. */
	enum c2_rpc_item_priority	 cv_prio;
	/** New deadline of rpc item. */
	c2_time_t			 cv_deadline;
	/** New rpc group given rpc item belongs to. */
	struct c2_rpc_group		*cv_rpcgroup;
};

/**
   Used to track the parameter changes in an rpc item.
 */
struct c2_rpc_frm_item_change_req {
	/* Specifies which field is going to change. */
	int					 field_type;
	/* New value of the field. */
	union c2_rpc_frm_item_change_val	*value;
};

/**
   Event object for rpc formation state machine.
 */
struct c2_rpc_frm_sm_event {
	/** Event identifier. */
	enum c2_rpc_frm_ext_evt_id		 se_event;
	/** Private data of event. */
	struct c2_rpc_frm_item_change_req	*se_pvt;
};

/**
   Callback function for addition of an rpc item to the list of
   its corresponding free slot.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
   @retval 0 (success) -errno (failure)
 */
int c2_rpc_frm_item_ready(struct c2_rpc_item *item);

/**
   Callback function for deletion of an rpc item from the rpc items cache.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
   @retval 0 (success) -errno (failure)
 */
int c2_rpc_frm_item_delete(struct c2_rpc_item *item);

/**
   Callback function for change in parameter of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
   @param field_type - type of field that has changed
   @param val - new value
   @retval 0 (success) -errno (failure)
 */
int c2_rpc_frm_item_changed(struct c2_rpc_item *item, int field_type,
		union c2_rpc_frm_item_change_val *value);

/**
   Callback function for reply received of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param reply_item - reply item.
   @param req_item - request item
   @retval 0 (success) -errno (failure)
 */
int c2_rpc_frm_item_reply_received(struct c2_rpc_item *rep_item,
		struct c2_rpc_item *req_item);

/**
   Callback function for deadline expiry of an rpc item.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
   @retval 0 (success) -errno (failure)
 */
int c2_rpc_frm_item_timeout(struct c2_rpc_item *item);

/**
   Callback function for slot becoming idle.
   Adds the slot to the list of ready slots in concerned rpcmachine.
   @param item - slot structure for the slot which has become idle.
 */
void c2_rpc_frm_slot_idle(struct c2_rpc_slot *slot);

/**
   Callback function for unbounded item getting added to session.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
   @param item - incoming rpc item.
   @retval 0 (success) -errno (failure)
 */
int c2_rpc_frm_ubitem_added(struct c2_rpc_item *item);

/**
  Callback function for <struct c2_net_buffer> which indicates that
  message has been sent out from the buffer. This callback function
  corresponds to the C2_NET_QT_MSG_SEND event
  @param ev - net buffer event
 */
void c2_rpc_frm_net_buffer_sent(const struct c2_net_buffer_event *ev);

/**
   Function to map the on-wire FOP format to in-core FOP format.
   @param in - file format fid
   @param out - memory format fid
 */
void c2_rpc_frm_item_io_fid_wire2mem(struct c2_fop_file_fid *in,
		struct c2_fid *out);

/**
   Try to coalesce rpc items with similar fid and intent.
   @param c_item - c2_rpc_frm_item_coalesced structure.
   @param b_item - Given bound rpc item.
   @retval - 0 if routine succeeds, -ve number(errno) otherwise.
 */
int c2_rpc_item_io_coalesce(struct c2_rpc_frm_item_coalesced *c_item,
		struct c2_rpc_item *b_item);

/**
  @todo Temporary fix.
  @param msg_size - Max message size
  @param max_rpcs - Max rpcs in flight
  @param max_fragments - Max fragments size
 */
void c2_rpc_frm_set_thresholds(uint64_t msg_size, uint64_t max_rpcs,
		uint64_t max_fragments);

/**
   Decrement the current number of rpcs in flight from given rpc item.
   First, formation state machine is located from c2_rpc_conn and
   c2_rpcmachine pointed to by given rpc item and if formation state
   machine is found, its current count of in flight rpcs is decremented.
   @param item - Given rpc item.
 */
void c2_rpc_frm_rpcs_inflight_dec(struct c2_rpc_item *item);

/** @} endgroup of rpc_formation */

#endif /* __C2_RPC_FORMATION_H__ */

