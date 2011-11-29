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
 * Original author: Subhash Arya <subhash_arya@xyratex.com>
 * Original creation date: 09/27/2011
 */

#ifndef __COLIBRI_RPC_RPCBASE_H__
#define __COLIBRI_RPC_RPCBASE_H__

/**
   @addtogroup rpc_layer_core

   This file contains rpc item type definitions. These were part of
   rpccore.h. The reason for this split is to break circular dependencies
   that otherwise forms due to embedding rpc item type into a fop type.

   @{
 */

/* Import */
struct c2_rpc_item;
struct c2_rpc;
struct c2_fop;
struct c2_bufvec_cursor;
struct c2_rpc_frm_item_coalesced;
struct c2_net_buf_desc;

/* Export */
struct c2_rpc_item_type_ops;
struct c2_rpc_item_type;

/* TBD: different callbacks called on events occured while processing
   in update stream */
struct c2_rpc_item_type_ops {
	/**
	   Restore original IO vector of rpc item.
	 */
	void (*rito_iovec_restore)(struct c2_rpc_item *b_item,
			struct c2_fop *bkpfop);
	/**
	   Find out the size of rpc item.
	 */
	size_t (*rito_item_size)(const struct c2_rpc_item *item);

	/**
	   Find out if given rpc items belong to same type or not.
	 */
	bool (*rito_items_equal)(struct c2_rpc_item *item1, struct
			c2_rpc_item *item2);
	/**
	   Find out if given rpc items refer to same c2_fid struct or not.
	 */
	bool (*rito_fid_equal)(struct c2_rpc_item *item1,
			       struct c2_rpc_item *item2);
	/**
	  Return true iff item1 and item2 are equal.
	 */
	bool (*rito_eq)(const struct c2_rpc_item *i1,
			const struct c2_rpc_item *i2);
	/**
	   Find out the count of fragmented buffers.
	 */
	uint64_t (*rito_get_io_fragment_count)(struct c2_rpc_item *item);
	/**
	   Coalesce rpc items that share same fid and intent(read/write).
	 */
	int (*rito_io_coalesce)(struct c2_rpc_frm_item_coalesced *coalesced_item,
			struct c2_rpc_item *item);
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
	/**
	   Return the c2_net_buf_desc from io fop.
	 */
	void (*rito_io_desc_get)(struct c2_rpc_item *item,
				 struct c2_net_buf_desc *desc);
	/**
	   Store the c2_net_buf_desc into io fop from its net buffer.
	 */
	int (*rito_io_desc_store)(struct c2_rpc_item *item,
				  struct c2_net_buf_desc *desc);
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

/** @} endgroup rpc_layer_core */
#endif

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
