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

#ifndef __MERO_RPC_ITEM_INT_H__
#define __MERO_RPC_ITEM_INT_H__

#include "rpc/item.h"

/**
   @addtogroup rpc

   @{
 */

/** Initialises the rpc item types list and lock */
M0_INTERNAL int m0_rpc_item_type_list_init(void);

/**
  Finalizes and destroys the rpc item type list by traversing the list and
  deleting and finalizing each element.
*/
M0_INTERNAL void m0_rpc_item_type_list_fini(void);

M0_INTERNAL bool m0_rpc_item_is_bound(const struct m0_rpc_item *item);

M0_INTERNAL bool m0_rpc_item_is_unbound(const struct m0_rpc_item *item);

M0_INTERNAL bool m0_rpc_item_is_oneway(const struct m0_rpc_item *item);

M0_INTERNAL void m0_rpc_item_sm_init(struct m0_rpc_item *item,
				     enum m0_rpc_item_dir dir);
M0_INTERNAL void m0_rpc_item_sm_fini(struct m0_rpc_item *item);

M0_INTERNAL void m0_rpc_item_change_state(struct m0_rpc_item *item,
					  enum m0_rpc_item_state state);

M0_INTERNAL void m0_rpc_item_failed(struct m0_rpc_item *item, int32_t rc);

M0_INTERNAL int m0_rpc_item_start_timer(struct m0_rpc_item *item);
M0_INTERNAL void m0_rpc_item_stop_timer(struct m0_rpc_item *item);

M0_INTERNAL int m0_rpc_item_start_resend_timer(struct m0_rpc_item *item);
M0_INTERNAL void m0_rpc_item_stop_resend_timer(struct m0_rpc_item *item);
M0_INTERNAL void m0_rpc_item_resend(struct m0_rpc_item *item);

M0_INTERNAL void m0_rpc_item_set_stage(struct m0_rpc_item *item,
				       enum m0_rpc_item_stage s);

M0_INTERNAL void m0_rpc_item_get(struct m0_rpc_item *item);
M0_INTERNAL void m0_rpc_item_put(struct m0_rpc_item *item);


/**
   Returns true if item modifies file system state, false otherwise
 */
M0_INTERNAL bool m0_rpc_item_is_update(const struct m0_rpc_item *item);

/**
   Returns true if item is request item. False if it is a reply item
 */
M0_INTERNAL bool m0_rpc_item_is_request(const struct m0_rpc_item *item);

M0_INTERNAL bool m0_rpc_item_is_reply(const struct m0_rpc_item *item);

M0_INTERNAL bool item_is_active(const struct m0_rpc_item *item);
M0_INTERNAL struct m0_verno *item_verno(struct m0_rpc_item *item, int idx);
M0_INTERNAL uint64_t item_xid(struct m0_rpc_item *item, int idx);
M0_INTERNAL const char *item_kind(const struct m0_rpc_item *item);
M0_INTERNAL const char *item_state_name(const struct m0_rpc_item *item);

M0_TL_DESCR_DECLARE(slot_item, M0_EXTERN);
M0_TL_DECLARE(slot_item, M0_INTERNAL, struct m0_rpc_item);

/** Helper macro to iterate over every item in a slot */
#define for_each_item_in_slot(item, slot) \
	        m0_tl_for(slot_item, &slot->sl_item_list, item)
#define end_for_each_item_in_slot m0_tl_endfor

/** @} */
#endif /* __MERO_RPC_ITEM_INT_H__ */
