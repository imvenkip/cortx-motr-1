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
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 04/25/2011
 */

#ifndef __C2_RPC_FORMATION_H__
#define __C2_RPC_FORMATION_H__

#include "lib/list.h"
#include "lib/mutex.h"
#include "lib/rwlock.h"
#include "addb/addb.h"

struct c2_fid;
struct c2_fop;
struct c2_fop_io_vec;
struct c2_fop_file_fid;
struct c2_rpc_slot;

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

   The Formation component will use a data structure (c2_rpc_frm_sm) that gives
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
   @verbatim

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

    @endverbatim

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
	- lock the internal data structure.
	- operate on the internal data structure.
	- release the internal data structure lock.
	- release the state lock.
	- pass through the subsequent states as states succeed and exit

   @todo A lot of data structures here, use a c2_list. Instead a
   hash function can be used wherever applicable to enhance the performance.

   There are no retries in the state machine. Any failure event will
   take the executing thread out of this state machine.

   Hierarchy of data structures in formation component.
   @verbatim
   c2_rpc_formation

     +--> c2_list <c2_rpc_frm_sm>

	    +--> c2_list <c2_rpc_frm_group>

   @endverbatim

   Locking order =>
   This order will ensure there are no deadlocks due to locking
   order reversal.
   - Always take the rf_sm_list_lock rwlock from struct c2_rpc_formation
      first and release when not needed any more.
   - Always take the fs_lock mutex from struct c2_rpc_frm_sm next
      and release when not needed any more.
 */

/**
   This structure builds up the summary form of data for all formation
   state machines. It contains a list of sub structures, one for each endpoint.
   This structure is embedded as inline object in c2_rpcmachine.
 */
struct c2_rpc_formation {
	/** List of formation state machine linked through
	    c2_rpc_frm_sm::fs_linkage. */
	struct c2_list			rf_frm_sm_list;
	/** Read/Write lock protecting the list from concurrent access. */
	struct c2_rwlock		rf_sm_list_lock;
	/** Flag denoting if current side is sender or receiver. */
	bool				rf_sender_side;
	/** ADDB context for all formation state machines. */
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
	enum c2_rpc_frm_state		 fs_state;
	/** List of structures containing data for each group linked
	    through c2_rpc_frm_group::frg_linkage. */
	struct c2_list			 fs_groups;
	/** List of coalesced rpc items linked through
	    c2_rpc_frm_item_coalesced::ic_linkage. */
	struct c2_list			 fs_coalesced_items;
	/** List of formed RPC objects kept with formation linked
	    through c2_rpc::r_linkage. */
	struct c2_list			 fs_rpcs;
	/** Array of lists (one per priority band) containing unformed
	    rpc items sorted according to increasing order of timeout. */
	struct c2_rpc_frm_prio_list
		fs_unformed_prio[C2_RPC_ITEM_PRIO_NR];
	/** Network layer attributes for buffer transfer. */
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
	/** Number of timed out rpc items. If this number is greater
	    than zero, formation algorithm will be invoked. */
	uint64_t			 fs_timedout_items_nr;
};

/**
   A magic constant to varify the sanity of c2_rpc_frm_buffer.
 */
enum {
	C2_RPC_FRM_BUFFER_MAGIC = 0x8135797531975313ULL,
};

/**
   Formation attributes for an rpc.
 */
struct c2_rpc_frm_buffer {
	/** A magic constant to verify sanity of buffer. */
	uint64_t		 fb_magic;
	/** The c2_net_buffer on which callback will trigger. */
	struct c2_net_buffer	 fb_buffer;
	/** The associated fromation state machine. */
	struct c2_rpc_frm_sm	*fb_frm_sm;
};

/**
   c2_rpc is a container of c2_rpc_items.
 */
struct c2_rpc {
	/** Linkage into list of rpc objects just formed or into the list
	    of rpc objects which are ready to be sent on wire. */
	struct c2_list_link		 r_linkage;
	/** List of member rpc items. */
	struct c2_list			 r_items;
	/** Items in this rpc are sent via this session. */
	struct c2_rpc_session		*r_session;
	/** Formation attributes (buffer, magic) for the rpc. */
	struct c2_rpc_frm_buffer	 r_fbuf;
};

void c2_rpcobj_init(struct c2_rpc *rpc);

void c2_rpcobj_fini(struct c2_rpc *rpc);

/**
   Connects resultant rpc item with its coalesced constituent rpc items.
   When a reply is received for a coalesced rpc item, it will find out
   the requesting coalesced rpc item and using this data structure,
   it will find out the constituent rpc items and invoke their
   completion callbacks accordingly.
 */
struct c2_rpc_frm_item_coalesced {
	/** Linkage to list of coalesced items anchored at
	    c2_rpc_formation::fs_coalesced_items. */
	struct c2_list_link		 ic_linkage;
	/** Resultant coalesced rpc item */
	struct c2_rpc_item		*ic_resultant_item;
	/** No of constituent rpc items. */
	uint64_t			 ic_member_nr;
	/** List of constituent rpc items for this coalesced item linked
	    through c2_rpc_item::ri_coalesced_linkage. */
	struct c2_list			 ic_member_list;
	/** Fop used to backup the original IO vector of resultant item
	    which is replaced during coalescing. */
	struct c2_fop			*ic_bkpfop;
};

/**
   This structure represents the summary data for a given rpc group
   destined for a given state machine.
   Formation tries to send all rpc items belonging to same rpc group
   together. This is a best-case effort. So when one item belonging
   to an rpc group arrives at formation, it is not immediately formed
   since more items from same group are expected to arrive shortly.
 */
struct c2_rpc_frm_group {
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
   Enumeration of internal and external events.
 */
enum c2_rpc_frm_evt_id {
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

	/** Execution succeeded in current state. */
	C2_RPC_FRM_INTEVT_STATE_SUCCEEDED = C2_RPC_FRM_EXTEVT_NR,
	/** Execution failed in current state. */
	C2_RPC_FRM_INTEVT_STATE_FAILED,
	/** Execution completed, exit the state machine. */
	C2_RPC_FRM_INTEVT_DONE,
	/** Max number of events. */
	C2_RPC_FRM_INTEVT_NR
};

/**
   Event object for rpc formation state machine.
 */
struct c2_rpc_frm_sm_event {
	/** Event identifier. */
	enum c2_rpc_frm_evt_id se_event;
};

/**
   Callback function for deletion of an rpc item from the rpc items cache.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
 */
int c2_rpc_frm_item_delete(struct c2_rpc_item *item);

/**
   Interfaces to change attributes of rpc items that have been already
   submitted to rpc layer.
 */
int c2_rpc_frm_item_priority_set(struct c2_rpc_item *item,
				 enum c2_rpc_item_priority prio);

int c2_rpc_frm_item_timeout_set(struct c2_rpc_item *item,
				c2_time_t deadline);

int c2_rpc_frm_item_group_set(struct c2_rpc_item *item,
			      struct c2_rpc_group *group);

/** @} endgroup of rpc_formation */

#endif /* __C2_RPC_FORMATION_H__ */
