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
 * Original author: Nikita_Danilov <Nikita_Danilov@xyratex.com>
 *		    Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 06/27/2012
 */

#pragma once

#ifndef __COLIBRI_RPC_ITEM_H__
#define __COLIBRI_RPC_ITEM_H__

#include "lib/misc.h"              /* C2_IN() */
#include "rpc/session_internal.h"
#include "sm/sm.h"                 /* c2_sm */

/**
   @addtogroup rpc_layer_core

   @{
 */

/* Forward declarations */
struct c2_rpc_item_ops;
struct c2_rpc_item_type;

enum c2_rpc_item_priority {
	C2_RPC_ITEM_PRIO_MIN,
	C2_RPC_ITEM_PRIO_MID,
	C2_RPC_ITEM_PRIO_MAX,
	C2_RPC_ITEM_PRIO_NR
};

enum c2_rpc_item_state {
	C2_RPC_ITEM_UNINITIALISED,
	C2_RPC_ITEM_INITIALISED,
	C2_RPC_ITEM_WAITING_IN_STREAM,
	C2_RPC_ITEM_ENQUEUED,
	C2_RPC_ITEM_SENDING,
	C2_RPC_ITEM_SENT,
	C2_RPC_ITEM_WAITING_FOR_REPLY,
	C2_RPC_ITEM_REPLIED,
	C2_RPC_ITEM_ACCEPTED,
	C2_RPC_ITEM_TIMEDOUT,
	C2_RPC_ITEM_FAILED,
	C2_RPC_ITEM_NR_STATES,
};

/** Stages of item in slot */
enum c2_rpc_item_stage {
	/** the reply for the item was received and the receiver confirmed
	    that the item is persistent */
	RPC_ITEM_STAGE_PAST_COMMITTED = 1,
	/** the reply was received, but persistence confirmation wasn't */
	RPC_ITEM_STAGE_PAST_VOLATILE,
	/** the item was sent (i.e., placed into an rpc) and no reply is
	    received */
	RPC_ITEM_STAGE_IN_PROGRESS,
	/** Don't know whether the request has been processed by receiver
	    or not */
	RPC_ITEM_STAGE_UNKNOWN,
	/** Failed to send the item */
	RPC_ITEM_STAGE_FAILED,
	/** the item is not sent */
	RPC_ITEM_STAGE_FUTURE,
};

enum {
	/** Maximum number of slots to which an rpc item can be associated */
	MAX_SLOT_REF    = 1,
};

enum {
	C2_RPC_ITEM_FIELD_MAGIC = 0xf12acec12c611111ULL,
	C2_RPC_ITEM_HEAD_MAGIC = 0x1007c095e511054eULL,
};

/**
   A single RPC item, such as a FOP or ADDB Record.  This structure should be
   included in every item being sent via RPC layer core to emulate relationship
   similar to inheritance and to allow extening the set of rpc_items without
   modifying core rpc headers.
   @see c2_fop.
 */
struct c2_rpc_item {
	enum c2_rpc_item_priority	 ri_prio;
	c2_time_t			 ri_deadline;
	c2_time_t                        ri_op_timeout;
	struct c2_sm                     ri_sm;
	enum c2_rpc_item_stage		 ri_stage;
	uint64_t			 ri_flags;
	struct c2_rpc_session		*ri_session;
	struct c2_rpc_slot_ref		 ri_slot_refs[MAX_SLOT_REF];
	/** Anchor to put item on c2_rpc_session::s_unbound_items list */
	struct c2_list_link		 ri_unbound_link;
	int32_t				 ri_error;
	/** Pointer to the type object for this item */
	const struct c2_rpc_item_type	*ri_type;
	/** reply item */
	struct c2_rpc_item		*ri_reply;
	bool                             ri_reply_pending;
	struct c2_sm_timeout             ri_timeout;
	/** item operations */
	const struct c2_rpc_item_ops	*ri_ops;
	/** Time spent in rpc layer. */
	c2_time_t			 ri_rpc_time;
	/** List of compound items. */
	struct c2_tl			 ri_compound_items;
	/** Link through which items are anchored on list of
	    c2_rpc_item:ri_compound_items. */
	struct c2_tlink			 ri_field;
	/** Link in one of c2_rpc_frm::f_itemq[] list.
	    List descriptor: itemq
	 */
	struct c2_tlink                  ri_iq_link;
	/** Link in RPC packet. c2_rpc_packet::rp_items
	    List descriptor: packet_item.
	    XXX An item cannot be in itemq and in packet at the same time.
	    Hence iff needed ri_iq_link and ri_plink can be replaced with
	    just one tlink.
	 */
	struct c2_tlink                  ri_plink;
	/** One of c2_rpc_frm::f_itemq[], in which this item is placed. */
	struct c2_tl                    *ri_itemq;
	/** Magic constatnt to verify sanity of linked rpc items. */
	uint64_t			 ri_link_magic;
};

struct c2_rpc_item_ops {
	/**
	   Called when given item is sent over the network. It does not state
	   anything about whether item is received on receiver or not.

	   IMP: Called with rpc-machine mutex held. Do not reenter in RPC.
	 */
	void (*rio_sent)(struct c2_rpc_item *item);
	/**
	   RPC calls this callback when processing on item is complete, i.e.
	   after sending one-way or reply items and when reply to a request
	   item is received.
	   This callback is also called when item moves to FAILED state.
	   Implementation of this callback can find out reason for failure
	   in item->ri_error.

	   IMP: Called with rpc-machine mutex held. Do not reenter in RPC.

	   @pre c2_rpc_machine_is_locked(item_to_rpc_machine(item))
	   @pre ergo(c2_rpc_item_is_request(item) && item->ri_error == 0,
		     item->ri_reply != NULL)
	 */
	void (*rio_done)(struct c2_rpc_item *item);

	/**
	   Finalise and free item.
	   @see c2_fop_default_item_ops
	   @see c2_fop_item_free(), can be used with fops that are not embedded
	   in any other object.
	 */
	void (*rio_free)(struct c2_rpc_item *item);
};

void c2_rpc_item_init(struct c2_rpc_item *item,
		      const struct c2_rpc_item_type *itype);

void c2_rpc_item_fini(struct c2_rpc_item *item);

bool c2_rpc_item_is_bound(const struct c2_rpc_item *item);

bool c2_rpc_item_is_unbound(const struct c2_rpc_item *item);

bool c2_rpc_item_is_unsolicited(const struct c2_rpc_item *item);

void c2_rpc_item_sm_init(struct c2_rpc_item *item, struct c2_sm_group *grp);
void c2_rpc_item_sm_fini(struct c2_rpc_item *item);

void c2_rpc_item_change_state(struct c2_rpc_item     *item,
			      enum c2_rpc_item_state  state);

void c2_rpc_item_failed(struct c2_rpc_item *item, int32_t rc);

c2_bcount_t c2_rpc_item_size(const struct c2_rpc_item *item);

int c2_rpc_item_start_timer(struct c2_rpc_item *item);

void c2_rpc_item_set_stage(struct c2_rpc_item *item, enum c2_rpc_item_stage s);

/**
   Returns true if item modifies file system state, false otherwise
 */
bool c2_rpc_item_is_update(const struct c2_rpc_item *item);

/**
   Returns true if item is request item. False if it is a reply item
 */
bool c2_rpc_item_is_request(const struct c2_rpc_item *item);

bool c2_rpc_item_is_reply(const struct c2_rpc_item *item);

__attribute__((unused))
static bool item_is_active(const struct c2_rpc_item *item)
{
	return C2_IN(item->ri_stage, (RPC_ITEM_STAGE_IN_PROGRESS,
				      RPC_ITEM_STAGE_FUTURE));
}

int c2_rpc_item_timedwait(struct c2_rpc_item *item,
			  uint64_t            states,
			  c2_time_t           timeout);

int c2_rpc_item_wait_for_reply(struct c2_rpc_item *item, c2_time_t timeout);

void c2_rpc_item_free(struct c2_rpc_item *item);

/** @todo: different callbacks called on events occured while processing
   in update stream */
struct c2_rpc_item_type_ops {
	/**
	   Find out the size of rpc payload.
	 */
	c2_bcount_t (*rito_payload_size)(const struct c2_rpc_item *item);

	/**
	  Return true iff item1 and item2 are equal.
	 */
	bool (*rito_eq)(const struct c2_rpc_item *i1,
			const struct c2_rpc_item *i2);

	/**
	   Coalesce rpc items that share same fid and intent(read/write).
	 */
	void (*rito_io_coalesce)(struct c2_rpc_item *head,
				 struct c2_list *list, uint64_t size);

	/**
	   Serialise @item on provided xdr stream @xdrs
	 */
	int (*rito_encode)(const struct c2_rpc_item_type *item_type,
		           struct c2_rpc_item *item,
	                   struct c2_bufvec_cursor *cur);
	/**
	   Create in memory item from serialised representation of item
	 */
	int (*rito_decode)(const struct c2_rpc_item_type *item_type,
			   struct c2_rpc_item **item,
			   struct c2_bufvec_cursor *cur);

	bool (*rito_try_merge)(struct c2_rpc_item *container,
			       struct c2_rpc_item *component,
			       c2_bcount_t         limit);
};

/**
   Possible values for c2_rpc_item_type::rit_flags.
   Flags C2_RPC_ITEM_TYPE_REQUEST, C2_RPC_ITEM_TYPE_REPLY and
   C2_RPC_ITEM_TYPE_UNSOLICITED are mutually exclusive.
 */
enum c2_rpc_item_type_flags {
	/** Receiver of item is expected to send reply to item of this type */
	C2_RPC_ITEM_TYPE_REQUEST = 1,
	/**
	  Item of this type is reply to some item of C2_RPC_ITEM_TYPE_REQUEST
	  type.
	*/
	C2_RPC_ITEM_TYPE_REPLY = (1 << 1),
	/**
	  This is a one-way item. There is no reply for this type of
	  item
	*/
	C2_RPC_ITEM_TYPE_UNSOLICITED = (1 << 2),
	/**
	  Item of this type can modify file-system state on receiver.
	*/
	C2_RPC_ITEM_TYPE_MUTABO = (1 << 3)
};

/**
   Type of an RPC item.
   There is an instance of c2_rpc_item_type for each value of rit_opcode.
 */
struct c2_rpc_item_type {
	/** Unique operation code. */
	uint32_t			   rit_opcode;
	/** Operations that can be performed on the type */
	const struct c2_rpc_item_type_ops *rit_ops;
	/** see @c2_rpc_item_type_flags */
	uint64_t			   rit_flags;
	/** Linkage into rpc item types list (c2_rpc_item_types_list) */
	struct c2_tlink			   rit_linkage;
	/** Magic no for the item type struct */
	uint64_t			   rit_magic;
};

#define C2_RPC_ITEM_TYPE_DEF(itype, opcode, flags, ops)  \
struct c2_rpc_item_type (itype) = {                      \
	.rit_opcode = (opcode),                          \
	.rit_flags = (flags),                            \
	.rit_ops = (ops)                                 \
};

/** Initialises the rpc item types list and lock */
int c2_rpc_base_init(void);

/**
  Finalizes and destroys the rpc item type list by traversing the list and
  deleting and finalizing each element.
*/
void c2_rpc_base_fini(void);

/**
  Registers a new rpc item type by adding an entry to the rpc item types list.
  Asserts when an entry for that opcode already exists in the item types
  list.

  @param item_type The rpc item type to be registered.
*/
int c2_rpc_item_type_register(struct c2_rpc_item_type *item_type);

/** De-registers an rpc item type by deleting the corresponding entry in the
    rpc item types list.

    @param item_type The rpc item type to be deregistered.
*/
void c2_rpc_item_type_deregister(struct c2_rpc_item_type *item_type);

/** Returns a pointer to rpc item type registered for an opcode

  @param opcode Unique operation code for the rpc item type to be looked up.
  @retval Pointer to the rpc item type for that opcode.
  @retval NULL if the item type is not registered.
*/
struct c2_rpc_item_type *c2_rpc_item_type_lookup(uint32_t opcode);

#endif

/** @} end of rpc-layer-core group */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
