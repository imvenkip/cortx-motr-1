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

struct c2_fop;

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
   These lists are sorted by deadlines. So the items with least deadline
   will be at the HEAD of list.
   Refer to the HLD of RPC Formation -
   https://docs.google.com/a/xyratex.com/Doc?docid=0AXXBPOl-5oGtZGRzMzZ2NXdfMGQ0ZjNweGdz&hl=en

   Formation is done on basis of various criterion
   - deadline, including URGENT items (deadline = 0)
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
   - deadline expired of an rpc item.
   - slot becomes idle.
   - unbounded item is added to the sessions list.
   - c2_net_buffer sent to destination, hence free it.

   Also, there are a number of states through which the formation state
   machine transitions.
   - WAITING (waiting for an event to trigger)
   - UPDATING (updates the formation state machine)
   - FORMING (core of formation algorithm)

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
           |          	        |    |                   |
           |      +-------------+    +-------------+     |
           |      |                                |     |
           V      |              d,e               |     v
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
 */

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
	/** Flag denoting if current side is sender or receiver. */
	bool				 fs_sender_side;
	/** ADDB context for all formation state machines. */
	struct c2_addb_ctx		 fs_rpc_form_addb;
	/** Mutex protecting the state machine from concurrent access. */
	struct c2_mutex			 fs_lock;
	enum c2_rpc_frm_state		 fs_state;
	/** List of structures containing data for each group linked
	    through c2_rpc_frm_group::frg_linkage. */
	struct c2_list			 fs_groups;
	/** List of formed RPC objects kept with formation linked
	    through c2_rpc::r_linkage. */
	struct c2_list			 fs_rpcs;
	/** Array of lists (one per priority band) containing unformed
	    rpc items sorted according to increasing order of deadline. */
	struct c2_rpc_frm_prio_list	 fs_unformed[C2_RPC_ITEM_PRIO_NR];
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
	/** Number of items still to be formed. */
	uint64_t			 fs_items_left;
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
   Callback function for deletion of an rpc item from the rpc items cache.
   Call the default handler function passing the rpc item and
   the corresponding event enum.
 */
int c2_rpc_frm_item_delete(struct c2_rpc_item *item);

/**
   Interfaces to change attributes of rpc items that have been already
   submitted to rpc layer.
 */
void c2_rpc_frm_item_priority_set(struct c2_rpc_item *item,
				  enum c2_rpc_item_priority prio);

void c2_rpc_frm_item_deadline_set(struct c2_rpc_item *item,
				  c2_time_t deadline);

void c2_rpc_frm_item_group_set(struct c2_rpc_item *item,
			       struct c2_rpc_group *group);

/** @} endgroup of rpc_formation */

#endif /* __C2_RPC_FORMATION_H__ */
