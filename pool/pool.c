#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "stob/stob_id.h"
#include "pool/pool.h"

/**
   @addtogroup pool

   XXX Stub code for now.
   @{
 */

int c2_pool_init(struct c2_pool *pool, uint32_t width)
{
	pool->po_width = width;
	return 0;
}
C2_EXPORTED(c2_pool_init);

void c2_pool_fini(struct c2_pool *lay)
{
}
C2_EXPORTED(c2_pool_fini);

int c2_pool_alloc(struct c2_pool *pool, struct c2_stob_id *id)
{
	static uint64_t seq = 3;

	id->si_bits.u_hi = (uint64_t)pool;
	id->si_bits.u_lo  = seq++;
	C2_POST(c2_stob_id_is_set(id));
	return 0;
}
C2_EXPORTED(c2_pool_alloc);

void c2_pool_put(struct c2_pool *pool, struct c2_stob_id *id)
{
	C2_PRE(c2_stob_id_is_set(id));
}
C2_EXPORTED(c2_pool_put);

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
