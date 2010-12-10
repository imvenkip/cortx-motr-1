/* -*- C -*- */

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

   @post c2_ergo(result == 0, c2_stob_id_is_set(id))
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
