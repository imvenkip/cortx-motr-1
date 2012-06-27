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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 *		    Anand Vidwansa <Anand_Vidwansa@xyratex.com>
 *                  Anup Barve <Anup_Barve@xyratex.com>
 * Original creation date: 06/27/2012
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lib/tlist.h"
#include "lib/rwlock.h"
#include "lib/misc.h"
#include "rpc/rpc2.h"

C2_TL_DESCR_DEFINE(rpcitem, "rpc item tlist", , struct c2_rpc_item, ri_field,
	           ri_link_magic, C2_RPC_ITEM_FIELD_MAGIC,
		   C2_RPC_ITEM_HEAD_MAGIC);

C2_TL_DEFINE(rpcitem, , struct c2_rpc_item);

enum {
	/* Hex ASCII value of "rit_link" */
	RPC_ITEM_TYPE_LINK_MAGIC = 0x7269745f6c696e6b,
	/* Hex ASCII value of "rit_head" */
	RPC_ITEM_TYPE_HEAD_MAGIC = 0x7269745f68656164,
};

C2_TL_DESCR_DEFINE(rit, "rpc_item_type_descr", static, struct c2_rpc_item_type,
		   rit_linkage,	rit_magic, RPC_ITEM_TYPE_LINK_MAGIC,
		   RPC_ITEM_TYPE_HEAD_MAGIC);

C2_TL_DEFINE(rit, static, struct c2_rpc_item_type);

/** Global rpc item types list. */
static struct c2_tl        rpc_item_types_list;
static struct c2_rwlock    rpc_item_types_lock;

/**
  Checks if the supplied opcode has already been registered.
  @param opcode RPC item type opcode.
  @retval true if opcode is a duplicate(already registered)
  @retval false if opcode has not been registered yet.
*/
static bool opcode_is_dup(uint32_t opcode)
{
	C2_PRE(opcode > 0);

	return c2_rpc_item_type_lookup(opcode) != NULL;
}

int c2_rpc_base_init(void)
{
	c2_rwlock_init(&rpc_item_types_lock);
	rit_tlist_init(&rpc_item_types_list);
	return 0;
}

void c2_rpc_base_fini(void)
{
	struct c2_rpc_item_type		*item_type;

	c2_rwlock_write_lock(&rpc_item_types_lock);
	c2_tl_for(rit, &rpc_item_types_list, item_type) {
		rit_tlink_del_fini(item_type);
	} c2_tl_endfor;
	rit_tlist_fini(&rpc_item_types_list);
	c2_rwlock_write_unlock(&rpc_item_types_lock);
	c2_rwlock_fini(&rpc_item_types_lock);
}

int c2_rpc_item_type_register(struct c2_rpc_item_type *item_type)
{

	C2_PRE(item_type != NULL);
	C2_PRE(!opcode_is_dup(item_type->rit_opcode));

	c2_rwlock_write_lock(&rpc_item_types_lock);
	rit_tlink_init_at(item_type, &rpc_item_types_list);
	c2_rwlock_write_unlock(&rpc_item_types_lock);

	return 0;
}

void c2_rpc_item_type_deregister(struct c2_rpc_item_type *item_type)
{
	C2_PRE(item_type != NULL);

	c2_rwlock_write_lock(&rpc_item_types_lock);
	rit_tlink_del_fini(item_type);
	item_type->rit_magic = 0;
	c2_rwlock_write_unlock(&rpc_item_types_lock);
}

struct c2_rpc_item_type *c2_rpc_item_type_lookup(uint32_t opcode)
{
	struct c2_rpc_item_type         *item_type = NULL;
	bool                             found = false;

	c2_rwlock_read_lock(&rpc_item_types_lock);
	c2_tl_for(rit, &rpc_item_types_list, item_type) {
		if (item_type->rit_opcode == opcode) {
			found = true;
			break;
		}
	} c2_tl_endfor;
	c2_rwlock_read_unlock(&rpc_item_types_lock);
	if (found)
		return item_type;

	return NULL;
}

void c2_rpc_item_init(struct c2_rpc_item *item)
{
	struct c2_rpc_slot_ref	*sref;

	C2_SET0(item);

	item->ri_state      = RPC_ITEM_UNINITIALIZED;
	item->ri_head_magic = C2_RPC_ITEM_HEAD_MAGIC;
	item->ri_link_magic = C2_RPC_ITEM_FIELD_MAGIC;

	sref = &item->ri_slot_refs[0];

	sref->sr_slot_id    = SLOT_ID_INVALID;
	sref->sr_sender_id  = SENDER_ID_INVALID;
	sref->sr_session_id = SESSION_ID_INVALID;

	c2_list_link_init(&sref->sr_link);
	c2_list_link_init(&sref->sr_ready_link);

        c2_list_link_init(&item->ri_unbound_link);

        c2_list_link_init(&item->ri_rpcobject_linkage);
	c2_list_link_init(&item->ri_unformed_linkage);
        c2_list_link_init(&item->ri_group_linkage);
        rpcitem_tlink_init(item);
	rpcitem_tlist_init(&item->ri_compound_items);

	c2_chan_init(&item->ri_chan);
}
C2_EXPORTED(c2_rpc_item_init);


void c2_rpc_item_fini(struct c2_rpc_item *item)
{
	struct c2_rpc_slot_ref	*sref;

	c2_chan_fini(&item->ri_chan);

	sref = &item->ri_slot_refs[0];
	sref->sr_slot_id = SLOT_ID_INVALID;
	c2_list_link_fini(&sref->sr_link);
	c2_list_link_fini(&sref->sr_ready_link);

	sref->sr_sender_id = SENDER_ID_INVALID;
	sref->sr_session_id = SESSION_ID_INVALID;

        c2_list_link_fini(&item->ri_unbound_link);

        c2_list_link_fini(&item->ri_rpcobject_linkage);
	c2_list_link_fini(&item->ri_unformed_linkage);
        c2_list_link_fini(&item->ri_group_linkage);
	rpcitem_tlink_fini(item);
	rpcitem_tlist_fini(&item->ri_compound_items);
	item->ri_state = RPC_ITEM_FINALIZED;
}
C2_EXPORTED(c2_rpc_item_fini);

c2_bcount_t c2_rpc_item_size(const struct c2_rpc_item *item)
{
	C2_PRE(item->ri_type != NULL &&
	       item->ri_type->rit_ops != NULL &&
	       item->ri_type->rit_ops->rito_item_size != NULL);

	return item->ri_type->rit_ops->rito_item_size(item);
}

bool c2_rpc_item_is_update(const struct c2_rpc_item *item)
{
	return (item->ri_type->rit_flags & C2_RPC_ITEM_TYPE_MUTABO) != 0;
}

bool c2_rpc_item_is_request(const struct c2_rpc_item *item)
{
	C2_PRE(item != NULL && item->ri_type != NULL);

	return (item->ri_type->rit_flags & C2_RPC_ITEM_TYPE_REQUEST) != 0;
}

bool c2_rpc_item_is_reply(const struct c2_rpc_item *item)
{
	C2_PRE(item != NULL && item->ri_type != NULL);

	return (item->ri_type->rit_flags & C2_RPC_ITEM_TYPE_REPLY) != 0;
}

bool c2_rpc_item_is_unsolicited(const struct c2_rpc_item *item)
{
	C2_PRE(item != NULL);
	C2_PRE(item->ri_type != NULL);

	return (item->ri_type->rit_flags & C2_RPC_ITEM_TYPE_UNSOLICITED) != 0;
}

bool c2_rpc_item_is_bound(const struct c2_rpc_item *item)
{
	C2_PRE(item != NULL);

	return item->ri_slot_refs[0].sr_slot != NULL;
}

bool c2_rpc_item_is_unbound(const struct c2_rpc_item *item)
{
	return !c2_rpc_item_is_bound(item) && !c2_rpc_item_is_unsolicited(item);
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
