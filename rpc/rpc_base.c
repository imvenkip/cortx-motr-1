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
 * Original author: Subhash Arya <Subhash_Arya@xyratex.com>
 * Original creation date: 09/28/2011
 */

#include "lib/tlist.h"
#include "rpc/rpc_base.h"
#include "lib/rwlock.h"

enum {
	/* Hex ASCII value of "rit_link" */
	RPC_ITEM_TYPE_LINK_MAGIC = 0x7269745f6c696e6b,
	/* Hex ASCII value of "rit_head" */
	RPC_ITEM_TYPE_HEAD_MAGIC = 0x7269745f68656164,
};


static const struct c2_tl_descr rpc_item_type_descr =
		    C2_TL_DESCR("item_type_tlist_descr",
		    struct c2_rpc_item_type, rit_linkage, rit_magic,
		    RPC_ITEM_TYPE_LINK_MAGIC, RPC_ITEM_TYPE_HEAD_MAGIC);

static struct c2_tl        rpc_item_types_list;
static struct c2_rwlock    rpc_item_types_lock;


bool opcode_is_dup(uint32_t opcode)
{
	struct c2_rpc_item_type        *item_type;

	C2_PRE(opcode > 0);

	item_type = c2_rpc_item_type_lookup(opcode);
	return item_type != NULL;
}

int c2_rpc_base_init(void)
{
	c2_rwlock_init(&rpc_item_types_lock);
	c2_tlist_init(&rpc_item_type_descr, &rpc_item_types_list);
	return 0;
}

void c2_rpc_base_fini(void)
{
	c2_rwlock_fini(&rpc_item_types_lock);
	c2_tlist_fini(&rpc_item_type_descr, &rpc_item_types_list);
}

int c2_rpc_item_type_register(struct c2_rpc_item_type *item_type)
{

	C2_PRE(item_type != NULL);
	C2_PRE(!opcode_is_dup(item_type->rit_opcode));

	c2_rwlock_write_lock(&rpc_item_types_lock);
	c2_tlist_add(&rpc_item_type_descr, &rpc_item_types_list, item_type);
	c2_rwlock_write_unlock(&rpc_item_types_lock);
	return 0;
}

struct c2_rpc_item_type *c2_rpc_item_type_lookup(uint32_t opcode)
{
	struct c2_rpc_item_type  	*item_type = NULL;
	bool 				 found = false;

	c2_rwlock_read_lock(&rpc_item_types_lock);
	c2_tlist_for(&rpc_item_type_descr, &rpc_item_types_list, item_type) {
		if (item_type->rit_opcode == opcode) {
			found = true;
			break;
		}
	}
	c2_tlist_endfor;
	c2_rwlock_read_unlock(&rpc_item_types_lock);
	if (found)
		return item_type;

	return NULL;
}

