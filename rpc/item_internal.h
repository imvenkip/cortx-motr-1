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
 * Original author: Amit Jambure <amit_jambure@xyratex.com>
 * Original creation date: 10/31/2012
 */

#pragma once

#ifndef __COLIBRI_RPC_ITEM_INT_H__
#define __COLIBRI_RPC_ITEM_INT_H__

#include "rpc/item.h"

/**
   @addtogroup rpc

   @{
 */

/** Initialises the rpc item types list and lock */
C2_INTERNAL int c2_rpc_item_type_list_init(void);

/**
  Finalizes and destroys the rpc item type list by traversing the list and
  deleting and finalizing each element.
*/
C2_INTERNAL void c2_rpc_item_type_list_fini(void);

C2_INTERNAL bool c2_rpc_item_is_bound(const struct c2_rpc_item *item);

C2_INTERNAL bool c2_rpc_item_is_unbound(const struct c2_rpc_item *item);

C2_INTERNAL bool c2_rpc_item_is_oneway(const struct c2_rpc_item *item);

C2_INTERNAL void c2_rpc_item_sm_init(struct c2_rpc_item *item,
				     struct c2_sm_group *grp,
				     enum c2_rpc_item_dir dir);
C2_INTERNAL void c2_rpc_item_sm_fini(struct c2_rpc_item *item);

C2_INTERNAL void c2_rpc_item_change_state(struct c2_rpc_item *item,
					  enum c2_rpc_item_state state);

C2_INTERNAL void c2_rpc_item_failed(struct c2_rpc_item *item, int32_t rc);

C2_INTERNAL int c2_rpc_item_start_timer(struct c2_rpc_item *item);

C2_INTERNAL void c2_rpc_item_set_stage(struct c2_rpc_item *item,
				       enum c2_rpc_item_stage s);

/**
   Returns true if item modifies file system state, false otherwise
 */
C2_INTERNAL bool c2_rpc_item_is_update(const struct c2_rpc_item *item);

/**
   Returns true if item is request item. False if it is a reply item
 */
C2_INTERNAL bool c2_rpc_item_is_request(const struct c2_rpc_item *item);

C2_INTERNAL bool c2_rpc_item_is_reply(const struct c2_rpc_item *item);

C2_INTERNAL bool item_is_active(const struct c2_rpc_item *item);
C2_INTERNAL struct c2_verno *item_verno(struct c2_rpc_item *item, int idx);
C2_INTERNAL uint64_t item_xid(struct c2_rpc_item *item, int idx);
C2_INTERNAL const char *item_kind(const struct c2_rpc_item *item);

C2_TL_DESCR_DECLARE(slot_item, C2_EXTERN);
C2_TL_DECLARE(slot_item, C2_INTERNAL, struct c2_rpc_item);

/** Helper macro to iterate over every item in a slot */
#define for_each_item_in_slot(item, slot) \
	        c2_tl_for(slot_item, &slot->sl_item_list, item)
#define end_for_each_item_in_slot c2_tl_endfor

/** @} */
#endif /* __COLIBRI_RPC_ITEM_INT_H__ */
