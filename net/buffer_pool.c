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
 * Original author: Madhavrao Vemuri <madhav_vemuri@xyratex.com>
 * Original creation date: 10/12/2011
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
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
C2_TL_DESCR_DEFINE(pool, "net_buffer_pool", ,
		   struct c2_net_buffer, nb_lru, nb_magic,
		   C2_NET_BUFFER_LINK_MAGIC, C2_NET_BUFFER_HEAD_MAGIC);
C2_TL_DEFINE(pool, , struct c2_net_buffer);

static const struct c2_addb_loc c2_pool_addb_loc = {
	.al_name = "buffer pool"
};
static bool pool_colour_check(const struct c2_net_buffer_pool *pool);
static bool pool_lru_buffer_check(const struct c2_net_buffer_pool *pool);

bool c2_net_buffer_pool_invariant(const struct c2_net_buffer_pool *pool)
{
	return pool != NULL &&
		/* domain must be set and initialized */
		pool->nbp_ndom != NULL && pool->nbp_ndom->nd_xprt != NULL &&
		/* must have the appropriate callback */
		pool->nbp_ops != NULL &&
		c2_net_buffer_pool_is_locked(pool) &&
		pool->nbp_free <= pool->nbp_buf_nr &&
		pool->nbp_free == pool_tlist_length(&pool->nbp_lru) &&
		pool_colour_check(pool) &&
		pool_lru_buffer_check(pool);
}

static bool pool_colour_check(const struct c2_net_buffer_pool *pool)
{
	int		      i;
	struct c2_net_buffer *nb;

	for (i = 0; i < pool->nbp_colours_nr; i++) {
		c2_tl_for(tm, &pool->nbp_colours[i], nb) {
			if (!pool_tlink_is_in(nb))
				return false;
		} c2_tl_endfor;
	}
	return true;
}

static bool pool_lru_buffer_check(const struct c2_net_buffer_pool *pool)
{
	struct c2_net_buffer *nb;
	c2_tl_for(pool, &pool->nbp_lru, nb) {
		if ((nb->nb_flags & C2_NET_BUF_QUEUED) ||
		   !(nb->nb_flags & C2_NET_BUF_REGISTERED))
			return false;
	} c2_tl_endfor;
	return true;
}

int c2_net_buffer_pool_init(struct c2_net_buffer_pool *pool,
			    struct c2_net_domain *ndom, uint32_t threshold,
			    uint32_t seg_nr, c2_bcount_t seg_size,
			    uint32_t colours, unsigned shift)
{
	int i;

	C2_PRE(pool != NULL);
	C2_PRE(ndom != NULL);
	C2_PRE(seg_nr   <= c2_net_domain_get_max_buffer_segments(ndom));
	C2_PRE(seg_size <= c2_net_domain_get_max_buffer_segment_size(ndom));

	pool->nbp_threshold  = threshold;
	pool->nbp_ndom	     = ndom;
	pool->nbp_free	     = 0;
	pool->nbp_buf_nr     = 0;
	pool->nbp_seg_nr     = seg_nr;
	pool->nbp_seg_size   = seg_size;
	pool->nbp_colours_nr = colours;
	pool->nbp_align	     = shift;

	if (colours == 0)
		pool->nbp_colours = NULL;
	else {
		C2_ALLOC_ARR_ADDB(pool->nbp_colours, colours, &ndom->nd_addb,
				 &c2_pool_addb_loc);
		if(pool->nbp_colours == NULL)
			return -ENOMEM;
	}
	c2_mutex_init(&pool->nbp_mutex);
	pool_tlist_init(&pool->nbp_lru);
	for (i = 0; i < colours; i++)
		tm_tlist_init(&pool->nbp_colours[i]);
	return 0;
}

/**
   Adds a buffer to the pool to increase the capacity.
   @pre c2_net_buffer_pool_is_locked(pool)
 */
static bool net_buffer_pool_grow(struct c2_net_buffer_pool *pool);


int c2_net_buffer_pool_provision(struct c2_net_buffer_pool *pool,
				 uint32_t buf_nr)
{
	int buffers = 0;

	C2_PRE(c2_net_buffer_pool_invariant(pool));

	while (buf_nr--) {
		if (!net_buffer_pool_grow(pool))
			return buffers;
		buffers++;
	}
	C2_POST(c2_net_buffer_pool_invariant(pool));
	return buffers;
}

/** It removes the given buffer from the pool */
static void buffer_remove(struct c2_net_buffer_pool *pool,
			  struct c2_net_buffer *nb)
{
	pool_tlink_del_fini(nb);
	if (tm_tlink_is_in(nb))
		tm_tlist_del(nb);
	tm_tlink_fini(nb);
	c2_net_buffer_deregister(nb, pool->nbp_ndom);
	c2_bufvec_free_aligned(&nb->nb_buffer, pool->nbp_align);
	c2_free(nb);
	C2_CNT_DEC(pool->nbp_buf_nr);
	C2_POST(c2_net_buffer_pool_invariant(pool));
}

void c2_net_buffer_pool_fini(struct c2_net_buffer_pool *pool)
{
	int		      i;
	struct c2_net_buffer *nb;

	C2_PRE(c2_net_buffer_pool_invariant(pool));
	C2_PRE(pool->nbp_free == pool->nbp_buf_nr);

	c2_tl_for(pool, &pool->nbp_lru, nb) {
		C2_CNT_DEC(pool->nbp_free);
		buffer_remove(pool, nb);
	} c2_tl_endfor;
	pool_tlist_fini(&pool->nbp_lru);
	for (i = 0; i < pool->nbp_colours_nr; i++)
		tm_tlist_fini(&pool->nbp_colours[i]);
	c2_free(pool->nbp_colours);
	c2_mutex_fini(&pool->nbp_mutex);
}

void c2_net_buffer_pool_lock(struct c2_net_buffer_pool *pool)
{
	c2_mutex_lock(&pool->nbp_mutex);
}

bool c2_net_buffer_pool_is_locked(const struct c2_net_buffer_pool *pool)
{
	return c2_mutex_is_locked(&pool->nbp_mutex);
}

void c2_net_buffer_pool_unlock(struct c2_net_buffer_pool *pool)
{
	c2_mutex_unlock(&pool->nbp_mutex);
}

struct c2_net_buffer *c2_net_buffer_pool_get(struct c2_net_buffer_pool *pool,
					     uint32_t colour)
{
	struct c2_net_buffer *nb;

	C2_PRE(c2_net_buffer_pool_invariant(pool));
	C2_PRE(colour == BUFFER_ANY_COLOUR || colour < pool->nbp_colours_nr);

	if (pool->nbp_free <= 0)
		return NULL;
	if (colour != BUFFER_ANY_COLOUR &&
	   !tm_tlist_is_empty(&pool->nbp_colours[colour]))
		nb = tm_tlist_head(&pool->nbp_colours[colour]);
	else
		nb = pool_tlist_head(&pool->nbp_lru);
	C2_ASSERT(nb != NULL);
	pool_tlist_del(nb);
	if (tm_tlink_is_in(nb))
		tm_tlist_del(nb);
	C2_CNT_DEC(pool->nbp_free);
	if (pool->nbp_free < pool->nbp_threshold)
		pool->nbp_ops->nbpo_below_threshold(pool);

	C2_POST(c2_net_buffer_pool_invariant(pool));
	return nb;
}

void c2_net_buffer_pool_put(struct c2_net_buffer_pool *pool,
			    struct c2_net_buffer *buf, uint32_t colour)
{
	C2_PRE(buf != NULL);
	C2_PRE(c2_net_buffer_pool_invariant(pool));
	C2_PRE(colour == BUFFER_ANY_COLOUR || colour < pool->nbp_colours_nr);
	C2_PRE(!(buf->nb_flags & C2_NET_BUF_QUEUED));
	C2_PRE(buf->nb_flags & C2_NET_BUF_REGISTERED);
	C2_PRE(pool->nbp_ndom == buf->nb_dom);

	C2_ASSERT(buf->nb_magic == C2_NET_BUFFER_LINK_MAGIC);
	C2_ASSERT(!pool_tlink_is_in(buf));
	if (colour != BUFFER_ANY_COLOUR) {
		C2_ASSERT(!tm_tlink_is_in(buf));
		tm_tlist_add(&pool->nbp_colours[colour], buf);
	}
	pool_tlist_add_tail(&pool->nbp_lru, buf);
	C2_CNT_INC(pool->nbp_free);
	if (pool->nbp_free == 1)
		pool->nbp_ops->nbpo_not_empty(pool);
	C2_POST(c2_net_buffer_pool_invariant(pool));
}

static bool net_buffer_pool_grow(struct c2_net_buffer_pool *pool)
{
	int		      rc;
	struct c2_net_buffer *nb;

	C2_PRE(c2_net_buffer_pool_invariant(pool));

	C2_ALLOC_PTR(nb);
	if (nb == NULL)
		return false;
	rc = c2_bufvec_alloc_aligned(&nb->nb_buffer, pool->nbp_seg_nr,
	                              pool->nbp_seg_size, pool->nbp_align);
	if (rc != 0)
		goto clean;
	rc = c2_net_buffer_register(nb, pool->nbp_ndom);
	if (rc != 0)
		goto clean;
	pool_tlink_init(nb);
	tm_tlink_init(nb);

	C2_CNT_INC(pool->nbp_buf_nr);
	c2_net_buffer_pool_put(pool, nb, BUFFER_ANY_COLOUR);
	C2_POST(c2_net_buffer_pool_invariant(pool));
	return true;
clean:
	C2_ASSERT(rc != 0);
	c2_bufvec_free_aligned(&nb->nb_buffer, pool->nbp_align);
	c2_free(nb);
	return false;
}

bool c2_net_buffer_pool_prune(struct c2_net_buffer_pool *pool)
{
	struct c2_net_buffer *nb;

	C2_PRE(c2_net_buffer_pool_invariant(pool));

	if (pool->nbp_free <= pool->nbp_threshold)
		return false;
	C2_CNT_DEC(pool->nbp_free);
	nb = pool_tlist_head(&pool->nbp_lru);
	C2_ASSERT(nb != NULL);
	buffer_remove(pool, nb);
	return true;
}

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
