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
 * Original author: Madhavrao Vemuri <madhav_vemuri@xyratex.com>
 * Original creation date: 10/12/2011
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "lib/memory.h"/* C2_ALLOC_PTR */
#include "lib/errno.h" /* ENOMEM */
#include "lib/arith.h" /* C2_CNT_INC, C2_CNT_DEC */
#include "net/buffer_pool.h"

/**
   @addtogroup net_buffer_pool  Network Buffer Pool
   @{
 */

/** Descriptor for the tlist of buffers. */
C2_TL_DESCR_DEFINE(pool, "net_buffer_pool", static,
		   struct c2_net_buffer, nb_tm_linkage, nb_magic,
		   NET_BUFFER_LINK_MAGIC, NET_BUFFER_HEAD_MAGIC);
C2_TL_DEFINE(pool, static, struct c2_net_buffer);

bool c2_net_buffer_pool_invariant(const struct c2_net_buffer_pool *pool)
{
	return pool != NULL &&
		/* domain must be set and initialized */
		pool->nbp_ndom != NULL && pool->nbp_ndom->nd_xprt != NULL &&
		/* must have the appropriate callback */
		pool->nbp_ops != NULL &&
		pool->nbp_free <= pool->nbp_buf_nr &&
		pool -> nbp_free == pool_tlist_length(&pool->nbp_head);
}

void c2_net_buffer_pool_init(struct c2_net_buffer_pool *pool,
			    struct c2_net_domain *ndom, uint32_t threshold,
			    uint32_t seg_nr, c2_bcount_t seg_size)
{
	c2_bcount_t buf_size;;
	C2_PRE(pool != NULL);
	C2_PRE(ndom != NULL);

	buf_size = seg_nr * seg_size;
	C2_PRE(buf_size <= c2_net_domain_get_max_buffer_size(ndom));
	C2_PRE(seg_size <= c2_net_domain_get_max_buffer_segment_size(ndom));

	pool->nbp_threshold = threshold;
	pool->nbp_ndom	    = ndom;
	pool->nbp_free	    = 0;
	pool->nbp_buf_nr    = 0;
	pool->nbp_seg_nr    = seg_nr;
	pool->nbp_seg_size  = seg_size;

	c2_mutex_init(&pool->nbp_mutex);
	pool_tlist_init(&pool->nbp_head);
}
C2_EXPORTED(c2_net_buffer_pool_init);

/*
   Adds a buffer to the pool to increase the capacity.
   @pre c2_net_buffer_pool_is_locked(pool)
 */
bool c2_net_buffer_pool_grow(struct c2_net_buffer_pool *pool);


int c2_net_buffer_pool_provision(struct c2_net_buffer_pool *pool,
				 uint32_t buf_nr)
{
	int buffers = 0;
	C2_PRE(pool != NULL);
	C2_PRE(pool->nbp_ndom != NULL);
	C2_PRE(c2_net_buffer_pool_is_locked(pool));
	C2_PRE(c2_net_buffer_pool_invariant(pool));
	pool->nbp_buf_nr += buf_nr;

	while (pool->nbp_free < pool->nbp_buf_nr) {
		buffers++;
		if (c2_net_buffer_pool_grow(pool) == false)
			return buffers;
	}
	C2_POST(c2_net_buffer_pool_invariant(pool));
	return buffers;
}
C2_EXPORTED(c2_net_buffer_pool_provision);

/* It removes the given buffer from the pool */
void c2_buffer_remove(struct c2_net_buffer_pool *pool, struct c2_net_buffer *nb){
	pool_tlist_del(nb);
	pool_tlink_fini(nb);
	c2_net_buffer_deregister(nb, pool->nbp_ndom);
	c2_bufvec_free(&nb->nb_buffer);
	c2_free(nb);
	C2_CNT_DEC(pool->nbp_free);
	C2_CNT_DEC(pool->nbp_buf_nr);
	C2_POST(c2_net_buffer_pool_invariant(pool));
}

void c2_net_buffer_pool_fini(struct c2_net_buffer_pool *pool)
{
	struct c2_net_buffer *nb;
	C2_PRE(pool != NULL);
	C2_PRE(c2_net_buffer_pool_invariant(pool));
	c2_tlist_for(&pool_tl, &pool->nbp_head, nb) {
		if (nb != NULL && pool->nbp_free == pool->nbp_buf_nr)
			c2_buffer_remove(pool, nb);
	} c2_tlist_endfor;
	pool_tlist_fini(&pool->nbp_head);
	c2_mutex_fini(&pool->nbp_mutex);
}
C2_EXPORTED(c2_net_buffer_pool_fini);

void c2_net_buffer_pool_lock(struct c2_net_buffer_pool *pool)
{
	c2_mutex_lock(&pool->nbp_mutex);
}
C2_EXPORTED(c2_net_buffer_pool_lock);

bool c2_net_buffer_pool_is_locked(const struct c2_net_buffer_pool *pool)
{
	return c2_mutex_is_locked(&pool->nbp_mutex);
}
C2_EXPORTED(c2_net_buffer_pool_is_locked);

void c2_net_buffer_pool_unlock(struct c2_net_buffer_pool *pool)
{
	c2_mutex_unlock(&pool->nbp_mutex);
}
C2_EXPORTED(c2_net_buffer_pool_unlock);

struct c2_net_buffer *c2_net_buffer_pool_get(struct c2_net_buffer_pool *pool)
{
	struct c2_net_buffer *nb;

	C2_PRE(pool != NULL);
	C2_PRE(c2_net_buffer_pool_invariant(pool));
	C2_PRE(c2_net_buffer_pool_is_locked(pool));
	if (pool->nbp_free <= 0)
		return NULL;
	nb = pool_tlist_head(&pool->nbp_head);
	if (nb != NULL) {
		pool_tlist_del(nb);
		C2_CNT_DEC(pool->nbp_free);
	}
	if (pool->nbp_free < pool->nbp_threshold)
		pool->nbp_ops->nbpo_below_threshold(pool);

	C2_POST(c2_net_buffer_pool_invariant(pool));
	return nb;
}
C2_EXPORTED(c2_net_buffer_pool_get);

void c2_net_buffer_pool_put(struct c2_net_buffer_pool *pool,
			    struct c2_net_buffer *buf)
{
	C2_PRE(pool != NULL);
	C2_PRE(c2_net_buffer_pool_invariant(pool));
	C2_PRE(buf != NULL);
	C2_PRE(c2_net_buffer_pool_is_locked(pool));
	C2_PRE(!(buf->nb_flags & C2_NET_BUF_IN_USE));
	C2_PRE(buf->nb_flags & C2_NET_BUF_REGISTERED);
	C2_PRE(pool->nbp_ndom == buf->nb_dom);

	C2_ASSERT(buf->nb_magic == NET_BUFFER_LINK_MAGIC);
	C2_ASSERT(!pool_tlink_is_in(buf));
	pool_tlist_add_tail(&pool->nbp_head, buf);
	C2_CNT_INC(pool->nbp_free);
	if (pool->nbp_free == 1)
		pool->nbp_ops->nbpo_not_empty(pool);
	C2_POST(c2_net_buffer_pool_invariant(pool));
}
C2_EXPORTED(c2_net_buffer_pool_put);

bool c2_net_buffer_pool_grow(struct c2_net_buffer_pool *pool)
{
	struct c2_net_buffer *nb;
	int rc;
	C2_PRE(c2_net_buffer_pool_invariant(pool));
	C2_PRE(c2_net_buffer_pool_is_locked(pool));
	C2_ALLOC_PTR(nb);
	if (nb == NULL)
		return -ENOMEM;
	rc = c2_bufvec_alloc(&nb->nb_buffer, pool->nbp_seg_nr,
			      pool->nbp_seg_size);
	if (rc != 0)
		goto clean;
	rc = c2_net_buffer_register(nb, pool->nbp_ndom);
	if (rc != 0)
		goto clean;
	pool_tlink_init(nb);
	c2_net_buffer_pool_put(pool, nb);
	C2_POST(c2_net_buffer_pool_invariant(pool));
	return true;
clean:
	if (rc != 0 && nb != NULL) {
		c2_bufvec_free(&nb->nb_buffer);
		c2_free(nb);
	}
	return false;
}

bool c2_net_buffer_pool_prune(struct c2_net_buffer_pool *pool)
{
	struct c2_net_buffer *nb;

	C2_PRE(pool != NULL);
	C2_PRE(c2_net_buffer_pool_invariant(pool));
	C2_PRE(c2_net_buffer_pool_is_locked(pool));
	if (pool->nbp_free <= pool->nbp_threshold)
		return false;
	nb = pool_tlist_head(&pool->nbp_head);
	if (nb != NULL &&
	  !(nb->nb_flags & C2_NET_BUF_IN_USE)) {
		c2_buffer_remove(pool, nb);
		return true;
	} else
		return false;
}
C2_EXPORTED(c2_net_buffer_pool_prune);

/** @} end of net_buffer_pool */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
