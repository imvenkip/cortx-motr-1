/* -*- C -*- */
/*
 * COPYRIGHT 2012 XYRATEX TECHNOLOGY LIMITED
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
 * Original creation date: 04/28/2011
 */

#pragma once

#ifndef __COLIBRI_RPC_FORMATION2_H__
#define __COLIBRI_RPC_FORMATION2_H__

/**
   Formation component for Colibri RPC layer is what IO scheduler is for
   block device. Because of network layer overhead associated with each
   message (i.e. buffer), sending each individual RPC item directly to network
   layer can be inefficient. Instead, formation component tries to send
   multiple RPC items in same network layer message to improve performance.

   RPC items that are posted to RPC layer for sending are enqueued in formation
   queue. Formation then prepares "RPC Packets". A RPC Packet is collection
   of RPC items that are sent together in same network layer buffer.

   To improve performance formation component does two things:

   - Batching:
     Formation batches multiple RPC items - that are targeted to same
     destination end-point - in RPC packets.

   - Merging:
     If there are any two items A and B such that they can be merged into
     _one_ item then Formation tries to merge these items and add this
     aggregate RPC item to the packet. How the "contents" of RPC items A and B
     are merged together is dependent on _item type_ of A and B. Formation
     is not aware about how these items are merged.

   While forming RPC packets Formation has to obey several "constraints":
   - max_packet_size:
   - max_nr_bytes_accumulated:
   - max_nr_segments
   - max_nr_packets_enqed
   @see c2_rpc_frm_constraints for more information.

   It is important to note that Formation has something to do only on
   "outgoing path".

   NOTE:
   - RPC Packet is also referred as "RPC" in some other parts of code and docs
   - A "one-way" item is also referred as "unsolicited" item.

   @todo XXX item merging support
   @todo XXX stats collection
   @todo XXX Support for "RPC Group"
   @todo XXX RPC item cancellation
   @todo XXX RPC item deadline timer based on generic state machine framework
   @todo XXX Better RPC level flow control than the one provided by
             c2_rpc_frm_constraints::fc_max_nr_packets_enqed
 */

#include "lib/types.h"
#include "lib/tlist.h"
#include "addb/addb.h"

/* Imports */
struct c2_rpc_packet;
struct c2_rpc_item;
struct c2_rpc_session;

/* Forward references */
struct c2_rpc_frm_ops;

/**
   Constraints that should be taken into consideration while forming packets.
 */
struct c2_rpc_frm_constraints {

	/**
	   Maximum number of packets such that they are submitted to network
	   layer, but its completion callback is not yet received.
	 */
	uint64_t    fc_max_nr_packets_enqed;

	/**
	   On wire size of a packet should not cross this limit. This is
	   usually set to maximum supported size of SEND network buffer.

	   @see c2_rpc_machine::rm_min_recv_size
	 */
	c2_bcount_t fc_max_packet_size;

	/**
	   Maximum number of non-contiguous memory segments allowed by
	   network layer in a network buffer.
	 */
	uint64_t    fc_max_nr_segments;

	/**
	   If sum of on-wire sizes of all the enqueued RPC items is greater
	   than fc_max_nr_bytes_accumulated, then formation should try to
	   form RPC packet out of them.
	 */
	c2_bcount_t fc_max_nr_bytes_accumulated;
};

/**
   Possible states of formation state machine.

   @see c2_rpc_frm::f_state
 */
enum frm_state {
	FRM_UNINITIALISED,
	/** There are no pending items in the formation queue AND
	    No callback is pending @see frm_is_idle()
	 */
	FRM_IDLE,
	/** There are few items waiting in the formation queue OR
	    some packet done callbacks are yet to be received.
	 */
	FRM_BUSY,
	FRM_NR_STATES
};

/**
   Formation partitions RPC items in these types of queues.
   An item can migrate from one queue to another depending on its state.

   TIMEDOUT_* are the queues which contain items whose deadline has been
   passed. These items should be sent as soon as possible.

   WAITING_* are the queues which contain items whose deadline is not yet
   reached. An item from these queues can be picked for formation even
   before its deadline is passed.

   (TIMEDOUT|WAITING)_BOUND queues contain items for which slot is already
   assigned. These are "ready to go" items.
   (TIMEDOUT|WAITING)_UNBOUND queues contain items for which slot needs to
   assigned first. @see c2_rpc_frm_ops::fo_item_bind()

   A bound item cannot be merged with other bound RPC items.
 */
enum c2_rpc_frm_itemq_type {
	FRMQ_TIMEDOUT_BOUND,
	FRMQ_TIMEDOUT_UNBOUND,
	FRMQ_TIMEDOUT_ONEWAY,
	FRMQ_WAITING_BOUND,
	FRMQ_WAITING_UNBOUND,
	FRMQ_WAITING_ONEWAY,
	FRMQ_NR_QUEUES
};

/**
   Formation state machine.

   There is one instance of c2_rpc_frm for each destination end-point.

   Events in which the formation state machine is interested are:

   - RPC item is posted for sending
   - RPC packet has been sent or packet sending is failed
   - deadline timer of WAITING item is expired
   - Ready slot is available

   Events that formation machine triggers for rest of RPC are:

   - Packet is ready for sending
   - Request to bind an item to a slot

@verbatim
                FRM_UNINITIALISED
                      |  ^
     c2_rpc_frm_init()|  |
                      |  | c2_rpc_frm_fini()
                      V  |
                    FRM_IDLE
                      |  ^
 c2_rpc_frm_enq_item()|  | frm_itemq_remove() or c2_rpc_frm_packet_done()
   [frm_is_idle()]    |  | [!frm_is_idle()]
                      V  |
                    FRM_BUSY
@endverbatim

   <B>Concurrency and Existence: </B> @n

   Access to c2_rpc_frm instance is synchronised by c2_rpc_machine::rm_mutex.

   c2_rpc_frm is not reference counted. It is responsibility of user to
   free c2_rpc_frm. Ensuring that c2_rpc_frm is in IDLE state, before
   finalising it, is left to user.

   @see frm_invariant()
 */
struct c2_rpc_frm {
	/**
	   Current state.

	   Note: Because of very simple nature of formation state machine,
	   currently we are not using generic sm framework. If need arises
	   in future, we should implement formation state machine using
	   generic sm framework.
	 */
	enum frm_state                 f_state;

	/**
	   Lists of items enqueued to Formation that are not yet
	   added to any Packet. @see c2_rpc_frm_itemq_type
	   itemq are sorted by c2_rpc_item::ri_prio (highest priority first).
	   All items having equal priority are sorted by
	   c2_rpc_item::ri_deadline (earlier first).
	   An item is removed from itemq immediately upon adding the item to
	   any packet.
	   link: c2_rpc_item::ri_iq_link
	   descriptor: itemq
	 */
	struct c2_tl                   f_itemq[FRMQ_NR_QUEUES];

	/** Total number of items waiting in itemq */
	uint64_t                       f_nr_items;

	/** Sum of on-wire size of all the items in itemq */
	c2_bcount_t                    f_nr_bytes_accumulated;

	/** Number of packets for which "Packet done" callback is pending */
	uint64_t                       f_nr_packets_enqed;

	/** Limits that formation should respect */
	struct c2_rpc_frm_constraints  f_constraints;

	const struct c2_rpc_frm_ops   *f_ops;

	struct c2_addb_ctx             f_addb_ctx;

	/** FRM_MAGIC */
	uint64_t                       f_magic;
};

/**
   Events reported by formation to rest of RPC layer.

   @see c2_rpc_frm_default_ops
 */
struct c2_rpc_frm_ops {
	/**
	   A packet is ready to be sent over network.
	   @return true iff packet has been submitted to network layer, false
		   otherwise.
		   If result is false then all the items in packet
		   p are moved to FAILED state and are removed from p.
		   c2_rpc_packet instance pointed by p is freed.
	 */
	bool (*fo_packet_ready)(struct c2_rpc_packet *p);

	/**
	   Bind a slot to the item.

	   @pre c2_rpc_item_is_unbound(item) && item->ri_session != NULL
	   @pre c2_rpc_machine_is_locked(item_machine(item))
	   @post equi(result, c2_rpc_item_is_bound(item))
	   @post c2_rpc_machine_is_locked(item_machine(item))
	 */
	bool (*fo_item_bind)(struct c2_rpc_item *item);
};

/**
   Default implementation of c2_rpc_frm_ops
 */
extern const struct c2_rpc_frm_ops c2_rpc_frm_default_ops;

/**
   Load default values for various constraints, that just works.
   Useful for unit tests.
 */
void
c2_rpc_frm_constraints_get_defaults(struct c2_rpc_frm_constraints *constraint);

/**
   Initialises frm instance.

   Object pointed by constraints is copied inside c2_rpc_frm.

   @pre  frm->f_state == FRM_UNINITIALISED
   @post frm->f_state == FRM_IDLE
 */
void c2_rpc_frm_init(struct c2_rpc_frm             *frm,
		     struct c2_rpc_frm_constraints *constraints,
		     const struct c2_rpc_frm_ops   *ops);

/**
   Finalises c2_rpc_frm instance.

   @pre  frm->f_state == FRM_IDLE
   @post frm->f_state == FRM_UNINITIALISED
 */
void c2_rpc_frm_fini(struct c2_rpc_frm *frm);

/**
   Enqueue an item for sending.
 */
void c2_rpc_frm_enq_item(struct c2_rpc_frm  *frm,
			 struct c2_rpc_item *item);

/**
   Callback for a packet which was previously enqueued.
 */
void c2_rpc_frm_packet_done(struct c2_rpc_packet *packet);

/**
   Runs formation algorithm.
 */
void c2_rpc_frm_run_formation(struct c2_rpc_frm *frm);

struct c2_rpc_frm *session_frm(const struct c2_rpc_session *s);

C2_TL_DESCR_DECLARE(itemq, extern);
C2_TL_DECLARE(itemq, extern, struct c2_rpc_item);

#endif /* __COLIBRI_RPC_FORMATION2_H__ */
