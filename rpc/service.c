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
 * Original author: Amit Jambure <Amit_Jambure@xyratex.com>
 * Original creation date: 02/23/2012
 */

#include "rpc/service.h"
#include "lib/tlist.h"
#include "lib/bob.h"
#include "lib/rwlock.h"

static struct c2_bob_type rpc_service_type_bob = {
	.bt_name         = "rpc_service_type",
	.bt_magix_offset = offsetof(struct c2_rpc_service_type, svt_magix),
	.bt_magix        = C2_RPC_SERVICE_TYPE_MAGIX,
	.bt_check        = NULL,
};

/** @todo XXX add "static" as first argument */
C2_BOB_DEFINE(, &rpc_service_type_bob, c2_rpc_service_type);

enum {
	RPC_SERVICE_TYPES_LIST_HEAD_MAGIX = 0x5356435459504844, /* "SVCTYPHD" */
};

C2_TL_DESCR_DEFINE(rpc_service_types, "rpc_service_type", static,
		   struct c2_rpc_service_type, svt_tlink, svt_magix,
		   C2_RPC_SERVICE_TYPE_MAGIX,
		   RPC_SERVICE_TYPES_LIST_HEAD_MAGIX);

C2_TL_DEFINE(rpc_service_types, static, struct c2_rpc_service_type);

static struct c2_tl     rpc_service_types;
static struct c2_rwlock rpc_service_types_lock;

int c2_rpc_service_module_init(void)
{
	rpc_service_types_tlist_init(&rpc_service_types);
	c2_rwlock_init(&rpc_service_types_lock);
	return 0;
}

void c2_rpc_service_module_fini(void)
{
	c2_rwlock_init(&rpc_service_types_lock);
	rpc_service_types_tlist_fini(&rpc_service_types);
}

void c2_rpc_service_type_register(struct c2_rpc_service_type *service_type)
{
	C2_PRE(service_type != NULL);
	C2_ASSERT(c2_rpc_service_type_bob_check(service_type));
	C2_PRE(c2_rpc_service_type_locate(service_type->svt_type_id) == NULL);

	c2_rwlock_write_lock(&rpc_service_types_lock);

	rpc_service_types_tlink_init_at_tail(service_type, &rpc_service_types);

	c2_rwlock_write_unlock(&rpc_service_types_lock);

	C2_POST(c2_rpc_service_type_locate(service_type->svt_type_id) ==
			service_type);
}

void c2_rpc_service_type_unregister(struct c2_rpc_service_type *service_type)
{
	C2_PRE(service_type != NULL);
	C2_ASSERT(c2_rpc_service_type_bob_check(service_type));
	C2_PRE(rpc_service_types_tlink_is_in(service_type));

	c2_rwlock_write_lock(&rpc_service_types_lock);

	rpc_service_types_tlink_del_fini(service_type);

	c2_rwlock_write_unlock(&rpc_service_types_lock);

	C2_POST(!rpc_service_types_tlink_is_in(service_type));
}

struct c2_rpc_service_type * c2_rpc_service_type_locate(uint32_t type_id)
{
	struct c2_rpc_service_type *service_type;

	c2_rwlock_read_lock(&rpc_service_types_lock);
	c2_tlist_for(&rpc_service_types_tl, &rpc_service_types, service_type) {

		C2_ASSERT(c2_rpc_service_type_bob_check(service_type));

		if (service_type->svt_type_id == type_id) {
			c2_rwlock_read_unlock(&rpc_service_types_lock);
			return service_type;
		}

	} c2_tlist_endfor;

	c2_rwlock_read_unlock(&rpc_service_types_lock);
	return NULL;
}

struct c2_rpc_service *
c2_rpc_service_alloc_and_init(struct c2_rpc_service_type *service_type)
{
	C2_PRE(service_type != NULL &&
	       service_type->svt_ops != NULL &&
	       service_type->svt_ops->rsto_alloc_and_init != NULL);

	return service_type->svt_ops->rsto_alloc_and_init(service_type);
}

