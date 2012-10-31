#pragma once

#ifndef __COLIBRI_RPC_ITEM_INT_H__
#define __COLIBRI_RPC_ITEM_INT_H__

#include "rpc/item.h"

/** Initialises the rpc item types list and lock */
int c2_rpc_item_type_list_init(void);

/**
  Finalizes and destroys the rpc item type list by traversing the list and
  deleting and finalizing each element.
*/
void c2_rpc_item_type_list_fini(void);

bool c2_rpc_item_is_bound(const struct c2_rpc_item *item);

bool c2_rpc_item_is_unbound(const struct c2_rpc_item *item);

bool c2_rpc_item_is_oneway(const struct c2_rpc_item *item);

void c2_rpc_item_sm_init(struct c2_rpc_item *item, struct c2_sm_group *grp,
			 enum c2_rpc_item_dir dir);
void c2_rpc_item_sm_fini(struct c2_rpc_item *item);

void c2_rpc_item_change_state(struct c2_rpc_item     *item,
			      enum c2_rpc_item_state  state);

void c2_rpc_item_failed(struct c2_rpc_item *item, int32_t rc);

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

bool             item_is_active(const struct c2_rpc_item *item);
struct c2_verno *item_verno(struct c2_rpc_item *item, int idx);
uint64_t         item_xid(struct c2_rpc_item *item, int idx);
const char      *item_kind(const struct c2_rpc_item *item);

C2_TL_DESCR_DECLARE(slot_item, extern);
C2_TL_DECLARE(slot_item, extern, struct c2_rpc_item);

/** Helper macro to iterate over every item in a slot */
#define for_each_item_in_slot(item, slot) \
	        c2_tl_for(slot_item, &slot->sl_item_list, item)
#define end_for_each_item_in_slot c2_tl_endfor

#endif /* __COLIBRI_RPC_ITEM_INT_H__ */
