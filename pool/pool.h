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
 * Original author: Nikita Danilov <Nikita_Danilov@xyratex.com>
 * Original creation date: 07/15/2010
 */

#ifndef __COLIBRI_LAYOUT_POOL_H__
#define __COLIBRI_LAYOUT_POOL_H__

#include "lib/cdefs.h"

/**
   @defgroup pool Storage pools.

   @{
 */

/* import */
struct c2_stob_id;

/* export */
struct c2_pool;

struct c2_pool {
	uint32_t po_width;
};

int  c2_pool_init(struct c2_pool *pool, uint32_t width);
void c2_pool_fini(struct c2_pool *pool);

/**
   Allocates object id in the pool.

   @post ergo(result == 0, c2_stob_id_is_set(id))
 */
int c2_pool_alloc(struct c2_pool *pool, struct c2_stob_id *id);

/**
   Releases object id back to the pool.

   @pre c2_stob_id_is_set(id)
 */
void c2_pool_put(struct c2_pool *pool, struct c2_stob_id *id);

int  c2_pools_init(void);
void c2_pools_fini(void);

/** @} end group pool */

/* __COLIBRI_LAYOUT_POOL_H__ */
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
