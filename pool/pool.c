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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 07/15/2010
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "lib/errno.h"
#include "stob/stob.h"
#include "pool/pool.h"

/**
   @addtogroup pool

   XXX Stub code for now.
   @{
 */

enum {
	POOL_ID_NONE = 0,
	MAX_POOL_ID  = 10
};

/**
 * @todo Temporarily storing the pool structures in this array since not
 * not many pool objects are expected to be instantiated.
 * See pool.h for note related to c2_pool_lookup().
 */
static struct c2_pool *pool_list[MAX_POOL_ID + 1];

bool c2_pool_id_is_valid(uint64_t pool_id)
{
	return pool_id != POOL_ID_NONE && pool_id <= MAX_POOL_ID;
}


int c2_pool_init(struct c2_pool *pool, uint64_t pid, uint32_t width)
{
	C2_ASSERT(pid <= MAX_POOL_ID);
	C2_ASSERT(pool_list[pid] == NULL);

	pool->po_id = pid;
	pool->po_width = width;

	pool_list[pool->po_id] = pool;

	return 0;
}

void c2_pool_fini(struct c2_pool *pool)
{
	C2_PRE(pool->po_id <= MAX_POOL_ID);

	pool_list[pool->po_id] = NULL;
}

/**
 * @note This interface is temporary and will be over-ridden by the interface
 * to be provided by configuration catching.
 * Provide c2_pool object with specified pool id.
 */
int c2_pool_lookup(uint64_t pid, struct c2_pool **out)
{
	C2_PRE(pid < MAX_POOL_ID);
	C2_PRE(out != NULL);

	if(pool_list[pid] == NULL)
		return -EINVAL;

	*out = pool_list[pid];
	return 0;
}

int c2_pool_alloc(struct c2_pool *pool, struct c2_stob_id *id)
{
	static uint64_t seq = 3;

	id->si_bits.u_hi = (uint64_t)pool;
	id->si_bits.u_lo  = seq++;
	C2_POST(c2_stob_id_is_set(id));
	return 0;
}

void c2_pool_put(struct c2_pool *pool, struct c2_stob_id *id)
{
	C2_PRE(c2_stob_id_is_set(id));
}

int c2_pools_init(void)
{
	return 0;
}

void c2_pools_fini(void)
{
}

/** @} end group pool */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
