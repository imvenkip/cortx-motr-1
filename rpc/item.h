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

#ifndef __COLIBRI_RPC_ITEM_H__
#define __COLIBRI_RPC_ITEM_H__

#include "rpc/session_internal.h"

/* Imports */
struct c2_rpc;

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
	/** Newly allocated object is in uninitialized state */
	RPC_ITEM_UNINITIALIZED = 0,
	/** After successful initialization item enters to "in use" state */
	RPC_ITEM_IN_USE = (1 << 0),
	/** After item's added to the formation cache */
	RPC_ITEM_SUBMITTED = (1 << 1),
	/** After item's added to an RPC it enters added state */
	RPC_ITEM_ADDED = (1 << 2),
	/** After item's sent  it enters sent state */
	RPC_ITEM_SENT = (1 << 3),
	/** After item's sent is failed, it enters send failed state */
	RPC_ITEM_SEND_FAILED = (1 << 4),
	/** After item's replied  it enters replied state */
	RPC_ITEM_REPLIED = (1 << 5),
	/** After finalization item enters finalized state*/
	RPC_ITEM_FINALIZED = (1 << 6)
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
	struct c2_chan			 ri_chan;
	enum c2_rpc_item_priority	 ri_prio;
	c2_time_t			 ri_deadline;
	struct c2_rpc_group		*ri_group;

	enum c2_rpc_item_state		 ri_state;
	enum c2_rpc_item_stage		 ri_stage;
	uint64_t			 ri_flags;
	struct c2_rpc_session		*ri_session;
	struct c2_rpc_slot_ref		 ri_slot_refs[MAX_SLOT_REF];
	/** Anchor to put item on c2_rpc_session::s_unbound_items list */
	struct c2_list_link		 ri_unbound_link;
	int32_t				 ri_error;
	/** Pointer to the type object for this item */
	struct c2_rpc_item_type		*ri_type;
	/** Linkage to the forming list, needed for formation */
	struct c2_list_link		 ri_rpcobject_linkage;
	/** Linkage to the unformed rpc items list, needed for formation */
	struct c2_list_link		 ri_unformed_linkage;
	/** Linkage to the group c2_rpc_group, needed for grouping */
	struct c2_list_link		 ri_group_linkage;
	/** Timer associated with this rpc item.*/
	struct c2_timer			 ri_timer;
	/** reply item */
	struct c2_rpc_item		*ri_reply;
	/** item operations */
	const struct c2_rpc_item_ops	*ri_ops;
	/** Time spent in rpc layer. */
	c2_time_t			 ri_rpc_time;
	/** Magic constant to verify sanity of ambient structure. */
	uint64_t			 ri_head_magic;
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
	   Called when given item's sent.
	   @param item reference to an RPC-item sent
	   @note ri_added() has been called before invoking this function.
	 */
	void (*rio_sent)(struct c2_rpc_item *item);
	/**
	   Called when item's added to an RPC
	   @param rpc reference to an RPC where item's added
	   @param item reference to an item added to rpc
	 */
	void (*rio_added)(struct c2_rpc *rpc, struct c2_rpc_item *item);

	/**
	   Called when given item's replied.
	   @param item reference to an RPC-item on which reply FOP was received.

	   @note ri_added() and ri_sent() have been called before invoking this
	   function.

	   c2_rpc_item::ri_error and c2_rpc_item::ri_reply are already set by
	   the time this method is called.
	 */
	void (*rio_replied)(struct c2_rpc_item *item);

	/**
	   Finalise and free item.
	   @see c2_fop_default_item_ops
	   @see c2_fop_item_free(), can be used with fops that are not embedded
	   in any other object.
	 */
	void (*rio_free)(struct c2_rpc_item *item);
};

void c2_rpc_item_init(struct c2_rpc_item *item);

void c2_rpc_item_fini(struct c2_rpc_item *item);

bool c2_rpc_item_is_bound(const struct c2_rpc_item *item);

bool c2_rpc_item_is_unbound(const struct c2_rpc_item *item);

bool c2_rpc_item_is_unsolicited(const struct c2_rpc_item *item);


c2_bcount_t c2_rpc_item_size(const struct c2_rpc_item *item);

/**
   Returns true if item modifies file system state, false otherwise
 */
bool c2_rpc_item_is_update(const struct c2_rpc_item *item);

/**
   Returns true if item is request item. False if it is a reply item
 */
bool c2_rpc_item_is_request(const struct c2_rpc_item *item);

/* TBD: different callbacks called on events occured while processing
   in update stream */
struct c2_rpc_item_type_ops {
	/**
	   Find out the size of rpc item.
	 */
	c2_bcount_t (*rito_item_size)(const struct c2_rpc_item *item);

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
	int (*rito_encode)(struct c2_rpc_item_type *item_type,
		           struct c2_rpc_item *item,
	                   struct c2_bufvec_cursor *cur);
	/**
	   Create in memory item from serialised representation of item
	 */
	int (*rito_decode)(struct c2_rpc_item_type *item_type,
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
  @retval 0 on success
  @retval -errno on failure.
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
